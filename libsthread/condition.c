/*
 *  Simple win32 threads - conditions.
 *
 *  Copyright (c) 2020 - 2022, Adam Young.
 *  All rights reserved.
 *
 *  This file is part of inetd-win32.
 *
 *  The applications are free software: you can redistribute it
 *  and/or modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation, version 3.
 *
 *  Redistributions of source code must retain the above copyright
 *  notice, and must be distributed with the license document above.
 *
 *  Redistributions in binary form must reproduce the above copyright
 *  notice, and must include the license document above in
 *  the documentation and/or other materials provided with the
 *  distribution.
 *
 *  This project is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *  ==end==
 */

#include "sthread.h"

#include <sys/socket.h>
#include <time.h>
#include <assert.h>
#include <unistd.h>

#include "timespec.h"
#include "satomic.h"


static inline DWORD
timespec_to_msec(const struct timespec *a)
{
    return (DWORD)(a->tv_sec * 1000) + (a->tv_nsec / 1000000);
}


int
pthread_cond_destroy(pthread_cond_t *cond)
{
    if (NULL == cond) {
        return EINVAL;
    }
    if (cond->flag) {
        assert(COND_MAGIC == cond->flag);
        cond->flag = 0;
        return 0;
    }
    return EINVAL;
}


int
pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
    (void)attr;
    if (NULL == cond) {
        return EINVAL;
    }
    assert(COND_MAGIC != cond->flag);           /* trap double init, yet *could* be a false positive */
    cond->lock = 1;
    InitializeConditionVariable(&cond->cv);     /* initialisation is unconditional */
    cond->flag = COND_MAGIC;
    cond->lock = 0;
    return 0;
}


static void __inline
condition_init_once(pthread_cond_t *cond)
{
#if !defined(NDEBUG)
    const long lock = cond->lock; assert(0 == lock || 1 == lock);
#endif
    if (COND_MAGIC != cond->flag) {
        satomic_lock(&cond->lock);
        if (COND_MAGIC != cond->flag) {
            InitializeConditionVariable(&cond->cv);
            cond->flag = COND_MAGIC;
        }
        satomic_unlock(&cond->lock);
    }
}


int
pthread_cond_broadcast(pthread_cond_t *cond)
{
    if (NULL == cond) {
        return EINVAL;
    }
    condition_init_once(cond);
    WakeAllConditionVariable(&cond->cv);
    return 0;
}


int
pthread_cond_signal(pthread_cond_t *cond)
{
    if (NULL == cond) {
        return EINVAL;
    }
    condition_init_once(cond);
    WakeConditionVariable(&cond->cv);
    return 0;
}


int
pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    BOOL ret;

    if (NULL == cond || NULL == mutex) {
        return EINVAL;
    }
    condition_init_once(cond);

    assert(MUTEX_MAGIC == mutex->flag);         /* assumed to be initialised */
    assert(1 == mutex->nest);                   /* must be locked once only */
    if (MUTEX_MAGIC != mutex->flag || 1 != mutex->nest) {
        return EINVAL;
    }

    --mutex->nest;
    ret = SleepConditionVariableCS(&cond->cv, &mutex->cs, INFINITE);
    ++mutex->nest;

    return (ret ? 0 : EINVAL);
}


static int
condition_wait(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *exptime, int isrelative)
{
    BOOL ret;

    if (NULL == cond || NULL == mutex) {
        return EINVAL;
    }
    condition_init_once(cond);

    if (exptime) {
        struct timespec then;

        if (isrelative) {                       /* relative time */
            then.tv_nsec = exptime->tv_nsec;
            then.tv_sec = exptime->tv_sec;

        } else {                                /* otherwise absolute time */
            struct timespec now;

            clock_gettime(CLOCK_REALTIME, &now);
            then.tv_nsec = exptime->tv_nsec - now.tv_nsec;
            then.tv_sec = exptime->tv_sec - now.tv_sec;
            if (then.tv_nsec < 0) {
                then.tv_nsec += 1000000000;     /* nsec/sec */
                --then.tv_sec;
            }

            if (((int)then.tv_sec < 0) ||
                ((then.tv_sec == 0) && (then.tv_nsec == 0))) {
                return ETIMEDOUT;               /* already passed */
            }
        }

        assert(COND_MAGIC == mutex->flag);      /* assumed to be initialised */
        assert(1 == mutex->nest);               /* must be locked once only */
        if (MUTEX_MAGIC != mutex->flag || 1 != mutex->nest) {
            return EINVAL;
        }

        --mutex->nest;
        ret = SleepConditionVariableCS(&cond->cv, &mutex->cs, timespec_to_msec(&then));
        ++mutex->nest;

        return (ret ? 0 : (GetLastError() == ERROR_TIMEOUT ? ETIMEDOUT : EINVAL));
    }

    assert(COND_MAGIC == mutex->flag);          /* assumed to be initialised */
    assert(1 == mutex->nest);                   /* must be locked once only */
    if (MUTEX_MAGIC != mutex->flag || 1 != mutex->nest) {
        return EINVAL;
    }

    --mutex->nest;
    ret = SleepConditionVariableCS(&cond->cv, &mutex->cs, INFINITE);
    ++mutex->nest;

    return (ret ? 0 : EINVAL);
}


int
pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime)
{
    return (condition_wait(cond, mutex, abstime, 0));
}


int
pthread_cond_timedwait_relative_np(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *reltime)
{
    return (condition_wait(cond, mutex, reltime, 1));
}

/*end*/

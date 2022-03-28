/*
 *  Simple win32 threads - mutexs.
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

#include "satomic.h"


int
pthread_mutexattr_init(pthread_mutexattr_t *attr)
{
    if (NULL == attr) {
        return EINVAL;
    }
    return 0;
}


int
pthread_mutexattr_destroy(pthread_mutexattr_t *attr)
{
    if (NULL == attr) {
        return EINVAL;
    }
    return 0;
}


int
pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    (void) attr;
    if (NULL == mutex) {
        return EINVAL;
    }
    assert(MUTEX_MAGIC != mutex->flag);         /* trap double init, yet *could* be a false positive */
    mutex->lock = 1;                            /* initialisation is unconditional */
#if defined(MUTEX_SPINCOUNT)
    InitializeCriticalSectionAndSpinCount(&mutex->cs, MUTEX_SPINCOUNT);
#else
    InitializeCriticalSection(&mutex->cs);
#endif
    mutex->flag = MUTEX_MAGIC;
    mutex->nest = 0;
    mutex->lock = 0;
    return 0;
}


static void __inline
mutex_init_once(pthread_mutex_t *mutex)
{
#if !defined(NDEBUG)
    const long lock = mutex->lock; assert(0 == lock || 1 == lock);
#endif
    if (MUTEX_MAGIC != mutex->flag) {
        satomic_lock(&mutex->lock);
        if (MUTEX_MAGIC != mutex->flag) {       /* runtime initialisation */
#if defined(MUTEX_SPINCOUNT)
            InitializeCriticalSectionAndSpinCount(&mutex->cs, MUTEX_SPINCOUNT);
#else
            InitializeCriticalSection(&mutex->cs);
#endif
            mutex->flag = MUTEX_MAGIC;
            mutex->nest = 0;
        }
        satomic_unlock(&mutex->lock);
    }
}


int
pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    if (NULL == mutex) {
        return EINVAL;
    }

    if (mutex->flag) {
        assert(MUTEX_MAGIC == mutex->flag);
        assert(0 == mutex->nest);               /* shouldnt be owned */
        mutex->flag = 0;
        DeleteCriticalSection(&mutex->cs);
        return 0;
    }

    return EINVAL;
}


int
pthread_mutex_lock(pthread_mutex_t *mutex)
{
    if (NULL == mutex) {
        return EINVAL;
    }

    mutex_init_once(mutex);
    EnterCriticalSection(&mutex->cs);
    ++mutex->nest;

    return 0;
}


int
pthread_mutex_trylock(pthread_mutex_t *mutex)
{
    if (NULL == mutex) {
        return EINVAL;
    }

    mutex_init_once(mutex);
    if (TryEnterCriticalSection(&mutex->cs)) {
        ++mutex->nest;
        return 0;
    }

    return EBUSY;
}


int
pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    if (NULL == mutex) {
        return EINVAL;
    }

    assert(MUTEX_MAGIC == mutex->flag);         /* initialised */
    assert(1 == mutex->nest);                   /* should be locked */
    --mutex->nest;

    LeaveCriticalSection(&mutex->cs);
    return 0;
}

/*end*/
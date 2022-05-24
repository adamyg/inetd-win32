/*
 *  Simple win32 threads - read-write locks.
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

#include <errno.h>
#include <assert.h>


int
pthread_rwlock_init(pthread_rwlock_t *rwlock, const pthread_rwlockattr_t *attr)
{
    (void)attr;
    if (NULL == rwlock) {
        return EINVAL;
    }
    InitializeSRWLock(&rwlock->srw);            /* initialisation is unconditional */
    return 0;
}


int
pthread_rwlock_destroy(pthread_rwlock_t *rwlock)
{
    if (NULL == rwlock) {
        return EINVAL;
    }
    return 0;
}


int
pthread_rwlock_rdlock(pthread_rwlock_t *rwlock)
{
    if (NULL == rwlock) {
        return EINVAL;
    }
    AcquireSRWLockShared(&rwlock->srw);
    return 0;
}


int
pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock)
{
    if (NULL == rwlock) {
        return EINVAL;
    }
    return (TryAcquireSRWLockShared(&rwlock->srw) ? 0 : EBUSY);
}


int
pthread_rwlock_wrlock(pthread_rwlock_t *rwlock)
{
    if (NULL == rwlock) {
        return EINVAL;
    }
    AcquireSRWLockExclusive(&rwlock->srw);
    rwlock->owner = (unsigned)GetCurrentThreadId();
    return 0;
}


int
pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock)
{
    if (NULL == rwlock) {
        return EINVAL;
    }
    if (! TryAcquireSRWLockExclusive(&rwlock->srw)) {
        return EBUSY;
    }
    rwlock->owner = GetCurrentThreadId();
    return 0;
}


int
pthread_rwlock_unlock(pthread_rwlock_t *rwlock)
{
    if (NULL == rwlock) {
        return EINVAL;
    }
    if (rwlock->owner) {
        assert(rwlock->owner == (unsigned)GetCurrentThreadId());
        rwlock->owner = 0;
        ReleaseSRWLockExclusive(&rwlock->srw);
    } else {
        ReleaseSRWLockShared(&rwlock->srw);
    }
    return 0;
}

/*end*/
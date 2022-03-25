/*
 *  Simple win32 threads - spinlocks locks.
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
#include "satomic.h"

#include <assert.h>


/*  The pthread_spin_init() function allocates any resources required for the 
 *  use of the spin lock referred to by lock and initializes the lock to be in
 *  the unlocked state.  The pshared argument must have one of the following values:
 *
 *  PTHREAD_PROCESS_PRIVATE
 *      The spin lock is to be operated on only by threads in thesame process as
 *      the thread that calls pthread_spin_init().
 *
 *  PTHREAD_PROCESS_SHARED
 *      The spin lock may be operated on by any thread in any process that has 
 *      access to the memory containing the lock.
 */
int
pthread_spin_init(pthread_spinlock_t *lock, int pshared)
{
        /*  Calling pthread_spin_init() on a spin lock that has already been
         *  initialized results in undefined behavior.
         */
    if (!lock || pshared)
        return EINVAL;
    lock->spin = 0;
    lock->owner = 0;
    return 0;
}


/*  The pthread_spin_destroy() function destroys a previously initialized spin lock, 
 *  freeing any resources that were allocated for that lock.  Destroying a spin lock
 *  that has not been previously been initialized or destroying a spin lock while
 *  another thread holds the lock results in undefined behavior.
 */
int
pthread_spin_destroy(pthread_spinlock_t *lock)
{
      /* Once a spin lock has been destroyed, performing any operation on the lock 
       * other than once more initializing it with pthread_spin_init() results in undefined behavior.
       */
    if (!lock)
        return EINVAL;
    if (lock->owner)
        return EBUSY;
    lock->spin = 0;
    lock->owner = 0;
    return 0;
}


/*  The pthread_spin_lock() function locks the spin lock referred to by lock.  
 *  If the spin lock is currently unlocked, the calling thread acquires the lock
 *  immediately.  If the spin lock is currently locked by another thread, the 
 *  calling thread spins, testing the lock until it becomes available, at which
 *  point the calling thread acquires the lock.
 */
int
pthread_spin_lock(pthread_spinlock_t *lock)
{
    const DWORD id = GetCurrentThreadId();
        /*  Calling pthread_spin_lock() on a lock that is already held by the
         *  caller or a lock that has not been initialized with pthread_spin_init(3) 
         *  results in undefined behavior.
         */
    if (!lock)
        return EINVAL;
    if (lock->owner == id)
        return EDEADLK;
    satomic_lock(&lock->spin);
    lock->owner = id;
    return 0;
}


/*  The pthread_spin_trylock() function is like pthread_spin_lock(), except that
 *  of the spin lock referred to by lock is currently locked, then, instead of
 *  spinning, the call returns immediately with the error EBUSY.
 */
int
pthread_spin_trylock(pthread_spinlock_t *lock)
{
    const DWORD id = GetCurrentThreadId();
    if (!lock)
        return EINVAL;
    if (lock->owner == id)
        return EDEADLK;
    if (0 == satomic_try_lock(&lock->spin))
        return EBUSY;
    lock->owner = id;
    return 0;
}


/*  The pthread_spin_unlock() function unlocks the spin lock referred to lock.
 *  If any threads are spinning on the lock, one of those threads will then acquire the lock.
 */
int
pthread_spin_unlock(pthread_spinlock_t *lock)
{
    const DWORD id = GetCurrentThreadId();
        /*  Calling pthread_spin_unlock() on a lock that is not held by the
         *  caller results in undefined behavior.
         */
    if (!lock)
        return EINVAL;
    if (lock->owner != id)
        return EPERM;
    lock->owner = 0;
    assert(satomic_read(&lock->spin));
    satomic_unlock(&lock->spin);
    return 0;
}

/*end*/
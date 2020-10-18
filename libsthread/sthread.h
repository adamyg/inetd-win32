#pragma once
#if !defined(STHREAD_H_INCLUDED)
#define STHREAD_H_INCLUDED
/*
 *  Simple pthreads ...
 *
 *    Return values:
 *      Most pthreads functions return 0 on success, and an error number on
 *      failure.  Note that the pthreads functions do not set errno.  For
 *      each of the pthreads functions that can return an error, POSIX.1-2001
 *      specifies that the function can never fail with the error EINTR.
 *
 *  Copyright (c) 2020, Adam Young.
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

#if defined(_WIN32)
#include <win32_include.h>
#endif

#include <sys/utypes.h>
#include <sys/uio.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  mutex's
 */

typedef struct pthread_tag {
    HANDLE handle;
    DWORD id;
} pthread_t;

typedef struct pthread_attr_tag {
    size_t attributes[4];
} pthread_attr_t;

#define PTHREAD_CREATE_DETACHED 1
#define PTHREAD_CREATE_JOINABLE 0

int pthread_attr_init(pthread_attr_t *attr);
int pthread_attr_destroy(pthread_attr_t *attr);
    //int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate);
    //int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *detachstate);
    //int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize);
    //int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize);
    //int pthread_attr_setstackaddr(pthread_attr_t *attr, void *stackaddr);
    //int pthread_attr_getstackaddr(const pthread_attr_t *attr, void **stackaddr);

int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg);
int pthread_join(pthread_t thread, void **value_ptr);

pthread_t pthread_self(void);
int pthread_equal(pthread_t t1, pthread_t t2);


/*
 *  mutex's
 */

typedef struct pthread_mutex_tag {
    CRITICAL_SECTION cs;
    long lock;
    unsigned flag, nest;
} pthread_mutex_t;

typedef struct pthread_mutexattr_tag {
    int attr;
} pthread_mutexattr_t;

#define PTHREAD_MUTEX_INITIALIZER {0}

int pthread_mutexattr_destroy(pthread_mutexattr_t *attr);
int pthread_mutexattr_init(pthread_mutexattr_t *attr);

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
int pthread_mutex_destroy(pthread_mutex_t *mutex);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_trylock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);


/*
 *  condition variables
 */

typedef struct pthread_cond_tag {
    CONDITION_VARIABLE cv;
    long lock;
    unsigned flag;
} pthread_cond_t;

typedef struct pthread_condattr_tag {
    int attr;
} pthread_condattr_t;

#define PTHREAD_COND_INITIALIZER {0}

int pthread_cond_destroy(pthread_cond_t *cond);
int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr);
int pthread_cond_broadcast(pthread_cond_t *cond);
int pthread_cond_signal(pthread_cond_t *cond);
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime);
int pthread_cond_timedwait_relative_np(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime);


/*
 *  once
 */

typedef struct pthread_once_tag {
    INIT_ONCE init_once;
} pthread_once_t;

#define PTHREAD_ONCE_INIT {0}

int pthread_once(pthread_once_t *once_control, void (*init_routine)(void));


/*
 *  thread storage
 */

typedef DWORD pthread_key_t;

int pthread_key_create(pthread_key_t *key, void (*destr_function) (void *));
int pthread_key_delete(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, const void *pointer);
void * pthread_getspecific(pthread_key_t key);


/*
 *  support
 */

#if !defined(CLOCK_REALTIME)
#define CLOCK_REALTIME 0
    //System-wide realtime clock. Setting this clock requires appropriate privileges.
#define CLOCK_MONOTONIC 1
    //Clock that cannot be set and represents monotonic time since some unspecified starting point.
extern int clock_gettime(int clockid, struct timespec *time_spec);
#endif
extern int usleep(useconds_t useconds);
extern unsigned sleep(unsigned seconds);

#ifdef __cplusplus
}
#endif

#endif  /*STHREAD_H_INCLUDED*/

/*end*/

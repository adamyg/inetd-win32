/*
 *  Simple win32 threads - threads.
 *
 *  API semnatics are not 100% POSIX,
 *   o avoid cloning pthread_t handles.
 *   o pthread_self() may only return a pseudo handle hence can not be used within pthread_detach().
 *   o pthread_join() avoid concurrent use
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

#include "sthread.h"
#include <processthreadsapi.h>

#include <sys/socket.h>
#include <time.h>
#include <assert.h>
#include <unistd.h>

#define ATTRIBUTE_MAGIC         0
#define ATTRIBUTE_STACKSIZE     1
#define ATTRIBUTE_DETACHED      2

#define VALID_HANDLE(_h)        (_h && INVALID_HANDLE_VALUE != _h)
#define CURRENT_THREAD          ((HANDLE)(-2))  /* current thread psuedo handle */

typedef struct {
    void *(*routine)(void *);
    void *arg;
} start_t;


static unsigned _stdcall
windows_thread(void *arg)
{
    start_t *start = (start_t *)arg;
    start->routine(start->arg);
    free((void *)start);
    return 0;
}


int
pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg)
{
    if (thread && start_routine) {
        start_t *start = malloc(sizeof(*start));

        if (start) {
            start->routine = start_routine;
            start->arg = arg;

            {   unsigned id = 0, stacksize = 0;
                if (attr) {                     /* optional stacksize? */
                    assert(0xBABEFACE == attr->attributes[ATTRIBUTE_MAGIC]);
                    stacksize = (unsigned)attr->attributes[ATTRIBUTE_STACKSIZE];
                }
                uintptr_t handle = _beginthreadex(NULL, stacksize, windows_thread, start, 0, &id);
                if (handle != (uintptr_t)-1) {
                    if (attr) {                 /* detached? */
                        if (PTHREAD_CREATE_DETACHED == attr->attributes[ATTRIBUTE_DETACHED]) {
                            CloseHandle((HANDLE) handle);
                            handle = 0;
                        }
                    }
                    thread->handle = (HANDLE) handle;
                    thread->id = id;
                    return 0;
                }
            }

            free((void *)start);
        }
        thread->handle = 0;
    }
    return EINVAL;
}


int
pthread_detach(pthread_t thread)
{
    if (VALID_HANDLE(thread.handle)) {
        if (CURRENT_THREAD == thread.handle) {
            assert(CURRENT_THREAD != thread.handle);
            return ENOSYS;                      /* pthread_detach(pthread_self()) */
        }
        CloseHandle(thread.handle);
        thread.handle = 0;
        return 0;
    }
    return EINVAL;
}


void
pthread_exit(void *value_ptr)
{
    assert(sizeof(char *) <= sizeof(unsigned));
    _endthreadex((unsigned)value_ptr);          /* does not explicity close the handle */
}


int
pthread_join(pthread_t thread, void **value_ptr)
{
    HANDLE handle = thread.handle;
    if (VALID_HANDLE(handle)) {
        if (CURRENT_THREAD != handle) {
            const DWORD ret = WaitForSingleObject(handle, INFINITE);
                /* Note: wont guard against concurrent join's */
            if (WAIT_OBJECT_0 == ret) {
                if (value_ptr) {                /* optional return value */
                    DWORD retval = 0;
                    if (! GetExitCodeThread(handle, &retval)) {
                        retval = 0;
                    }
                    *value_ptr = (void *)retval;
                }
                CloseHandle(thread.handle);
                thread.handle = 0;
                return 0;
            }
            return ESRCH;                       /* no thread could be found; cloned handle? */
        }
        return EDEADLK;                         /* deadlock was detected; self reference. */
    }
    return EINVAL;                              /* invalid/not-joinable. */
}


pthread_t
pthread_self(void)
{
    pthread_t pt = {0};
 // pt.handle = GetCurrentThread();             /* note: returns only a psuedo handle. */
 // assert(CURRENT_THREAD == pt.handle);
    pt.handle = 0;
    pt.id = GetCurrentThreadId();
    return pt;
}


int
pthread_equal(pthread_t t1, pthread_t t2)
{
    return (t1.id == t2.id);                    /* note: ignore handles as detached wont have one reported */
}


int
pthread_attr_init(pthread_attr_t *attr)
{
    if (attr) {
        (void) memset(attr, 0, sizeof(*attr));
        attr->attributes[ATTRIBUTE_MAGIC] = 0xBABEFACE;
    }
    return 0;
}


int
pthread_attr_destroy(pthread_attr_t *attr)
{
    if (attr) {
        assert(0xBABEFACE == attr->attributes[ATTRIBUTE_MAGIC]);
        memset(attr, 0, sizeof(*attr));
    }
    return 0;
}


int
pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate)
{
    assert(PTHREAD_CREATE_DETACHED == detachstate || PTHREAD_CREATE_JOINABLE == detachstate);
    if (NULL == attr) {
        return EINVAL;
    }
    if (PTHREAD_CREATE_DETACHED != detachstate && PTHREAD_CREATE_JOINABLE != detachstate) {
        return EINVAL;
    }
    assert(0xBABEFACE == attr->attributes[ATTRIBUTE_MAGIC]);
    attr->attributes[ATTRIBUTE_DETACHED] = detachstate;
    return 0;
}


int
pthread_attr_getdetachstate(const pthread_attr_t *attr, int *detachstate)
{
    if (NULL == attr || NULL == detachstate) {
        return EINVAL;
    }
    assert(0xBABEFACE == attr->attributes[ATTRIBUTE_MAGIC]);
    *detachstate = attr->attributes[ATTRIBUTE_DETACHED];
    return 0;
}


int
pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize)
{
    if (NULL == attr) {
        return EINVAL;
    }
    assert(0xBABEFACE == attr->attributes[ATTRIBUTE_MAGIC]);
    attr->attributes[ATTRIBUTE_STACKSIZE] = stacksize;
    return 0;
}


int
pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize)
{
    if (NULL == attr || NULL == stacksize) {
        return EINVAL;
    }
    assert(0xBABEFACE == attr->attributes[ATTRIBUTE_MAGIC]);
    *stacksize = attr->attributes[ATTRIBUTE_STACKSIZE];
    return 0;
}


int
pthread_attr_setstackaddr(pthread_attr_t *attr, void *stackaddr)
{
    (void) stackaddr;
    if (NULL == attr) {
        return EINVAL;
    }
    assert(0xBABEFACE == attr->attributes[ATTRIBUTE_MAGIC]);
    return ENOSYS;
}


int
pthread_attr_getstackaddr(const pthread_attr_t *attr, void **stackaddr)
{
    (void) stackaddr;
    if (NULL == attr) {
        return EINVAL;
    }
    assert(0xBABEFACE == attr->attributes[ATTRIBUTE_MAGIC]);
    return ENOSYS;
}

/*end*/


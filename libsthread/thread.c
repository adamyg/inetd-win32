/*
 *  Simple win32 threads - threads.
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

#include <sys/socket.h>
#include <time.h>
#include <assert.h>
#include <unistd.h>

#define ATTRIBUTE_MAGIC     0
#define ATTRIBUTE_STACKSIZE 1
#define ATTRIBUTE_DETACHED  2

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

            {   unsigned id = 0;
                uintptr_t handle = _beginthreadex(NULL, /*stack*/0, windows_thread, start, 0 /*or CREATE_SUSPENDED*/, &id);
                if (handle != (uintptr_t)-1) {
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
    if (thread.handle) {
        CloseHandle(thread.handle);
        thread.handle = 0;
    }
    return 0;
}


void
pthread_exit(void *value_ptr)
{
    _endthread();
}


int
pthread_join(pthread_t thread, void **value_ptr)
{
    assert(NULL == value_ptr);                  /* not supported */
    if (thread.handle) {
        DWORD ret = WaitForSingleObject(thread.handle, INFINITE);
        if (ret == WAIT_OBJECT_0) {
            return 0;
        }
    }
    return EINVAL;
}


pthread_t
pthread_self(void)
{
    pthread_t pt = {0};
    pt.handle = GetCurrentThread();
    pt.id = GetCurrentThreadId();
    return pt;
}



int
pthread_equal(pthread_t t1, pthread_t t2)
{
    return (t1.id == t1.id);
}


int
pthread_attr_init(pthread_attr_t *attr)
{
    if (attr) {
        memset(attr, 0, sizeof(*attr));
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
    (void) detachstate;
    if (NULL == attr) {
        return EINVAL;
    }
    assert(0xBABEFACE == attr->attributes[ATTRIBUTE_MAGIC]);
    return ENOSYS;
}


int
pthread_attr_getdetachstate(const pthread_attr_t *attr, int *detachstate)
{
    (void) detachstate;
    if (NULL == attr) {
        return EINVAL;
    }
    assert(0xBABEFACE == attr->attributes[ATTRIBUTE_MAGIC]);
    return ENOSYS;
}


int
pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize)
{
    (void) stacksize;
    if (NULL == attr) {
        return EINVAL;
    }
    assert(0xBABEFACE == attr->attributes[ATTRIBUTE_MAGIC]);
    return ENOSYS;
}


int
pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize)
{
    (void) stacksize;
    if (NULL == attr) {
        return EINVAL;
    }
    assert(0xBABEFACE == attr->attributes[ATTRIBUTE_MAGIC]);
    return ENOSYS;
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

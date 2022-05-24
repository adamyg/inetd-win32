/*
 *  Simple win32 threads - threads.
 *
 *  API semnatics are not 100% POSIX,
 *   o avoid cloning pthread_t handles.
 *   o pthread_join() avoid concurrent use
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

#if defined(__WATCOMC__) || \
        (defined(_MSC_VER) && (_MSC_VER <= 1800)) /*2013**/
#include <process.h>
#define  WIN32_LEAN_AND_MEAN
#include <Windows.h>
#if !defined(RTL_RUN_ONCE_INIT)
#define RTL_RUN_ONCE_INIT       {0}
#endif

#else //_MSC_VER
#include <processthreadsapi.h>
#endif

#include <sys/socket.h>
#include <time.h>
#include <assert.h>
#include <unistd.h>

#include "satomic.h"
#include "thread_instance.h"

#define ATTRIBUTE_MAGIC         0
#define ATTRIBUTE_STACKSIZE     1
#define ATTRIBUTE_DETACHED      2

#define VALID_HANDLE(_h)        (_h && INVALID_HANDLE_VALUE != _h)
#define CURRENT_THREAD          ((HANDLE)(-2))  /* current thread psuedo handle */

static pthread_instance_t *instance_new(void);

static pthread_once_t thread_once_ = RTL_RUN_ONCE_INIT; 
static DWORD thread_tls_ = 0;

static pthread_instance_t *
instance_new(void)
{      
    pthread_instance_t *instance;
    if (NULL != (instance = (pthread_instance_t *)calloc(sizeof(pthread_instance_t), 1))) {
        instance->magic = THREAD_MAGIC;
    }
    return instance;
}


static void
instance_free(pthread_instance_t *instance)
{      
    assert(THREAD_MAGIC == instance->magic);
    instance->magic = 0;
    free(instance);
}


static void
thread_tls_once(void)
{
    thread_tls_ = TlsAlloc();
}


pthread_instance_t *
_pthread_instance(int create)
{
    pthread_instance_t *instance;

    pthread_once(&thread_once_, thread_tls_once);
    if (NULL == (instance = (pthread_instance_t *) TlsGetValue(thread_tls_))) {
        if (create) {           
            if (NULL != (instance = instance_new())) {
                TlsSetValue(thread_tls_, (void *)instance);
            }
        }
    }
    return instance;
}


static unsigned _stdcall
windows_thread(void *arg)
{
    pthread_instance_t *instance = (pthread_instance_t *)arg;

    assert(THREAD_MAGIC == instance->magic);
    TlsSetValue(thread_tls_, instance);
    instance->ret = instance->routine(instance->arg);
    _pthread_tls_cleanup(instance);
    if (0 == instance->handle) {                    /* detached? */
        if (0 == satomic_read(&instance->joining)) { /* join in process? */
            TlsSetValue(thread_tls_, (void *)-1);
            instance_free(instance);
        }
    }
    return 0;
}


int
pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg)
{
    if (thread && start_routine) {
        pthread_once(&thread_once_, thread_tls_once);
        if (TLS_OUT_OF_INDEXES != thread_tls_) {
            pthread_instance_t *instance = instance_new();

            if (instance) {
                instance->routine = start_routine;
                instance->arg = arg;

                {   unsigned id = 0, stacksize = 0;
                    uintptr_t handle;
                    if (attr) {                     /* optional stacksize? */
                        assert(0xBABEFACE == attr->attributes[ATTRIBUTE_MAGIC]);
                        stacksize = (unsigned)attr->attributes[ATTRIBUTE_STACKSIZE];
                    }
                    handle = _beginthreadex(NULL, stacksize, windows_thread, instance, 0, &instance->id);
                    if (handle != (uintptr_t)-1) {
                        instance->handle = (HANDLE) handle;
                        if (attr) {                 /* detached? */
                            if (PTHREAD_CREATE_DETACHED == attr->attributes[ATTRIBUTE_DETACHED]) {
                                instance->handle = (HANDLE) 0;
                                CloseHandle((HANDLE) handle);
                            }
                        }
                        *thread = instance;
                        return 0;
                    }
                }
                instance_free(instance);
            }
        }
        *thread = 0;
    }
    return EINVAL;
}


int
pthread_detach(pthread_t thread)
{
    if (thread) {
        pthread_instance_t *instance = (pthread_instance_t *) thread;

        assert(THREAD_MAGIC == instance->magic);
        if (THREAD_MAGIC == instance->magic) {
            HANDLE handle = instance->handle;
            if (handle) {
                    /* Attempting to detach an already detached thread results in unspecified behavior. */
                instance->handle = 0;
                CloseHandle(handle);
                return 0;
            }
        }
    }
    return EINVAL;
}


void
pthread_exit(void *value_ptr)
{
    pthread_instance_t *instance = pthread_self();

    if (instance) {
        if (satomic_read(&instance->joining) || /* join in process? */
                instance->handle) {             /* attached ?*/
            instance->ret = value_ptr;
        } else {
            TlsSetValue(thread_tls_, (void *)-1);
            instance_free(instance);
        }
        _pthread_tls_cleanup(instance);
    }
    _endthreadex(0);                            /* does not explicity close the handle */
}


int
pthread_join(pthread_t thread, void **value_ptr)
{
    if (thread) {
        pthread_instance_t *instance = (pthread_instance_t *) thread;

        assert(THREAD_MAGIC == instance->magic);
        if (THREAD_MAGIC != instance->magic) {
                /* The thread specified by thread must be joinable. */
                /* Joining with a thread that has previously been joined results in
                   undefined behavior. */
            return EINVAL;
        }

        if (thread == pthread_self()) {
            return EDEADLK;                     /* deadlock was detected; self reference. */
        }

        if (! satomic_try_lock(&instance->joining)) {
            return EINVAL;                      /* another thread is already waiting */
                /* If multiple threads simultaneously try to join with the same thread, 
                   the results are undefined. */
        }

        if (CURRENT_THREAD == instance->handle /*root thread*/ ||
                WaitForSingleObject(instance->handle, INFINITE) != WAIT_OBJECT_0) {
            satomic_unlock(&instance->joining);

        } else {
            assert(instance->joining);
            if (instance->handle) {             /* still attached ? */
                if (value_ptr) {                /* optional return value */
                    *value_ptr = instance->ret;
                }
                CloseHandle(instance->handle);
                instance_free(instance);
                return 0;                       /* complete */
            }
            instance_free(instance);
        }
        return ESRCH;                           /* no thread could be found; cloned handle? */
    }
    return EINVAL;                              /* invalid/not-joinable. */
}


pthread_t
pthread_self(void)
{
    pthread_instance_t *instance = _pthread_instance(0);

    if (NULL == instance) {                     /* main thread */
        if (NULL != (instance = _pthread_instance(1))) {
            instance->handle = GetCurrentThread();
            instance->id = GetCurrentThreadId();
        }
    } else if ((void *)-1 == instance) {
        instance = NULL;                        /* terminated; shouldnt occur */
    } else {
        assert(THREAD_MAGIC == instance->magic);
    }
    return (pthread_t) instance;
}


int
pthread_equal(pthread_t t1, pthread_t t2)
{
    return (t1 && t1 == t2);
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
    if (NULL == attr || stacksize < PTHREAD_STACK_MIN) {
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
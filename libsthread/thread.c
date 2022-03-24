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
#include <processthreadsapi.h>

#include <sys/socket.h>
#include <time.h>
#include <assert.h>
#include <unistd.h>

#include "satomic.h"

#define ATTRIBUTE_MAGIC         0
#define ATTRIBUTE_STACKSIZE     1
#define ATTRIBUTE_DETACHED      2

#define VALID_HANDLE(_h)        (_h && INVALID_HANDLE_VALUE != _h)
#define CURRENT_THREAD          ((HANDLE)(-2))  /* current thread psuedo handle */

typedef struct pthread_instance {
    unsigned magic;
    HANDLE handle;
    satomic_lock_t joining;
    DWORD id;
    void *(*routine)(void *);
    void *arg;
    void *ret;

//#define PTHREAD_MAX_KEYS      32
#if defined(PTHREAD_MAX_KEYS)
    void *keys[PTHREAD_MAX_KEYS];
#endif
} instance_t;

#if defined(PTHREAD_MAX_KEYS)
typedef void (*destructor_t)(void *);

#define KEY_DESTRUCTOR_NULL     ((destructor_t)-1)
static pthread_rwlock_t key_lock_ = PTHREAD_RWLOCK_INITIALIZER;
static destructor_t key_destructors_[PTHREAD_MAX_KEYS] = {0};
#endif

static pthread_once_t thread_once_ = RTL_RUN_ONCE_INIT; 
static DWORD thread_tls_ = 0;


static instance_t *
instance_new()
{      
    instance_t *instance;
    if (NULL != (instance = calloc(sizeof(instance_t), 1))) {
        instance->magic = THREAD_MAGIC;
    }
    return instance;
}


static void
instance_free(instance_t *instance)
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


static instance_t *
thread_instance(int create)
{
    instance_t *instance;

    pthread_once(&thread_once_, thread_tls_once);
    if (NULL == (instance = (instance_t *) TlsGetValue(thread_tls_))) {
        if (create) {           
            if (NULL != (instance = instance_new())) {
                TlsSetValue(thread_tls_, (void *)instance);
            }
        }
    }
    return instance;
}


#if defined(PTHREAD_MAX_KEYS)
static pthread_key_t
thread_key_new(destructor_t destructor)
{
    pthread_key_t val = (pthread_key_t)-1;
    unsigned key;

    pthread_rwlock_wrlock(&key_lock_);
    for (key = 0; key < PTHREAD_MAX_KEYS; ++key) {
        if (NULL == key_destructors_[key]) {
            key_destructors_[key] =
                    (destructor ? destructor : KEY_DESTRUCTOR_NULL);
            val = key;
             break;
        }
    }
    pthread_rwlock_unlock(&key_lock_);
    return val;
}


static pthread_key_t
thread_key_delete(pthread_key_t key)
{
    if (key > PTHREAD_MAX_KEYS)
        return EINVAL;

    pthread_rwlock_wrlock(&key_lock_);
    key_destructors_[key] = NULL;
    pthread_rwlock_unlock(&key_lock_);
    return 0;
}


static void
thread_key_cleanup(instance_t *instance)
{  
    unsigned i, key;

    /* Both pthread_getspecific() and pthread_setspecific() may be called from a thread-specific 
       data destructor function. A call to pthread_getspecific() for the thread-specific data key being
       destroyed shall return the value NULL, unless the value is changed (after the destructor starts)
       by a call to pthread_setspecific().  Calling pthread_setspecific() from a thread-specific data
       destructor routine may result either in lost storage (after at least PTHREAD_DESTRUCTOR_ITERATIONS
       attempts at destruction) or in an infinite loop.
    */
    for (i = 0; i < PTHREAD_DESTRUCTOR_ITERATIONS; ++i) {
        unsigned count = 0;

        for (key = 0; key < PTHREAD_MAX_KEYS; ++key) {
            void *val = instance->keys[key];

            if (val) {
                destructor_t destructor;

                pthread_rwlock_rdlock(&key_lock_);
                destructor = key_destructors_[key];
                if (destructor) { //assigned
                    instance->keys[key] = NULL;
                    if (KEY_DESTRUCTOR_NULL != destructor)
                        destructor(val);
                    ++count;
                }
                pthread_rwlock_unlock(&key_lock_);
            }
        }

        if (0 == count) return;
    }   
}
#endif  /*PTHREAD_MAX_KEYS*/


static unsigned _stdcall
windows_thread(void *arg)
{
    instance_t *instance = (instance_t *)arg;

    assert(THREAD_MAGIC == instance->magic);
    TlsSetValue(thread_tls_, instance);
    instance->ret = instance->routine(instance->arg);
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
            instance_t *instance = instance_new(1);

            if (instance) {
                instance->routine = start_routine;
                instance->arg = arg;

                {   unsigned id = 0, stacksize = 0;
                    if (attr) {                     /* optional stacksize? */
                        assert(0xBABEFACE == attr->attributes[ATTRIBUTE_MAGIC]);
                        stacksize = (unsigned)attr->attributes[ATTRIBUTE_STACKSIZE];
                    }
                    uintptr_t handle = _beginthreadex(NULL, stacksize, windows_thread, instance, 0, &instance->id);
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
        instance_t *instance = (instance_t *) thread;

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
    instance_t *instance = pthread_self();

    if (instance) {
        if (satomic_read(&instance->joining) || /* join in process? */
                instance->handle) {             /* attached ?*/
            instance->ret = value_ptr;
        } else {
            TlsSetValue(thread_tls_, (void *)-1);
            instance_free(instance);
        }
    }
    _endthreadex(0);                            /* does not explicity close the handle */
}


int
pthread_join(pthread_t thread, void **value_ptr)
{
    if (thread) {
        instance_t *instance = (instance_t *) thread;

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
    instance_t *instance = thread_instance(0);

    if (NULL == instance) {                     /* main thread */
        if (NULL != (instance = thread_instance(1))) {
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

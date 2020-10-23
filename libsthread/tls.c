/*
 *  Simple win32 threads - thread local storage.
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


int
pthread_key_create(pthread_key_t *key, void (*destructor)(void *))
{
    DWORD dkey;

    if (! key) {
        return EINVAL;
    }

    assert(0 == destructor);                    /* not implemented */
    if (destructor) {
        return EINVAL;
    }
    if ((dkey = TlsAlloc()) != TLS_OUT_OF_INDEXES) {
        *key = dkey;
        return 0;
    }
    return EAGAIN;
}


int
pthread_key_delete(pthread_key_t key)
{
    if (TlsFree(key)) {
        return 0;
    }
    return EINVAL;
}


int
pthread_setspecific(pthread_key_t key, const void *pointer)
{
    if (TlsSetValue(key,(LPVOID)pointer)){
        return 0;
    }
    return EINVAL;
}


void *
pthread_getspecific(pthread_key_t key)
{
    return TlsGetValue(key);
}

/*end*/

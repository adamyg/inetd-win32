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


/*
 NAME
    pthread_once - dynamic package initialisation

 SYNOPSIS
    #include <pthread.h>

    int pthread_once(pthread_once_t *once_control, void (*init_routine)(void));
    pthread_once_t once_control = PTHREAD_ONCE_INIT;

 DESCRIPTION
    The first call to pthread_once() by any thread in a process, with a given once_control, will call the init_routine() with no arguments.
    Subsequent calls of pthread_once() with the same once_control will not call the init_routine().
    On return from pthread_once(), it is guaranteed that init_routine() has completed.
    The once_control parameter is used to determine whether the associated initialisation routine has been called.

    The function pthread_once() is not a cancellation point.
    However, if init_routine() is a cancellation point and is canceled, the effect on once_control is as if pthread_once() was never called.

    The constant PTHREAD_ONCE_INIT is defined by the header <pthread.h>.

    The behaviour of pthread_once() is undefined if once_control has automatic storage duration or is not initialised by PTHREAD_ONCE_INIT.

 RETURN VALUE
    Upon successful completion, pthread_once() returns zero. Otherwise, an error number is returned to indicate the error.

    EINVAL      If either once_control or init_routine is invalid.
*/

typedef void (*init_routine_t)(void);

static BOOL CALLBACK
InitHandleFunction(PINIT_ONCE InitOnce, PVOID Parameter, PVOID *lpContext)
{
    init_routine_t init_routine = (init_routine_t) Parameter;
    init_routine();
    return TRUE;
}


int
pthread_once(pthread_once_t *once_control, void (*init_routine)(void))
{
    PVOID lpContext = 0;

    if (!once_control || !init_routine) {
        return EINVAL;
    }

    (void) InitOnceExecuteOnce(&once_control->init_once, InitHandleFunction, (PVOID) init_routine, &lpContext);
    return 0;
}

/*end*/
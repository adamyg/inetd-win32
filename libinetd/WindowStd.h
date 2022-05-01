#pragma once
/* -*- mode: c; indent-width: 8; -*- */
/*
 * Common windows definitions
 * windows inetd service.
 *
 * Copyright (c) 2020 - 2022, Adam Young.
 * All rights reserved.
 *
 * The applications are free software: you can redistribute it
 * and/or modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 3.
 *
 * Redistributions of source code must retain the above copyright
 * notice, and must be distributed with the license document above.
 *
 * Redistributions in binary form must reproduce the above copyright
 * notice, and must include the license document above in
 * the documentation and/or other materials provided with the
 * distribution.
 *
 * This project is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * license for more details.
 * ==end==
 */

#if !defined(WINDOWS_MEAN_AND_LEAN)
#define  _WINSOCK_DEPRECATED_NO_WARNINGS	// gethostbyname
#define  WINDOWS_MEAN_AND_LEAN

#include <Winsock2.h>
#include <ws2tcpip.h>				// getaddrinfo
#if defined(HAVE_AF_UNIX)
#include <afunix.h>
#endif
#include <mswsock.h>				// IOCP
#include <Windows.h>

#pragma comment(lib, "Ws2_32.lib")		// WinSock2
#endif

//end
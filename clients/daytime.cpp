/* -*- mode: c; indent-width: 8; -*- */
/*
 * daytime client - RFC 867
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

/* 
 *  daytime client - RFC 867
 */

#if !defined(_WINSOCK_DEPRECATED_NO_WARNINGS)
#define _WINSOCK_DEPRECATED_NO_WARNINGS         /* gethostbyname */
#endif
#if !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#if !defined(WINDOWS_MEAN_AND_LEAN)
#define  WINDOWS_MEAN_AND_LEAN
#include <Winsock2.h>
#include <ws2tcpip.h>                           // getaddrinfo
#include <Windows.h>
#endif

#pragma comment(lib, "Ws2_32.lib")

#include <stdio.h>
#include <stdlib.h>
#include <string.h>                             /* for fgets */
#include <errno.h>
#include <io.h>

#include "client.h"

#define MAXLINE     8192                        /* max text line length */

int
main(int argc, char **argv)
{
        int  port = 13;                         /* default port */
        const char *host;
        char buf[MAXLINE] = { 0 };
        int  n;

        if (argc < 2 || argc > 3) {
                fprintf(stderr, "usage: %s <host> [<port>]\n", argv[0]);
                exit(0);
        }
        
        Client client;

        host = argv[1];
        if (3 == argc) {
                port = atoi(argv[2]);
        } else {
                if (0 == (port = client.getservport("daytime"))) {
                        fputs("unknown service: daytime tcp\n", stderr);
                        exit(1);
                }
        }

        if (! client.connect(host, port)) {
                printf("connection failure: %u\n", (unsigned)::WSAGetLastError());
        } else {
                n = client.read(buf, sizeof(buf));
                _write(1, buf, n), _write(1, "\n", 1);
                client.close();
        }
}

/*end*/

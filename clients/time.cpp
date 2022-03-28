/* -*- mode: c; indent-width: 8; -*- */
/*
 * time client - RFC 868
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

#define  _WINSOCK_DEPRECATED_NO_WARNINGS        /* gethostbyname */
#define  _CRT_SECURE_NO_WARNINGS
#define  WINDOWS_MEAN_AND_LEAN

#include <Winsock2.h>
#include <Windows.h>

#pragma comment(lib, "Ws2_32.lib")

#include <stdio.h>
#include <stdlib.h>
#include <string.h>                             /* for fgets */
#include <time.h>
#include <errno.h>
#include <io.h>

#include "client.h"

#define OFFSET ((DWORD)25567 * 24*60*60)        /* 1/1/1970 */

int
main(int argc, char **argv)
{
        int  port = 37;                         /* default port */
        const char *host;
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
                if (0 == (port = client.getservport("time"))) {
                        fputs("unknown service: time tcp\n", stderr);
                        exit(1);
                }
        }

        if (! client.connect(host, port)) {
                printf("connection failure: %u\n", (unsigned)::WSAGetLastError());
        } else {
                DWORD u32time;

                n = client.read((void *)&u32time, sizeof(u32time));
                if (n == sizeof(u32time)) {
                        u32time = ntohl(u32time);
                        if (u32time >= OFFSET) {
                                const time_t machtime = (u32time - OFFSET);
                                printf("machine time: %s", ctime(&machtime));

                        } else {
                                printf("machine time: <1970 or >7/Feb/2037");
                        }
                }
                client.close();
        }
}

/*end*/

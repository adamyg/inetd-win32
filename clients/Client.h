/* -*- mode: c; indent-width: 8; -*- */
/*
 * Basic TCP client
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

#include "../libinetd/inetd_namespace.h"

#if !defined(_WINSOCK_DEPRECATED_NO_WARNINGS)
#define _WINSOCK_DEPRECATED_NO_WARNINGS         /* gethostbyname */
#endif
#if !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <errno.h>

#if !defined(WINDOWS_MEAN_AND_LEAN)
#define  WINDOWS_MEAN_AND_LEAN
#include <Winsock2.h>
#include <ws2tcpip.h>                           // getaddrinfo
#include <Windows.h>
#endif

#pragma comment(lib, "Ws2_32.lib")


class Client {
        INETD_DELETED_FUNCTION(Client(const Client &client))
        INETD_DELETED_FUNCTION(Client& operator=(const Client &))

public:
        Client() : socket_(0)
        {
                initialise();
        }

        Client(SOCKET socket) : socket_(socket)
        {
        }

        ~Client()
        {
                close();
        }

        static unsigned
        getservport(const char *name, const char *proto = "tcp")
        {
                struct servent *sp = ::getservbyname(name, proto);
                if (NULL == sp) {
                        return 0;
                }
                return ::ntohs(sp->s_port);
        }

        bool
        connect(const char *hostname, int port)
        {
                struct hostent *hp = NULL;
                struct sockaddr_in serveraddr = {0};
                SOCKET fd;

                if (socket_) {
                       return false;
                }

                if ((fd = ::socket(AF_INET, SOCK_STREAM, 0)) < 0) {
                        return false;
                }

                if (NULL == (hp = ::gethostbyname(hostname))) {
                        const DWORD ret = WSAGetLastError();
                        ::closesocket(fd);
                        WSASetLastError(ret);
                        return false;
                }

                memset((char *)&serveraddr, 0, sizeof(serveraddr));
                serveraddr.sin_family = AF_INET;
                memcpy((char *)&serveraddr.sin_addr.s_addr, (const char *)hp->h_addr, hp->h_length);
                serveraddr.sin_port = htons(port);

                if (::connect(fd, (struct sockaddr*)&serveraddr, sizeof(serveraddr)) < 0) {
                        const DWORD ret = WSAGetLastError();
                        ::closesocket(fd);
                        WSASetLastError(ret);
                        return false;
                }

                socket_ = fd;
                return true;
        }

        void
        close()
        {
                if (socket_) {
                        ::closesocket(socket_);
                        socket_ = 0;
                }
        }

        int
        read(void *buffer, size_t nbyte)
        {
                int ret;

                if (! socket_) {
                        errno = EBADF;
                        ret = -1;
                } else {
                        if (SOCKET_ERROR == (ret =
                                ::recvfrom(socket_, (char *)buffer, (int)nbyte, 0, NULL, 0))) {
                            errno = EIO;
                            ret = -1;
                    }
                }
                return ret;
        }

        int
        write(const void *buffer, size_t nbyte)
        {
                int ret;

                if (! socket_) {
                        errno = EBADF;
                        ret = -1;
                } else {
                        if (SOCKET_ERROR == (ret =
                                    ::sendto(socket_, (const char *)buffer, (int)nbyte, 0, NULL, 0))) {
                                errno = EIO;
                                ret = -1;
                        }
                }
                return ret;
        }

        size_t
        readline(void *buffer, size_t sz)
        {
                unsigned total = 0;
                char *buf = (char *)buffer;
                char ch;

                if (! socket_ || sz <= 0 || NULL == buffer) {
                        return -1;
                }

                for (;;) {
                        const int num =
                                ::recvfrom(socket_, &ch, (int)1, 0, NULL, 0);

                        if (num < 0 /*SOCKET_ERROR*/) {
                               return -1;
                        } else if (0 == num) {
                                if (0 == total)
                                        return 0;
                                break;
                        } else {
                                if (total < (sz - 1)) {
                                        ++total;
                                        *buf++ = ch;
                                }
                                if (ch == '\n') break;
                        }
                }
                *buf = '\0';
                return total;
        }

public:
        static void
        initialise()
        {
                static WSADATA wsaData = {0};
                if (0 == wsaData.wVersion) {
                        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
                                wsaData.wVersion = 0;
                        }
                }
        }

private:
        SOCKET socket_;
};

//end

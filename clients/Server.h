/* -*- mode: c; indent-width: 8; -*- */
/*
 * Basic TCP server
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

#include <vector>

#if !defined(WINDOWS_MEAN_AND_LEAN)
#define  _WINSOCK_DEPRECATED_NO_WARNINGS        // gethostbyname
#define  WINDOWS_MEAN_AND_LEAN

#include <Winsock2.h>
#include <ws2tcpip.h>                           // getaddrinfo
#include <Windows.h>

#pragma comment(lib, "Ws2_32.lib")
#endif

class Server {
    Server(const Server &) = delete;
    Server& operator=(const Server &) = delete;

    typedef std::vector<SOCKET> Sockets;

public:
    Server() : shutdown_(false)
    {
        initialise();
    }

    ~Server()
    {
        close();
    }

    bool
    bind(const char *nodename, const char *port)
    {
        struct addrinfo hints, *res;

        // resolve interfaces

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;            // since we're going to bind on this socket.

        if (getaddrinfo(nodename, port, &hints, &res) != NO_ERROR) {
            //  nodename,
            //      A pointer to a NULL-terminated ANSI string that contains a host (node) name or a numeric host address string.
            //      For the Internet protocol, the numeric host address string is a dotted-decimal IPv4 address or an IPv6 hex address.
            //
            //  serviceName,
            //      A pointer to a NULL-terminated ANSI string that contains either a service name or port number represented as a string.
            //      A service name is a string alias for a port number.
            //      For example, 'http' is an alias for port 80 defined by the Internet Engineering Task Force (IETF) as the default port used by web servers for the HTTP protocol.
            //      Possible values for the pServiceName parameter when a port number is not specified are listed in the following file:
            //
            //          '%WINDIR%\system32\drivers\etc\services'
            //
            fprintf(stderr, "getaddrinfo failed: %u\n", (unsigned)::WSAGetLastError());
            goto error;
        }

        // bind to each returned interface

        for (struct addrinfo *addr = res; addr; addr = addr->ai_next) {
            const char *type = NULL;
            SOCKET socket = INVALID_SOCKET;

            // IPv4 or IPv6

            switch (addr->ai_family) {
            case AF_INET:
                type = "IPv4";
                break;
            case AF_INET6:
                type = "IPv6";
                break;
            }

            // create socket and bind to socket

            if (type) {
                printf("Trying Address : (%s) ", type);
                    print_address(addr->ai_addr, (DWORD) addr->ai_addrlen);

                if ((socket = ::WSASocket(addr->ai_family,
                        addr->ai_socktype, addr->ai_protocol, NULL, 0, 0)) == INVALID_SOCKET) {
                    fprintf(stderr,"socket creation failure: %u\n", (unsigned)::WSAGetLastError());
                    fprintf(stderr,"Ignoring this address and continuing with the next. \n\n");
                    continue;
                }

                if (::bind(socket, (struct sockaddr*)addr->ai_addr, (int) addr->ai_addrlen) == SOCKET_ERROR) {
                    fprintf(stderr, "bind failure: %u\n", (unsigned)::WSAGetLastError());
                    ::closesocket(socket);
                    continue;
                }

                sockets_.push_back(socket);
            }
        }

        freeaddrinfo(res);
        if (sockets_.empty()) {
            fprintf(stderr, "couldn't bind to any suitable socket\n");
            goto error;
        }
        return true;

    error:;
        close();
        return false;
    }

    template <typename Accept>
    bool listen(Accept &accept, unsigned depth = 5)
    {
        // enable each interface
        fd_set listeners = {0};

        shutdown_ = false;
        for (Sockets::iterator it(sockets_.begin()), end(sockets_.end()); it != end;) {
            SOCKET socket = *it;

            if (SOCKET_ERROR == ::listen(socket, depth))  {
                fprintf(stderr, "listen failure: %u\n", (unsigned)::WSAGetLastError());
                ::closesocket(socket);
                *it = INVALID_SOCKET;
                it = sockets_.erase(it);
                continue;
            }
            FD_SET(socket, &listeners);
            ++it;
        }

        if (0 == sockets_.size()) {
            fprintf(stderr, "couldn't listen on any suitable socket\n");
            return false;
        }

        while (true) {
            struct timeval timeval = { 30, 0 };
            fd_set readers = listeners;
            int n;

	    if ((n = ::select(sockets_.size(), &readers,
                        (fd_set *)NULL, (fd_set *)NULL, &timeval)) <= 0) {
                if (shutdown_) {
                    break;
                }
                accept((SOCKET)-1);
                continue;
	    }

            for (Sockets::iterator it(sockets_.begin()), end(sockets_.end()); it != end; ++it) {
                SOCKET socket = *it;
                if (FD_ISSET(socket, &readers)) {
                    accept(socket);
                    --n;
                }
            }
        }
        return true;
    }

    void
    signal_shutdown()
    {
        shutdown_ = true;
    }

    void
    close()
    {
        if (sockets_.size()) {
            for (Sockets::iterator it(sockets_.begin()), end(sockets_.end()); it != end; ++it) {
                ::closesocket(*it);
            }
            sockets_.clear();
        }
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
    static void
    print_address(LPSOCKADDR pSockAddr, DWORD dwSockAddrLen)
    {
        // INET6_ADDRSTRLEN is the maximum size of a valid IPv6 address including port,colons,NULL,etc.
        char buf[INET6_ADDRSTRLEN] = {0};
        DWORD dwBufSize =  sizeof(buf);

        if (SOCKET_ERROR == WSAAddressToString(pSockAddr, dwSockAddrLen, NULL, buf, &dwBufSize)) {
            printf("ERROR: WSAAddressToString failed %u\n", (unsigned)::WSAGetLastError());
        } else  {
            printf("%s\n", buf);
        }
    }

private:
    Sockets sockets_;
    bool shutdown_;
};

//end

/*
 * Socket duplication test client (and server)
 * windows inetd service.
 *
 * Copyright (c) 2020, Adam Young.
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

#pragma comment(lib, "Ws2_32.lib")

#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>                              /* for fgets */
#include <io.h>

#include "../libinetd/ScopedHandle.h"
#include "../libinetd/SocketShare.h"
#include "../libinetd/ProcessGroup.h"

#include "Server.h"
#include "Client.h"

#define DEFAULT_PORT    "8765"

static void             sigchld();
static void             usage(const char *prog, const char *msg = NULL, ...);

class Acceptor {
public:
    Acceptor(inetd::ProcessGroup &process_group, const char *progname) : 
            process_group_(process_group), progname_(progname) {
    }

    void operator()(SOCKET listener) {
        if ((SOCKET)-1 == listener) {           // timeout event
            int status;

            while (true) {
                int pid = process_group_.wait(true  /*nohang*/, status);
                if (pid <= 0)
                    break;
                printf("child[%d]: exited, status %u\n", pid, status);
            }

        } else {
            struct sockaddr addr = {0};
            int addrlen = sizeof(addr);
            SOCKET socket;

            socket = ::accept(listener, &addr, &addrlen);
            if (INVALID_SOCKET != socket) {     // create child
                inetd::SocketShare::Server 
                    server(process_group_.job_handle(), progname_);

                if (server.publish(socket)) {
                    process_group_.track(server.child());
                }

                ::closesocket(socket);          // local reference

            } else {
                fprintf(stderr, "accept() failed: %u\n", (unsigned)::WSAGetLastError());
            }
        }
    }

private:
    inetd::ProcessGroup &process_group_;
    const char *progname_;
};


int
main(int argc, char **argv)
{
    const char *progname = argv[0];
    const char *address  = NULL, *port = DEFAULT_PORT;
    const char *basename = NULL;
    bool asserver = true;

    // command line options

    if (argc > 1) {
        for (int i = 1; i < argc; ++i) {
            if ((argv[i][0] == '-') || (argv[i][0] == '/')) {
                const char optchr = argv[i][1];

                if (argv[i][2]) {               /* -scap */
                    usage(progname, "unknown option '%s'", argv[i]);
                }

                switch (optchr) {
                case 's':   // server mode
                    asserver = true;
                    break;
                case 'i':   // client mode
                    if ((i + 1) < argc) {
                        basename = argv[++i];
                    } else {
                        usage(progname, "missing argument interface name");
                    }
                    asserver = false;
                    break;
                case 'a':   // address
                    if ((i + 1) < argc) {
                        address = argv[++i];
                    } else {
                        usage(progname, "missing argument address");
                    }
                    break;
                case 'p':   // port
                    if ((i + 1) < argc) {
                        port = argv[++i];
                    } else {
                        usage(progname, "missing argument port");
                    }
                    break;
                default:    // invalid
                    usage(progname, "unknown option '%c'", optchr);
                    break;
                }
            } else {
                usage(progname);
            }
        }
    }

    // execute mode

    if (asserver) {
        inetd::ProcessGroup process_group; 
        Server server;

        process_group.open(sigchld);            // process monitoring
        if (server.bind(address, port)) {
            server.listen(Acceptor(process_group, progname));
        }
        return 0;
    }

    // client mode

    printf("initialising ...\n");

    SOCKET socket = inetd::SocketShare::GetSocket(basename, 0);
    if (INVALID_SOCKET != socket) {
        Client client(socket);
        char buf[1024+1];                       // \n
        int n;
                                                // echo
        printf("connected ...\n");
        while ((n = client.readline(buf, sizeof(buf)-1)) > 0) {
            if (buf[n - 1] == '\n') {
                buf[--n] = 0, printf("echo: <%s>\n", buf);
                buf[n++] = '\n', client.write(buf, n);
            } else {
                printf("echo: <%s> ...\n", buf);
                client.write(buf, n);
            }
        }
        printf("bye\n");
    }
}


static void
sigchld()
{
    printf("sigchld\n");
}


static void
usage(const char *progname, const char *msg /*= NULL*/, ...)
{
    if (msg) {
        va_list ap;
        va_start(ap, msg);
        vfprintf(stderr, msg, ap), fputs("\n\n", stderr);
        va_end(ap);
    }

    fprintf(stderr, 
        "Usage: %s [-s] [-a <address>] [-p <port>]\n\n", progname);

    fprintf(stderr,
        "options:\n"
        "   -s              Server mode (default), otherwise client.\n"
        "   -a <address>    Address, default localhost.\n"
        "   -p <port>       Port, default (%s)\n",
        DEFAULT_PORT);

    exit(3);
}

/*end*/

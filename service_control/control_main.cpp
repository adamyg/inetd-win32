/* -*- mode: c; indent-width: 8; -*- */
/*
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

#include <Windows.h>

#include <cstdio>
#include <cstdlib>
#include <iostream>

#ifndef SERVICE_CONTROL_USER
#define SERVICE_CONTROL_USER    128             // User control message base.
#endif

#include <buildinfo.h>

static void usage(const char *progname, const char *msg = NULL, ...);

int
main(int argc, const char **argv)
{
    const char *progname = argv[0];
    SC_HANDLE schSCManager = 0, schService = 0;
    SERVICE_STATUS controlParms = {0};
    DWORD retStatus = (DWORD)-1;
    bool success = false, verbose = false;

    // Command line

    if (argc > 1) {
        for (int i = 1; i < argc; ++i) {
            const char *arg = argv[i];

            if ((arg[0] == '-') || (arg[0] == '/')) {
                const char optchr = arg[1];

                if (optchr && arg[2]) { /* -?xxx options */
                    if ('-' == arg[1]) {
                        if (0 == strcmp(arg, "--verbose")) {
                            verbose = true;
                            continue;
                        } else if (0 == strcmp(arg, "--help")) {
                            usage(progname);
                        }
                    }
                    usage(progname, "unknown option '%s'", arg);
                }

                switch (optchr) {
                case 'd':   // run-time debug
                    verbose = true;
                    break;
                case 'h':   // help
                case '?':
                    usage(progname);
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

    // Execute

    schSCManager = ::OpenSCManagerA(NULL, NULL, GENERIC_READ);
    if (NULL != schSCManager) {
        schService = ::OpenServiceA(schSCManager, "inetd_service",
                            SERVICE_USER_DEFINED_CONTROL | SERVICE_QUERY_STATUS);

        if (NULL != schService) {
            if (verbose) {
                std::cout << "connected to service" << std::endl;
            }

            const DWORD retStatus =
                    ::ControlService(schService, SERVICE_CONTROL_USER, &controlParms);
            if (retStatus) {                    // service return code
                if (verbose) {
                    std::cout << "Command sent, return code from service was " << \
                        controlParms.dwWin32ExitCode << std::endl;
                }
                success = true;
            } else {
                std::cout << "Sending command failed : " \
                    << ::GetLastError() << std::endl;
            }
            ::CloseServiceHandle(schService);

        } else {
            std::cout << "Error: could not connect to Service : " << \
                ::GetLastError() << std::endl;
        }
        ::CloseServiceHandle(schSCManager);

    } else {
        std::cout << "Error: could not open service manager : " << \
            ::GetLastError() << std::endl;
    }
    return (success ? 0 : 1);
}


static void
usage(const char *progname, const char *msg /*= NULL*/, ...)
{
    if (msg) {
        va_list ap;
        va_start(ap, msg);
        vfprintf(stderr, msg, ap), fputs("\n\n", stderr);
        va_end(ap);
    } else {
        fprintf(stderr, "%s %s [Build %s %s] - services control\n\n",
            WININETD_PACKAGE, WININETD_VERSION, WININETD_BUILD_NUMBER, WININETD_BUILD_DATE);
    }

    fprintf(stderr,
        "Usage: %s [options]\n\n", progname);
    fprintf(stderr,
        "options:\n"\
        "   -d,--verbose        Diagnostics.\n"\
        "   -h,--help           Command line usage.\n");
    exit(3);
}

//end
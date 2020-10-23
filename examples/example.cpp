/* -*- mode: c; indent-width: 4; -*- */
/*
 * Example client
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

#include <cassert>

#include "../libinetd/ServiceGetOpt.h"
#include "../libinetd/SocketShare.h"

static const char *short_options = "i:";
static struct inetd::Getopt::Option long_options[] = {
    { "usage", inetd::Getopt::argument_none, NULL, 1000 },
    { NULL }
};

extern void usage(const char *fmt = NULL, ...); /*no-return*/
extern int  process(SOCKET socket);

int
main(int argc, const char **argv)
{
    inetd::Getopt options(short_options, long_options, argv[0]);
    const char *basename = NULL;
    std::string msg;

    while (-1 != options.shift(argc, argv, msg)) {
        switch (options.optret()) {
        case 'i':   // interface
            basename = options.optarg();
            break;
        case 1000:  // usage
            usage();
        default:    // error
            usage("%s", msg.c_str());
        }
    }

    if (NULL == basename) {
        usage("missing interface specification");
    }

    argv += options.optind();
    if (0 != (argc -= options.optind())) {
        usage("unexpected arguments");
    }

    SOCKET socket = inetd::SocketShare::GetSocket(basename);
    if (INVALID_SOCKET != socket) {
        return process(socket);
    }
    std::abort();
    /*NOTREACHED*/
}

void
usage(const char *fmt, ...)
{
#if !defined(PROGNAME)
#define PROGNAME "example"
#endif

    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap), fputs("\n\n", stderr);
        va_end(ap);
    }

    fprintf(stderr,
        "Usage: %s [-i interface]]\n\n", PROGNAME);
    fprintf(stderr,
        "options:\n"
        "   -i <interface>  Parent interface.\n");
    exit(3);
}


int
process(SOCKET socket)
{
    const char text[] = "hello world\n";

    send(socket, text, sizeof(text)-1, 0);
    shutdown(socket, SD_SEND);
    return 0;
}

/*end*/


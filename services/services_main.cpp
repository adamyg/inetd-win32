/* -*- mode: c; indent-width: 8; -*- */
/*
 * inetd builtin services.
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

#include "../libinetd/SocketShare.h"
#include "../libinetd/inetd.h"

#include <err.h>
#include <unistd.h>

#include <buildinfo.h>

#include "service_license.h"
#include "bsd_license.h"

static void	usage(const char *prog, const char *msg = NULL, ...);
static void	license(void);

extern int	options = 0;
extern int	debug = 0;

int
main(int argc, const char **argv)
{
	const char *progname = argv[0];
	const char *basename = NULL,
		*service = "default";

	// command line options

	if (argc > 1) {
		for (int i = 1; i < argc; ++i) {
			if ((argv[i][0] == '-') || (argv[i][0] == '/')) {
				const char optchr = argv[i][1];

				if (argv[i][2]) {	/* -dis */
					if (0 == strcmp(argv[i], "--license")) {
						license();
					} else if (0 == strcmp(argv[i], "--help")) {
						usage(progname);
					}
					usage(progname, "unknown option '%s'", argv[i]);
				}

				switch (optchr) {
				case 'd':   // run-time debug
					debug = 1;
					options |= SO_DEBUG;
					break;
				case 'i':   // interface mode
					if ((i + 1) < argc) {
						basename = argv[++i];
					} else {
						usage(progname, "missing interface label argument");
					}
					break;
				case 's':   // service
					if ((i + 1) < argc) {
						service = argv[++i];
					} else {
						usage(progname, "missing service name argument");
					}
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

	if (NULL == basename || NULL == service) {
		usage(progname);
	}

	// execute service

	SOCKET socket = inetd::SocketShare::GetSocket(basename);
	if (INVALID_SOCKET == socket) {
		exit(3);	// unable to transfer socket
	}

	// socket options

	if (SO_DEBUG & options) {
		int on = 1;
		if (setsockopt(socket, SOL_SOCKET, SO_DEBUG, (char *)&on, sizeof(on)) < 0) {
			warnx("setsockopt (SO_DEBUG): %u", (unsigned) WSAGetLastError());
				// Note: Enables debug output. Microsoft providers currently do not output any debug information.
		}
	}

	// route

	const struct biltin *bi;
	for (bi = biltins; bi->bi_service; ++bi) {
		if (bi->bi_socktype == SOCK_STREAM &&
				0 == strcmp(bi->bi_service, service)) {
			break;
		}
	}

	if (NULL == bi->bi_service) {
		warnx("internal service %s unknown", service);
		exit(3);
	}

	struct servtab sep;
	sep.se_service = (const char *)service;
	(bi->bi_fn)(socket, &sep);
	return 0;
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
		fprintf(stderr, "%s %s [Build %s %s] - services utility\n\n",
		    WININETD_PACKAGE, WININETD_VERSION, WININETD_BUILD_NUMBER, WININETD_BUILD_DATE);
	}

	fprintf(stderr,
		"Usage: %s -i <interface> -s <service>\n\n", progname);
	fprintf(stderr,
		"options:\n"
		"   -i <interface>  Interface label.\n"
		"   -s <service>    Service name.\n"
		"   --license       License.\n");
	exit(3);
}


static void
license(void)
{
	printf(WININETD_PACKAGE " - " WININETD_PACKAGE_NAME " " WININETD_VERSION "\n\n");

	printf("Copyright (C) 2020-2022 Adam Young, All rights reserved.\n");
	printf("Licensed under GNU General Public License version 3.0.\n");

	printf("\n\nThis program comes with ABSOLUTELY NO WARRANTY. This is free software,\n");
	printf("\nand you are welcome to redistribute it under certain conditions.\n");
	printf("See LICENSE details below.\n\n");
	for (unsigned i = 0; i < _countof(service_license); ++i) {
		printf("%s\n", service_license[i]);
	}

	for (unsigned i = 0; i < _countof(bsd_license); ++i) {
		printf("%s\n", bsd_license[i]);
	}
	exit(3);
}


/*extern "C"*/ void
inetd_setproctitle(const char *a, int s)
{
	socklen_t size;
	struct sockaddr_storage ss;
	char buf[80], pbuf[NI_MAXHOST];

	size = sizeof(ss);
	if (getpeername(s, (struct sockaddr *)&ss, &size) == 0) {
		getnameinfo((struct sockaddr *)&ss, SOCKLEN_SOCKADDR_STORAGE(ss), pbuf, sizeof(pbuf), NULL, 0, NI_NUMERICHOST);
		(void) sprintf_s(buf, sizeof(buf), "%s [%s]", a, pbuf);
	} else {
		(void) sprintf_s(buf, sizeof(buf), "%s", a);
	}
	setproctitle("%s", buf);
}


/*extern "C"*/ int
check_loop(const struct sockaddr *sa, const struct servtab *sep)
{
	// Not required for TCP services.
	return 0;
}

//end
/*
 * inetd service ...
 *
 * Copyright (c) 2020 - 2022, Adam Young.
 * All rights reserved.
 *
 * This file is part of inetd-win32.
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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>

#include "../libNTService/NTServiceConfig.h"    // CNTServiceConfig
#include "../libNTService/NTServiceControl.h"   // CNTServiceControl

#include "../libService/Service.h"              // Service implementation.
#include "../libinetd/libinetd.h"               // implementation

#include <iostream>
#include <fstream>
#include <algorithm>
#include <string>

#include <buildinfo.h>

#include "inetd_usage.h"
#include "service_license.h"
#include "bsd_license.h"

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Iphlpapi.lib")

#define OPTIONS 	"UNhiVd"		// short arguments
#define ARGUMENTS	"S:Z:p:"
#define ARGUMENTDESCS	""

static const struct option long_options[] = {   // long arguments
	{ "service",        required_argument,  NULL, 'S'  },   //-S,service <name>
	{ "config-file",    required_argument,  NULL, 'Z'  },   //-Z,--config-file <file>
	{ "noconfig",       no_argument,        0,    'N'  },   //-N,--noconfig
	{ "help",           no_argument,        0,    'h'  },   //-h,--help
	{ "license",        no_argument,        0,    'i'  },   //-i,--license
	{ "version",        no_argument,        0,    'V'  },   //-V,--version
	{ "daemon",         no_argument,        0,    'd'  },   //-d,--daemon
	{ "delay",          no_argument,        NULL, 1001 },   //--start-delay
	{ "conmode",        no_argument,        NULL, 1002 },   //--conmode
	{ "no-conmode",     no_argument,        NULL, 1003 },   //--no-conmode
	{ "stdout",         no_argument,        NULL, 1004 },   //--stdout
	{ "no-stdout",      no_argument,        NULL, 1005 },   //--no-stdout
	{ "logger",         no_argument,        NULL, 1006 },   //--logger
	{ "no-logger",      no_argument,        NULL, 1007 },   //--no-logger
	{ NULL }
	};

enum { OUSAGE = 1, OHELP, OLICENSE, OVERSION };

static bool		isconsole();
static void		usage(const char *msg);
static void		help(void);
static void		license(void);
static void		version(void);

static const char * appname = "inetd";
static bool		console_mode;


int
main(int argc, const char **argv)
{
	char t_service_name[256] = {0}, msg[512] = {0};
	const char *service_name = CNTService::DefaultServiceName(argv[0], t_service_name, sizeof(t_service_name));

	Service::Options options;
	const char **oargv = argv, *verb = NULL;
	unsigned do_info = 0;
	int ch;

	// Service options
	// Note: Leading "-" on options to force parse in-order, ie stop at the first non option.
	// Several options are duplicates of inetd() options, which need to be handled prior to service initialisation.

	console_mode = isconsole();

	while (NULL == verb && 0 == msg[0] && !do_info &&
			-1 != (ch = getopt_long2(argc, (char * const *)argv,
					"-" OPTIONS ARGUMENTS, long_options, NULL, msg, sizeof(msg)))) {
		switch (ch) {
		case 'S':   //-S,--service=<name>
			service_name = optarg;
			break;
		case 'Z':   //-Z,--config-file=<file>
			options.conf = optarg;
			break;
		case 'd':   //-d,--daemon (hide console)
			options.daemon_mode = true;
			break;
		case 'N':   //-N,--noconfig
			options.ignore = true;
			break;
		case 'h':   //-h,--help
			do_info = OHELP;
			break;
		case 'i':   //-i,--license
			do_info = OLICENSE;
			break;
		case 'V':   //-V,--version
			do_info = OVERSION;
			break;
		case 1001:  //--delay-start (debug support)
			options.delay_start = true;
			break;
		case 1002:  //--conmode (debug support)
			console_mode = true;
			break;
		case 1003:  //--no-conmode (debug support)
			console_mode = false;
			break;
		case 1004:  //--stdout
			options.console_output = true;
			break;
		case 1005:  //--no-stdout
			options.console_output = false;
			break;
		case 1006:  //--logger
			options.logger = true;
			break;
		case 1007:  //--no-logger
			options.logger = false;
			break;
		case 1:     //verb -- first non optional argument
			verb = optarg;
			--optind;		// move back cursor
			break;
		case ':':
		case '?':
		default:
			if (0 == msg[0]) {
				snprintf(msg, sizeof(msg)-1, // error; default message
				      "%s: illegal option : %c", service_name, optopt);
			}
			break;
		}
	}

	// Instantiate service

	Service service(appname, service_name, console_mode);
	const int ooptind = optind;

	optreset = 1;				// reset getopt(); usage below
	optind = 1;

	if (msg[0] || do_info) {		// command line error, info option.
		if (console_mode) {
			if (msg[0] || OUSAGE == do_info) {
				usage(msg);
			} else {
				switch (do_info) {
				case OHELP: help(); break;
				case OLICENSE: license(); break;
				case OVERSION: version(); break;
				}
			}

		} else {
			service.LogMessage(msg[0] ? msg : "unexpected service option <%s>", argv[ooptind]);
		}
		return EXIT_FAILURE;
	}

	// Control/configuration command.

	argv += ooptind; argc -= ooptind;	// consume leading options.
	assert(NULL == verb || verb == argv[0]);

	if (verb) {
		//
		//  inetd [service_options] run [configuration] ...
		//
		if (0 == _stricmp(verb, "run")) {
			verb = "";

		} else if (console_mode) {	// <class> <command> [arguments]
			NTService::IDiagnostics &diags = service.diags();
			int ret = NTSERVICE_CMD_UNKNOWN;

			if (0 == _stricmp(verb, "config")) {
				CNTServiceConfig service(service_name, service.GetCompany(), diags);
				ret = service.ExecuteCommand(argc - 1, argv + 1);

			} else if (0 == _stricmp(verb, "control")) {
				CNTServiceControl service(service_name, diags);
				ret = service.ExecuteCommand(argc - 1, argv + 1);

			} else {		// primary verbs
				ret = service.ExecuteCommand(argc, argv);
				verb = NULL;
			}

			if (const char *msg = CNTService::ExecuteReturnDesc(ret)) {
				if (verb) {	// config or control
					if (argc >= 2) {
						diags.ferror("command <%s %s> : %s", verb, argv[1], msg);
					} else {
						diags.ferror("command <%s> : %s", argv[0], msg);
					}
				} else {
					diags.ferror("command <%s> : %s", argv[0], msg);
				}
			}

			if (ret < 0) {		// command line error.
				if (ret != NTSERVICE_CMD_HELP) {
					diags.ferror("\nsee '%s %shelp' for more details",
						(verb ? verb : "inetd_service"), (verb ? "" : "--"));
				}
			}
			return (1 == ret ? EXIT_SUCCESS : EXIT_FAILURE);

		} else {
			service.LogError(true, "unexpected command <%s>, ignored", verb);
			return EXIT_FAILURE;
		}

	} else {
		//
		//      inetd [service_options] -- [options]
		// or   inetd [service_options]
		//
		if (argc) {
			--argv, ++argc;
			if (argv[0] != oargv[0] && 0 != strcmp(argv[0], "--")) {
				service.LogMessage(msg[0] ? msg : "unexpected service option <%s>", argv[1]);
				return EXIT_FAILURE;
			}
		}
	}

	// Service entry

	options.service_main = inetd_main;
	options.service_shutdown = inetd_signal_stop;
	options.arg0 = oargv[0];
	options.argc = argc ? argc - 1 : 1;
	options.argv = argc ? argv + 1 : oargv;

	service.Start(options);

	return EXIT_SUCCESS;
}


static bool
isconsole()
{
	return (NULL != GetConsoleWindow());
}


static bool
is_tty()
{
	return (console_mode || _isatty(STDERR_FILENO));
}


static void
usage(const char *msg)
{
	if (msg && *msg) {
		fprintf(stderr, "%s: %s\n", appname, msg);
	}
	fprintf(stderr, "\nusage: %s [-%s] %s start [service-configuration]", appname, OPTIONS, ARGUMENTDESCS);
	fprintf(stderr, "\nusage: %s <command> <command-options>\n\n", appname);
	fprintf(stderr, "see '%s --help' for service configuration\n", appname);
}


static void
help(void)
{
	printf(WININETD_PACKAGE " - " WININETD_PACKAGE_NAME " " WININETD_VERSION ", service options\n\n");

	printf("NAME:\n\n");
	printf("inetd_service - internet service daemon\n\n");

	printf("SYNOPSIS:\n\n");
	printf("inetd_service [options] <command> [configuration]\n\n");

	printf("OPTIONS:\n\n");
	printf("-S, --service=<name>      Service name.\n");
	printf("-Z, --config-file <file>  Configuration file.\n");
	printf("-N, --noconfig            Ignore service configuration file/registry.\n");
	printf("\n");

	printf("Following are effective when running under a console:\n");
	printf("\n");
	printf("-d, --daemon              Hide console.\n");
	printf("--no-logger               Control logger and associated stdout/sterr redirection.\n");
	printf("--stdout                  Redirect to both logger and console.\n");
	printf("\n");

	printf("COMMANDS:\n\n");
	printf("version                   Service version information.\n");
	printf("install                   Install as a service; see 'install help' for details.\n");
	printf("uninstall                 Uninstall service.\n");
	printf("config <attribute>        Config service attributes; see 'config help' for details.\n");
	printf("control <action>          Service control functions; see 'control help' for details\n");
	printf("run <configuration>       Run the service.\n");
	printf("\n");

	printf("CONFIGURATION:\n\n");
	printf("These programs follow the usual UNIX command line syntax.\n");
	printf("A summary of configuration options is included below.\n\n");

	for (unsigned i = 0; i < _countof(inetd_usage); ++i) {
		printf("%s\n", inetd_usage[i]);
	}

	exit(3);
}


static void
license(void)
{
	printf(WININETD_PACKAGE " - " WININETD_PACKAGE_NAME " " WININETD_VERSION "\n\n");

	printf("Copyright (C) 2020 - 2022 Adam Young, All rights reserved.\n");
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


static void
version(void)
{
	printf("%s %s [Build %s %s]\n", WININETD_PACKAGE, WININETD_VERSION, WININETD_BUILD_NUMBER, WININETD_BUILD_DATE);
	exit(3);
}

/*end*/

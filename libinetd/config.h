#pragma once
/* -*- mode: c; indent-width: 8; -*- */
/*
 * Configuration
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

#ifndef INET4
#define INET4
#endif

#ifndef INET6
#define INET6
#endif

#ifndef TOOMANY
#define TOOMANY 	256		/* don't start more than TOOMANY */
#endif

#ifndef MAXCHILD
#define MAXCHILD	-1		/* maximum number of this service < 0 = no limit */
#endif

#ifndef MAXCPM
#define MAXCPM		-1		/* rate limit invocations from a single remote address, < 0 = no limit */
#endif

#ifndef MAXPERIP
#define MAXPERIP	-1		/* maximum number of this service from a single remote address, < 0 = no limit */
#endif

#define MAX_MAXCHLD	32767		/* max allowable max children */

struct configparams {
	configparams() {
		euid      = 0;
		egid      = 0;
		sockopts  = 0;		/* SO_DEBUG */
		toomany   = TOOMANY;
		maxperip  = MAXPERIP;
		maxcpm    = MAXCPM;
		maxchild  = MAXCHILD;
		maxthread = 0;
		v4bind_ok = 0;
		v6bind_ok = 0;
		bind_sa4  = nullptr;
		bind_sa6  = nullptr;
	}
	uid_t	euid;
	gid_t	egid;
	int	sockopts;		/* global socket options */
	int	toomany;
	int	maxperip;
	int	maxcpm;
	int	maxchild;
	int	maxthread;
	int	v4bind_ok;
	int	v6bind_ok;
	struct sockaddr_in *bind_sa4;
	struct sockaddr_in6 *bind_sa6;
};

int	setconfig(const char *path);
struct servconfig *getconfigent(const struct configparams *params, int *ret);
void	endconfig(void);
void	syslogconfig(const char *label, const struct servconfig *sep);
void	freeconfig(struct servconfig *);

/*end*/

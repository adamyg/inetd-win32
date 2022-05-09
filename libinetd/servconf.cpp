/* -*- mode: c; indent-width: 8; -*- */
/*
 * windows inetd service.
 *
 * Copyright (c) 2022, Adam Young.
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

#include "inetd.h"

#include <algorithm>
#include <climits>

#include <sysexits.h>
#include <syslog.h>

#include "config.h"


struct snode { /* name string cache */
	LIST_ENTRY(snode) node_;
	char name_[1]; // implied nul
};

static LIST_HEAD(snodes, snode) string_collection;


servconfig::servconfig()
	: se_service(nullptr), se_bi(nullptr), se_server_name(nullptr)
{
	memset(se_argv, 0, sizeof(se_argv));
	freeconfig(this);
}


//static
const char *
servconfig::newname(const char *name)
{
	static bool initialised = false;
	snode *node;

	if (! initialised) {
		LIST_INIT(&string_collection);
		initialised = true;
	}

	LIST_FOREACH(node, &string_collection, node_) {
		if (0 == strcmp(node->name_, name)) {
			return node->name_;
		}
	}

	const size_t slen = strlen(name);
	if (nullptr == (node = (snode *)::malloc(sizeof(snode) + slen))) {
		throw std::bad_alloc();
		/*NOREACHED*/
	}

	(void) memcpy(node->name_, name, slen + 1 /*nul*/);
	LIST_INSERT_HEAD(&string_collection, node, node_);
	return node->name_;
}


//static
const char *
servconfig::newarg(const char *arg)
{
	char *ret;

	if (nullptr == (ret = _strdup(nullptr != arg ? arg : ""))) {
		throw std::bad_alloc();
		/*NOREACHED*/
	}
	return ret;
}


void
syslogconfig(const char *label, const struct servconfig *sep)
{
	const char *family = "";
	char addr[128] = {0};

	switch (sep->se_family) {
	case AF_INET:
		if (sep->se_ctrladdr4.sin_port) {
			char t_addr[64] = {0};
			sprintf(addr, "%s:%u", inet_ntop(AF_INET, (void *)&sep->se_ctrladdr4.sin_addr,
				t_addr, sizeof(t_addr)), ntohs(sep->se_ctrladdr4.sin_port));
		}
		family = "/ip4";
		break;
#ifdef INET6
	case AF_INET6:
		if (sep->se_ctrladdr6.sin6_port) {
			char t_addr[64] = {0};
			sprintf(addr, "[%s]:%u", inet_ntop(AF_INET6, (void *)&sep->se_ctrladdr6.sin6_addr,
				t_addr, sizeof(t_addr)), ntohs(sep->se_ctrladdr6.sin6_port));
		}
		family = "/ip6";
		break;
#endif
	}

	syslog(LOG_DEBUG,
		"%s: %s proto=%s%s, addr=%s, accept=%d, max=%d, user=%s, group=%s"
#ifdef LOGIN_CAP
		", class=%s"
#endif
		", builtin=0x%p, name=%s, server=%s, args=%s"
#ifdef IPSEC
		", policy=\"%s\""
#endif
		"\n",
		label, sep->se_service, sep->se_proto, family, addr,
		sep->se_accept, sep->se_maxchild, sep->se_user.c_str(), sep->se_group.c_str(),
#ifdef LOGIN_CAP
		sep->se_class.c_str(),
#endif
		(void *) sep->se_bi, (sep->se_server_name?sep->se_server_name:""), sep->se_server, sep->se_arguments
#ifdef IPSEC
		, (sep->se_policy.c_str())
#endif
		);

	sep->se_access_times.sysdump();
	sep->se_addresses.sysdump();
}


void
freeconfig(struct servconfig *sep)
{
	assert(nullptr == sep->se_service || (sep->se_service == servconfig::newname(sep->se_service)));
	sep->se_service = nullptr;	/* name of service */
	sep->se_bi = nullptr;		/* if built-in, description */
	sep->se_socktype = 0;		/* type of socket to use */
	sep->se_family = 0;		/* address family */
	sep->se_port = 0;		/* port */
	sep->se_proto.clear();		/* protocol */
	sep->se_sndbuf = 0;		/* sndbuf size (netbsd) */
	sep->se_rcvbuf = 0;		/* rcvbuf size (netbsd) */
	sep->se_maxchild = 0;		/* max number of children */
	sep->se_cpmmax = 0;		/* max connects per IP per minute */
	sep->se_cpmwait = 0;		/* delay post cpm limit, in seconds */
	sep->se_user.clear();		/* user name to run as */
	sep->se_group.clear();		/* group name to run as */
	sep->se_banner.clear(); 	/* banner sources; optional */
	sep->se_banner_success.clear();
	sep->se_banner_fail.clear();
#ifdef LOGIN_CAP
	sep->se_class.clear();		/* login class name to run with */
#endif
#ifdef IPSEC
	sep->se_policy.clear(); 	/* IPsec policy string */
#endif
	sep->se_server.clear(); 	/* server program */
	sep->se_server_name = nullptr;	/* server program without path */
	sep->se_working_directory.clear(); /* optional working directory */
	sep->se_arguments.clear();	/* program arguments */
	for (unsigned av = 0; av < MAXARGV; ++av)
		free((char *)sep->se_argv[av]);
	memset(sep->se_argv, 0, sizeof(sep->se_argv));
	sep->se_access_times.clear();	/* access times */
	sep->se_addresses.clear();	/* access control */
	sep->se_geoips.clear();		/* geoip rules */
	sep->se_environ.clear();	/* environment */
	memset(&sep->se_un, 0, sizeof(sep->se_un)); /* bound address */
	sep->se_ctrladdr_size = 0;
	sep->se_remote_family = 0;	/* remote address family */
	sep->se_remote_port = 0;	/* remote port */
	sep->se_remote_name.clear();
	memset(&sep->se_un_remote, 0, sizeof(sep->se_un_remote)); /* remote bound address */
	sep->se_remoteaddr_size = 0;
	sep->se_sockuid = 0;		/* Owner for unix domain socket */
	sep->se_sockgid = 0;		/* Group for unix domain socket */
	sep->se_sockmode = 0;		/* Mode for unix domain socket */
	sep->se_type = 0;		/* type: normal, mux, or mux+ */
	sep->se_accept = 0;		/* i.e., wait/nowait mode */
	sep->se_nomapped = 0;
#if defined(RPC)
	sep->se_rpc = 0;		/* ==1 if RPC service */
	sep->se_rpc_prog = 0;		/* RPC program number */
	sep->se_rpc_lowvers = 0;	/* RPC low version */
	sep->se_rpc_highvers = 0;	/* RPC high version */
#endif
	sep->se_maxperip;		/* max number of children per src */
}

//end
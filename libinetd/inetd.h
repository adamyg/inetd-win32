/* -*- mode: c; indent-width: 8; -*- */
/*
 * windows inetd service.
 *
 * Copyright (c) 2020 - 2021, Adam Young.
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
/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1991, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/queue.h>

#include <netinet/in.h>

#include <stdio.h>

#include <vector>

#include "SimpleLock.h"
#include "IntrusiveList.h"              // Intrusive list
#include "IOCPService.h"                // IO completion port support

#if defined(HAVE_AFUNIX_H)
#include <afunix.h>
#define HAVE_AF_UNIX	1		/* Insider Build 17063 */
		/*https://devblogs.microsoft.com/commandline/af_unix-comes-to-windows/*/
#endif

#define BUFSIZE		8192
#define LINESIZ		72

#define NORM_TYPE	0
#define MUX_TYPE	1
#define MUXPLUS_TYPE	2
#define FAITH_TYPE	4
#define ISMUX(sep)	(((sep)->se_type == MUX_TYPE) || ((sep)->se_type == MUXPLUS_TYPE))
#define ISMUXPLUS(sep)	((sep)->se_type == MUXPLUS_TYPE)

#if defined(_WIN32)
#define SOCKLEN_SOCKADDR(sa) sizeof(struct sockaddr)
#define SOCKLEN_SOCKADDR_PTR(sa) sizeof(struct sockaddr)
#define SOCKLEN_SOCKADDR_STORAGE(ss) sizeof(struct sockaddr_storage)
#define SOCKLEN_SOCKADDR_STORAGE_PTR(ss) sizeof(struct sockaddr_storage)
#else
#define SOCKLEN_SOCKADDR(sa) sa.sa_len
#define SOCKLEN_SOCKADDR_PTR(sa) sa->sa_len
#define SOCKLEN_SOCKADDR_STORAGE(ss) ss.ss_len
#define SOCKLEN_SOCKADDR_STORAGE_PTR(ss) ss->ss_len
#endif

#define satosin(sa)	((struct sockaddr_in *)(void *)sa)
#define csatosin(sa)	((const struct sockaddr_in *)(const void *)sa)
#ifdef INET6
#define satosin6(sa)	((struct sockaddr_in6 *)(void *)sa)
#define csatosin6(sa)	((const struct sockaddr_in6 *)(const void *)sa)
#endif

struct procinfo {
	procinfo() : pr_pid(-1), pr_conn(nullptr) {
	}
	inetd::Intrusive::TailMemberHook<procinfo> pr_link_;
	pid_t	pr_pid;                 /* child pid & linked, otherwise -1 */
	struct conninfo	*pr_conn; 	/* associated host connection */
};

typedef inetd::Intrusive::ListContainer<procinfo, inetd::Intrusive::TailMemberHook<procinfo>, &procinfo::pr_link_> ProcInfoList;

struct connprocs {
	struct Guard : public inetd::SpinLock::Guard {
		Guard(connprocs &cps) : inetd::SpinLock::Guard(cps.cp_lock) { }
	};

	connprocs() : cp_maxchild(0) { }
	int numchild() const {
		return (int)cp_procs.size();
	}

	inetd::SpinLock cp_lock;	/* spin lock */
	std::vector<struct procinfo *> cp_procs; /* child proc entries */
	int	cp_maxchild;		/* max number of children */
};

struct conninfo {
	conninfo() {
	}
	inetd::Intrusive::ListMemberHook<conninfo> co_link_;
	struct sockaddr_storage	co_addr;/* source address */
	connprocs co_procs;		/* array of child proc entry, from same host/addr */
};

typedef inetd::Intrusive::ListContainer<conninfo, inetd::Intrusive::ListMemberHook<conninfo>, &conninfo::co_link_> ConnInfoList;

#define PERIPSIZE	256

struct	stabchild {
	stabchild(pid_t pid) : sc_pid(pid) {
	}
	inetd::Intrusive::ListMemberHook<stabchild> sc_link_;
	pid_t	sc_pid;
};

typedef inetd::Intrusive::ListContainer<stabchild, inetd::Intrusive::ListMemberHook<stabchild>, &stabchild::sc_link_> StabChildList;

struct	servconfig {
	const char *se_service;		/* name of service */
	int	se_socktype;		/* type of socket to use */
	int	se_family;		/* address family */
	const char *se_proto;		/* protocol used */
	int	se_sndbuf;		/* sndbuf size (netbsd) */
	int	se_rcvbuf;		/* rcvbuf size (netbsd) */
	int	se_maxchild;		/* max number of children */
	int	se_maxcpm;		/* max connects per IP per minute */
	const char *se_user;		/* user name to run as */
	const char *se_group;		/* group name to run as */
#ifdef  LOGIN_CAP
	const char *se_class;		/* login class name to run with */
#endif
	const struct biltin *se_bi;	/* if built-in, description */
	const char *se_server;		/* server program */
	const char *se_server_name;	/* server program without path */
#define	MAXARGV 20
	const char *se_argv[MAXARGV+1];	/* program arguments */
#ifdef IPSEC
	const char *se_policy;		/* IPsec policy string */
#endif
	union {				/* bound address */
		struct	sockaddr se_un_ctrladdr;
		struct	sockaddr_in se_un_ctrladdr4;
		struct	sockaddr_in6 se_un_ctrladdr6;
#if defined(HAVE_AF_UNIX)
		struct	sockaddr_un se_un_ctrladdr_un;
#endif
	} se_un;
#define se_ctrladdr	se_un.se_un_ctrladdr
#define se_ctrladdr4	se_un.se_un_ctrladdr4
#define se_ctrladdr6	se_un.se_un_ctrladdr6
#define se_ctrladdr_un	se_un.se_un_ctrladdr_un
	socklen_t se_ctrladdr_size;
	uid_t	se_sockuid;		/* Owner for unix domain socket */
	gid_t	se_sockgid;		/* Group for unix domain socket */
	mode_t	se_sockmode;		/* Mode for unix domain socket */
	u_char	se_type;		/* type: normal, mux, or mux+ */
	u_char	se_accept;		/* i.e., wait/nowait mode */
#if defined(RPC)
	u_char	se_rpc;			/* ==1 if RPC service */
	int	se_rpc_prog;		/* RPC program number */
	u_int	se_rpc_lowvers;		/* RPC low version */
	u_int	se_rpc_highvers;	/* RPC high version */
#endif
	struct se_flags {
		u_int se_nomapped : 1;
		u_int se_reset : 1;
	} se_flags;
	int	se_maxperip;		/* max number of children per src */
};

struct	servtab : public servconfig {
	servtab() : servconfig(),
			se_fd(-1), se_count(0), se_time(), se_next(nullptr) {
		se_state.running = false;
                se_state.enabled = false;
	}
	servtab(const servconfig &cfg) : servconfig(cfg), 
			se_fd(-1), se_count(0), se_time(), se_next(nullptr) {
		se_state.running = false;
		se_state.enabled = false;
	}
	struct {
		inetd::CriticalSection lock;
		bool running;		/* is the service running */
		bool enabled;		/* is the service enabled/accepting connections */
	} se_state;
	int	se_fd;			/* open descriptor */
	inetd::IOCPService::Listener se_listener; /* iocp listener */
	int	se_count;		/* number started since se_time */
	struct	timespec se_time;	/* start of se_count */
	struct	servtab *se_next;
	u_char	se_checked;		/* looked at during configuration merge */

	ConnInfoList se_conn[PERIPSIZE];/* per host connection management */
	StabChildList se_children;      /* active child processes */
};

#define	se_nomapped		se_flags.se_nomapped
#define	se_reset		se_flags.se_reset

#define	SERVTAB_EXCEEDS_LIMIT(sep)	\
	((sep)->se_maxchild > 0 && (sep)->se_children.count() >= (sep)->se_maxchild)
#define	SERVTAB_EXCEEDS_LIMITX(sep, count) \
	((sep)->se_maxchild > 0 && (count) >= (sep)->se_maxchild)

extern int	debug;

typedef void (bi_fn_t)(int, struct servtab *);

struct biltin {
	const char *bi_service;		/* internally provided service name */
	int	bi_socktype;		/* type of socket supported */
	short	bi_fork;		/* 1 if should fork before call */
	int	bi_maxchild;		/* max number of children, -1=default */
	bi_fn_t	*bi_fn;			/* function which performs it */
};

extern const struct biltin biltins[];

int		cpmip(const struct servtab *sep, int ctrl);

__BEGIN_DECLS
int		check_loop(const struct sockaddr *, const struct servtab *sep);
void		inetd_setproctitle(const char *, int);
#if defined(TCPMUX)
struct servtab *tcpmux(int);
#endif
__END_DECLS

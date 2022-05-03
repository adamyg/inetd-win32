#pragma once
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
#include <errno.h>

#include <msvc_system_error.hpp>
#include <memory>
#include <array>
#include <vector>

#include "SimpleLock.h"
#include "SimpleString.h"
#include "IntrusiveList.h"		// Intrusive list
#include "IntrusivePtr.h"		// Intrusive ptr
#include "IOCPService.h"		// IO completion port support

#include "netaddrs.h"
#include "accesstm.h"
#include "environ.h"
#include "peerinfo.h"


#if defined(HAVE_AFUNIX_H)
#include <afunix.h>
#define HAVE_AF_UNIX	1		/* Insider Build 17063 */
	/*https://devblogs.microsoft.com/commandline/af_unix-comes-to-windows/*/
#endif

#define BUFSIZE		8192
#define LINESIZ		72

#define NORM_TYPE	0		// well-known service.
#define MUX_TYPE	1
#define MUXPLUS_TYPE	2
#define FAITH_TYPE	4
#define INTERNAL_TYPE	5		// well-known internal service; xinetd
#define UNLISTED_TYPE	6		// unlisted external/rpc service; xinetd
#define ISMUX(sep)	(((sep)->se_type == MUX_TYPE) || ((sep)->se_type == MUXPLUS_TYPE))
#define ISMUXPLUS(sep)	((sep)->se_type == MUXPLUS_TYPE)

#if defined(_WIN32)
#define SOCKLEN_SOCKADDR(__sa) (AF_INET6 == __sa.sa_family ? sizeof(struct sockaddr_in6) :  sizeof(struct sockaddr_in))
#define SOCKLEN_SOCKADDR_PTR(__sa) (AF_INET6 == __sa->sa_family ? sizeof(struct sockaddr_in6) :  sizeof(struct sockaddr_in))
#define SOCKLEN_SOCKADDR_STORAGE(__ss) (AF_INET6 == __ss.ss_family ? sizeof(struct sockaddr_in6) :  sizeof(struct sockaddr_in))
#define SOCKLEN_SOCKADDR_STORAGE_PTR(__ss) (AF_INET6 == __ss->ss_family ? sizeof(struct sockaddr_in6) :  sizeof(struct sockaddr_in))
#else
#define SOCKLEN_SOCKADDR(sa) sa.sa_len
#define SOCKLEN_SOCKADDR_PTR(sa) sa->sa_len
#define SOCKLEN_SOCKADDR_STORAGE(ss) ss.ss_len
#define SOCKLEN_SOCKADDR_STORAGE_PTR(ss) ss->ss_len
#endif

#define satosin(sa)	((struct sockaddr_in *)(void *)sa)
#define csatosin(sa)	((const struct sockaddr_in *)(const void *)sa)
#define satosin6(sa)	((struct sockaddr_in6 *)(void *)sa)
#define csatosin6(sa)	((const struct sockaddr_in6 *)(const void *)sa)

// process information
struct procinfo {
	procinfo(const procinfo &) = delete;
	procinfo operator=(const procinfo &) = delete;

	procinfo() : pr_pid(-1), pr_conn(nullptr), pr_sep(nullptr) {
	}

	inetd::Intrusive::TailMemberHook<procinfo> pr_procinfo_link_;
	inetd::Intrusive::ListMemberHook<procinfo> pr_child_link_;
	pid_t pr_pid; 			/* child pid & linked, otherwise -1 */
	struct conninfo *pr_conn;	/* associated host connection */
	struct servtab *pr_sep; 	/* associated service */
};

typedef inetd::intrusive_list<procinfo, inetd::Intrusive::TailMemberHook<procinfo>, &procinfo::pr_procinfo_link_> ProcInfoList;
typedef inetd::intrusive_list<procinfo, inetd::Intrusive::ListMemberHook<procinfo>, &procinfo::pr_child_link_> ChildList;

// host connection collection
struct connprocs {
	connprocs(const connprocs &) = delete;
	connprocs operator=(const connprocs &) = delete;

	connprocs(int maxperip);
	bool resize(int maxperip);
	struct procinfo *newproc(struct conninfo *conn, int &maxchild);
	bool unlink(struct procinfo *proc);
	void clear(struct conninfo *conn);
	int numchild() const {
		return (int)cp_procs.size();
	}

private:
	struct Guard;
	inetd::SpinLock cp_lock;	/* spin lock */
	std::vector<struct procinfo *> cp_procs; /* child proc entries */
	int cp_maxchild;		/* max number of children */
};

// host connection
struct conninfo {
	conninfo(const conninfo &) = delete;
	conninfo operator=(const conninfo &) = delete;

	conninfo(int maxperip) : co_procs(maxperip) {
	}

	inetd::Intrusive::ListMemberHook<conninfo> co_link_;
	struct sockaddr_storage co_addr;/* source address */
	connprocs co_procs;		/* array of child proc entry, from same host/addr */
};

typedef inetd::intrusive_list<conninfo, inetd::Intrusive::ListMemberHook<conninfo>, &conninfo::co_link_> ConnInfoList;

#define PERIPSIZE	256		/* procinfo hash table size */

// service configuration
struct servconfig {
	servconfig operator=(const servconfig &) = delete;

	static const char *newname(const char *name);
	static const char *newarg(const char *arg);

	servconfig();

	const char *se_service;		/* name of service */
	const struct biltin *se_bi;	/* if built-in, description */
	int	se_socktype;		/* type of socket to use */
	int	se_family;		/* address family */
	int	se_port;		/* port */
	inetd::String se_proto;		/* protocol used */
	int	se_sndbuf;		/* sndbuf size (netbsd) */
	int	se_rcvbuf;		/* rcvbuf size (netbsd) */
	int	se_maxchild;		/* max number of children */
	int	se_cpmmax;		/* max connects per IP per minute */
	int	se_cpmwait;		/* delay post cpm limit, in seconds */
	int	se_maxperip;		/* max number of children per src */
	inetd::String se_user;		/* user name to run as */
	inetd::String se_group;		/* group name to run as */
	inetd::String se_banner;	/* banner sources; optional */
	inetd::String se_banner_success;
	inetd::String se_banner_fail;
#ifdef LOGIN_CAP
	inetd::String se_class; 	/* login class name to run with */
#endif
#ifdef IPSEC
	inetd::String se_policy;	/* IPsec policy string */
#endif
	inetd::String se_server;	/* server program */
	const char *se_server_name;	/* server program without path; se_server reference */
	inetd::String se_working_directory; /* optional working directory */
#define MAXARGV 20
	inetd::String se_arguments;	/* program arguments */
	const char *se_argv[MAXARGV+1]; /* program arguments; vector */
	environment se_environ;		/* application environment */
	access_times se_access_times;	/* access time ranges */
	netaddrs se_addresses;		/* only_from/no_access addresses */
	union { 			/* bound address */
		struct sockaddr se_un_ctrladdr;
		struct sockaddr_in se_un_ctrladdr4;
		struct sockaddr_in6 se_un_ctrladdr6;
#if defined(HAVE_AF_UNIX)
		struct sockaddr_un se_un_ctrladdr_un;
#endif
#define se_ctrladdr se_un.se_un_ctrladdr
#define se_ctrladdr4 se_un.se_un_ctrladdr4
#define se_ctrladdr6 se_un.se_un_ctrladdr6
#define se_ctrladdr_un se_un.se_un_ctrladdr_un
	} se_un;
	socklen_t se_ctrladdr_size;
	int	se_remote_family;	/* remote address family */
	int	se_remote_port; 	/* port */
	inetd::String se_remote_name;	/* remote host name */
	union { 			/* bound address */
		struct sockaddr se_un_remoteaddr;
		struct sockaddr_in se_un_remoteaddr4;
		struct sockaddr_in6 se_un_remoteaddr6;
#define se_remoteaddr se_un_remote.se_un_remoteaddr
#define se_remoteaddr4 se_un_remote.se_un_remoteaddr4
#define se_remoteaddr6 se_un_remote.se_un_remoteaddr6
	} se_un_remote;
	socklen_t se_remoteaddr_size;
	uid_t	se_sockuid;		/* Owner for unix domain socket */
	gid_t	se_sockgid;		/* Group for unix domain socket */
	mode_t	se_sockmode;		/* Mode for unix domain socket */
	u_char	se_type;		/* type: normal, mux, or mux+ */
	u_char	se_accept;		/* i.e., wait/nowait mode */
	u_char	se_nomapped;
#if defined(RPC)
	u_char	se_rpc; 		/* ==1 if RPC service */
	int	se_rpc_prog;		/* RPC program number */
	u_int	se_rpc_lowvers; 	/* RPC low version */
	u_int	se_rpc_highvers;	/* RPC high version */
#endif
};

// service instance
struct servtab : public servconfig,
	    public inetd::intrusive::enable_shared_from_this<servtab> {

	servtab(const servtab &) = delete;
	servtab operator=(const servtab &) = delete;

	servtab() : servconfig(),
			se_fd(-1), se_count(0), se_time() {
		se_state.enabled = false;
		se_state.running = false;
	}

	servtab(const servconfig &cfg) : servconfig(cfg),
			se_fd(-1), se_count(0), se_time() {
		se_state.enabled = true;
		se_state.running = false;
	}

	static void intrusive_deleter(struct servtab *sep);

	struct {
		inetd::CriticalSection lock;
		bool enabled;		/* is the service enabled/accepting connections */
		bool running;		/* is the service running */
	} se_state;
	struct se_flags {
		u_int se_checked : 1;	/* looked at during configuration merge */
		u_int se_reset : 1;	/* channel reset required */
	} se_flags;
	int	se_fd;			/* open descriptor */
	inetd::IOCPService::Listener se_listener; /* iocp listener */
	int	se_count;		/* number started since se_time */
	struct	timespec se_time;	/* start of se_count */

	ConnInfoList se_conn[PERIPSIZE];/* per host connection management */
	ChildList se_children;		/* active child processes */
};

#define se_reset	se_flags.se_reset
#define se_checked	se_flags.se_checked

#define SERVTAB_EXCEEDS_LIMIT(sep)	\
	((sep)->se_maxchild > 0 && (sep)->se_children.count() >= (sep)->se_maxchild)
#define SERVTAB_EXCEEDS_LIMITX(sep, count) \
	((sep)->se_maxchild > 0 && (count) >= (sep)->se_maxchild)

typedef std::vector<inetd::instrusive_ptr<servtab>> ServiceCollection;
typedef std::shared_ptr<ServiceCollection> Services;

typedef void (bi_fn_t)(int, struct servtab *);

struct biltin {
	const char *bi_service;		/* internally provided service name */
	int	bi_socktype;		/* type of socket supported */
	short	bi_fork;		/* 1 if should fork before call */
	int	bi_maxchild;		/* max number of children, -1=default */
	bi_fn_t *bi_fn;			/* function which performs it */
};

extern const struct biltin biltins[];
extern int debug;

Services services();

int	accessip(PeerInfo &remote);
int	geoip(PeerInfo &remote);
int	accesstm(PeerInfo &remote);
int	cpmip(PeerInfo &remote);

int	banner(PeerInfo &remote);
int	banner_success(PeerInfo &remote);
int	banner_fail(PeerInfo &remote);

int	check_loop(const struct sockaddr *, const struct servtab *sep);
void	inetd_setproctitle(const char *, int);
#if defined(TCPMUX)
struct servtab *tcpmux(int);
#endif

//end

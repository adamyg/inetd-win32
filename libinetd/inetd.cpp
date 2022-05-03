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
 */

#include <sys/cdefs.h>

/*
 * Inetd - Internet super-server
 *
 * This program invokes all internet services as needed.  Connection-oriented
 * services are invoked each time a connection is made, by creating a process.
 * This process is passed the connection as file descriptor 0 and is expected
 * to do a getpeername to find out the source host and port.
 *
 * Datagram oriented services are invoked when a datagram
 * arrives; a process is created and passed a pending message
 * on file descriptor 0.  Datagram servers may either connect
 * to their peer, freeing up the original socket for inetd
 * to receive further messages on, or ``take over the soockcket'',
 * processing all arriving datagrams and, eventually, timing
 * out.	 The first type of server is said to be ``multi-threaded'';
 * the second type of server ``single-threaded''.
 *
 * Inetd uses a configuration file which is read at startup
 * and, possibly, at some later time in response to a hangup signal.
 * The configuration file is ``free format'' with fields given in the
 * order shown below.  Continuation lines for an entry must begin with
 * a space or tab.  All fields must be present in each entry.
 *
 *	service name			must be in /etc/services
 *					or name a tcpmux service
 *					or specify a unix domain socket
 *	socket type			stream/dgram/raw/rdm/seqpacket
 *	protocol			tcp[4][6], udp[4][6], unix
 *	wait/nowait			single-threaded/multi-threaded
 *	user[:group][/login-class]	user/group/login-class to run daemon as
 *	server program			full path name
 *	server program arguments	maximum of MAXARGS (20)
 *
 * TCP services without official port numbers are handled with the
 * RFC1078-based tcpmux internal service. Tcpmux listens on port 1 for
 * requests. When a connection is made from a foreign host, the service
 * requested is passed to tcpmux, which looks it up in the servtab list
 * and returns the proper entry for the service. Tcpmux returns a
 * negative reply if the service doesn't exist, otherwise the invoked
 * server is expected to return the positive reply if the service type in
 * inetd.conf file has the prefix "tcpmux/". If the service type has the
 * prefix "tcpmux/+", tcpmux will return the positive reply for the
 * process; this is for compatibility with older server code, and also
 * allows you to invoke programs that use stdin/stdout without putting any
 * special server code in them. Services that use tcpmux are "nowait"
 * because they do not have a well-known port and hence cannot listen
 * for new requests.
 *
 * For RPC services
 *	service name/version		must be in /etc/rpc
 *	socket type			stream/dgram/raw/rdm/seqpacket
 *	protocol			rpc/tcp[4][6], rpc/udp[4][6]
 *	wait/nowait			single-threaded/multi-threaded
 *	user[:group][/login-class]	user/group/login-class to run daemon as
 *	server program			full path name
 *	server program arguments	maximum of MAXARGS
 *
 * Comment lines are indicated by a `#' in column 1.
 *
 * #ifdef IPSEC
 * Comment lines that start with "#@" denote IPsec policy string, as described
 * in ipsec_set_policy(3).  This will affect all the following items in
 * inetd.conf(8).  To reset the policy, just use "#@" line.  By default,
 * there's no IPsec policy.
 * #endif
 */

#if !defined(NOMINMAX)
#define  NOMINMAX
#endif
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/un.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#if defined(RPC)
#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>
#endif

#include "ScopedHandle.h"
#include "SocketShare.h"
#include "ProcessGroup.h"
#include "ObjectPool.h"
#include "CPULoadInfo.h"

#include <limits.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <err.h>
#include <netdb.h>
#if !defined(_WIN32)
#include <pwd.h>
#include <grp.h>
#endif
#include <signal.h>
#include "libutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <sysexits.h>
#include <syslog.h>
#ifdef LIBWRAP
#include <tcpd.h>
#endif
#include <unistd.h>

#if defined(_WIN32)
#include <libcompat.h>
#include <sthread.h>
#endif

#include "inetd.h"
#include "config.h"
#include "config2.h"
#include "pathnames.h"

#ifdef IPSEC
#include <netipsec/ipsec.h>
#ifndef IPSEC_POLICY_IPSEC		/* no ipsec support on old ipsec */
#undef IPSEC
#endif
#endif

#ifndef LIBWRAP_ALLOW_FACILITY
#define LIBWRAP_ALLOW_FACILITY  LOG_AUTH
#endif
#ifndef LIBWRAP_ALLOW_SEVERITY
#define LIBWRAP_ALLOW_SEVERITY  LOG_INFO
#endif
#ifndef LIBWRAP_DENY_FACILITY
#define LIBWRAP_DENY_FACILITY   LOG_AUTH
#endif
#ifndef LIBWRAP_DENY_SEVERITY
#define LIBWRAP_DENY_SEVERITY   LOG_WARNING
#endif

#define ISWRAP(sep)	\
	   ( ((wrap_ex && !(sep)->se_bi) || (wrap_bi && (sep)->se_bi)) \
	&& (sep->se_family == AF_INET || sep->se_family == AF_INET6) \
	&& ( ((sep)->se_accept && (sep)->se_socktype == SOCK_STREAM) \
	    || (sep)->se_socktype == SOCK_DGRAM))

#ifdef LOGIN_CAP
#include <login_cap.h>

/* see init.c */
#define RESOURCE_RC "daemon"

#endif

#define CNT_INTVL	60		/* servers in CNT_INTVL sec. */
#define RETRYTIME	(60*10) 	/* retry after bind or server fail */

#if !defined(MAX)
#define MIN(X,Y)	((X) < (Y) ? (X) : (Y))
#define MAX(X,Y)	((X) > (Y) ? (X) : (Y))
#endif

#if !defined(sockclose)
#define sockclose(__fd) close(__fd)
#endif

#if defined(_DEBUG)
#define SANITY_CHECK			/* runtime sanity checks */
#endif

static void	terminate(int value);
static int	body(int argc, char **argv);
static void	getservicesprog(char *servicesprog, size_t buflen);
static int	do_accept(PeerInfo &remote);
static void	setalarm(unsigned seconds);
static int	do_fork(const struct servtab *sep, int ctrl);
static void	child(struct servtab *sep, int ctrl);
static void	close_sep(struct servtab *, bool end = false);
static void	sigchld(void);
static void	sigterm(void);
static void	flag_signal(int);
static void	config(void);

static void	addchild(struct servtab *sep, pid_t pid, struct procinfo *proc);
static void	reapchildren(void);
static const char *reapchild(pid_t pid);

static void	enable(struct servtab *sep);
static void	disable(struct servtab *, bool closing = false);
static void	retry(void);
static void	setup(struct servtab *);
#ifdef IPSEC
static void	ipsecsetup(struct servtab *);
#endif
#if defined(RPC)
static void	unregisterrpc(register struct servtab *sep);
#endif

static struct conninfo *search_connections(PeerInfo &remote);
static bool	new_connection(const struct servtab *sep, struct conninfo *conn, struct procinfo *&proc);
static void	connections_resize(struct servtab *sep, int maxperip);
static void	connections_free(struct servtab *sep);

static struct procinfo *connprocs_new(struct conninfo *conn, int &maxchild);
static void	connprocs_clear(struct conninfo *conn);

static void	free_conn(struct conninfo *conn);

static struct procinfo *search_proc(pid_t pid, struct procinfo *add = nullptr);
static int	hashval(const char *p, int len);
static void	free_proc(struct procinfo *proc);

static void	print_service(const char *, const struct servconfig *);

#ifdef LIBWRAP	/* tcpd.h */
int		allow_severity;
int		deny_severity;
#endif

int		debug = 0;

static int	wrap_ex = 0;
static int	wrap_bi = 0;
static int	dolog = 0;
static fd_set	allsock;

static char	*hostname = nullptr;

static int	signalpipe[2];
#ifdef SANITY_CHECK
static int	nsock;
#endif

static mode_t	mask;

static struct configparams params;

static const char *SERVICES = _PATH_INETDCONF;
static const char *pid_file = _PATH_INETDPID;
static struct pidfh *pfh = nullptr;

static inetd::ProcessGroup process_group;
static inetd::IOCPService iocp;
static DWORD	mainthreadid;

static char	servicesprog[MAX_PATH];

#if defined(RPC)
static struct netconfig *udpconf, *tcpconf, *udp6conf, *tcp6conf;
#endif

static ProcInfoList proctable[PERIPSIZE];

static int
getvalue(const char *arg, int *value, const char *whine, int limit = 0)
{
	int  tmp;
	char *p;

	tmp = strtol(arg, &p, 0);
	if (tmp < 0 || *p || (limit > 1 && tmp > limit)) {
		syslog(LOG_ERR, whine, arg);
		return 1;			/* failure */
	}
	*value = tmp;
	return 0;				/* success */
}

#ifdef LIBWRAP
static sa_family_t
whichaf(struct request_info *req)
{
	struct sockaddr *sa;

	sa = (struct sockaddr *)req->client->sin;
	if (sa == NULL)
		return AF_UNSPEC;
#ifdef INET6
	if (sa->sa_family == AF_INET6 &&
			IN6_IS_ADDR_V4MAPPED(&satosin6(sa)->sin6_addr))
		return AF_INET;
#endif
	return sa->sa_family;
}
#endif

extern "C" int
inetd_main(int argc, char **argv)
{
	int ret = 99;

	mainthreadid = GetCurrentThreadId();
	try {
		if (process_group.open(sigchld)) {
			ret = body(argc, argv);
		}
		process_group.close();
	} catch (int exit_code) {
		ret = exit_code;
	} catch (std::exception &msg) {
		syslog(LOG_ERR, "service terminated : exception, %s", msg.what());
	} catch (...) {
		syslog(LOG_ERR, "service terminated : exception");
	}

	iocp.Terminate();
//TODO	close_sockets();
	iocp.Close();
	return ret;
}

static void
terminate(int value)
{
	if (mainthreadid == GetCurrentThreadId())
		throw value;
	sigterm();
}

static int
body(int argc, char **argv)
{
	int ch;
#ifdef LOGIN_CAP
	login_cap_t *lc = NULL;
#endif
#ifdef LIBWRAP
	struct request_info req;
	int denied;
	char *service = NULL;
#endif
	struct addrinfo hints, *res;
	const char *servname;
	int error;

	getservicesprog(servicesprog, sizeof(servicesprog));
	openlog("inetd", LOG_PID | LOG_NOWAIT | (getlogoption() & LOG_NOHEADER), LOG_DAEMON);
	while ((ch = getopt(argc, argv, "dlwWR:a:c:C:p:s:t:")) != -1)
		switch(ch) {
		case 'd':
			debug = 1;
			params.sockopts |= SO_DEBUG;
			break;
		case 'l':
			dolog = 1;
			break;
		case 'R':
			getvalue(optarg, &params.toomany,
				"-R %s: bad value for service invocation rate");
			break;
		case 'c':
			getvalue(optarg, &params.maxchild,
				"-c %s: bad value for maximum children");
			break;
		case 'C':
			getvalue(optarg, &params.maxcpm,
				"-C %s: bad value for maximum children/minute");
			break;
		case 'a':
			hostname = optarg;
			break;
		case 'p':
			pid_file = optarg;
			break;
		case 's':
			getvalue(optarg, &params.maxperip,
				"-s %s: bad value for maximum children per source address");
			break;
		case 'w':
			wrap_ex++;
			break;
		case 'W':
			wrap_bi++;
			break;
		case 't': {
				SYSTEM_INFO si = {0};

				::GetSystemInfo(&si);
				getvalue(optarg, &params.maxthread,
					"-T %s: bad value for maximum thread count", inetd::IOCPService::MAX_WORKERS);
				if (params.maxthread > (int)(si.dwNumberOfProcessors * 2)) {
					params.maxthread = si.dwNumberOfProcessors * 2;
				}
				break;
			}
		case '?':
		default:
			syslog(LOG_ERR,
				"usage: inetd [-dlwW] [-a address] [-R rate]"
				" [-c maximum] [-C rate] [-t threads] [-p pidfile] [conf-file]");
			terminate(EX_USAGE);
		}

	/*
	 * Initialize Bind Addrs.
	 *   When hostname is NULL, wild card bind addrs are obtained from getaddrinfo().
	 *   But getaddrinfo() requires at least one of hostname or servname is non NULL.
	 *
	 *   So when hostname is NULL, set dummy value to servname.
	 *   Since getaddrinfo() doesn't accept numeric servname, and we doesn't use ai_socktype
	 *   of struct addrinfo returned from getaddrinfo(), we set dummy value to ai_socktype.
	 */
	servname = (hostname == NULL) ? "0" /* dummy */ : NULL;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM; /* dummy */
	error = getaddrinfo(hostname, servname, &hints, &res);
	if (error != 0) {
		syslog(LOG_ERR, "-a %s: %s", hostname, gai_strerror(error));
#if defined(EAI_SYSTEM)
		if (error == EAI_SYSTEM)
			syslog(LOG_ERR, "%s", strerror(errno));
#endif
		terminate(EX_USAGE);
	}

	do {
		if (res->ai_addr == NULL) {
			syslog(LOG_ERR, "-a %s: getaddrinfo failed", hostname);
			terminate(EX_USAGE);
		}
		switch (res->ai_addr->sa_family) {
		case AF_INET:
			if (params.v4bind_ok)
				continue;
			params.bind_sa4 = satosin(res->ai_addr);
			/* init port num in case servname is dummy */
			params.bind_sa4->sin_port = 0;
			params.v4bind_ok = 1;
			continue;
#ifdef INET6
		case AF_INET6:
			if (params.v6bind_ok)
				continue;
			params.bind_sa6 = satosin6(res->ai_addr);
			/* init port num in case servname is dummy */
			params.bind_sa6->sin6_port = 0;
			params.v6bind_ok = 1;
			continue;
#endif
		}
		if (params.v4bind_ok
#ifdef INET6
		    && params.v6bind_ok
#endif
		    )
			break;
	} while ((res = res->ai_next) != NULL);
	if (!params.v4bind_ok
#ifdef INET6
	    && !params.v6bind_ok
#endif
	    ) {
		syslog(LOG_ERR, "-a %s: unknown address family", hostname);
		terminate(EX_USAGE);
	}

	umask(mask = umask(0777));

	argc -= optind;
	argv += optind;

	if (argc > 0)
		SERVICES = argv[0];
	if (access(SERVICES, R_OK) < 0)
		syslog(LOG_ERR, "Accessing %s: %m, continuing anyway.", SERVICES);

	if (0 == debug) {
		pid_t otherpid;

		pfh = pidfile_open(pid_file, 0600, &otherpid);
		if (pfh == NULL) {
			if (errno == EEXIST) {
				syslog(LOG_ERR, "%s already running, pid: %d", getprogname(), otherpid);
				terminate(EX_OSERR);
			}
			syslog(LOG_WARNING, "pidfile_open() failed: %m");
		}

		if (pfh != NULL && pidfile_write(pfh) == -1) {
			syslog(LOG_WARNING, "pidfile_write(): %m");
		}
	}

#if defined(MADV_PROTECT)
	if (madvise(NULL, 0, MADV_PROTECT) != 0)
		syslog(LOG_WARNING, "madvise() failed: %s", strerror(errno));
#endif

#if defined(RPC)
	if (params.v4bind_ok) {
		udpconf = getnetconfigent("udp");
		tcpconf = getnetconfigent("tcp");
		if (udpconf == NULL || tcpconf == NULL) {
			syslog(LOG_ERR, "unknown rpc/udp or rpc/tcp");
			terminate(EX_USAGE);
		}
	}
#ifdef INET6
	if (params.v6bind_ok) {
		udp6conf = getnetconfigent("udp6");
		tcp6conf = getnetconfigent("tcp6");
		if (udp6conf == NULL || tcp6conf == NULL) {
			syslog(LOG_ERR, "unknown rpc/udp6 or rpc/tcp6");
			terminate(EX_USAGE);
		}
	}
#endif
#endif //RPC

	if (params.maxthread > 1 && !iocp.Initialise(params.maxthread)) {
		terminate(EX_OSERR);
	}

	config();

#if !defined(O_CLOEXEC)
#define O_CLOEXEC	0
#endif
	signalpipe[0] = -1, signalpipe[1] = -1;
	if (socketpair(AF_INET, SOCK_STREAM, 0, signalpipe) < 0) {
		syslog(LOG_ERR, "pipe: %m");
		terminate(EX_OSERR);
	}
	FD_SET(signalpipe[0], &allsock);
#ifdef SANITY_CHECK
	nsock++;
#endif

	for (;;) {
		int n;
		fd_set readable;

#ifdef SANITY_CHECK
		if (nsock == 0) {
			syslog(LOG_ERR, "%s: nsock=0", __func__);
			terminate(EX_SOFTWARE);
		}
#endif

		/* WIN32: limit 64 sockets and HANDLE's should be reorged reducing starved objects */
		readable = allsock;
		if ((n = select(FD_SETSIZE /*dummy*/, &readable, (fd_set *)0, (fd_set *)0, (struct timeval *)0)) <= 0) {
			if (n < 0 && errno != EINTR) {
				syslog(LOG_WARNING, "select: %m");
				sleep(1);
			}
			continue;
		}

#define SIGALRM 	1001
#define SIGHUP		1002

		/* handle any queued signal flags */
		if (FD_ISSET(signalpipe[0], &readable)) {
			int nsig = 0, signo;

			n--;
			if (ioctlsocket(signalpipe[0], FIONREAD, &nsig) != 0) {
				syslog(LOG_ERR, "ioctl: %m");
				terminate(EX_OSERR);
			}
			nsig /= sizeof(signo);
			while (--nsig >= 0) {
				size_t len;

				len = sockread(signalpipe[0], &signo, sizeof(signo));
				if (len != sizeof(signo)) {
					syslog(LOG_ERR, "read signal: %m");
					terminate(EX_OSERR);
				}
				if (debug)
					syslog(LOG_DEBUG, "handling signal flag %d", signo);
				switch (signo) {
				case SIGALRM:
					retry();
					break;
				case SIGCHLD:
					reapchildren();
					break;
				case SIGHUP:
					config();
					break;
				case SIGTERM:
					return 0;
				}
			}
		}

		/* network events */
		Services current_services(services());
		for (auto sit : *current_services) {
			struct servtab *sep = sit.get();

			if (n <= 0) {
				break;
			}

			if (sep->se_fd < 0 ||
					sep->se_listener.is_open()) {
				continue;
			}

			if (FD_ISSET(sep->se_fd, &readable)) {
				n--;
				if (debug)
					syslog(LOG_DEBUG, "someone wants %s", sep->se_service);
				if (sep->se_accept && sep->se_socktype == SOCK_STREAM) {
					if (socknonblockingio(sep->se_fd, 1) < 0)
						syslog(LOG_ERR, "ioctl (FIONBIO, 1): %m");
					int ctrl = accept(sep->se_fd, (struct sockaddr *)0, (socklen_t *)0);
					if (debug)
						syslog(LOG_DEBUG, "accept, ctrl %d", ctrl);
					if (ctrl < 0) {
						if (errno != EINTR)
							syslog(LOG_WARNING, "accept (for %s): %m", sep->se_service);
						if (sep->se_accept && sep->se_socktype == SOCK_STREAM)
							sockclose(ctrl);
						continue;
					}
					if (socknonblockingio(sep->se_fd, 0) < 0)
						syslog(LOG_ERR, "ioctl2 (FIONBIO, 0): %m");
					if (socknonblockingio(ctrl, 0) < 0)
						syslog(LOG_ERR, "ioctl3 (FIONBIO, 0): %m");

					PeerInfo remote(ctrl, sep);
					if (accessip(remote) < 0 || geoip(remote) < 0 || cpmip(remote) < 0) {
						sockclose(ctrl);
						continue;
					}

					do_accept(remote);
					sockclose(ctrl);

				} else {
					PeerInfo remote(sep->se_fd, sep);
					do_accept(remote);
				}
			}
		}
	}
}

static void async_accept(inetd::instrusive_ptr<struct servtab> &service,
		std::shared_ptr<inetd::IOCPService::Socket> &cxt, bool success)
{
	struct servtab *sep(service.get());

	{	std::shared_ptr<inetd::IOCPService::Socket>
			acceptor = std::make_shared<inetd::IOCPService::Socket>();
		inetd::IOCPService::AcceptCallback callback(std::bind(&async_accept, service, acceptor, std::placeholders::_1));

		inetd::CriticalSection::Guard guard(sep->se_state.lock);
		if (sep->se_state.running) {	// rearm acceptor.
			iocp.Accept(sep->se_listener, *acceptor.get(), std::move(callback));
		} else {
			success = false;
		}
	}

	if (success) {				// connection made and running.
		PeerInfo remote(cxt->fd(), sep);
		if (accessip(remote) >= 0 && geoip(remote) >= 0 && cpmip(remote) >= 0) {
			do_accept(remote);
		}
	}
}

static int
do_accept(PeerInfo &remote)
{
	struct servtab *sep = remote.getserv();
	const int dofork = (!sep->se_bi || sep->se_bi->bi_fork || ISWRAP(sep));
	struct conninfo *conn = nullptr;
	struct procinfo *proc = nullptr;
	pid_t pid = 0;

	if (accesstm(remote) < 0) {
		syslog(LOG_ERR, "%s from %s out-side allowed time(s)",
			sep->se_service, remote.getname());
		return 0;
	}

	if (sep->se_accept && sep->se_socktype == SOCK_STREAM) {
		if (dofork && (conn = search_connections(remote)) != nullptr) {
			if (conn == (conninfo *)-1)
				return 0;
			if (! new_connection(sep, conn, proc)) {
				assert(proc == nullptr);
				free_conn(conn);
				return 0;
			}
		}
	}

	if (dolog && !ISWRAP(sep)) {
		syslog(LOG_INFO, "%s from %s", sep->se_service, remote.getname());
	}

	/*
	 * Fork for all external services, builtins which need to
	 * fork and anything we're wrapping (as wrapping might
	 * block or use hosts_options(5) twist).
	 */
	if (dofork) {
		if (sep->se_count++ == 0) {
			sep->se_time = remote.timestamp();
		} else if (params.toomany > 0 && sep->se_count >= params.toomany) {
			const struct timespec &now = remote.timestamp();

			if (now.tv_sec - sep->se_time.tv_sec > CNT_INTVL) {
				sep->se_time = now;
				sep->se_count = 1;
			} else {
				syslog(LOG_ERR, "%s/%s server failing (looping), service terminated",
					sep->se_service, sep->se_proto);
				free_proc(proc);
				free_conn(conn);
				close_sep(sep);
				setalarm(RETRYTIME);
				return -1;	// shutdown.
			}
		}

		pid = do_fork(sep, remote.fd());

		if (-1 == pid) {		// fork error.
			syslog(LOG_ERR, "fork: %m");
			free_proc(proc);
			free_conn(conn);
			sleep(1);
			return 0;
		}

		if (pid) {			// fork success.
			assert(nullptr == proc || proc->pr_conn == conn);
			addchild(sep, pid, proc);
			return 2;
		}
	}

	assert(! pid);
	assert(! dofork);
	assert(! proc);
	assert(! conn);

	if (0 == pid) { 			// local execution.
		assert(sep->se_bi);
		if (sep->se_bi) {
			(*sep->se_bi->bi_fn)(remote.fd(), sep);
		}
	}
	return 1;
}

static void
getservicesprog(char *path, size_t pathlen)
{
	static const char defservicesprog[] = "inetd_services.exe";
	char *cursor = path;
	unsigned ret;

	assert(pathlen >= 255);
	if ((ret = ::GetModuleFileNameA(nullptr, path, pathlen - sizeof(defservicesprog))) > 0) {
		path[ret] = 0;
		if (char *delim = const_cast<char *>(strrchr(path, '\\'))) {
			cursor = delim + 1;	// inherit current path.
		}
	}
	strcpy(cursor, defservicesprog);
}

static VOID CALLBACK
alarm_timer(PVOID lpParam, BOOLEAN TimerOrWaitFired)
{
	int *ptimingout = (int *)lpParam;

	*ptimingout = false;
	flag_signal(SIGALRM);			// queue alarm event.
}

static void
setalarm(unsigned seconds)
{
	static HANDLE hTimerQueue, hTimer;
	static int timingout = false;

	if (timingout)
		return;
	timingout = true;

	if (hTimer) {				// stop existing.
		if (! ::DeleteTimerQueueTimer(hTimerQueue, hTimer, INVALID_HANDLE_VALUE)) {
			syslog(LOG_ERR, "delete timer: %M");
		}
		hTimer = nullptr;
	}

	assert(seconds);
	if (seconds) {				// reschedule.
		if (nullptr == hTimerQueue &&
			    nullptr == (hTimerQueue = ::CreateTimerQueue())) {
			syslog(LOG_ERR, "create timer queue: %M");
			terminate(EX_OSERR);
			return;
		}

		if (! ::CreateTimerQueueTimer(&hTimer, hTimerQueue,
			    (WAITORTIMERCALLBACK)alarm_timer, (void *)&timingout, seconds * 1000, 0, 0)) {
			syslog(LOG_ERR, "create timer: %M");
			terminate(EX_OSERR);
		}
		return;
	}
	timingout = false;
}

static int
do_fork(const struct servtab *sep, int ctrl)
{
	const char *service = (sep->se_server_name ? sep->se_server_name : sep->se_service);
	const char *progname = nullptr;
	const char *cd = nullptr;
	const char **argv = nullptr;
	const char **envv = nullptr;
	const char *t_argv[3];

	if (sep->se_bi) {			// inbuilt.
		t_argv[0] = "-s";
		t_argv[1] = sep->se_service;
		t_argv[2] = nullptr;
		progname = servicesprog;
		argv = (const char **)t_argv;

	} else {				// external
		progname = sep->se_server;
		argv = (const char **)sep->se_argv;
		envv = sep->se_environ.get();
		cd = sep->se_working_directory.data();
	}

	inetd::SocketShare::Server server(progname, cd, process_group.job_handle(), argv, envv);
	if (server.publish(ctrl)) {
		process_group.track(server.child());
		return server.pid();		// resulting process identifier
	}
	errno = EINVAL;
	return -1;
}

#if !defined(_WIN32)
static void
child(struct servtab *sep, int ctrl)
{
	struct passwd *pwd = nullptr;
	struct group *grp = nullptr;
	char buf[50];

#if defined(TCPMUX)	// deprecated in 2016 by RFC 7805
	/*
	 *  Call tcpmux to find the real service to exec.
	 */
	if (sep->se_bi && sep->se_bi->bi_fn == (bi_fn_t *) tcpmux) {
		sep = tcpmux(ctrl);
		if (sep == nullptr) {
			sockclose(ctrl);
			_exit(0);
		}
	}
#endif

#ifdef LIBWRAP
	if (ISWRAP(sep)) {
		inetd_setproctitle("wrapping", ctrl);
		service = sep->se_server_name ? sep->se_server_name : sep->se_service;
		request_init(&req, RQ_DAEMON, service, RQ_FILE, ctrl, 0);
		fromhost(&req);
		deny_severity = LIBWRAP_DENY_FACILITY|LIBWRAP_DENY_SEVERITY;
		allow_severity = LIBWRAP_ALLOW_FACILITY|LIBWRAP_ALLOW_SEVERITY;
		denied = !hosts_access(&req);
		if (denied) {
			syslog(deny_severity,
				"refused connection from %.500s, service %s (%s%s)",
				eval_client(&req), service, sep->se_proto,
					(whichaf(&req) == AF_INET6) ? "6" : "");
			if (sep->se_socktype != SOCK_STREAM)
					recv(ctrl, buf, sizeof (buf), 0);
			if (dofork) {
					sleep(1);
					_exit(0);
			}
		}
		if (dolog) {
			syslog(allow_severity,
				"connection from %.500s, service %s (%s%s)",
				eval_client(&req), service, sep->se_proto,
					(whichaf(&req) == AF_INET6) ? "6" : "");
		}
	}
#endif

	if (sep->se_bi) {
		(*sep->se_bi->bi_fn)(ctrl, sep);

	} else {
		if (debug)
			warnx("%d execl %s", getpid(), sep->se_server.c_str());

		/* Clear close-on-exec. */
#if defined(F_SETFD)
		if (fcntl(ctrl, F_SETFD, 0) < 0) {
			syslog(LOG_ERR, "%s/%s: fcntl (F_SETFD, 0): %m",
				sep->se_service, sep->se_proto);
			_exit(EX_OSERR);
		}
#endif
		if (ctrl != 0) {
			dup2(ctrl, 0);
			sockclose(ctrl);
		}

		dup2(0, 1);
		dup2(0, 2);

		if ((pwd = getpwnam(sep->se_user)) == nullptr) {
			syslog(LOG_ERR, "%s/%s: %s: no such user",
				sep->se_service, sep->se_proto, sep->se_user);
			if (sep->se_socktype != SOCK_STREAM)
				recv(0, buf, sizeof (buf), 0);
			_exit(EX_NOUSER);
		}

		grp = nullptr;
		if (sep->se_group != nullptr && (grp = getgrnam(sep->se_group)) == nullptr) {
			syslog(LOG_ERR, "%s/%s: %s: no such group",
				sep->se_service, sep->se_proto, sep->se_group);
			if (sep->se_socktype != SOCK_STREAM)
				recv(0, buf, sizeof (buf), 0);
			_exit(EX_NOUSER);
		}

		if (grp != nullptr)
			pwd->pw_gid = grp->gr_gid;
#ifdef LOGIN_CAP
		if ((lc = login_getclass(sep->se_class)) == nullptr) {
			/* error syslogged by getclass */
			syslog(LOG_ERR, "%s/%s: %s: login class error",
			    sep->se_service, sep->se_proto, sep->se_class);
			if (sep->se_socktype != SOCK_STREAM)
				recv(0, buf, sizeof (buf), 0);
			_exit(EX_NOUSER);
		}
#endif

		if (setsid() < 0) {
			syslog(LOG_ERR, "%s: can't setsid(): %m",
			    sep->se_service);
			/* _exit(EX_OSERR); not fatal yet */
		}

#ifdef LOGIN_CAP
		if (setusercontext(lc, pwd, pwd->pw_uid, LOGIN_SETALL & ~LOGIN_SETMAC) != 0) {
			syslog(LOG_ERR, "%s: can't setusercontext(..%s..): %m",
				sep->se_service, sep->se_user);
			_exit(EX_OSERR);
		}
		login_close(lc);
#else
		if (pwd->pw_uid) {
			if (setlogin(sep->se_user) < 0) {
				syslog(LOG_ERR, "%s: can't setlogin(%s): %m",
				    sep->se_service, sep->se_user);
				/* _exit(EX_OSERR); not yet */
			}
			if (setgid(pwd->pw_gid) < 0) {
				syslog(LOG_ERR, "%s: can't set gid %d: %m",
				    sep->se_service, pwd->pw_gid);
				_exit(EX_OSERR);
			}
			(void) initgroups(pwd->pw_name, pwd->pw_gid);
			if (setuid(pwd->pw_uid) < 0) {
				syslog(LOG_ERR, "%s: can't set uid %d: %m",
				    sep->se_service, pwd->pw_uid);
				_exit(EX_OSERR);
			}
		}
#endif
		execv(sep->se_server, sep->se_argv);
		syslog(LOG_ERR, "cannot execute %s: %m", sep->se_server.c_str());
		if (sep->se_socktype != SOCK_STREAM)
			recv(0, buf, sizeof (buf), 0);
	}
}
#endif	/*_WIN32*/

/*
 * Add a signal flag to the signal flag queue for later handling
 */

extern "C" void
inetd_signal_reconfig(int verbose)
{
	flag_signal(SIGHUP);
}

extern "C" void
inetd_signal_stop(void)
{
	flag_signal(SIGTERM);
}

static void
sigchld()
{
	flag_signal(SIGCHLD);
}

static void
sigterm()
{
	flag_signal(SIGTERM);
}

static void
flag_signal(int signo)
{
	size_t len;

	if (debug) {
		const char *name = "NA";
		switch (signo) {
		case SIGHUP: name = "HUP"; break;
		case SIGTERM: name = "TERM"; break;
		case SIGCHLD: name = "CHLD"; break;
		default:
			break;
		}
		syslog(LOG_DEBUG, "signal L %d (SIG%s)", signo, name);
	}

	len = sockwrite(signalpipe[1], &signo, sizeof(signo));
	if (len != sizeof(signo)) {
		syslog(LOG_ERR, "write signal: %m");
			// Note: maybe running within an alternative thread; unsafe to teminate().
	}
}

/*
 * Record a new child pid for this service.
 * If we've reached the limit on children, then stop accepting incoming requests.
 */

static void
addchild(struct servtab *sep, pid_t pid, struct procinfo *proc)
{
	assert(sep);
	assert(pid != -1);

#ifdef SANITY_CHECK11
	if (SERVTAB_EXCEEDS_LIMIT(sep)) {
		syslog(LOG_ERR, "%s: %d >= %d",
		    __func__, sep->se_children.count(), sep->se_maxchild);
		terminate(EX_SOFTWARE);
		return;
	}
#endif

	if (nullptr == proc) {
		proc = new(std::nothrow) procinfo;
		if (nullptr == proc) {
			syslog(LOG_ERR, "new: %m");
			terminate(EX_OSERR);
			return;
		}
	}

	if (search_proc(pid, proc) != nullptr) {
		syslog(LOG_ERR, "addchild: child already on process list");
		terminate(EX_OSERR);
		return;
	}

	inetd::CriticalSection::Guard guard(sep->se_state.lock);
	assert(proc->pr_sep == nullptr);
	const int count = sep->se_children.push_front_r(*proc);
	proc->pr_sep = sep;
	if (SERVTAB_EXCEEDS_LIMITX(sep, count)) {
		disable(sep);
	}
}

static void
reapchildren(void)
{
	int status;
	pid_t pid;

	for (;;) {
		pid = process_group.wait(true /*nohang*/, status);
		if (pid <= 0)
			break;
		if (debug)
			syslog(LOG_DEBUG, "%d reaped, %s %u", pid,
			    WIFEXITED(status) ? "status" : "signal",
			    WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status));
		if (const char *server = reapchild(pid)) {
			if (WIFSIGNALED(status) || WEXITSTATUS(status)) {
				syslog(LOG_WARNING,
				    "%s[%d]: exited, %s %u",
				    server, pid,
				    WIFEXITED(status) ? "status" : "signal",
				    WIFEXITED(status) ? WEXITSTATUS(status): WTERMSIG(status));
			}
		}
	}
}

static const char *
reapchild(pid_t pid)
{
	const char *ret = nullptr;
	struct procinfo *proc;
	struct conninfo *conn;

	assert(pid != -1);
	if (pid == -1)
		return nullptr;

	if ((proc = search_proc(pid)) == nullptr) {
		if (debug) {
			syslog(LOG_DEBUG, "reapchild %d : not found", pid);
		}
		return nullptr;
	}

	if (debug) {
		syslog(LOG_DEBUG, "reapchild %d", pid);
	}

	if (servtab *sep = proc->pr_sep) {
		inetd::CriticalSection::Guard guard(sep->se_state.lock);
		const int count = (int)sep->se_children.remove(proc);
		if (! SERVTAB_EXCEEDS_LIMITX(sep, count))
			enable(sep);
		proc->pr_sep = nullptr;
		ret = sep->se_server;
	}

	conn = proc->pr_conn;
	free_proc(proc);
	free_conn(conn);

	return ret;
}

static inetd::CriticalSection service_lock_;
static Services services_ = std::make_shared<ServiceCollection>();

Services
services()
{
	inetd::CriticalSection::Guard guard(service_lock_);
	return services_;
}

static struct servtab *
config_match(Services &services, struct servconfig *cfg)
{
	for (auto sit : *services) {
		struct servtab *sep = sit.get();
		if (strcmp(sep->se_service, cfg->se_service) == 0 &&
		    strcmp(sep->se_proto, cfg->se_proto) == 0 &&
#if defined(RPC)
		    sep->se_rpc == cfg->se_rpc &&
#endif
		    sep->se_socktype == cfg->se_socktype &&
		    sep->se_family == cfg->se_family) {
			return sep;
		}
	}
	return nullptr;
}

static void
config(void)
{
	const char *dot;
	struct servconfig *(*configent)(const struct configparams *params, int *ret);
	struct servconfig *cfg;
	int cfgerr = 0;
#ifdef LOGIN_CAP
	login_cap_t *lc = nullptr;
#endif

	if (nullptr != (dot = strrchr(SERVICES, '.')) && 0 == strcmp(dot, ".xconf")) {
		if (! setconfig2(SERVICES)) {
			syslog(LOG_ERR, "%s", setconfig2status(nullptr));
			return;
		}
		configent = getconfigent2;
	} else {
		if (! setconfig(SERVICES)) {
			syslog(LOG_ERR, "%s: %m", SERVICES);
			return;
		}
		configent = getconfigent;
	}

	for (auto sit : *services_)
		sit->se_checked = 0;

	Services t_services(std::make_shared<ServiceCollection>());

	t_services->reserve(services_->size() > 64 ? services_->size() + 8 : 64);

	while ((cfg = configent(&params, &cfgerr))) {
#if !defined(_WIN32)
		if (getpwnam(cfg->se_user) == nullptr) {
			syslog(LOG_ERR, "%s/%s: no such user '%s', service ignored",
				cfg->se_service, cfg->se_proto, cfg->se_user);
			continue;
		}
		if (cfg->se_group && getgrnam(cfg->se_group) == nullptr) {
			syslog(LOG_ERR, "%s/%s: no such group '%s', service ignored",
				cfg->se_service, cfg->se_proto, cfg->se_group);
			continue;
		}
#endif
#ifdef LOGIN_CAP
		if ((lc = login_getclass(cfg->se_class)) == nullptr) {
			/* error syslogged by getclass */
			syslog(LOG_ERR, "%s/%s: %s: login class error, service ignored",
				cfg->se_service, cfg->se_proto, cfg->se_class);
			continue;
		}
		login_close(lc);
#endif


		if (config_match(t_services, cfg)) {
			syslog(LOG_ERR, "%s/%s: service duplicated, secondary ignored",
				cfg->se_service, cfg->se_proto);
			continue;
		}

		const int new_nomapped = cfg->se_nomapped;
		struct servtab *sep = config_match(services_, cfg);

		if (sep != nullptr) {
			int i;

#define SWAP(t,a, b) { t c = a; a = b; b = c; }
			if (sep->se_nomapped != cfg->se_nomapped) {
				/* for rpc keep old nommaped till unregister */
#if defined(RPC)
				if (!sep->se_rpc)
#endif
					sep->se_nomapped = cfg->se_nomapped;
				sep->se_reset = 1;
			}

			/*
			 * children tracked remain; we want numchild to still reflect
			 * how many jobs are running so we don't throw off our accounting.
			 */
			if (debug)
				syslog(LOG_DEBUG, "recreating %s", cfg->se_service);

			sep->se_maxchild = cfg->se_maxchild;
			sep->se_cpmmax = cfg->se_cpmmax;
			sep->se_cpmwait = cfg->se_cpmwait;
			connections_resize(sep, cfg->se_maxperip);

			sep->se_bi = cfg->se_bi;

			/* might need to turn on or off service now */
			{	inetd::CriticalSection::Guard guard(sep->se_state.lock);
				if (sep->se_fd >= 0) {
					if (SERVTAB_EXCEEDS_LIMIT(sep) ||
							(sep->se_accept != cfg->se_accept)) {
						disable(sep);
					}
				}
			}
			sep->se_accept = cfg->se_accept;

			sep->se_user = std::move(cfg->se_user);
			sep->se_group = std::move(cfg->se_group);
			sep->se_banner = std::move(cfg->se_banner);
			sep->se_banner_success = std::move(cfg->se_banner_success);
			sep->se_banner_fail = std::move(cfg->se_banner_fail);
#ifdef LOGIN_CAP
			sep->se_class = std::move(cfg->se_class);
#endif
			sep->se_server = std::move(cfg->se_server);
			SWAP(const char *, sep->se_server_name, cfg->se_server_name);
			sep->se_working_directory = std::move(cfg->se_working_directory);
			sep->se_arguments = std::move(cfg->se_arguments);
			for (i = 0; i < MAXARGV; i++)
				SWAP(const char *, sep->se_argv[i], cfg->se_argv[i]);
			sep->se_environ = std::move(cfg->se_environ);
			sep->se_access_times = std::move(cfg->se_access_times);
			sep->se_addresses = std::move(cfg->se_addresses);
#ifdef IPSEC
			sep->se_policy = std::move(cfg->se_policy);
			ipsecsetup(sep);
#endif
			freeconfig(cfg);
			print_service("REDO", sep);

		} else {
			if (debug)
				syslog(LOG_DEBUG, "creating %s", cfg->se_service);
			sep = new(std::nothrow) servtab(*cfg);
			if (sep == nullptr) {
				syslog(LOG_ERR, "new: %m");
				terminate(EX_OSERR);
			}
			print_service("ADD ", sep);
		}
		t_services->push_back(sep);

		sep->se_checked = 1;
		if (ISMUX(sep)) {
			sep->se_fd = -1;
			continue;
		}
		switch (sep->se_family) {
		case AF_INET:
			if (!params.v4bind_ok) {
				sep->se_fd = -1;
				continue;
			}
			break;
#ifdef INET6
		case AF_INET6:
			if (!params.v6bind_ok) {
				sep->se_fd = -1;
				continue;
			}
			break;
#endif
		}

#if defined(RPC)
		if (!sep->se_rpc) {
#endif
			switch (sep->se_family) {
			case AF_INET:
				if (sep->se_port != sep->se_ctrladdr4.sin_port) {
					sep->se_ctrladdr4.sin_port = sep->se_port;
					sep->se_reset = 1;
				}
				break;
#ifdef INET6
			case AF_INET6:
				if (sep->se_port != sep->se_ctrladdr6.sin6_port) {
					sep->se_ctrladdr6.sin6_port = sep->se_port;
					sep->se_reset = 1;
				}
				break;
#endif
			}
			if (sep->se_reset != 0 && sep->se_fd >= 0)
				close_sep(sep);
#if defined(RPC)
		} else {
			struct rpcent rpc = getrpcbyname(sep->se_service);
			if (rpc == 0) {
				syslog(LOG_ERR, "%s/%s unknown RPC service",
					sep->se_service, sep->se_proto);
				if (sep->se_fd != -1)
					(void) close(sep->se_fd);
				sep->se_fd = -1;
				continue;
			}
			if (sep->se_reset != 0 || rpc->r_number != sep->se_rpc_prog) {
				if (sep->se_rpc_prog)
					unregisterrpc(sep);
				sep->se_rpc_prog = rpc->r_number;
				if (sep->se_fd != -1)
					(void) close(sep->se_fd);
				sep->se_fd = -1;
			}
			sep->se_nomapped = new_nomapped;
		}
#endif
		sep->se_reset = 0;

		inetd::CriticalSection::Guard guard(sep->se_state.lock);
		if (sep->se_state.enabled) {
			if (sep->se_fd == -1) {
				setup(sep);
			} else if (! SERVTAB_EXCEEDS_LIMIT(sep)) {
				enable(sep);
			}
		}
	}

	if (cfgerr) terminate(cfgerr);
	endconfig();
	endconfig2();

	/*
	 * Swap resources and purge anything not looked at above.
	 */
	{	inetd::CriticalSection::Guard guard(service_lock_);
		services_.swap(t_services);
	}

	for (auto sit : *t_services) {
		struct servtab *sep = sit.get();
		if (! sep->se_checked) {
			close_sep(sep, true);
		}
	}
}

#if defined(RPC)
static void
unregisterrpc(struct servtab *sep)
{
	u_int i;
	struct servtab *sepp;
	struct netconfig *netid4, *netid6;

	netid4 = sep->se_socktype == SOCK_DGRAM ? udpconf : tcpconf;
	netid6 = sep->se_socktype == SOCK_DGRAM ? udp6conf : tcp6conf;
	if (sep->se_family == AF_INET)
		netid6 = nullptr;
	else if (sep->se_nomapped)
		netid4 = nullptr;

	/*
	 * Conflict if same prog and protocol - In that case one should look
	 * to versions, but it is not interesting: having separate servers for
	 * different versions does not work well.
	 * Therefore one do not unregister if there is a conflict.
	 * There is also transport conflict if destroying INET when INET46
	 * exists, or destroying INET46 when INET exists
	 */
	for (sepp = servtabs; sepp; sepp = sepp->se_next) {
		if (sepp == sep)
			continue;
		if (sepp->se_checked == 0 || !sepp->se_rpc ||
			    strcmp(sep->se_proto, sepp->se_proto) != 0 ||
			    sep->se_rpc_prog != sepp->se_rpc_prog)
			continue;
		if (sepp->se_family == AF_INET)
			netid4 = nullptr;
		if (sepp->se_family == AF_INET6) {
			netid6 = nullptr;
			if (!sep->se_nomapped)
				netid4 = nullptr;
		}
		if (netid4 == nullptr && netid6 == nullptr)
			return;
	}
	print_service("UNREG", sep);
	for (i = sep->se_rpc_lowvers; i <= sep->se_rpc_highvers; i++) {
		if (netid4)
			rpcb_unset(sep->se_rpc_prog, i, netid4);
		if (netid6)
			rpcb_unset(sep->se_rpc_prog, i, netid6);
	}
	if (sep->se_fd != -1)
		(void) close(sep->se_fd);
	sep->se_fd = -1;
}
#endif //RPC

static void
retry(void)
{
	Services current_services(services());
	for (auto sit : *current_services) {
		struct servtab *sep = sit.get();

		inetd::CriticalSection::Guard guard(sep->se_state.lock);
		if (sep->se_state.enabled) {
			if (sep->se_fd == -1 && !ISMUX(sep)) {
				setup(sep);
			}
		}
	}
}

static void
setup(struct servtab *sep)
{
	int on = 1;

	/* Set all listening sockets to close-on-exec. */
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif

	assert(sep->se_state.enabled);

	if ((sep->se_fd = socket(sep->se_family, sep->se_socktype | SOCK_CLOEXEC, 0)) < 0) {
		// Note: the socket function creates a socket that supports overlapped I/O operations as the default behavior.
		syslog(LOG_ERR, "%s/%s: socket: %m",
		    sep->se_service, sep->se_proto);
		return;
	}

#define turnon(fd, opt) \
		setsockopt(fd, SOL_SOCKET, opt, (char *)&on, sizeof (on))
	if (strcmp(sep->se_proto, "tcp") == 0 && (params.sockopts & SO_DEBUG) && turnon(sep->se_fd, SO_DEBUG) < 0)
		syslog(LOG_ERR, "setsockopt (SO_DEBUG): %m");
	if (turnon(sep->se_fd, SO_REUSEADDR) < 0)
		syslog(LOG_ERR, "setsockopt (SO_REUSEADDR): %m");

	/* Set the socket buffer sizes, if specified. (netbsd) */
	if (sep->se_sndbuf != 0 && setsockopt(sep->se_fd, SOL_SOCKET, SO_SNDBUF, (char *)&sep->se_sndbuf, sizeof(sep->se_sndbuf)) < 0)
		syslog(LOG_ERR, "setsockopt (SO_SNDBUF %d): %m", sep->se_sndbuf);
	if (sep->se_rcvbuf != 0 && setsockopt(sep->se_fd, SOL_SOCKET, SO_RCVBUF, (char *)&sep->se_rcvbuf, sizeof(sep->se_rcvbuf)) < 0)
		syslog(LOG_ERR, "setsockopt (SO_RCVBUF %d): %m", sep->se_rcvbuf);

#ifdef SO_PRIVSTATE
	if (turnon(sep->se_fd, SO_PRIVSTATE) < 0)
		syslog(LOG_ERR, "setsockopt (SO_PRIVSTATE): %m");
#endif

	/* tftpd opens a new connection then needs more infos */
#ifdef INET6
#if defined(IPV6_RECVPKTINFO)
	if ((sep->se_family == AF_INET6) && //Set delivery of the IPV6_PKTINFO control message on incoming datagrams/UDP.
			(strcmp(sep->se_proto, "udp") == 0) && (sep->se_accept == 0) &&
			(setsockopt(sep->se_fd, IPPROTO_IPV6, IPV6_RECVPKTINFO, (char *)&on, sizeof (on)) < 0))
		syslog(LOG_ERR, "setsockopt (IPV6_RECVPKTINFO): %m");
#endif
	if (sep->se_family == AF_INET6) {
		int flag = sep->se_nomapped ? 1 : 0;
		if (setsockopt(sep->se_fd, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&flag, sizeof (flag)) < 0)
			syslog(LOG_ERR, "setsockopt (IPV6_V6ONLY): %m");
	}
#endif
#undef turnon
#ifdef IPSEC
	ipsecsetup(sep);
#endif
#if defined(HAVE_AF_UNIX)
	if (sep->se_family == AF_UNIX) {
		(void) unlink(sep->se_ctrladdr_un.sun_path);
		umask(0777); /* Make socket with conservative permissions */
	}
#endif //HAVE_AF_UNIX

	if (bind(sep->se_fd, (struct sockaddr *)&sep->se_ctrladdr, sep->se_ctrladdr_size) < 0) {
		syslog(LOG_ERR, "%s/%s: bind: %m", sep->se_service, sep->se_proto);
		(void) sockclose(sep->se_fd);
		sep->se_fd = -1;
		setalarm(RETRYTIME);
#if defined(HAVE_AF_UNIX)
		if (sep->se_family == AF_UNIX)
			umask(mask);
#endif //HAVE_AF_UNIX
		return;
	}

#if defined(HAVE_AF_UNIX)
	if (sep->se_family == AF_UNIX) {
		/* Ick - fch{own,mod} don't work on Unix domain sockets */
		if (chown(sep->se_service, sep->se_sockuid, sep->se_sockgid) < 0)
			syslog(LOG_ERR, "chown socket: %m");
		if (chmod(sep->se_service, sep->se_sockmode) < 0)
			syslog(LOG_ERR, "chmod socket: %m");
		umask(mask);
	}
#endif //HAVE_AF_UNIX

#if defined(RPC)
	if (sep->se_rpc) {
		u_int i;
		socklen_t len = sep->se_ctrladdr_size;
		struct netconfig *netid, *netid2 = nullptr;
#ifdef INET6
		struct sockaddr_in sock;
#endif
		struct netbuf nbuf, nbuf2;

		if (getsockname(sep->se_fd,
				(struct sockaddr*)&sep->se_ctrladdr, &len) < 0){
			syslog(LOG_ERR, "%s/%s: getsockname: %m",
				sep->se_service, sep->se_proto.c_str());
			(void) close(sep->se_fd);
			sep->se_fd = -1;
			return;
		}
		nbuf.buf = &sep->se_ctrladdr;
		nbuf.len = sep->se_ctrladdr.sa_len;
		if (sep->se_family == AF_INET)
			netid = sep->se_socktype==SOCK_DGRAM? udpconf:tcpconf;
#ifdef INET6
		else  {
			netid = sep->se_socktype==SOCK_DGRAM? udp6conf:tcp6conf;
			if (!sep->se_nomapped) { /* INET and INET6 */
				netid2 = netid==udp6conf? udpconf:tcpconf;
				memset(&sock, 0, sizeof sock); /* ADDR_ANY */
				nbuf2.buf = &sock;
				nbuf2.len = sock.sin_len = sizeof sock;
				sock.sin_family = AF_INET;
				sock.sin_port = sep->se_ctrladdr6.sin6_port;
			}
		}
#else
		else {
			syslog(LOG_ERR, "%s/%s: inetd compiled without inet6 support\n",
			    sep->se_service, sep->se_proto);
			(void) close(sep->se_fd);
			sep->se_fd = -1;
			return;
		}
#endif
		print_service("REG ", sep);
		for (i = sep->se_rpc_lowvers; i <= sep->se_rpc_highvers; i++) {
			rpcb_unset(sep->se_rpc_prog, i, netid);
			rpcb_set(sep->se_rpc_prog, i, netid, &nbuf);
			if (netid2) {
				rpcb_unset(sep->se_rpc_prog, i, netid2);
				rpcb_set(sep->se_rpc_prog, i, netid2, &nbuf2);
			}
		}
	}
#endif //RPC

	if (sep->se_socktype == SOCK_STREAM)
		listen(sep->se_fd, -1);
	enable(sep);
	if (debug) {
		syslog(LOG_DEBUG, "registered %s on %d",
			sep->se_server, sep->se_fd);
	}
}

#ifdef IPSEC
static void
ipsecsetup(struct servtab *sep)
{
	char *buf;
	char *policy_in = nullptr;
	char *policy_out = nullptr;
	int level;
	int opt;

	switch (sep->se_family) {
	case AF_INET:
		level = IPPROTO_IP;
		opt = IP_IPSEC_POLICY;
		break;
#ifdef INET6
	case AF_INET6:
		level = IPPROTO_IPV6;
		opt = IPV6_IPSEC_POLICY;
		break;
#endif
	default:
		return;
	}

	if (!sep->se_policy || sep->se_policy[0] == '\0') {
		static char def_in[] = "in entrust", def_out[] = "out entrust";
		policy_in = def_in;
		policy_out = def_out;
	} else {
		if (!strncmp("in", sep->se_policy, 2))
			policy_in = sep->se_policy;
		else if (!strncmp("out", sep->se_policy, 3))
			policy_out = sep->se_policy;
		else {
			syslog(LOG_ERR, "invalid security policy \"%s\"",
				sep->se_policy);
			return;
		}
	}

	if (policy_in != nullptr) {
		buf = ipsec_set_policy(policy_in, strlen(policy_in));
		if (buf != nullptr) {
			if (setsockopt(sep->se_fd, level, opt,
					buf, ipsec_get_policylen(buf)) < 0 && debug != 0)
				syslog(LOG_DEBUG, "%s/%s: ipsec initialization failed; %s",
					sep->se_service, sep->se_proto, policy_in);
			free(buf);
		} else
			syslog(LOG_ERR, "invalid security policy \"%s\"",
				policy_in);
	}
	if (policy_out != nullptr) {
		buf = ipsec_set_policy(policy_out, strlen(policy_out));
		if (buf != nullptr) {
			if (setsockopt(sep->se_fd, level, opt,
					buf, ipsec_get_policylen(buf)) < 0 && debug != 0)
				syslog(LOG_DEBUG, "%s/%s: ipsec initialization failed; %s",
					sep->se_service, sep->se_proto, policy_out);
			free(buf);
		} else
			syslog(LOG_ERR, "invalid security policy \"%s\"",
				policy_out);
	}
}
#endif

/*
 *  Finish with a service and its socket.
 */

static void
close_sep(struct servtab *sep, bool end /*=false*/)
{
	assert(sep->se_state.enabled);

	if (debug)
		syslog(LOG_DEBUG, "closing %s, fd %d", sep->se_service, sep->se_fd);

	inetd::CriticalSection::Guard guard(sep->se_state.lock);
	disable(sep, true);
	if (end) {
		sep->se_state.enabled = false;
	}

	sep->se_children.drain_r(		// forget about any existing children
		[](auto sc) {
			delete sc;
		});
	sep->se_count = 0;			// reset usage
}

void servtab::intrusive_deleter(struct servtab *sep)
{
	assert(! sep->se_state.enabled);
	assert(! sep->se_state.running);

	print_service("FREE", sep);
#if defined(RPC)
	if (sep->se_rpc && sep->se_rpc_prog > 0)
		unregisterrpc(sep);
#endif
	connections_free(sep);
	freeconfig(static_cast<struct servconfig *>(sep));
	delete sep;
}

/*
 *  Service control
 */

static void
enable(struct servtab *sep)
{
	assert(sep->se_state.enabled);
	if (sep->se_state.running) {
#ifdef SANITY_CHECK
		assert(sep->se_fd >= 0);
		if (sep->se_accept && sep->se_socktype == SOCK_STREAM && iocp.Enabled()) {
			assert(sep->se_listener.is_open());
		} else {
			assert(FD_ISSET(sep->se_fd, &allsock));
		}
#endif
		return;
	}
	sep->se_state.running = true;

	if (debug)
		syslog(LOG_DEBUG, "enabling %s, fd %d", sep->se_service, sep->se_fd);

#ifdef SANITY_CHECK
	if (sep->se_fd < 0) {
		syslog(LOG_ERR, "%s: %s: bad fd", __func__, sep->se_service);
		terminate(EX_SOFTWARE);
		return;
	}
	if (ISMUX(sep)) {
		syslog(LOG_ERR, "%s: %s: is mux", __func__, sep->se_service);
		terminate(EX_SOFTWARE);
		return;
	}
	if (FD_ISSET(sep->se_fd, &allsock)) {
		syslog(LOG_ERR, "%s: %s: stream is sync", __func__, sep->se_service);
		terminate(EX_SOFTWARE);
	}
#endif

	if (sep->se_accept && sep->se_socktype == SOCK_STREAM && iocp.Enabled()) {
		if (! iocp.Listen(sep->se_listener, sep->se_fd)) {
			terminate(EX_SOFTWARE);
			return;
		}

		std::shared_ptr<inetd::IOCPService::Socket>
			acceptor = std::make_shared<inetd::IOCPService::Socket>();

		if (! iocp.Accept(sep->se_listener, *acceptor.get(),
			    std::bind(&async_accept, sep->shared_from_this(), acceptor, std::placeholders::_1))) {
			terminate(EX_SOFTWARE);
			return;
		}

	} else {
		FD_SET(sep->se_fd, &allsock);
#ifdef SANITY_CHECK
		++nsock;
#endif
	}
}

static void
disable(struct servtab *sep, bool closing /*=false*/)
{
	assert(sep->se_state.enabled);
	if (sep->se_state.running) {
		sep->se_state.running = false;

		if (debug)
			syslog(LOG_DEBUG, "disabling %s, fd %d", sep->se_service, sep->se_fd);

#ifdef SANITY_CHECK
		if (sep->se_fd < 0) {
			syslog(LOG_ERR, "%s: %s: bad fd", __func__, sep->se_service);
			terminate(EX_SOFTWARE);
			return;
		}
		if (ISMUX(sep)) {
			syslog(LOG_ERR, "%s: %s: is mux", __func__, sep->se_service);
			terminate(EX_SOFTWARE);
			return;
		}
		if (FD_ISSET(sep->se_fd, &allsock)) {
			syslog(LOG_ERR, "%s: %s: not off", __func__, sep->se_service);
			terminate(EX_SOFTWARE);
		}
#endif

		if (sep->se_accept && sep->se_socktype == SOCK_STREAM && iocp.Enabled()) {
			if (! closing && ! iocp.Cancel(sep->se_listener)) {
				terminate(EX_SOFTWARE);
			}

		} else {
			FD_CLR(sep->se_fd, &allsock);
#ifdef SANITY_CHECK
			--nsock;
#endif
		}
	}

	if (closing && sep->se_fd >= 0) {
		if (sep->se_accept && sep->se_socktype == SOCK_STREAM && iocp.Enabled()) {
			iocp.Shutdown(sep->se_listener);
		}
		sockclose(sep->se_fd);
		sep->se_fd = -1;
	}
}

void
inetd_setproctitle(const char *a, int s)
{
	socklen_t size;
	struct sockaddr_storage ss;
	char buf[80], pbuf[NI_MAXHOST] = "unknown";

	size = sizeof(ss);
	if (getpeername(s, (struct sockaddr *)&ss, &size) == 0) {
		getnameinfo((struct sockaddr *)&ss, SOCKLEN_SOCKADDR_STORAGE(ss), pbuf, sizeof(pbuf), nullptr, 0, NI_NUMERICHOST);
		(void) sprintf_s(buf, sizeof(buf), "%s [%s]", a, pbuf);
	} else {
		(void) sprintf_s(buf, sizeof(buf), "%s", a);
	}
	setproctitle("%s", buf);
}


//TODO: see port_good_dg(), NETBSD inetd
int
check_loop(const struct sockaddr *sa, const struct servtab *sep)
{
	char pname[NI_MAXHOST] = "unknown";

	Services current_services(services());
	for (auto se2 : *current_services) {
		if (!se2->se_bi || se2->se_socktype != SOCK_DGRAM)
			continue;

		switch (se2->se_family) {
		case AF_INET:
			if (csatosin(sa)->sin_port == se2->se_ctrladdr4.sin_port)
				goto isloop;
			continue;
#ifdef INET6
		case AF_INET6:
			if (csatosin6(sa)->sin6_port == se2->se_ctrladdr6.sin6_port)
				goto isloop;
			continue;
#endif
		default:
			continue;
		}
	isloop:
		getnameinfo(sa, SOCKLEN_SOCKADDR_PTR(sa), pname, sizeof(pname), nullptr, 0, NI_NUMERICHOST);
		syslog(LOG_WARNING, "%s/%s:%s/%s loop request REFUSED from %s",
		       sep->se_service, sep->se_proto, se2->se_service, se2->se_proto, pname);
		return 1;
	}
	return 0;
}


static void
print_service(const char *action, const struct servconfig *sep)
{
	if (debug)
		syslogconfig(action, sep);
}


static struct conninfo *
search_connections(PeerInfo &remote)
{
	struct servtab *sep = remote.getserv();
	struct conninfo *conn = nullptr;
	int hv;

	if (sep->se_maxperip <= 0)
		return nullptr;

	const struct sockaddr_storage *ss = remote.getaddr();
	if (nullptr == ss)
		return nullptr;

	switch (ss->ss_family) {
	case AF_INET:
		hv = hashval((char *)&((struct sockaddr_in *)ss)->sin_addr, sizeof(struct in_addr));
		break;
#ifdef INET6
	case AF_INET6:
		hv = hashval((char *)&((struct sockaddr_in6 *)ss)->sin6_addr, sizeof(struct in6_addr));
		break;
#endif
	default:
		/*
		 * Since we only support AF_INET and AF_INET6, just
		 * let other than AF_INET and AF_INET6 through.
		 */
		return nullptr;
	}

	const char *pname = remote.getname();
	if (nullptr == pname) {
		return nullptr;
	}

	sep->se_conn[hv].foreach_term_r([hv, ss, &sep, pname, &conn](struct conninfo *ci) {
		if (ci) {
			char pname2[NI_MAXHOST];

			if (getnameinfo((const struct sockaddr *)&ci->co_addr,
				    SOCKLEN_SOCKADDR_STORAGE(ci->co_addr), pname2, sizeof(pname2), nullptr, 0, NI_NUMERICHOST) == 0) {
				if (strcmp(pname, pname2) == 0) {
					conn = ci;
					return true; //match
				}
			} else {
				syslog(LOG_ERR, "%s getnameinfo error : %M", sep->se_service);
			}
			return false; //next
		}

		if ((ci = new(std::nothrow) conninfo(sep->se_maxperip)) == nullptr) {
			syslog(LOG_ERR, "new: %m");
			terminate(EX_OSERR);
			conn = (conninfo *)-1;
			return false; //done
		}

		static_assert(sizeof(ci->co_addr) == sizeof(*ss), "address structures");
		memcpy(&ci->co_addr, &ss, sizeof(ci->co_addr));
		sep->se_conn[hv].push_front_r(*ci);
		conn = ci;
		return true; //done
	});

	/*
	 * Since a child process is not invoked yet, we cannot determine a pid of a child.
	 * So, co_proc and co_numchild should be filled leter.
	 */

	return conn;
}

static bool
new_connection(const struct servtab *sep, struct conninfo *conn, struct procinfo *&proc)
{
	char pname[NI_MAXHOST] = "unknown";
	int maxchild = 0;

	proc = nullptr;
	if (nullptr == (proc = connprocs_new(conn, maxchild))) {
		if (maxchild > 0) { // limit imposed
			if (getnameinfo((struct sockaddr *)&conn->co_addr,
				    SOCKLEN_SOCKADDR_STORAGE(conn->co_addr),
				    pname, sizeof(pname), nullptr, 0, NI_NUMERICHOST)) {
				syslog(LOG_ERR, "%s getnameinfo error : %M", sep->se_service);
			}
			syslog(LOG_ERR, "%s from %s exceeded count (limit %d)",
				sep->se_service, pname, maxchild);
			return false;
		}
	}
	return true;
}

static void
connections_resize(struct servtab *sep, int maxperip)
{
	if (maxperip <= 0) {
		connections_free(sep);
		return;
	}
	for (unsigned i = 0; i < PERIPSIZE; ++i) {
		sep->se_conn[i].foreach_r([maxperip](auto conn) {
			if (! conn->co_procs.resize(maxperip)) {
				terminate(EX_OSERR);
			}
			return false; //next
		});
	}
}

static void
connections_free(struct servtab *sep)
{
	for (unsigned i = 0; i < PERIPSIZE; ++i) {
		sep->se_conn[i].foreach_safe_r([](auto conn) {
			connprocs_clear(conn);
			free_conn(conn);
			return false; //next
		});
	}
	assert(0 == sep->se_children.count());
}

static void
free_conn(struct conninfo *conn)
{
	if (nullptr == conn)
		return;
	if (conn->co_procs.numchild() <= 0) {
		ConnInfoList::remove_self_r(conn);
		delete conn;
	}
}


/////////////////////////////////////////////////////////////////////////////////////////
//	connprocs

static struct procinfo *
connprocs_new(struct conninfo *conn, int &maxchild)
{
	if (struct procinfo *proc = conn->co_procs.newproc(conn, maxchild)) {
		if (proc == (struct procinfo *) -1) {
			syslog(LOG_ERR, "new: %m");
			terminate(EX_OSERR);
			proc = nullptr;
		}
		return proc;
	}
	return nullptr;
}

static void
connprocs_clear(struct conninfo *conn)
{
	conn->co_procs.clear(conn);
		// note: underlying procinfo's are not destroyed
		//  these are assumed to be owned by the childlist.
}


/////////////////////////////////////////////////////////////////////////////////////////
//	procinfo

static struct procinfo *
search_proc(pid_t pid, struct procinfo *add)
{
	struct procinfo *proc = nullptr;
	int hv;

	assert(nullptr == add || -1 == add->pr_pid);

	hv = hashval((const char *)&pid, sizeof(pid));
	proctable[hv].foreach_term_r([hv, pid, add, &proc](struct procinfo *pi) {
		if (pi) {
			if (pi->pr_pid == pid) {
				proc = pi;
				return true;
			}

		} else if (proc == nullptr /*nomatch*/ && add) {
			proctable[hv].push_back(*add);
			add->pr_pid = pid;
			return true;
		}
		return false; //next
	});
	return proc;
}

static void
free_proc(struct procinfo *proc)
{
	if (nullptr == proc)
		return;

	if (struct conninfo *conn = proc->pr_conn) {
		conn->co_procs.unlink(proc);
		proc->pr_conn = nullptr;
	}

	if (-1 != proc->pr_pid) {
		if (struct servtab *sep = proc->pr_sep) {
			sep->se_children.remove_r(proc);
			proc->pr_sep = nullptr;
		}
		ProcInfoList::remove_self_r(proc);
		proc->pr_pid = -1;
	}

	assert(! proc->pr_child_link_.is_hooked());
	assert(! proc->pr_procinfo_link_.is_hooked());
	assert(nullptr == proc->pr_conn);
	assert(nullptr == proc->pr_sep);
	delete proc;
}

static int
hashval(const char *p, int len)
{
	unsigned int hv = 0xABC3D20F;
	int i;

	for (i = 0; i < len; ++i, ++p)
		hv = (hv << 5) ^ (hv >> 23) ^ *p;
	hv = (hv ^ (hv >> 16)) & (PERIPSIZE - 1);
	return hv;
}

//end

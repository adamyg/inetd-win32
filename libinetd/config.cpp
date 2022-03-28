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

/*
 *  TODO: extended configuration -
 *      xinetd style, yet JSON or registry sourced.
 *
 *  defaults
 *  {
 *      <attribute> = <value> <value> ...
 *      ...
 *  }
 *
 *  service <service_name>
 *  {
 *      <attribute> <assign_op> <value> <value> ...
 *      ...
 *  }
 *
 */

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif

#include <exception>

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#if defined(RPC)
#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <err.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <sysexits.h>
#include "../service/syslog.h"
#include <unistd.h>

#include "inetd.h"
#include "config.h"

#if !defined(MAX)
#define MIN(X,Y)	((X) < (Y) ? (X) : (Y))
#define MAX(X,Y)	((X) > (Y) ? (X) : (Y))
#endif

struct snode {  /* name string cache */
	LIST_ENTRY(snode) node_;
	char name_[1];  //implied nul
};

struct servconfig *nextconfigent(const struct configparams *params);
static int	matchservent(const char *, const char *, const char *);
static char	*skip(char **);
static char	*sskip(char **);
static const char *newname(const char *);
static const char *newstr(const char *);
static char	*nextline(FILE *);
static bool	parse_protocol_sizes(struct servconfig *sep);

static LIST_HEAD(snodes, snode) strings;
static const char *CONFIG = "";
static FILE	*fconfig = (FILE *)-1;
static struct	servconfig configent;
static char	line[LINE_MAX];

int
setconfig(const char *path)
{
	if ((FILE *)-1 == fconfig) {
		LIST_INIT(&strings);
	} else if (fconfig) {
		fseek(fconfig, 0L, SEEK_SET);
		return (1);
	}
	fconfig = fopen(CONFIG = path, "r");
	return (fconfig != NULL);
}

void
endconfig(void)
{
	if (fconfig && fconfig != (FILE *)-1) {
		(void) fclose(fconfig);
		fconfig = NULL;
	}
}

struct servconfig *
getconfigent(const struct configparams *params, int *ret)
{
	try {
		if (ret) *ret = 0;
		return nextconfigent(params);
	} catch (int exit_code) {
		if (ret) *ret = exit_code;
	} catch (std::exception &msg) {
		syslog(LOG_ERR, "config error : exception, %s", msg.what());
		if (ret) *ret = EX_SOFTWARE;
	} catch (...) {
		syslog(LOG_ERR, "config error : exception");
		if (ret) *ret = EX_SOFTWARE;
	}
	return NULL;
}

static struct servconfig *
nextconfigent(const struct configparams *params)
{
	struct servconfig *sep = &configent;
	int argc;
	char *cp, *arg, *s;
	static char TCPMUX_TOKEN[] = "tcpmux/";
#define MUX_LEN 	(sizeof(TCPMUX_TOKEN)-1)
#ifdef IPSEC
	char *policy;
#endif
#ifdef INET6
	int v4bind;
	int v6bind;
#endif
#if !defined(_WIN32)
	size_t unsz;
#endif

#ifdef IPSEC
	policy = NULL;
#endif
more:
#ifdef INET6
	v4bind = 0;
	v6bind = 0;
#endif

	if (NULL == fconfig || (FILE *)-1 == fconfig)
		return (NULL);

	while ((cp = nextline(fconfig)) != NULL) {
#ifdef IPSEC
		/* lines starting with #@ is not a comment, but the policy */
		if (cp[0] == '#' && cp[1] == '@') {
			char *p;
			for (p = cp + 2; p && *p && isspace(*p); p++)
				;
			if (*p == '\0') {
				free(policy);
				policy = NULL;
			} else if (ipsec_get_policylen(p) >= 0) {
				free(policy);
				policy = newstr(p);
			} else {
				syslog(LOG_ERR,
					"%s: invalid ipsec policy \"%s\"",
					CONFIG, p);
				terminate(EX_CONFIG);
			}
		}
#endif
		if (*cp == '#' || *cp == '\0')
			continue;
		break;
	}
	if (cp == NULL) {
#ifdef IPSEC
		free(policy);
#endif
		return (NULL);
	}

	/*
	 * clear the static buffer, since some fields (se_ctrladdr,
	 * for example) don't get initialized here.
	 */
	memset(sep, 0, sizeof *sep);

	//
	//  service-name
	arg = skip(&cp);
	if (cp == NULL) {
		/* got an empty line containing just blanks/tabs. */
		goto more;
	}
	if (arg[0] == ':') {	/* :user:group:perm: */
		char *user, *group, *perm;
		struct passwd *pw;
		struct group *gr;
		user = arg+1;
		if ((group = strchr(user, ':')) == NULL) {
			syslog(LOG_ERR, "no group after user '%s'", user);
			goto more;
		}
		*group++ = '\0';
		if ((perm = strchr(group, ':')) == NULL) {
			syslog(LOG_ERR, "no mode after group '%s'", group);
			goto more;
		}
		*perm++ = '\0';
		if ((pw = getpwnam(user)) == NULL) {
			syslog(LOG_ERR, "no such user '%s'", user);
			goto more;
		}
		sep->se_sockuid = pw->pw_uid;
		if ((gr = getgrnam(group)) == NULL) {
			syslog(LOG_ERR, "no such user '%s'", group);
			goto more;
		}
		sep->se_sockgid = gr->gr_gid;
		sep->se_sockmode = (mode_t)strtol(perm, &arg, 8);
		if (*arg != ':') {
			syslog(LOG_ERR, "bad mode '%s'", perm);
			goto more;
		}
		*arg++ = '\0';
	} else {
		sep->se_sockuid = params->euid;
		sep->se_sockgid = params->egid;
		sep->se_sockmode = 0200;
	}
	if (strncmp(arg, TCPMUX_TOKEN, MUX_LEN) == 0) {
		char *c = arg + MUX_LEN;
		if (*c == '+') {
			sep->se_type = MUXPLUS_TYPE;
			c++;
		} else
			sep->se_type = MUX_TYPE;
		sep->se_service = newname(c);
	} else {
		sep->se_service = newname(arg);
		sep->se_type = NORM_TYPE;
	}

	//
	//  socket-type
	//
	arg = sskip(&cp);
	if (strcmp(arg, "stream") == 0)
		sep->se_socktype = SOCK_STREAM;
	else if (strcmp(arg, "dgram") == 0)
		sep->se_socktype = SOCK_DGRAM;
	else if (strcmp(arg, "rdm") == 0)
		sep->se_socktype = SOCK_RDM;
	else if (strcmp(arg, "seqpacket") == 0)
		sep->se_socktype = SOCK_SEQPACKET;
	else if (strcmp(arg, "raw") == 0)
		sep->se_socktype = SOCK_RAW;
	else
		sep->se_socktype = -1;

	//
	//  protocol[,sndbuf=#][,rcvbuf=##]
	//
	arg = sskip(&cp);
	sep->se_proto = newstr(arg);
	if (strncmp(arg, "tcp", 3) == 0) {
		if (NULL != (arg = strchr(arg, '/')) && //tcp[46]/faith
			    strncmp(arg, "/faith", 6) == 0) {
			syslog(LOG_ERR, "faith has been deprecated");
			goto more;
		}
	} else {					//faith/xxx
		if (sep->se_type == NORM_TYPE && strncmp(arg, "faith/", 6) == 0) {
			syslog(LOG_ERR, "faith has been deprecated");
			goto more;
		}
	}

	/* extract the send and receive buffer sizes before parsing the protocol (netbsd).*/
	if (! parse_protocol_sizes(sep))
		goto more;

	if (strncmp(sep->se_proto, "rpc/", 4) == 0) {
#if defined(RPC)
		char *versp;
		memmove(sep->se_proto, sep->se_proto + 4, strlen(sep->se_proto) + 1 - 4);
		sep->se_rpc = 1;
		sep->se_rpc_prog = sep->se_rpc_lowvers =
			sep->se_rpc_highvers = 0;
		if ((versp = strrchr(sep->se_service, '/'))) {
			*versp++ = '\0';
			switch (sscanf(versp, "%u-%u", &sep->se_rpc_lowvers, &sep->se_rpc_highvers)) {
			case 2:
				break;
			case 1:
				sep->se_rpc_highvers = sep->se_rpc_lowvers;
				break;
			default:
				syslog(LOG_ERR,
					"bad RPC version specifier; %s",
					sep->se_service);
				freeconfig(sep);
				goto more;
			}
		}
		else {
			sep->se_rpc_lowvers =
			sep->se_rpc_highvers = 1;
		}
#else
		syslog(LOG_ERR, "%s: rpc services not supported", sep->se_service);
		goto more;
#endif
	}
	sep->se_nomapped = 0;

	if (strcmp(sep->se_proto, "unix") == 0) {
#if defined(HAVE_AF_UNIX)
		sep->se_family = AF_UNIX;
#else
		syslog(LOG_ERR, "%s: unix services not supported", sep->se_service);
		goto more;
#endif
	} else {
		int protolen = strlen(sep->se_proto);
		char *proto = (char *) sep->se_proto;
		if (0 == protolen) {
			syslog(LOG_ERR, "%s: invalid protocol specified", sep->se_service);
			goto more;
		}
		while (protolen-- && isdigit(proto[protolen])) {
			if (proto[protolen] == '6') { /*tcp6 or udp6*/
#ifdef INET6
				proto[protolen] = '\0';
				v6bind = 1;
				continue;
#else
				syslog(LOG_ERR, "IPv6 is not available, ignored");
				freeconfig(sep);
				goto more;
#endif
			}

			if (proto[protolen] == '4') { /*tcp4 or udp4*/
				proto[protolen] = '\0';
#ifdef INET6
				v4bind = 1;
#endif
				continue;
			}

			/* illegal version num */
			syslog(LOG_ERR, "bad IP version for %s", sep->se_proto);
			freeconfig(sep);
			goto more;
		}

#ifdef INET6
		if (v6bind && !params->v6bind_ok) {
			syslog(LOG_INFO, "IPv6 bind is ignored for %s", sep->se_service);
			if (v4bind && params->v4bind_ok)
				v6bind = 0;
			else {
				freeconfig(sep);
				goto more;
			}
		}
		if (v6bind) {
			sep->se_family = AF_INET6;
			if (!v4bind || !params->v4bind_ok)
				sep->se_nomapped = 1;
		} else
#endif
		{ /* default to v4 bind if not v6 bind */
			if (!params->v4bind_ok) {
				syslog(LOG_NOTICE, "IPv4 bind is ignored for %s", sep->se_service);
				freeconfig(sep);
				goto more;
			}
			sep->se_family = AF_INET;
		}
	}

	/* init ctladdr */
	switch(sep->se_family) {
	case AF_INET:
		memcpy(&sep->se_ctrladdr4, params->bind_sa4, sizeof(sep->se_ctrladdr4));
		sep->se_ctrladdr_size = sizeof(sep->se_ctrladdr4);
		break;
#ifdef INET6
	case AF_INET6:
		memcpy(&sep->se_ctrladdr6, params->bind_sa6, sizeof(sep->se_ctrladdr6));
		sep->se_ctrladdr_size = sizeof(sep->se_ctrladdr6);
		break;
#endif
#if defined(HAVE_AF_UNIX)
	case AF_UNIX:
#define SUN_PATH_MAXSIZE	sizeof(sep->se_ctrladdr_un.sun_path)
		memset(&sep->se_ctrladdr, 0, sizeof(sep->se_ctrladdr));
		sep->se_ctrladdr_un.sun_family = sep->se_family;
		if ((unsz = strlcpy(sep->se_ctrladdr_un.sun_path,
		    sep->se_service, SUN_PATH_MAXSIZE) >= SUN_PATH_MAXSIZE)) {
			syslog(LOG_ERR,
			    "domain socket pathname too long for service %s",
			    sep->se_service);
			goto more;
		}
		sep->se_ctrladdr_un.sun_len = unsz;
#undef SUN_PATH_MAXSIZE
		sep->se_ctrladdr_size = SUN_LEN(&sep->se_ctrladdr_un);
		break;
#endif //AF_UNIX
	}

	//
	//  {wait|nowait}[/max-child[/max-connections-per-ip-per-minute[/max-child-per-ip]]]
	//
	arg = sskip(&cp);
	if (!strncmp(arg, "wait", 4))
		sep->se_accept = 0;
	else if (!strncmp(arg, "nowait", 6))
		sep->se_accept = 1;
	else {
		syslog(LOG_ERR,
			"%s: bad wait/nowait for service %s",
			CONFIG, sep->se_service);
		goto more;
	}

	sep->se_maxchild = -1;
	sep->se_maxcpm   = -1;
	sep->se_maxperip = -1;
	if ((s = strchr(arg, '/')) != NULL) {
		char *eptr;
		u_long val;

		if (s[1] == '/') {		/* allow empty definition; default */
			eptr = s + 1;
		} else {
			val = strtoul(s + 1, &eptr, 10);
			if (eptr == s + 1 || val > MAX_MAXCHLD) {
				syslog(LOG_ERR, "%s: bad max-child for service %s", CONFIG, sep->se_service);
				goto more;
			}
			if (debug && !sep->se_accept && val != 1)
				syslog(LOG_WARNING, "maxchild=%lu for wait service %s"
					" not recommended", val, sep->se_service);
			sep->se_maxchild = val;
		}
		if (*eptr == '/') {
			if (*++eptr != '/') {	/* allow empty definition; default */
				sep->se_maxcpm = strtol(eptr, &eptr, 10);
			}
		}
		if (*eptr == '/') {
			if (*++eptr != '/') {	/* allow empty definition; default */
				sep->se_maxperip = strtol(eptr, &eptr, 10);
			}
		}

		/*
		 * explicitly do not check for \0 for future expansion /
		 * backwards compatibility
		 */
	}
	if (ISMUX(sep)) {
		/*
		 * Silently enforce "nowait" mode for TCPMUX services
		 * since they don't have an assigned port to listen on.
		 */
		sep->se_accept = 1;
		if (strcmp(sep->se_proto, "tcp")) {
			syslog(LOG_ERR,
				"%s: bad protocol for tcpmux service %s",
				CONFIG, sep->se_service);
			goto more;
		}
		if (sep->se_socktype != SOCK_STREAM) {
			syslog(LOG_ERR,
				"%s: bad socket type for tcpmux service %s",
				CONFIG, sep->se_service);
			goto more;
		}
	}

	//
	//  user[:group][/login-class]
	//
	sep->se_user = newstr(sskip(&cp));
#ifdef LOGIN_CAP
	if ((s = (char *)strrchr(sep->se_user, '/')) != NULL) {
		*s = '\0';
		sep->se_class = newstr(s + 1);
	} else
		sep->se_class = newstr(RESOURCE_RC);
#endif
	if ((s = (char *)strrchr(sep->se_user, ':')) != NULL) {
		*s = '\0';
		sep->se_group = newstr(s + 1);
	} else
		sep->se_group = NULL;

	//
	//  server-program
	//
	sep->se_server = newstr(sskip(&cp));
	if ((sep->se_server_name = strrchr(sep->se_server, '/')))
		sep->se_server_name++;

	if (strcmp(sep->se_server, "internal") == 0) {
		const struct biltin *bi;

		for (bi = biltins; bi->bi_service; bi++)
			if (bi->bi_socktype == sep->se_socktype &&
				    matchservent(bi->bi_service, sep->se_service, sep->se_proto))
				break;
		if (bi->bi_service == 0) {
			syslog(LOG_ERR, "internal service %s unknown", sep->se_service);
			goto more;
		}
		sep->se_accept = 1;	/* force accept mode for built-ins */
		sep->se_bi = bi;
	} else
		sep->se_bi = NULL;

	if (sep->se_maxperip < 0)
		sep->se_maxperip = params->maxperip;

	if (sep->se_maxcpm < 0)
		sep->se_maxcpm = params->maxcpm;

	if (sep->se_maxchild < 0) {	/* apply default max-children */
		if (sep->se_bi && sep->se_bi->bi_maxchild >= 0)
			sep->se_maxchild = sep->se_bi->bi_maxchild;
		else if (sep->se_accept)
			sep->se_maxchild = MAX(params->maxchild, 0);
		else
			sep->se_maxchild = 1;
	}

	//
	//  server-program-arguments
	//
	argc = 0;
	for (arg = skip(&cp); cp; arg = skip(&cp))
		if (argc < MAXARGV) {
			sep->se_argv[argc++] = newstr(arg);
		} else {
			syslog(LOG_ERR, "%s: too many arguments for service %s", CONFIG, sep->se_service);
			goto more;
		}
	while (argc <= MAXARGV)
		sep->se_argv[argc++] = NULL;

#ifdef IPSEC
	sep->se_policy = policy ? newstr(policy) : NULL;
	free(policy);
#endif
	return (sep);
}

static int
matchservent(const char *name1, const char *name2, const char *proto)
{
	char **alias;
	const char *p;
	struct servent *se;

	if (strcmp(proto, "unix") == 0) {
		if ((p = strrchr(name1, '/')) != NULL)
			name1 = p + 1;
		if ((p = strrchr(name2, '/')) != NULL)
			name2 = p + 1;
	}
	if (strcmp(name1, name2) == 0)
		return(1);
	if ((se = getservbyname(name1, proto)) != NULL) {
		if (strcmp(name2, se->s_name) == 0)
			return(1);
		for (alias = se->s_aliases; *alias; alias++)
			if (strcmp(name2, *alias) == 0)
				return(1);
	}
	return(0);
}

/*
 * Safe skip - if skip returns null, log a syntax error in the
 * configuration file and exit.
 */
static char *
sskip(char **cpp)
{
	char *cp;

	cp = skip(cpp);
	if (cp == NULL) {
		syslog(LOG_ERR, "%s: syntax error", CONFIG);
		throw EX_DATAERR;
	}
	return (cp);
}

static char *
skip(char **cpp)
{
	char *cp = *cpp;
	char *start;
	char quote = '\0';

again:
	while (*cp == ' ' || *cp == '\t')
		cp++;
	if (*cp == '\0') {
		int c;

		c = getc(fconfig);
		(void) ungetc(c, fconfig);
		if (c == ' ' || c == '\t')
			if ((cp = nextline(fconfig)))
				goto again;
		*cpp = (char *)0;
		return ((char *)0);
	}
	if (*cp == '"' || *cp == '\'')
		quote = *cp++;
	start = cp;
	if (quote)
		while (*cp && *cp != quote)
			cp++;
	else
		while (*cp && *cp != ' ' && *cp != '\t')
			cp++;
	if (*cp != '\0')
		*cp++ = '\0';
	*cpp = cp;
	return (start);
}

static char *
nextline(FILE *fd)
{
	char *cp;

	if (fgets(line, sizeof (line), fd) == NULL)
		return ((char *)0);
	cp = strchr(line, '\n');
	if (cp)
		*cp = '\0';
	return (line);
}

static const char *
newstr(const char *cp)
{
	char *cr;

	if ((cr = _strdup(cp != NULL ? cp : "")))
		return (cr);
	syslog(LOG_ERR, "strdup: %m");
	throw EX_OSERR;
	/*NOREACHED*/
	return NULL;
}

static const char *
newname(const char *name)
{
	snode *node;
	LIST_FOREACH(node, &strings, node_) {
		if (0 == strcmp(node->name_, name)) {
			return node->name_;
		}
	}

	const size_t slen = strlen(name);
	if (NULL == (node = (snode *)malloc(sizeof(snode) + slen))) {
		syslog(LOG_ERR, "malloc: %m");
		throw EX_OSERR;
		/*NOREACHED*/
	}
	(void) memcpy(node->name_, name, slen + 1 /*nul*/);
	LIST_INSERT_HEAD(&strings, node, node_);
	return node->name_;
}

static bool
parse_protocol_sizes(struct servconfig *sep)
{ /*source: netbsd/openbsd inetd */
	char *buf0, *buf1, *sz0, *sz1;
	int val;

#define MALFORMED(arg) \
do { \
	syslog(LOG_ERR, "%s: malformed buffer size option `%s'", \
	    sep->se_service, (arg)); \
	return false; \
} while (0)

#define GETVAL(arg) \
do { \
	char *cp0 = NULL; \
	if (!isdigit((unsigned char)*(arg))) \
		MALFORMED(arg); \
	val = strtol((arg), &cp0, 10); \
	if (cp0 != NULL) { \
		if (cp0[1] != '\0') \
			MALFORMED((arg)); \
		if (cp0[0] == 'k') \
			val *= 1024; \
		if (cp0[0] == 'm') \
			val *= 1024 * 1024; \
	} \
	if (val < 1) { \
		syslog(LOG_ERR, "%s: invalid buffer size `%s'", \
		    sep->se_service, (arg)); \
		return false; \
	} \
} while (0)

#define ASSIGN(arg) \
do { \
	if (strcmp((arg), "sndbuf") == 0) \
		sep->se_sndbuf = val; \
	else if (strcmp((arg), "rcvbuf") == 0) \
		sep->se_rcvbuf = val; \
	else \
		MALFORMED((arg)); \
} while (0)

	buf0 = buf1 = sz0 = sz1 = NULL;
	sep->se_sndbuf = sep->se_rcvbuf = 0;
	if ((buf0 = (char *)strchr(sep->se_proto, ',')) != NULL) {
		/* Not meaningful for Tcpmux services. */
		if (ISMUX(sep)) {
			syslog(LOG_ERR, "%s: can't specify buffer sizes for tcpmux services", sep->se_service);
			return false;
		}

		/* Skip the , */
		*buf0++ = '\0';

		/* Check to see if another socket buffer size was specified. */
		if ((buf1 = strchr(buf0, ',')) != NULL) {
			/* Skip the , */
			*buf1++ = '\0';

			/* Make sure a 3rd one wasn't specified. */
			if (strchr(buf1, ',') != NULL) {
				syslog(LOG_ERR, "%s: too many buffer sizes", sep->se_service);
				return false;
			}

			/* Locate the size. */
			if ((sz1 = strchr(buf1, '=')) == NULL)
				MALFORMED(buf1);

			/* Skip the = */
			*sz1++ = '\0';
		}

		/* Locate the size. */
		if ((sz0 = strchr(buf0, '=')) == NULL)
			MALFORMED(buf0);

		/* Skip the = */
		*sz0++ = '\0';

		GETVAL(sz0);
		ASSIGN(buf0);

		if (buf1 != NULL) {
			GETVAL(sz1);
			ASSIGN(buf1);
		}
	}

#undef ASSIGN
#undef GETVAL
#undef MALFORMED

	return true;
}

void
freeconfig(struct servconfig *cp)
{
        assert(NULL == cp->se_service || (cp->se_service == newname(cp->se_service)));
	free((char *)cp->se_proto);
	free((char *)cp->se_user);
	free((char *)cp->se_group);
#ifdef LOGIN_CAP
	free((char *)cp->se_class);
#endif
	free((char *)cp->se_server);
#ifdef IPSEC
	free((char *)cp->se_policy);
#endif
}

/*end*/

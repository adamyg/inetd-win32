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
 */

#if !defined(NOMINMAX)
#define  NOMINMAX
#endif

#include <sys/cdefs.h>
#include <sys/tree.h>

#include <exception>

#include <sys/param.h>
#include <sys/time.h>
#include <sys/stat.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <errno.h>
#include <err.h>
#include <limits.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../service/syslog.h"
#include <unistd.h>

#include "SimpleLock.h"
#include "ObjectPool.h"
#include "inetd.h"

#define CHTGRAN		10
#define CHTSIZE		6

typedef struct CTime {
	unsigned long 	ct_Ticks;
	int		ct_Count;
} CTime;

typedef struct CHash {
	RB_ENTRY(CHash) ch_rbnode;
	TAILQ_ENTRY(CHash) ch_listnode;
	union {
		struct in_addr c4_Addr;
		struct in6_addr c6_Addr;
	} cu_Addr;
#define	ch_Addr4	cu_Addr.c4_Addr
#define	ch_Addr6	cu_Addr.c6_Addr
	int		ch_Family;
	time_t		ch_LTime;
	const char	*ch_Service;
	CTime		ch_Times[CHTSIZE];
} CHash;

typedef RB_HEAD(CHashTree, CHash) CHashTree_t;
typedef TAILQ_HEAD(CHashList, CHash) CHashList_t;

static int cmp(const CHash *a, const CHash *b) {
	const int cmp = strcmp(a->ch_Service, b->ch_Service);
	if (0 == cmp) {
		if (a->ch_Family == b->ch_Family) {
			if (AF_INET == a->ch_Family) {
				return memcmp(&a->ch_Addr4, &b->ch_Addr4, sizeof(a->ch_Addr4));
			} else {
				return memcmp(&a->ch_Addr6, &b->ch_Addr6, sizeof(a->ch_Addr6));
			}
		}
		return (a->ch_Family < b->ch_Family ? -1 : 1);
	}
	return cmp;
}

RB_PROTOTYPE_STATIC(CHashTree, CHash, ch_rbnode, cmp);
RB_GENERATE_STATIC(CHashTree, CHash, ch_rbnode, cmp);

class HostCollection {
	HostCollection(const HostCollection &) = delete;
	HostCollection& operator=(const HostCollection &) = delete;

public:
	HostCollection() {
		RB_INIT(&tree_);
		TAILQ_INIT(&list_);
	}

	bool check_limit(const struct sockaddr_storage &rss, const char *service, int maxcpm) {
		const time_t now = time(NULL);
		const unsigned int ticks = (unsigned int)(now / CHTGRAN);
		CHash *node = nullptr;
		int cnt = 0;

		if (nullptr == (node = get_node(rss, service, now)))
			return false;

		{	CTime &ct = node->ch_Times[ticks % CHTSIZE];
			if (ct.ct_Ticks != ticks) {
				ct.ct_Ticks = ticks;
				ct.ct_Count = 0;
			}
			++ct.ct_Count;
		}

		for (unsigned i = 0; i < CHTSIZE; ++i) {
			const CTime *ct = &node->ch_Times[i];
			if (ct->ct_Ticks <= ticks && ct->ct_Ticks >= ticks - CHTSIZE) {
				cnt += ct->ct_Count;
			}
		}

		return (((cnt * 60) / (CHTSIZE * CHTGRAN)) > maxcpm);
	}

private:
	CHash *get_node(const struct sockaddr_storage &rss, const char *service, time_t now) {
		CHash t_node, *node;

		// temporary node

		(void) memset(&t_node, 0, sizeof(t_node));
		t_node.ch_Service = (char *)service;
		t_node.ch_Family = rss.ss_family;
		if (AF_INET == rss.ss_family) {
			t_node.ch_Addr4 = ((struct sockaddr_in *)&rss)->sin_addr;
		} else {
			t_node.ch_Addr6 = ((struct sockaddr_in6 *)&rss)->sin6_addr;
		}

		// lookup existing

		if (nullptr != (node = RB_FIND(CHashTree, &tree_, &t_node))) {
			TAILQ_REMOVE(&list_, node, ch_listnode);

		// expire an existing, re-cycle node

		} else if (nullptr != (node = TAILQ_FIRST(&list_)) &&
				node->ch_LTime < (now - 60)) {
			TAILQ_REMOVE(&list_, node, ch_listnode);
			RB_REMOVE(CHashTree, &tree_, node);

			*node = t_node;
			RB_INSERT(CHashTree, &tree_, node);

		// build new

		} else if (nullptr != (node = pool_.construct_nothrow(t_node))) {
			RB_INSERT(CHashTree, &tree_, node);

		} else {
			return nullptr;
		}

		// update

		TAILQ_INSERT_TAIL(&list_, node, ch_listnode);
		node->ch_LTime = now;
		return node;
	}

private:
	inetd::ObjectPool<CHash> pool_;
	CHashTree_t tree_;
	CHashList_t list_;
};

static HostCollection hosts;

int
cpmip(const struct servtab *sep, int ctrl)
{
	struct sockaddr_storage rss;
	socklen_t rssLen = sizeof(rss);
	const int maxcpm = sep->se_maxcpm;
	int r = 0;

	/*
	 * If getpeername() fails, just let it through (if logging is
	 * enabled the condition is caught elsewhere)
	 */
	if (maxcpm > 0 &&
		    (sep->se_family == AF_INET || sep->se_family == AF_INET6) &&
		    getpeername(ctrl, (struct sockaddr *)&rss, &rssLen) == 0 ) {

		if (hosts.check_limit(rss, sep->se_service, maxcpm)) {
			char pname[NI_MAXHOST];

			getnameinfo((struct sockaddr *)&rss,
				    SOCKLEN_SOCKADDR_PTR((struct sockaddr *)&rss),
				    pname, sizeof(pname), NULL, 0,
				    NI_NUMERICHOST);
			syslog(LOG_ERR,
			    "%s from %s exceeded counts/min (limit %d/min)",
			    sep->se_service, pname, maxcpm);
			r = -1;
		}
	}
	return(r);
}

//end

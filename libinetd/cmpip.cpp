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

#if !defined(NOMINMAX)
#define  NOMINMAX
#endif

#include <sys/cdefs.h>
#include <sys/tree.h>

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

#include <syslog.h>
#include <unistd.h>

#include "SimpleLock.h"
#include "IntrusiveTree.h"
#include "IntrusiveList.h"
#include "ObjectPool.h"
#include "inetd.h"

/////////////////////////////////////////////////////////////////////////////////////////
//      Host collection

namespace {

#define CHTGRAN		10
#define CHTSIZE		6

typedef struct CTime {
	unsigned long ct_ticks;
	unsigned ct_count;
} CTime;

typedef struct CHost {
	struct Compare {
		int operator()(const CHost *a, const CHost *b) const
		{
			const int cmp = strcmp(a->ch_service, b->ch_service);
			if (0 == cmp) {
				if (a->ch_family == b->ch_family) {
					if (AF_INET6 == a->ch_family) {
						return memcmp(&a->ch_addrs.addr6, &b->ch_addrs.addr6, sizeof(a->ch_addrs.addr6));
					} else {
						return memcmp(&a->ch_addrs.addr4, &b->ch_addrs.addr4, sizeof(a->ch_addrs.addr4));
					}
				}
				return (a->ch_family < b->ch_family ? -1 : 1);
			}
			return cmp;
		}
	};

	CHost(const struct sockaddr_storage &rss, const char *service) :
			ch_family(rss.ss_family), ch_addrs(), ch_service(service), ch_times(), ch_ltime(0), ch_dtime(0)
	{
		if (AF_INET6 == ch_family) {
			ch_addrs.addr6 = ((const struct sockaddr_in6 *)&rss)->sin6_addr;
		} else {
			ch_addrs.addr4 = ((const struct sockaddr_in *)&rss)->sin_addr;
		}
	}

	CHost(const CHost &chash)
	{
		reassign(chash);
	}

	CHost operator=(const CHost &chash) = delete;

	void reassign(const CHost &chash)
	{
		ch_service = chash.ch_service;
		ch_family = chash.ch_family;
		ch_addrs = chash.ch_addrs;
		(void) memset(ch_times, 0, sizeof(ch_times));
	}

	inetd::Intrusive::TreeMemberHook<CHost> ch_rbnode;
	inetd::Intrusive::TailMemberHook<CHost> ch_listnode;

	int ch_family;				// AF_INET or AF_INET6
	union {
		struct in_addr addr4;
		struct in6_addr addr6;
	} ch_addrs;
	const char *ch_service; 		// service name.
	CTime ch_times[CHTSIZE];		// usage.
	time_t ch_ltime;			// late update time.
	time_t ch_dtime;			// delay timestamp.
} CHost;

typedef inetd::intrusive_tree<CHost, CHost::Compare, inetd::Intrusive::TreeMemberHook<CHost>, &CHost::ch_rbnode> CHostTree_t;
typedef inetd::intrusive_list<CHost, inetd::Intrusive::TailMemberHook<CHost>, &CHost::ch_listnode> CHostList_t;

class HostCollection {
	HostCollection(const HostCollection &) = delete;
	HostCollection& operator=(const HostCollection &) = delete;

public:
	HostCollection()
	{
	}

	int check_limit(const struct sockaddr_storage &rss, const char *service, int maxcpm, int cpmwait)
	{
		const time_t now = time(nullptr);
		const unsigned int ticks = (unsigned int)(now / CHTGRAN);
		inetd::CriticalSection::Guard guard(cs_);
		CHost *node = nullptr;
		int cnt = 0;

		if (nullptr == (node = get_node(rss, service, now)))
			return 0;		// node assignment error.

		if (node->ch_dtime) {
			if (now < node->ch_dtime && cpmwait > 0) {
				return 2;	// temporarily disable active.
			}
			node->ch_dtime = 0;
		}

		{	CTime &ct = node->ch_times[ticks % CHTSIZE];
			if (ct.ct_ticks != ticks) {
				ct.ct_ticks = ticks;
				ct.ct_count = 0;
			}
			++ct.ct_count;
		}

		for (unsigned i = 0; i < CHTSIZE; ++i) {
			const CTime *ct = &node->ch_times[i];
			if (ct->ct_ticks <= ticks && ct->ct_ticks >= ticks - CHTSIZE) {
				cnt += ct->ct_count;
			}
		}

		if (((cnt * 60) / (CHTSIZE * CHTGRAN)) > maxcpm) {
			if (cpmwait > 0) {	// temporarily disabled for cpmwait seconds
				node->ch_dtime = now + cpmwait;
			}
			return 1;
		}

		return 0;
	}

private:
	CHost *get_node(const struct sockaddr_storage &rss, const char *service, time_t now)
	{
		CHost t_node(rss, service), *node = nullptr;

		// lookup existing

		if (nullptr != (node = tree_.find(t_node))) {
			list_.remove(node);

		// expire an existing, re-cycle node

		} else if (nullptr != (node = list_.front()) &&
				(now + 60) > node->ch_ltime && now >= node->ch_dtime) {
			list_.remove(node);
			tree_.remove(node);

			node->reassign(t_node);
			tree_.insert(*node);

		// build new

		} else if (nullptr != (node = pool_.construct_nothrow(t_node))) {
			tree_.insert(*node);

		} else {
			return nullptr;
		}

		// update

		list_.push_back(*node);
		node->ch_ltime = now;
		return node;
	}

private:
	inetd::CriticalSection cs_;
	inetd::ObjectPool<CHost> pool_;
	CHostTree_t tree_;
	CHostList_t list_;
};

};  //namespace anon


/////////////////////////////////////////////////////////////////////////////////////////
//      Public interface

static HostCollection hosts;

int
cpmip(PeerInfo &remote)
{
	const struct servtab *sep = remote.getserv();
	const int maxcpm = sep->se_cpmmax, cpmwait = sep->se_cpmwait;
	const struct sockaddr_storage *rss;
	int r = 0;

	/*
	 *  If getpeername() fails, just let it through (if logging is enabled the condition is caught elsewhere)
	 */
	if (maxcpm <= 0)
		return r;

	if ((sep->se_family == AF_INET || sep->se_family == AF_INET6) &&
			nullptr != (rss = remote.getaddr()))
	{
		const int clret = hosts.check_limit(*rss, sep->se_service, maxcpm, cpmwait);
		if (clret) {
			syslog(LOG_ERR,
			    "%s from %s exceeded counts/min (limit %d/min)%s",
			    sep->se_service, remote.getname(), maxcpm, (2 == clret ? " -- wait delay" : ""));
			r = -1;
		}
	}
	return r;
}

//end

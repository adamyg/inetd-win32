/*
 * Copyright (C) Internet Systems Consortium, Inc. ("ISC")
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, you can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * See the COPYRIGHT file distributed with this work for additional
 * information regarding copyright ownership.
 */

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "netaddr.h"

#define true	1
#define false	0

static int	netaddr_pton4m(const char *src, const char *end, struct netaddr *res);

int
getnetaddr(const char *addr, struct netaddr *res, int numeric)
{
	const char *p = addr;
	char t_addr[128], *t = t_addr;
	struct addrinfo hints, *ai;
	int ret_ga;

	if (NULL == addr || NULL == res)
		return false;

	memset(res, 0, sizeof(*res));
	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_DGRAM;
	if (numeric)
		hints.ai_flags = AI_NUMERICHOST;

	for (char *tend = t + (sizeof(t_addr)-1); t < tend;) {
		const char ch = *p++;
		if (0 == ch)  { 		/* EOS */
			p = NULL;
			break;
		} else if (ch == '/') { 	/* prefix only with numeric addresses */
			hints.ai_flags |= AI_NUMERICHOST;
			break;
		}
		*t++ = ch;
	}
	*t = 0;

	// address
	if (0 == (ret_ga = getaddrinfo(t_addr, NULL, &hints, &ai))) {

		switch (res->family = ai->ai_family) {
		case AF_INET:
			res->netaddr_v4addr = ((struct sockaddr_in *) ai->ai_addr)->sin_addr;
			res->length = sizeof(res->netaddr_v4addr);
			break;
		case AF_INET6:
			res->netaddr_v6addr = ((struct sockaddr_in6 *) ai->ai_addr)->sin6_addr;
			res->length = sizeof(res->netaddr_v6addr);
			break;
		default:
			freeaddrinfo(ai);
			return false;
		}
		freeaddrinfo(ai);
		ai = NULL;

	} else {

		if (NULL == p) {		/* x.x.*.* */
			if (netaddr_pton4m(addr, addr + strlen(addr), res)) {
				res->family = AF_INET;
				res->length = sizeof(res->netaddr_v4addr);
				return true;
			}
		}

		char *ep = NULL;	    	/* <numeric-value> */
		const uint64_t val = strtoull(addr, &ep, 10);
		if ((p && ep == p) || (!p && ep && !*ep)) {
			if (val >= 0xffffffffULL) {
				res->family = AF_INET6;
				res->length = sizeof(res->netaddr_v6addr);
				res->netaddr_viaddr = ((uint64_t)htonl((uint32_t)val)) << 32 |
					htonl((uint32_t)(val >> 32));
			} else {
				res->family = AF_INET;
				res->length = sizeof(res->netaddr_v4addr);
				res->netaddr_v4addr.s_addr = htonl((uint32_t)val);
			}

		} else {
//			fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret_ga));
			return false;
		}
	}

	// netmask
	if (NULL == p) {
		memset(&res->mask, 0xff, res->length);

	} else {
		char *ep = NULL;
		const int64_t prefix =
			strtoll(p, &ep, res->family == AF_INET6 ? 128 : 32);
		if (ep == p || *ep) {
			if (AF_INET == res->family &&
					1 == inet_pton(AF_INET, p, &res->netaddr_v4mask)) {
				return true;
			}
//			fprintf(stderr, "invalid prefix: %s\n", p);
			return false;
		}

		switch (res->family) {
		case AF_INET:
			memset(&res->netaddr_v4mask, 0, sizeof(res->netaddr_v4mask));
			res->netaddr_v4mask.s_addr = htonl((uint32_t) (0xffffffffffULL << (32 - prefix)));
			break;
		case AF_INET6: {
				const int64_t q = prefix >> 3;
				const int64_t r = prefix & 7;

				memset(&res->netaddr_v6mask, 0, sizeof(res->netaddr_v6mask));
				if (q > 0) {
					memset((void *)&res->netaddr_v6mask, 0xff, (size_t)q);
				}
				if (r > 0) {
					*((u_char *)&res->netaddr_v6mask + q) = (0xff00 >> r) & 0xff;
				}
			}
			break;
		default:
			return true;
		}
	}
	return true;
}


static int
netaddr_pton4m(const char *src, const char *end, struct netaddr *res /*uint8_t *dst, uint8_t *mask*/)
{
	int saw_digit = 0, octets = 0, ch;
	unsigned char t_addr[4], t_mask[4], *a, *m;

	*(a = t_addr) = 0;
	*(m = t_mask) = 255;
	while (src < end) {
		ch = *src++;
		if ('*' == ch && !saw_digit) {
			if (++octets > 4)
				return false;	/* >4 elements. */
			*a = 255;
			*m = 0;
			while (src < end) {	/* trailing .* */
				if (++octets > 4 || '.' != *src++)
					return false;
				if (src >= end	 || '*' != *src++)
					return false;
				*++a = 255;
				*++m = 0;
			}

		} else if (ch >= '0' && ch <= '9') {
			unsigned int val = *a * 10 + (ch - '0');
			if (saw_digit && *a == 0)
				return false;	/* leading zero. */
			if (val > 255)
				return false;	/* overflow. */
			*a = val;
			if (! saw_digit && ++octets > 4)
				return false;	/* >4 elements. */
			saw_digit = 1;

		} else if (ch == '.' && saw_digit) {
			if (octets == 4)
				return false;	/* >4 elements. */
			*++a = 0;
			*++m = 255;
			saw_digit = 0;

		} else {
			return false;		/* unknown. */
		}
	}

	if (octets < 4)
		return false;

	memcpy(&res->netaddr_v4addr, t_addr, sizeof(t_addr));
	memcpy(&res->netaddr_v4mask, t_mask, sizeof(t_mask));
	return true;
}


int
getmasklength(const struct netaddr *res) {
	unsigned int nbits = 0, nbytes = 0, ipbytes = 0, i;
	const unsigned char *p;

	switch (res->family) {
	case AF_INET:
		p = (const unsigned char *)&res->netaddr_v4mask;
		ipbytes = 4;
		break;
	case AF_INET6:
		p = (const unsigned char *)&res->netaddr_v6mask;
		ipbytes = 16;
		break;
	default:
		return -1;
	}
	for (i = 0; i < ipbytes; i++) {
		if (p[i] != 0xFF) {
			break;
		}
	}
	nbytes = i;
	if (i < ipbytes) {
		unsigned int c = p[nbytes];
		while ((c & 0x80) != 0 && nbits < 8) {
			c <<= 1;
			nbits++;
		}
		if ((c & 0xFF) != 0) {
			return -1;
		}
		i++;
	}
	for (; i < ipbytes; i++) {
		if (p[i] != 0) {
			return -1;
		}
	}
	return nbytes * 8 + nbits;
}

//end

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
#include <assert.h>

#include "isc_netaddr.h"
#include "isc_radix.h"

#define true	1
#define false	0

#ifndef UNUSED
#define UNUSED(__x) (void)(__x);
#endif

#ifndef ISC__IPADDR			/*% IP address */
#define ISC__IPADDR(x) ((uint32_t)htonl((uint32_t)(x)))
#endif

#ifndef ISC_IPADDR_ISMULTICAST		/*% Is IP address multicast? */
#define ISC_IPADDR_ISMULTICAST(i) \
	(((uint32_t)(i)&ISC__IPADDR(0xf0000000)) == ISC__IPADDR(0xe0000000))
#endif

#ifndef ISC_IPADDR_ISEXPERIMENTAL	/*% Is IP address multicast? */
#define ISC_IPADDR_ISEXPERIMENTAL(i) \
	(((uint32_t)(i)&ISC__IPADDR(0xf0000000)) == ISC__IPADDR(0xf0000000))
#endif

#ifndef IN6_IS_ADDR_MULTICAST		/*% Is IPv6 address multicast? */
#define IN6_IS_ADDR_MULTICAST(a) ((a)->s6_addr[0] == 0xff)
#endif

#ifndef IN6_IS_ADDR_LINKLOCAL		/*% Is IPv6 address linklocal? */
#define IN6_IS_ADDR_LINKLOCAL(a) \
	(((a)->s6_addr[0] == 0xfe) && (((a)->s6_addr[1] & 0xc0) == 0x80))
#endif

#ifndef IN6_IS_ADDR_SITELOCAL		/*% is IPv6 address sitelocal? */
#define IN6_IS_ADDR_SITELOCAL(a) \
	(((a)->s6_addr[0] == 0xfe) && (((a)->s6_addr[1] & 0xc0) == 0xc0))
#endif

#ifndef IN6_IS_ADDR_LOOPBACK		/*% is IPv6 address loopback? */
#define IN6_IS_ADDR_LOOPBACK(x) \
	(memcmp((x)->s6_addr, in6addr_loopback.s6_addr, 16) == 0)
#endif

#ifndef IN6_IS_ADDR_V4MAPPED		/*% Is IPv6 address V4 mapped? */
#define IN6_IS_ADDR_V4MAPPED(x) \
	(memcmp((x)->s6_addr, in6addr_any.s6_addr, 10) == 0 && (x)->s6_addr[10] == 0xff && (x)->s6_addr[11] == 0xff)
#endif

#define ISC_R_FAILURE -1
#define ISC_R_NOSPACE -1
#define ISC_R_MASKNONCONTIG -1
#define ISC_R_RANGE -1

int
isc_netaddr_equal(const isc_netaddr_t *a, const isc_netaddr_t *b)
{
	assert(a != NULL && b != NULL);

	if (a->family != b->family) {
		return (false);
	}

	switch (a->family) {
	case AF_INET:
		if (a->netaddr_v4addr.s_addr != b->netaddr_v4addr.s_addr) {
			return (false);
		}
		break;
	case AF_INET6:
		if (memcmp(&a->netaddr_v6addr, &b->netaddr_v6addr, sizeof(a->netaddr_v6addr)) != 0 ||
		    		a->zone != b->zone)
		{
			return (false);
		}
		break;
#if defined(HAVE_AF_UNIX) || !defined(_WIN32)
	case AF_UNIX:
		if (strcmp(a->type.un, b->type.un) != 0) {
			return (false);
		}
		break;
#endif /* ifndef _WIN32 */
	case AF_UNSPEC:
		break;
	default:
		return (false);
	}
	return (true);
}

int
isc_netaddr_eqprefix(const isc_netaddr_t *a, const isc_netaddr_t *b, unsigned int prefixlen)
{
	const unsigned char *pa = NULL, *pb = NULL;
	unsigned int ipabytes = 0;		/* Length of whole IP address in bytes */
	unsigned int nbytes;			/* Number of significant whole bytes */
	unsigned int nbits;			/* Number of significant leftover bits */

	assert(a != NULL && b != NULL);

	if (a->family != b->family) {
		return (false);
	}

	switch (a->family) {
	case AF_INET:
		pa = (const unsigned char *)&a->netaddr_v4addr;
		pb = (const unsigned char *)&b->netaddr_v4addr;
		ipabytes = 4;
		break;
	case AF_INET6:
		pa = (const unsigned char *)&a->netaddr_v6addr;
		pb = (const unsigned char *)&b->netaddr_v6addr;
		ipabytes = 16;
		break;
	default:
		return (false);
	}

	/*
	 * Don't crash if we get a pattern like 10.0.0.1/9999999.
	 */
	if (prefixlen > ipabytes * 8) {
		prefixlen = ipabytes * 8;
	}

	nbytes = prefixlen / 8;
	nbits = prefixlen % 8;

	if (nbytes > 0) {
		if (memcmp(pa, pb, nbytes) != 0) {
			return (false);
		}
	}
	if (nbits > 0) {
		unsigned int bytea, byteb, mask;
		assert(nbytes < ipabytes);
		assert(nbits < 8);
		bytea = pa[nbytes];
		byteb = pb[nbytes];
		mask = (0xFF << (8 - nbits)) & 0xFF;
		if ((bytea & mask) != (byteb & mask)) {
			return (false);
		}
	}
	return (true);
}

int
isc_netaddr_totext(const isc_netaddr_t *netaddr, char *buffer, size_t buflen /*isc_buffer_t *target*/)
{
	char abuf[sizeof("xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:255.255.255.255")];
	char zbuf[sizeof("%4294967295")];
	unsigned int alen;
	int zlen;
	const char *r;
	const void *type;

	assert(netaddr != NULL);

	switch (netaddr->family) {
	case AF_INET:
		type = &netaddr->netaddr_v4addr;
		break;
	case AF_INET6:
		type = &netaddr->netaddr_v6addr;
		break;
#if defined(HAVE_AF_UNIX) || !defined(_WIN32)
	case AF_UNIX:
		alen = strlen(netaddr->type.un);
		if ((alen + 1) > buflen) {
			return -1;
		}

		memcpy(buffer, (const unsigned char *)(netaddr->type.un), alen);
		buffer[alen] = 0;
		return alen;
#endif
	default:
		return -1;
	}
	r = inet_ntop(netaddr->family, (void *)type, abuf, sizeof(abuf));
	if (r == NULL) {
		return -1;
	}

	alen = strlen(abuf);
	assert(alen < sizeof(abuf));

	zlen = 0;
	if (netaddr->family == AF_INET6 && netaddr->zone != 0) {
		zlen = snprintf(zbuf, sizeof(zbuf), "%%%u", netaddr->zone);
		if (zlen < 0) {
			return -1;
		}
		assert((unsigned int)zlen < sizeof(zbuf));
	}

	if ((alen + zlen + 1) > buflen) {
		return -1;
	}

	memcpy(buffer, (unsigned char *)abuf, alen);
	memcpy(buffer + alen, (unsigned char *)zbuf, (unsigned int)zlen);
	buffer[alen + zlen] = 0;

        return (alen + zlen);
}

#if (XXX)
void
isc_netaddr_format(const isc_netaddr_t *na, char *array, unsigned int size)
{
	int result;
	isc_buffer_t buf;

	isc_buffer_init(&buf, array, size);
	result = isc_netaddr_totext(na, &buf);

	if (size == 0) {
		return;
	}

	/*
	 * Null terminate.
	 */
	if (result == ISC_R_SUCCESS) {
		if (isc_buffer_availablelength(&buf) >= 1) {
			isc_buffer_putuint8(&buf, 0);
		} else {
			result = ISC_R_NOSPACE;
		}
	}

	if (result != ISC_R_SUCCESS) {
		snprintf(array, size, "<unknown address, family %u>", na->family);
		array[size - 1] = '\0';
	}
}
#endif  //XXX

int
isc_netaddr_prefixok(const isc_netaddr_t *na, unsigned int prefixlen)
{
	static const unsigned char zeros[16];
	unsigned int nbits, nbytes, ipbytes = 0;
	const unsigned char *p;

	switch (na->family) {
	case AF_INET:
		p = (const unsigned char *)&na->netaddr_v4addr;
		ipbytes = 4;
		if (prefixlen > 32) {
			return (ISC_R_RANGE);
		}
		break;
	case AF_INET6:
		p = (const unsigned char *)&na->netaddr_v6addr;
		ipbytes = 16;
		if (prefixlen > 128) {
			return (ISC_R_RANGE);
		}
		break;
	default:
		return (ISC_R_NOTIMPLEMENTED);
	}
	nbytes = prefixlen / 8;
	nbits = prefixlen % 8;
	if (nbits != 0) {
		assert(nbytes < ipbytes);
		if ((p[nbytes] & (0xff >> nbits)) != 0U) {
			return (ISC_R_FAILURE);
		}
		nbytes++;
	}
	if (nbytes < ipbytes && memcmp(p + nbytes, zeros, ipbytes - nbytes) != 0) {
		return (ISC_R_FAILURE);
	}
	return (ISC_R_SUCCESS);
}

int
isc_netaddr_masktoprefixlen(const isc_netaddr_t *s, unsigned int *lenp)
{
	unsigned int nbits = 0, nbytes = 0, ipbytes = 0, i;
	const unsigned char *p;

	switch (s->family) {
	case AF_INET:
		p = (const unsigned char *)&s->netaddr_v4addr;
		ipbytes = 4;
		break;
	case AF_INET6:
		p = (const unsigned char *)&s->netaddr_v6addr;
		ipbytes = 16;
		break;
	default:
		return (ISC_R_NOTIMPLEMENTED);
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
			return (ISC_R_MASKNONCONTIG);
		}
		i++;
	}
	for (; i < ipbytes; i++) {
		if (p[i] != 0) {
			return (ISC_R_MASKNONCONTIG);
		}
	}
	*lenp = nbytes * 8 + nbits;
	return (ISC_R_SUCCESS);
}

void
isc_netaddr_fromin(isc_netaddr_t *netaddr, const struct in_addr *ina)
{
	memset(netaddr, 0, sizeof(*netaddr));
	netaddr->family = AF_INET;
	netaddr->netaddr_v4addr = *ina;
}

void
isc_netaddr_fromin6(isc_netaddr_t *netaddr, const struct in6_addr *ina6)
{
	memset(netaddr, 0, sizeof(*netaddr));
	netaddr->family = AF_INET6;
	netaddr->netaddr_v6addr = *ina6;
}

#if (TODO)
int
isc_netaddr_frompath(isc_netaddr_t *netaddr, const char *path)
{
#if defined(HAVE_AF_UNIX) || !defined(_WIN32)
	if (strlen(path) > sizeof(netaddr->type.un) - 1) {
		return (ISC_R_NOSPACE);
	}

	memset(netaddr, 0, sizeof(*netaddr));
	netaddr->family = AF_UNIX;
	strlcpy(netaddr->type.un, path, sizeof(netaddr->type.un));
	netaddr->zone = 0;
	return (ISC_R_SUCCESS);
#else
	UNUSED(netaddr);
	UNUSED(path);
	return (ISC_R_NOTIMPLEMENTED);
#endif
}
#endif  /*TODO*/

void
isc_netaddr_setzone(isc_netaddr_t *netaddr, uint32_t zone)
{
	/* we currently only support AF_INET6. */
	assert(netaddr->family == AF_INET6);

	netaddr->zone = zone;
}

uint32_t
isc_netaddr_getzone(const isc_netaddr_t *netaddr)
{
	return (netaddr->zone);
}

#if (TODO)
void
isc_netaddr_fromsockaddr(isc_netaddr_t *t, const isc_sockaddr_t *s)
{
	int family = s->type.sa.sa_family;
	t->family = family;
	switch (family) {
	case AF_INET:
		t->netaddr_v4addr = s->type.sin.sin_addr;
		t->zone = 0;
		break;
	case AF_INET6:
		memmove(&t->netaddr_v6addr, &s->type.sin6.sin6_addr, 16);
		t->zone = s->type.sin6.sin6_scope_id;
		break;
#if defined(HAVE_AF_UNIX) || !defined(_WIN32)
	case AF_UNIX:
		memmove(t->type.un, s->type.sunix.sun_path, sizeof(t->type.un));
		t->zone = 0;
		break;
#endif
	default:
		assert(0);
		ISC_UNREACHABLE();
	}
}
#endif  //TODO

void
isc_netaddr_any(isc_netaddr_t *netaddr)
{
	memset(netaddr, 0, sizeof(*netaddr));
	netaddr->family = AF_INET;
	netaddr->netaddr_v4addr.s_addr = INADDR_ANY;
}

void
isc_netaddr_any6(isc_netaddr_t *netaddr)
{
#if defined(__WATCOMC__)
	static struct in6_addr in6addr_any = IN6ADDR_ANY_INIT;
#endif

	memset(netaddr, 0, sizeof(*netaddr));
	netaddr->family = AF_INET6;
	netaddr->netaddr_v6addr = in6addr_any;
}

void
isc_netaddr_unspec(isc_netaddr_t *netaddr)
{
	memset(netaddr, 0, sizeof(*netaddr));
	netaddr->family = AF_UNSPEC;
}

int
isc_netaddr_ismulticast(const isc_netaddr_t *na)
{
	switch (na->family) {
	case AF_INET:
		return (ISC_IPADDR_ISMULTICAST(na->netaddr_v4addr.s_addr));
	case AF_INET6:
		return (IN6_IS_ADDR_MULTICAST(&na->netaddr_v6addr));
	default:
		return (false); /* XXXMLG ? */
	}
}

int
isc_netaddr_isexperimental(const isc_netaddr_t *na)
{
	switch (na->family) {
	case AF_INET:
		return (ISC_IPADDR_ISEXPERIMENTAL(na->netaddr_v4addr.s_addr));
	default:
		return (false); /* XXXMLG ? */
	}
}

int
isc_netaddr_islinklocal(const isc_netaddr_t *na)
{
	switch (na->family) {
	case AF_INET:
		return (false);
	case AF_INET6:
		return (IN6_IS_ADDR_LINKLOCAL(&na->netaddr_v6addr));
	default:
		return (false);
	}
}

int
isc_netaddr_issitelocal(const isc_netaddr_t *na) {
	switch (na->family) {
	case AF_INET:
		return (false);
	case AF_INET6:
		return (IN6_IS_ADDR_SITELOCAL(&na->netaddr_v6addr));
	default:
		return (false);
	}
}

#define ISC_IPADDR_ISNETZERO(i) \
	(((uint32_t)(i)&ISC__IPADDR(0xff000000)) == ISC__IPADDR(0x00000000))

int
isc_netaddr_isnetzero(const isc_netaddr_t *na)
{
	switch (na->family) {
	case AF_INET:
		return (ISC_IPADDR_ISNETZERO(na->netaddr_v4addr.s_addr));
	case AF_INET6:
		return (false);
	default:
		return (false);
	}
}

void
isc_netaddr_fromv4mapped(isc_netaddr_t *t, const isc_netaddr_t *s)
{
	isc_netaddr_t *src;

//	DE_CONST(s, src); /* Must come before IN6_IS_ADDR_V4MAPPED. */
	src = (isc_netaddr_t *)s; /* Must come before IN6_IS_ADDR_V4MAPPED. */

	assert(s->family == AF_INET6);
	assert(IN6_IS_ADDR_V4MAPPED(&src->netaddr_v6addr));

	memset(t, 0, sizeof(*t));
	t->family = AF_INET;
	memmove(&t->netaddr_v4addr, (char *)&src->netaddr_v6addr + 12, 4);
	return;
}

int
isc_netaddr_isloopback(const isc_netaddr_t *na)
{
	switch (na->family) {
	case AF_INET:
		return (((ntohl(na->netaddr_v4addr.s_addr) & 0xff000000U) == 0x7f000000U));
	case AF_INET6:
		return (IN6_IS_ADDR_LOOPBACK(&na->netaddr_v6addr));
	default:
		return (false);
	}
}

//end


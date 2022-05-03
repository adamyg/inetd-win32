/* -*- mode: c; indent-width: 8; -*- */
/*
 * windows inetd service - ACL.
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

#include "accessip.h"

#define NETADDR_TO_PREFIX_X(na, pt, bits) \
	do {						\
		memset(&(pt), 0, sizeof(pt));		\
		if (na != NULL) {			\
			(pt).family = (na)->family;	\
			(pt).bitlen = (bits);		\
			if ((pt).family == AF_INET6) {	\
				memmove(&(pt).add.sin6, &(na)->network.v6, ((bits) + 7) / 8); \
			} else {			\
				memmove(&(pt).add.sin, &(na)->network.v4, ((bits) + 7) / 8); \
			}				\
		} else {				\
			(pt).family = AF_UNSPEC;	\
			(pt).bitlen = 0;		\
		}					\
	} while (0)

#define SOCKSTORAGE_TO_PREFIX_X(ss, pt, bits) \
	do {						\
		memset(&(pt), 0, sizeof(pt));		\
		if (ss != NULL) {			\
			(pt).family = (ss)->ss_family;	\
			(pt).bitlen = (bits);		\
			if ((pt).family == AF_INET6) {	\
				memmove(&(pt).add.sin6, &((const struct sockaddr_in6 *)ss)->sin6_addr, ((bits) + 7) / 8); \
			} else {			\
				memmove(&(pt).add.sin, &((const struct sockaddr_in *)ss)->sin_addr, ((bits) + 7) / 8); \
			}				\
		} else {				\
			(pt).family = AF_UNSPEC;	\
			(pt).bitlen = 0;		\
		}					\
	} while (0)

static bool acl_neg = false;
static bool acl_pos = true;


AccessIP::AccessIP(const netaddrs &netaddrs, int match_default)
	: at_acl(nullptr)
{
	memset(&at_mct, 0, sizeof(at_mct));
	if (! netaddrs.empty() || match_default) {
		acl_create(netaddrs, match_default);
	}
}


AccessIP::~AccessIP()
{
	acl_reset();
}


bool
AccessIP::allowed(const netaddr &addr) const
{
	if (acl_active()) {
		int match = 0;
		if (acl_match(&addr, match)) {
			return (match > 0);
		}
	}
	return true;
}


bool
AccessIP::allowed(const struct sockaddr_storage *addr) const
{
	if (acl_active()) {
		int match = 0;
		if (acl_match(addr, match)) {
			return (match > 0);
		}
	}
	return true;
}


void
AccessIP::acl_create(const netaddrs &netaddrs, int match_default)
{
	bool ret;
	isc_radix_create(&at_mct, &at_acl, RADIX_MAXBITS);
	if (match_default) {
		ret = acl_add((const netaddr *)NULL, (match_default > 0 ? true : false));
		assert(ret);
	}
	for (const auto &netaddr : netaddrs()) {
		ret = acl_add(&netaddr.addr, '+' == netaddr.op);
		assert(ret);
	}
}


bool
AccessIP::acl_active() const
{
	return (nullptr != at_acl);
}


bool
AccessIP::acl_add(const netaddr *addr, bool pos)
{
	const int bitlen = (addr ? getmasklength(addr) : 0);
	isc_prefix_t pfx = {0};

	NETADDR_TO_PREFIX_X(addr, pfx, bitlen);
	return acl_add(&pfx, pos);
}


bool
AccessIP::acl_add(isc_prefix_t *pfx, bool pos)
{
	isc_radix_node_t *node = NULL;

	assert(at_acl);
	if (nullptr == at_acl) {
		return false;
	}
	const auto result = isc_radix_insert(at_acl, &node, NULL, pfx);
	if (result != ISC_R_SUCCESS) {
		return false;
	}

	const int family = pfx->family;
	if (AF_UNSPEC == family) {
		// "any" or "none".
		assert(pfx->bitlen == 0);
		for (unsigned i = 0; i < RADIX_FAMILIES; i++) {
			if (node->data[i] == NULL) { // new node?
				node->data[i] =
				    reinterpret_cast<void *>(pos ? &acl_pos : &acl_neg);
			}
		}

	} else {
		// any other prefix.
		const int fam = ISC_RADIX_FAMILY(pfx);
		if (node->data[fam] == NULL) { // node node?
			node->data[fam] =
			    reinterpret_cast<void *>(pos ? &acl_pos : &acl_neg);
		}
	}
	return true;
}


bool
AccessIP::acl_match(const netaddr *addr, int &match) const
{
	const int bitlen = (addr->family == AF_INET6) ? 128 : 32;
	isc_prefix_t pfx = {0};
	NETADDR_TO_PREFIX_X(addr, pfx, bitlen);
	return acl_match(&pfx, match);
}


bool
AccessIP::acl_match(const struct sockaddr_storage *addr, int &match) const
{
	const int bitlen = (addr->ss_family == AF_INET6) ? 128 : 32;
	isc_prefix_t pfx = {0};

	SOCKSTORAGE_TO_PREFIX_X(addr, pfx, bitlen);
	return acl_match(&pfx, match);
}


bool
AccessIP::acl_match(const isc_prefix_t *pfx, int &match) const
{
	assert(pfx);
	assert(at_acl);

	if (nullptr == at_acl) {
		match = 0;
		return false;
	}

	isc_radix_node_t *node = NULL;

	if (isc_radix_search_best(at_acl, &node, pfx) != ISC_R_SUCCESS || NULL == node) {
		match = 0;
		return false;
        }

	const int fam = ISC_RADIX_FAMILY(pfx);
	const int match_num = node->node_num[fam];
	if (*(const bool *)node->data[fam]) {
		match = match_num;
	} else {
		match = -match_num;
	}
	assert(match);
	return true;
}


void
AccessIP::acl_reset()
{
	if (nullptr == at_acl)
		return;
	isc_radix_destroy(at_acl, nullptr);
	memset(&at_mct, 0, sizeof(at_mct));
	at_acl = nullptr;
}

//end

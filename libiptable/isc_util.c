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

#include <assert.h>

#include "isc_radix.h"
#include "isc_util.h"

int
isc_compare_eqprefix(const isc_prefix_t *a, const isc_prefix_t *b, unsigned int prefixlen)
{
	const unsigned char *pa = NULL, *pb = NULL;
	unsigned int ipabytes = 0;	/* Length of whole IP address in bytes */
	unsigned int nbytes;		/* Number of significant whole bytes */
	unsigned int nbits;		/* Number of significant leftover bits */

	assert(a != NULL && b != NULL);

	if (a->family != b->family) {
		return 0;
	}

	switch (a->family) {
	case AF_INET:
		pa = (const unsigned char *)&a->add.sin;
		pb = (const unsigned char *)&b->add.sin;
		ipabytes = 4;
		break;
	case AF_INET6:
//		if (a->zone != b->zone && b->zone != 0) {
//			return (false);
//		}
		pa = (const unsigned char *)&a->add.sin6;
		pb = (const unsigned char *)&b->add.sin6;
		ipabytes = 16;
		break;
	default:
		return 0;
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
			return 0;
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
			return 0;
		}
	}
	return 1;
}

//end


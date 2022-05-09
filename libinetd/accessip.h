#pragma once
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

#include "../libiptable/isc_radix.h"
#include "../libiptable/isc_netaddr.h"

class AccessIP {
public:
	AccessIP(const netaddrs &netaddrs, int match_default = 0 /*<0=none,>0=ALL*/);
	~AccessIP();

	bool allowed(const netaddr &addr) const;
	bool allowed(const struct sockaddr_storage *addr) const;

private:
	void acl_create(const netaddrs &netaddrs, int match_default);
	bool acl_active() const;
	bool acl_add(const netaddr *addr, bool pos);
	bool acl_add(isc_prefix_t *pfx, bool pos);
	bool acl_match(const netaddr *addr, int &match) const;
	bool acl_match(const struct sockaddr_storage *addr, int &match) const;
	bool acl_match(const isc_prefix_t *pfx, int &match) const;
	void acl_reset();

private:
	isc_mem_t at_mct;
	isc_radix_tree_t *at_acl;
};

//end 

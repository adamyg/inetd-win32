#pragma once
/* -*- mode: c; indent-width: 8; -*- */
/*
  * windows inetd service.
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
 * ==
 */

#include <vector>

#include "../libiptable/netaddr.h"

class AccessIP;

class netaddrs {
	netaddrs operator=(const netaddrs &) = delete;

public:
	struct netaddress {
		struct netaddr addr;
		char op;
	};
	typedef std::vector<struct netaddress> Collection;

	netaddrs();
	netaddrs(const netaddrs &rhs);
	netaddrs& operator=(netaddrs &&rhs);
	~netaddrs();

	const Collection& operator()() const;
	int match_default() const;
	bool match_default(int status);
	bool build();
	bool allowed(const struct netaddr &addr) const;
	bool allowed(const struct sockaddr_storage *addr) const;
	bool has_unspec(char op) const;
	bool push(const netaddr &addr, char op);
	bool erase(const netaddr &addr, char op);
	void sysdump() const;
	size_t size() const;
	bool empty() const;
	void clear(char op);
	void clear();
	void reset();

private:
	int match_default_;
	Collection addresses_;
	mutable AccessIP *table_;
};

//end
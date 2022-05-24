#pragma once
/* -*- mode: c; indent-width: 8; -*- */
/*
 * netaddrs
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

#include <string>
#include <vector>

#include "SimpleString.h"

class geoipdb;

class geoips {
	geoips operator=(const geoips &) = delete;

public:
	enum geoip_type { GEOIP_NONE, GEOIP_CONTINENT, GEOIP_COUNTRY, GEOIP_TIMEZONE, GEOIP_CITY };

	struct rule {
		std::string spec;
		geoip_type type;
		char op;
	};
	typedef std::vector<struct rule> Collection;

	geoips();
	geoips(const geoips &rhs);
	geoips& operator=(geoips &&rhs);
	~geoips();

	const Collection& operator()() const;
	bool build();
	bool allowed(const struct netaddr &addr) const;
	bool allowed(const struct sockaddr *addr) const;
	int match_default() const;
	bool match_default(int status);
	const inetd::String& database() const;
	bool database(const char *database);
	bool push(const std::vector<std::string> &rules, char op);
	bool push(const char *value, char op);
	bool erase(const std::vector<std::string> &rules, char op);
	bool erase(const char *value, char op);
	void sysdump() const;
	size_t size() const;
	bool empty() const;
	size_t clear(char op);
	void clear();
	void reset();

private:
	int match_default_;
	inetd::String database_;
	Collection rules_;
	mutable geoipdb *geoipdb_;
};

//end

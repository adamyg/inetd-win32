/* -*- mode: c; indent-width: 8; -*- */
/*
 * windows inetd service - geoip.
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

#include <map>
#include <syslog.h>

#include "geoips.h"
#include "xinetd.h"


/////////////////////////////////////////////////////////////////////////////////////////
//  Geoipdb
//
//      see: https://maxmind.github.io/libmaxminddb/
//      and: https://github.com/P3TERX/GeoLite.mmdb
//

#if defined(HAVE_LIBMAXMINDDB)
#if defined(ssize_t)
#undef ssize_t
#endif
#pragma comment(lib, "libmaxminddbd.lib")

#include <maxminddb/maxminddb.h>

class geoipdb {
public:
	struct Profile {
		std::string country;
		std::string country_name;
		std::string continent;
		std::string profile;
		std::string timezone;
		std::string city;
	};

public:
	geoipdb() : mmdb_(), is_open_(false)
	{
	}

	~geoipdb()
	{
		close();
	}

	bool open(const char *filename)
	{
		if (! is_open_) {
			const auto status = MMDB_open(filename, MMDB_MODE_MMAP, &mmdb_);
			if (status == MMDB_SUCCESS) {
				syslog(LOG_INFO, "geoeip: <%s>, version %s", filename, MMDB_lib_version());
				is_open_ = true;
				return true;
			}
			syslog(LOG_ERR, "geoeip: cannot open <%s> : %s", filename, MMDB_strerror(status));
		}
		return false;
	}

	bool open_dir(const char *dirname = nullptr /*cwd*/)
	{
		static const char *filenames[] = {
			"GeoLite2-City.mmdb", "GeoLite2-Country.mmdb", NULL };

		if (! is_open_) {
			char t_filename[1024] = {0};

			if (!dirname || !*dirname) dirname = ".";
			for (unsigned idx = 0; filenames[idx]; ++idx) {
				snprintf(t_filename, sizeof(t_filename)-1, "%s/%s", dirname, filenames[idx]);
				if (open(t_filename)) {
					return true;
				}
			}
		}
		return false;
	}

	void close()
	{
		if (is_open()) {
			MMDB_close(&mmdb_);
			is_open_ = false;
		}
	}

	bool is_open() const
	{
		return is_open_;
	}

	bool summary(const struct sockaddr *sa, std::string &country, std::string *city = nullptr)
	{
		if (!is_open_)
			return false;

		int status;
		MMDB_lookup_result_s result = MMDB_lookup_sockaddr(&mmdb_, sa, &status);
		if (status != MMDB_SUCCESS)
			return false;

		if (result.found_entry) {
			MMDB_entry_data_s entry_data = {};
			status = MMDB_get_value(&result.entry, &entry_data, "country", "iso_code", NULL);
			if (status != MMDB_SUCCESS)
				status = MMDB_get_value(&result.entry, &entry_data, "registered_country", "iso_code", NULL);
			if (status == MMDB_SUCCESS && entry_data.has_data) {
				if (entry_data.type == MMDB_DATA_TYPE_UTF8_STRING) {
					country.assign(entry_data.utf8_string, entry_data.data_size);
					if (city) {
						status = MMDB_get_value(&result.entry, &entry_data, "city", "names", "en", NULL);
						if (status == MMDB_SUCCESS && entry_data.has_data)
							 city->assign(entry_data.utf8_string, entry_data.data_size);
					}
					return true;
				}
			}
		}
		return false;
	}

	bool profile(const struct sockaddr *sa, Profile &profile)
	{
		if (!is_open_)
			return false;

		int status;
		MMDB_lookup_result_s result = MMDB_lookup_sockaddr(&mmdb_, sa, &status);
		if (status != MMDB_SUCCESS || !result.found_entry)
			return false;

		MMDB_entry_data_s entry_data = {};
		status = MMDB_get_value(&result.entry, &entry_data, "country", "iso_code", NULL);
		if (status != MMDB_SUCCESS)
			status = MMDB_get_value(&result.entry, &entry_data, "registered_country", "iso_code", NULL);
		if (status != MMDB_SUCCESS || !entry_data.has_data)
			return false;
		profile.country.assign(entry_data.utf8_string, entry_data.data_size);

		status = MMDB_get_value(&result.entry, &entry_data, "country", "names", "en", NULL);
		if (status != MMDB_SUCCESS)
			status = MMDB_get_value(&result.entry, &entry_data, "registered_country", "names", "en", NULL);
		if (status == MMDB_SUCCESS && entry_data.has_data)
			profile.country_name.assign(entry_data.utf8_string, entry_data.data_size);

		status = MMDB_get_value(&result.entry, &entry_data, "continent", "names", "en", NULL);
		if (status == MMDB_SUCCESS && entry_data.has_data)
			profile.continent.assign(entry_data.utf8_string, entry_data.data_size);

		status = MMDB_get_value(&result.entry, &entry_data, "location", "time_zone", NULL);
		if (status == MMDB_SUCCESS && entry_data.has_data)
			profile.timezone.assign(entry_data.utf8_string, entry_data.data_size);

		status = MMDB_get_value(&result.entry, &entry_data, "city", "names", "en", NULL);
		if (status == MMDB_SUCCESS && entry_data.has_data)
			profile.city.assign(entry_data.utf8_string, entry_data.data_size);

		return true;
	}

private:
	MMDB_s mmdb_;
	bool is_open_;
};


static inetd::CriticalSection geoip_lock;
static std::map<std::string, geoipdb *> geoip_databases;

static geoipdb *
get_geoipdb(const inetd::String &database)
{
	inetd::CriticalSection::Guard guard(geoip_lock);
	auto it = geoip_databases.find(database.c_str());
	if (it != geoip_databases.end()) {
		return it->second;
	}

	auto *geoip = new(std::nothrow) geoipdb;
	if (geoip && geoip->open(database)) {
		geoip_databases.emplace(database.c_str(), geoip);
	} else {
		delete geoip;
		geoip = nullptr;
	}
	return geoip;
}

#else	//HAVE_LIBMAXMINDDB

class geoipdb { /*none*/ };

geoipdb *
get_geoipdb(const inetd::String &database, int match_default)
{
	return nullptr;
}

#endif	//HAVE_LIBMAXMINDDB


/////////////////////////////////////////////////////////////////////////////////////////
//  Geoip implementation

geoips::geoips()
	: match_default_(0), geoipdb_(nullptr)
{
}


geoips::geoips(const geoips &rhs)
	: match_default_(0), geoipdb_(nullptr)
{
	rules_ = rhs.rules_;
}


geoips&
geoips::operator=(geoips &&rhs)
{
	if (this != &rhs) {
		rules_ = std::move(rhs.rules_);
		rhs.reset();
		reset();
	}
	return *this;
}


geoips::~geoips()
{
	clear();
}


const geoips::Collection&
geoips::operator()() const
{
	return rules_;
}


bool
geoips::build()
{
	if (size() && nullptr == geoipdb_) {
		if (nullptr == (geoipdb_ = get_geoipdb(database())))
			return false;
	}
	return true;
}


bool
geoips::allowed(const struct netaddr &addr) const
{
	if (0 == addr.family)
		return true;

	struct sockaddr_storage t_addr = {0};
	t_addr.ss_family = addr.family;
	if (addr.family == AF_INET6) {
		((struct sockaddr_in6 *)&t_addr)->sin6_addr = addr.network.v6;
	} else if (addr.family == AF_INET) {
		((struct sockaddr_in *)&t_addr)->sin_addr = addr.network.v4;
	}

	return geoips::allowed((const struct sockaddr *)&t_addr);
}


bool
geoips::allowed(const struct sockaddr *addr) const
{
	if (nullptr == addr)
		return true;

	if (size()) {
		if (nullptr == geoipdb_)
			geoipdb_ = get_geoipdb(database());

		geoipdb::Profile profile;
		if (geoipdb_ && geoipdb_->profile(addr, profile)) {
			for (auto &rule : rules_) {
				switch (rule.type) {
				case GEOIP_CITY:
					if (rule.spec == profile.city)
						return ('+' == rule.op);
					break;
				case GEOIP_TIMEZONE:
					if (rule.spec == profile.timezone)
						return ('+' == rule.op);
					break;
				case GEOIP_COUNTRY:
					if (rule.spec == profile.country)
						return ('+' == rule.op);
					break;
				case GEOIP_CONTINENT:
					if (rule.spec == profile.continent)
						return ('+' == rule.op);
					break;
				}
			}
		}
	}

	return (match_default() >= 0);
}


int
geoips::match_default() const
{
	return match_default_;
}


// <allow|deny> ALL
bool
geoips::match_default(int status)
{
	if (status && match_default_) {
		return ((match_default_ < 0 && status < 0) || (match_default_ > 0 && status > 0));
	}
	match_default_ = status;
	return true;
}


const inetd::String&
geoips::database() const
{
	return database_;
}


bool
geoips::database(const char *database)
{
	if (database && *database) {
		database_ = database;
		return true;
	}
	return false;
}


// <city|country|continent> <values ...>
bool
geoips::push(const std::vector<std::string> &rules, char op)
{
	if (rules.size() < 2)
		return false;

	enum geoip_type type = GEOIP_NONE;
	if (rules[0] == "city") {
		type = GEOIP_CITY;
	} else if (rules[0] == "timezone") {
		type = GEOIP_TIMEZONE;
	} else if (rules[0] == "country") {
		type = GEOIP_COUNTRY;
	} else if (rules[0] == "continent") {
		type = GEOIP_CONTINENT;
	} else {
		return false;
	}

	for (auto it(rules.begin() + 1), end(rules.end()); it != end; ++it) {
		const auto &rule = *it;

		if (std::find_if(rules_.begin(), rules_.end(), [&](const auto &element) {
					return (type == element.type && 0 == strcmp(rule.c_str(), element.spec.c_str()));
				}) != rules_.end())
			return false; // non-unique

		rules_.push_back({rule, type, op});
	}
	return true;
}


bool
geoips::push(const char *value, char op)
{
	xinetd::Split split;
	return geoips::push(split(value), op);
}


bool 
geoips::erase(const std::vector<std::string> &rules, char op)
{
	if (rules.size() < 2)
		return false;

	enum geoip_type type = GEOIP_NONE;
	if (rules[0] == "city") {
		type = GEOIP_CITY;
	} else if (rules[0] == "timezone") {
		type = GEOIP_TIMEZONE;
	} else if (rules[0] == "country") {
		type = GEOIP_COUNTRY;
	} else if (rules[0] == "continent") {
		type = GEOIP_CONTINENT;
	} else {
		return false;
	}

	unsigned count = 0;
	for (auto it(rules.begin() + 1), end(rules.end()); it != end; ++it) {
		const auto &rule = *it;

		rules_.erase(std::remove_if(rules_.begin(), rules_.end(),
					[&](const auto &element) {
						return (op == element.op && type == element.type &&
						         0 == strcmp(rule.c_str(), element.spec.c_str())) ? ++count : false;
					}), rules_.end());
	}
	return (0 != count);
}


bool 
geoips::erase(const char *value, char op)
{
	xinetd::Split split;
	return geoips::erase(split(value), op);
}


void
geoips::sysdump() const
{
	for (const auto &rule : rules_) {
		syslog(LOG_DEBUG, "%c: %s/%d (%s)", rule.op, rule.spec.c_str());
	}
}


size_t
geoips::size() const
{
	return rules_.size();
}


bool
geoips::empty() const
{
	return rules_.empty();
}


size_t
geoips::clear(char op)
{
	size_t count = 0;
	rules_.erase(std::remove_if(rules_.begin(), rules_.end(), [&](const auto &element) {
					return (op == element.op ? ++count : 0);
				}), rules_.end());
	return count;
}


void
geoips::clear()
{
	rules_.clear();
	reset();
}


void
geoips::reset()
{
	geoipdb_ = nullptr;
}


/////////////////////////////////////////////////////////////////////////////////////////
//  geoip

int
geoip(PeerInfo &remote)
{
#if defined(HAVE_GEOIP)
	const struct servtab *sep = remote.getserv();
	return sep->se_geoip.allowed(remote.getaddr());

#else
	return 0;

#endif
}

//end

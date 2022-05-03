/* -*- mode: c; indent-width: 8; -*- */
/*
 * windows inetd service - netaddrs.
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
#include <syslog.h>

// see: https://maxmind.github.io/libmaxminddb/
//  and: https://github.com/P3TERX/GeoLite.mmdb
#if defined(HAVE_LIBMAXMINDDB)
#if defined(ssize_t)
#undef ssize_t
#endif
#include <maxminddb/maxminddb.h>
#pragma comment(lib, "libmaxminddbd.lib")
#endif

namespace {

class GeoIpDB {
public:
	GeoIpDB() : mmdb_(), is_open_(false)
	{
	}

	~GeoIpDB()
	{
		close();
	}

	bool open(const char *filename)
	{
		if (! is_open_) {
                        //TODO: add "GeoLite2-Country.mmdb" if (no .mmdb extension)
			const auto status = MMDB_open(filename, MMDB_MODE_MMAP, &mmdb_);
			if (status != MMDB_SUCCESS) {
				//MMDB_strerror(status);
				return false;
			}
			//MMDB_lib_version()
			is_open_ = true;
		}
		return true;
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

	bool get(struct sockaddr *sa, std::string &iso_country)
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
					iso_country.assign(entry_data.utf8_string, entry_data.data_size);
					return true;
				}
			}

//			status = MMDB_get_value(&result.entry, &entry_data, "country", "names", "en", NULL);
//			if (status != MMDB_SUCCESS)
//				status = MMDB_get_value(&result.entry, &entry_data, "registered_country", "names", "en", NULL);
//			if (status == MMDB_SUCCESS && entry_data.has_data)
//				result->country_name.assign(entry_data.utf8_string, entry_data.data_size);

//			status = MMDB_get_value(&result.entry, &entry_data, "continent", "names", "en", NULL);
//			if (status == MMDB_SUCCESS && entry_data.has_data)
//				result->continent.assign(entry_data.utf8_string, entry_data.data_size);

//			status = MMDB_get_value(&result.entry, &entry_data, "location", "time_zone", NULL);
//			if (status == MMDB_SUCCESS && entry_data.has_data) {
//				result->timezone.assign(entry_data.utf8_string, entry_data.data_size);

//			status = MMDB_get_value(&result.entry, &entry_data, "city", "names", "en", NULL);
//			if (status == MMDB_SUCCESS && entry_data.has_data) {
//				result->city.assign(entry_data.utf8_string, entry_data.data_size);
		}
		return false;
	}

private:
	MMDB_s mmdb_;
	bool is_open_;
};

} //namespace anon


/////////////////////////////////////////////////////////////////////////////////////////
// geoip

int
geoip(PeerInfo &remote)
{
#if defined(HAVE_GEOIP)
	const struct servtab *sep = remote.getserv();
//TODO
//  remote.set_geoip( ... )
	return sep->se_geoip.allowed(remote.getaddr());

#else
	return 0;
#endif
}

//end

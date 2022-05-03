/* -*- mode: c; indent-width: 8; -*- */
/*
 * windows inetd service - connection banners.
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

#include <fstream>

namespace {

template<typename Pred>
bool load(const char *filename, Pred &pred)
{
	std::ifstream stream(filename, std::ios::in|std::ios::binary);
	if (stream.fail()) {
		syslog(LOG_ERR, "unable to open source <%s>", filename);
		return false;
	}

	char buffer[1024]; // MTU
	while (stream.read(buffer, sizeof(buffer))) {
		const size_t count = static_cast<size_t>(stream.gcount());
		pred(buffer, count);
	}
	return (stream.eof() ? true : false);
}


void
load_file(PeerInfo &remote, const inetd::String &filename)
{
	if (filename.empty())
		return;

	const struct servtab *sep = remote.getserv();
	if (sep->se_proto == "tcp") {
		int fd = remote.fd();

		load<>(filename, [&](const char *buffer, size_t count) {
					send(fd, buffer, count, 0);
				});

	} else if (sep->se_proto == "udp") {
		const struct sockaddr_storage *sa = remote.getaddr();
		int fd = remote.fd();

		if (sa) {
			load<>(filename, [&](const char *buffer, size_t count) {
						sendto(fd, buffer, count, 0, (const struct sockaddr *)sa, sizeof(*sa));
					});
		}
	}
}

};  //namespace anon


/////////////////////////////////////////////////////////////////////////////////////////
//  banner operations

int
banner(PeerInfo &remote)
{
	const struct servtab *sep = remote.getserv();
	load_file(remote, sep->se_banner);
	return 0;
}


int
banner_success(PeerInfo &remote)
{
	const struct servtab *sep = remote.getserv();
	load_file(remote, sep->se_banner_success);
	return 0;
}


int
banner_fail(PeerInfo &remote)
{
	const struct servtab *sep = remote.getserv();
	load_file(remote, sep->se_banner_fail);
	return 0;
}

//end

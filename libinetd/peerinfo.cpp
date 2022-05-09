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
 * ==end==
 */

#include "inetd.h"

#include <algorithm>
#include <climits>

#include <sthread.h>
#include <sysexits.h>
#include <syslog.h>


PeerInfo::PeerInfo(int fd, struct servtab *sep)
	: fd_(fd), sep_(sep), timestamp_()
{
	memset(&rss_, 0, sizeof(rss_));
	memset(pname_, 0, sizeof(pname_));
	(void)clock_gettime(CLOCK_REALTIME, &timestamp_);
}


// connection file descriptor.
int
PeerInfo::fd() const
{
	return fd_;
}


// associated service definition.
struct servtab *
PeerInfo::getserv() const
{
	return sep_;
}


// connection timestamp.
const struct timespec &
PeerInfo::timestamp() const
{
	return timestamp_;
}


// remote address.
const struct sockaddr_storage *
PeerInfo::getaddr()
{
	if (0 == rss_.ss_family) {
		socklen_t rsslen = sizeof(rss_);
		if (0 != getpeername(fd_, (struct sockaddr *)&rss_, &rsslen)) {
			rss_.ss_family = -1;
		}
	}
	return (-1 == rss_.ss_family ? nullptr : &rss_);
}


// human readable remote identify/address.
const char *
PeerInfo::getname()
{
	if (pname_[0]) return pname_;
	strcpy(pname_, "unknown");

	const struct sockaddr_storage *peer = getaddr();
	if (peer) {
		if (getnameinfo((const struct sockaddr *)peer, SOCKLEN_SOCKADDR_STORAGE_PTR(peer),
				    pname_, sizeof(pname_), NULL, 0, NI_NUMERICHOST) != 0) {
			syslog(LOG_ERR, "%s getnameinfo error : %M", sep_->se_service);
		}

	} else {
		struct pollfd fds = {0};

		fds.fd = fd();
		fds.events = POLLRDNORM;

		if (poll(&fds, 1, 50 /*milliseconds*/) <= 0) {
			syslog(LOG_ERR, "%s getnameinfo error : timeout", sep_->se_service);

		} else {
			struct sockaddr_storage from = {0};
			socklen_t fromlen = sizeof(from);
			char buf[64];

			if (recvfrom(fd(), buf, sizeof(buf), MSG_PEEK, (struct sockaddr *)&from, &fromlen) >= 0) {
				if (getnameinfo((struct sockaddr *)&from, SOCKLEN_SOCKADDR_STORAGE(from),
					    pname_, sizeof(pname_), nullptr, 0, NI_NUMERICHOST)) {
					syslog(LOG_ERR, "%s getnameinfo error : %M", sep_->se_service);
				}
			} else {
				syslog(LOG_ERR, "%s getname error : no data", sep_->se_service);
			}
		}
	}

	return pname_;
}

//end
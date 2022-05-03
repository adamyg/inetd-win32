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

struct servtab;

class PeerInfo {
public:
	PeerInfo(int fd, struct servtab *sep);
	int fd() const;
	struct servtab *getserv() const;
	const struct timespec &timestamp() const;
	const struct sockaddr_storage *getaddr();
	const char *getname();

private:
	const int fd_;
	struct servtab *sep_;
	struct sockaddr_storage rss_;
	struct timespec timestamp_;
	char pname_[NI_MAXHOST];
};

//end
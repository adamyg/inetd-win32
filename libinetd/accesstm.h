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

#include <time.h>

class access_times {
public:
	struct time {
		unsigned start;
		unsigned end;
	};

	static unsigned to_time(unsigned hh, unsigned mm);
	static bool to_access_range(const char *arg, struct time &range);

	access_times();
	bool allowed(unsigned now) const;
	bool push(const time &tm);
	size_t size() const;
	bool empty() const;
	void clear();
	void sysdump() const;

private:
#define MAXACCESSV 10
	struct time times_[MAXACCESSV];
};

//end

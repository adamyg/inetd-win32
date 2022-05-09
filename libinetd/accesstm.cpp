/* -*- mode: c; indent-width: 8; -*- */
/*
 * windows inetd service - access times.
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

#include <time.h>
#include <algorithm>


//static
unsigned
access_times::to_time(unsigned hh, unsigned mm)
{
	if (mm > 59 || (mm && hh > 23) || (0 == mm && hh > 24))
		return UINT_MAX;
	return (hh * 60) + mm;
}


//static
static bool
eos(const char *arg)
{
	while (*arg && ' ' == *arg)
		++arg;
	return (0 == *arg);
}


bool
access_times::to_access_range(const char *arg, struct time &range)
{
	unsigned shh, smm, ehh, emm;
	int end = 0;

	range.start = 0;
	range.end = 0;

	if (arg) {
		const int ret = sscanf(arg, "%2u:%2u-%2u:%2u%n", &shh, &smm, &ehh, &emm, &end);
		if (4 == ret && eos(arg + end)) {
			if (UINT_MAX != (range.start = to_time(shh, smm)) &&
			    UINT_MAX != (range.end = to_time(ehh, emm)) &&
				range.start < range.end) {
				return true;
			}
		}
	}
	return false;
}


access_times::access_times() : times_()
{
}


bool
access_times::push(const struct time &range)
{
	unsigned ai = 0;

	for (struct time *ap = times_; ai < MAXACCESSV && ap->end; ++ap, ++ai) {
		if (ap->start >= range.start && ap->end <= range.end) {
			*ap = range;
			return true;
		} else if (range.start >= ap->start && range.end <= ap->end) {
			return true;
		}
	}
	if (ai < MAXACCESSV) {
		times_[ai] = range;
		return true;
	}
	return false;
}


bool
access_times::allowed(const unsigned now) const
{
	if (empty())
		return true;

	for (const struct time *ap = times_; ap->end; ++ap) {
		if (now >= ap->start && now < ap->end) {
			return true;
		}
	}
	return false;
}


size_t
access_times::size() const
{
	size_t count = 0;
	for (const struct time *ap = times_; ap->end; ++ap) {
		++count;
	}
	return count;
}


bool
access_times::empty() const
{
	return (0 == times_[0].end);
}


void
access_times::clear()
{
	memset(times_, 0, sizeof(times_));
}


void
access_times::sysdump() const
{
	for (const struct time *ap = times_; ap->end; ++ap) {
		syslog(LOG_DEBUG, "%02u:%02u-%02u:%02u", ap->start/60, ap->start%60, ap->end/60, ap->end%60);
	}
}


/////////////////////////////////////////////////////////////////////////////////////////
// access times

int
accesstm(PeerInfo &remote)
{
	const struct servtab *sep = remote.getserv();

	if (! sep->se_access_times.empty()) {
		const time_t now = remote.timestamp().tv_sec;
		struct tm tm = { 0 };

		localtime_s(&tm, &now);
		if (! sep->se_access_times.allowed(access_times::to_time(tm.tm_hour, tm.tm_min))) {
			return -1; // deny
		}
		return 1; // allowed
	}
	return 0; // unlimited
}

//end
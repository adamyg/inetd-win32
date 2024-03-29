#include <edidentifier.h>
__CIDENT_RCSID(syslog_c,"$Id: syslog.c,v 1.3 2022/05/24 03:46:21 cvsuser Exp $")

/* -*- mode: c; indent-width: 8; -*- */
/*
 * syslog emulation
 *
 * Copyright (c) 2020 - 2022, Adam Young.
 * All rights reserved.
 *
 * This file is part of inetd-win32.
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

//#if defined(HAVE_CONFIG_H)
//#include "w32config.h"
//#endif

#include <sys/cdefs.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

#define  SYSLOG_NAMES
#include "syslog.h"

#if !defined(WIN32_MEAN_AND_LEAN)
#define  WIN32_MEAN_AND_LEAN
#endif
#include <Windows.h>

static const char * month[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

static char		syslog_hostname[256+1]; // Max plus leading space.
static const char *	syslog_ident = "app";
static int		syslog_facility = LOG_USER;
static int		syslog_mask = 0xfff;
static int		syslog_option = 0;
static char		syslog_pid[64]; 	// process identifier; cached preformated.

static int		syslog_network(void *, int op, int pri, const char *message, size_t len);

static SYSLOGPROXYCB	syslog_proxy = syslog_network;
static void *		syslog_data;


void
openlog(const char* ident, int option, int facility)
{
	syslog_ident = (ident ? ident : getprogname());

	if (facility && 0 == (facility & ~LOG_FACMASK)) {
		syslog_facility = facility;
	}

	setlogoption(option);
	(void) facilitynames;			// unused warning
}


void
closelog(void)
{
}


int
setlogmask(int nmask)
{
	const int omask = syslog_mask;
	if (nmask)
		syslog_mask = nmask;
	return omask;
}


int
getlogmask(void)
{
	return syslog_mask;
}


int
setlogoption(int option)
{
	const int ooption = syslog_option;	// previous option setting

	syslog_option = option;

	syslog_hostname[0] = 0;
	if (0 == (LOG_NOHOST & option) &&
			0 == w32_gethostname(syslog_hostname + 1,
				sizeof(syslog_hostname) - 3 /*lead+trailg+nul*/)) {
		syslog_hostname[0] = ' ';	// leading space
		if (syslog_ident[0]) {		// trailing space
			strcat(syslog_hostname, " ");
		}
	}

	syslog_pid[0] = 0;			// apply LOG_PID
	if (LOG_PID & option) {
		sprintf(syslog_pid, "[%u]", (unsigned)GetCurrentProcessId());
	}
	return ooption;
}


int
getlogoption()
{
	return syslog_option;
}


void
setlogproxy(SYSLOGPROXYCB proxy, void *data)
{
	if (proxy) {				// set
		syslog_proxy = proxy;
		syslog_data = data;
	} else {				// restore
		syslog_proxy = syslog_network;
		syslog_data = NULL;
	}
}


void
syslog(int pri, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vxsyslog(pri, fmt, ap, NULL);
	va_end(ap);
}


void
WSASyslogx(int pri, const char *fmt, ...)
{
	char buf[256];
	DWORD dwError = WSAGetLastError();
	DWORD len = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS |
			FORMAT_MESSAGE_MAX_WIDTH_MASK, NULL, dwError,
			    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf+2, sizeof(buf)-3 /*nul*/, NULL);
	va_list ap;
	if (0 == len) {
		memcpy(buf, ": Unknown error", sizeof("Unknown error"));
	} else {
		char *cursor = buf + 2;
		buf[0] = ':', buf[1] = ' ';
		while (--len) { 		// remove trailing whitespace.
			const char ch = cursor[len];
			if (ch == ' ' || ch == '.' || ch == '\n' || ch == '\r') {
				continue;	// consume.
			}
			break;			// done
		}
		cursor[len+1] = 0;		// terminate
	}
	va_start(ap, fmt);
	vxsyslog(pri, fmt, ap, buf);
	va_end(ap);
}


void
vsyslog(int pri, const char *fmt, va_list ap)
{
	vxsyslog(pri, fmt, ap, NULL);
}


void
vxsyslog(int pri, const char *fmt, va_list ap, const char *suffix)
{
#define MESSAGE_LEN (2*1024)
#define FMT_LEN 1024

	const int saved_errno = errno;
	const char *label = prioritynames[LOG_PRI(pri)].c_name;
	char message[MESSAGE_LEN], fmt_copy[FMT_LEN];
	int hdrlen = 0, msglen, space, len;
	const char *p;

	// Parameters

	if (pri & ~(LOG_PRIMASK|LOG_FACMASK)) {
		syslog(LOG_ERR, "syslog: unknown facility/priority: %x", pri);
		pri &= LOG_PRIMASK|LOG_FACMASK;
	}

	if (!(LOG_MASK(LOG_PRI(pri)) & syslog_mask)) {
		return;
	}

	if (0 == (pri & LOG_FACMASK)) {
		pri |= syslog_facility;
	}

	// Parse format, %[mM] expansion

	for (p = strchr(fmt, '%'); p;) {
		if (p[1] == 'm' || p[1] == 'M') {
			int left = sizeof(fmt_copy) - 1 /*nul*/;
			char *f, ch;

			for (f = fmt_copy; 0 != (ch = *fmt++) && left;) {
				if ('%' == ch && left > 4) { // strerror
					if ('m' == *fmt) {
						int len = snprintf(f, left, "%s", strerror(saved_errno));
						if (len < 0 || len >= left) len = left;
						f += len, left -= len;
						++fmt;
						continue;

					} else if ('M' == *fmt) { // windows error
						DWORD dwError = GetLastError();
						DWORD len = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS |
								FORMAT_MESSAGE_MAX_WIDTH_MASK, NULL, dwError,
								    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), f, left, NULL);
						f += len, left -= len;
						++fmt;
						continue;
					}
				}
				*f++ = ch;
				--left;
			}
			*f  = 0;
			fmt = fmt_copy;
			break;			// done
		}
		p = strchr((char *)(p + 1), '%');
	}

	// Format:
	//  <pri>|<[label :]> MMM DD HH:MM:SS[.mmm] <hostname> <ident><[pid]>[.<tid>]
	//

#define NLCR	3   //\n\r\0

	if (0 == (LOG_NOHEADER & syslog_option)) {
		SYSTEMTIME stm = {0};

		GetLocalTime(&stm); // wall clock

		if (LOG_TID & syslog_option) {
			const DWORD tid = GetCurrentThreadId();

			if (LOG_MSTIME & syslog_option) {
				hdrlen = sprintf_s(message, sizeof(message) - NLCR, "[%-7.7s] : %s %2d %02u:%02u:%02u.%03u%s %s%s.%u: ",
					    label, month[ stm.wMonth - 1 ], stm.wDay, stm.wHour, stm.wMinute, stm.wSecond, stm.wMilliseconds,
						syslog_hostname, syslog_ident, syslog_pid, tid);

			} else {
				hdrlen = sprintf_s(message, sizeof(message) - NLCR, "[%-7.7s] : %s %2d %02u:%02u:%02u%s %s%s.%u: ",
					    label, month[ stm.wMonth - 1 ], stm.wDay, stm.wHour, stm.wMinute, stm.wSecond,
						syslog_hostname, syslog_ident, syslog_pid, tid);
			}

		} else {
			if (LOG_MSTIME & syslog_option) {
				hdrlen = sprintf_s(message, sizeof(message) - NLCR, "[%-7.7s] : %s %2d %02u:%02u:%02u.%03u%s %s%s: ",
					    label, month[ stm.wMonth - 1 ], stm.wDay, stm.wHour, stm.wMinute, stm.wSecond, stm.wMilliseconds,
						syslog_hostname, syslog_ident, syslog_pid);

			} else {
				hdrlen = sprintf_s(message, sizeof(message) - NLCR, "[%-7.7s] : %s %2d %02u:%02u:%02u%s%s%s: ",
					    label, month[ stm.wMonth - 1 ], stm.wDay, stm.wHour, stm.wMinute, stm.wSecond,
						syslog_hostname, syslog_ident, syslog_pid);
			}
		}
	}
	space = (sizeof(message) - NLCR) - hdrlen;

	if ('%' == fmt[0] && 's' == fmt[1] && 0 == fmt[2]) {
		// syslog( "%s", buffer )
		const char *buffer = va_arg(ap, const char *);

		msglen = 0;
		if (buffer && *buffer) { // format optimization
			if ((msglen = strlen(buffer)) > space) {
				msglen = space;
			}
			memcpy(message + hdrlen, buffer, msglen);
		}
	} else {
		// syslog( ... )
		msglen = vsprintf_s(message + hdrlen, space, fmt, ap);
	}

	len = hdrlen + msglen;
	assert(len <= (sizeof(message) - NLCR));
	if (suffix && *suffix) {
		int suflen;

		space = (sizeof(message) - NLCR) - len;
		if ((suflen = strlen(suffix)) > space) {
			suflen = space;
		}
		(void) memcpy(message + len, suffix, suflen);
		len += suflen;
	}

	// Direct result

	if (LOG_PERROR & syslog_option) {
		message[len] = '\n', message[len + 1] = 0;
		fwrite(message, len + 1, 1, stderr);
		message[len] = 0;
	}

	if (! syslog_proxy(syslog_data, 0, pri, message, (size_t)len)) {
		if (LOG_CONS == ((LOG_CONS|LOG_PERROR) & syslog_option)) {
			message[len] = '\n', message[len + 1] = 0;
			fwrite(message, len + 1, 1, stdout);
		}
	}
}


static int
syslog_network(void *data, int op, int pri, const char *msg, size_t len)
{
	//TODO: not implementated
	return 0;
}

//end


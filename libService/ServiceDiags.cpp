#include <edidentifier.h>
__CIDENT_RCSID(ServiceDiags_cpp,"$Id: ServiceDiags.cpp,v 1.7 2022/04/29 04:47:10 cvsuser Exp $")

/* -*- mode: c; indent-width: 8; -*- */
/*
 * Service diagnositics adapter
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

#include <cstring>
#include <cassert>

#include "ServiceDiags.h"
#include "Logger.h"

#define  WIN32_MEAN_AND_LEAN
#include <Windows.h>

static const char *loglevels[] = {
    "",
    "ERROR  | ",
    "WARNING| ",
    "INFO   | ",
    "DEBUG  | "
    };

static const char *month[] = {
    "Jan", 
    "Feb", 
    "Mar", 
    "Apr", 
    "May",
    "Jun",
    "Jul",
    "Aug",
    "Sep",
    "Oct",
    "Nov",
    "Dec"
    };

bool ServiceDiags::Adapter::logtid_ = false;
bool ServiceDiags::Adapter::logms_ = false;


void
ServiceDiags::Adapter::setlogtid(bool value)
{
    logtid_ = value;
}


void
ServiceDiags::Adapter::setlogms(bool value)
{
    logms_ = value;
}


void
ServiceDiags::Adapter::print(Logger &logger, enum loglevel type, const char *fmt, va_list *ap)
{
#define	MESSAGE_LEN     (2 * 1024)
#define	FMT_LEN	        1024

    const int saved_errno = errno;
    char message[MESSAGE_LEN], fmt_copy[FMT_LEN];
    int mlen;

    // Parse format, %[mM] expansion

    for (const char *p = std::strchr(fmt, '%'); p;) {
        if (p[1] == 'm' || p[1] == 'M') {
            int left = sizeof(fmt_copy) - 1 /*nul*/;
            char *f, ch;

            for (f = fmt_copy; 0 != (ch = *fmt++) && left;) {
                if ('%' == ch && left > 4) {    // strerror
                    if ('m' == *fmt) {
                        int len = snprintf(f, left, "%s", std::strerror(saved_errno));
                        if (len < 0 || len >= left) len = left;
                        f += len, left -= len;
                        ++fmt;
                        continue;

                    } else if ('M' == *fmt) {   // windows error
                        DWORD dwError = ::GetLastError();
                        DWORD len = ::FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS |
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
            break;                              // done
        }
        p = std::strchr((char *)(p + 1), '%');
    }

    // Message

    if (ap) {
        if ('%' == fmt[0] && 's' == fmt[1] && 0 == fmt[2]) {
            //
            //  log( "%s", buffer )
            const char *buffer = va_arg(*ap, const char *);
            mlen = strlen(buffer ? buffer : "");
        } else {
            //
            //  log( ... )
            mlen = vsprintf_s(message, sizeof(message), fmt, *ap);
        }
        fmt  = message;
    } else {
        //
        //  log ( message )
        mlen = strlen(fmt);
    }

    // Push
    
    if (mlen > 0) {
        assert(mlen <= sizeof(message) || (fmt != message));
        push(logger, type, (const char *)fmt, (size_t)mlen);
    }
}


void
ServiceDiags::Adapter::push(Logger &logger, enum loglevel type, const char *buffer, size_t buflen)
{
    const char *label = loglevels[type];
    char header[64];
    SYSTEMTIME stm = {0};
    int hlen;

    if (0 == buflen)
        return;

    // Header

    ::GetLocalTime(&stm);                       // wall clock

    if (logtid_) {
        const unsigned tid = (unsigned)::GetCurrentThreadId();
        if (logms_) {
            hlen = sprintf_s(header, sizeof(header) - 1, "%s%s %2d %02u:%02u:%02u.%03u <%u>: ",
                        label, month[ stm.wMonth - 1 ], stm.wDay, stm.wHour, stm.wMinute, stm.wSecond, stm.wMilliseconds, tid);
        } else {
            hlen = sprintf_s(header, sizeof(header) - 1, "%s%s %2d %02u:%02u:%02u <%u>: ",
                        label, month[ stm.wMonth - 1 ], stm.wDay, stm.wHour, stm.wMinute, stm.wSecond, tid);
        }

    } else {
        if (logms_) {
            hlen = sprintf_s(header, sizeof(header) - 1, "%s%s %2d %02u:%02u:%02u.%03u: ",
                        label, month[stm.wMonth - 1], stm.wDay, stm.wHour, stm.wMinute, stm.wSecond, stm.wMilliseconds);
        } else {
            hlen = sprintf_s(header, sizeof(header) - 1, "%s%s %2d %02u:%02u:%02u: ",
                        label, month[stm.wMonth - 1], stm.wDay, stm.wHour, stm.wMinute, stm.wSecond);
        }
    }
    assert(hlen <= sizeof(header));

    // Result

    struct Logger::liovec iovec[2];

    iovec[0].liov_base = header;
    iovec[0].liov_len  = hlen;
    iovec[1].liov_base = (void *)buffer;
    iovec[1].liov_len  = buflen;

    logger.pushv(iovec, 2);
}  

/*end*/
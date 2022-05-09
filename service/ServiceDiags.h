#pragma once
#ifndef SERVICEDIAGS_H_INCLUDED
#define SERVICEDIAGS_H_INCLUDED
/* -*- mode: c; indent-width: 8; -*- */
/*
 * Service diagnostics adapter
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

#include <cstdarg>

#if defined(_MSC_VER)
#include <msvc_system_error.hpp>
#endif
#include "../libNTService/NTServiceIIO.h"

class Logger;

class ServiceDiags {
public:
    struct Adapter {
        enum loglevel {
            LLNONE, LLERROR, LLWARNING, LLINFO, LLDEBUG
        };

        static void setlogtid(bool value);
        static void setlogms(bool value);
        static void print(Logger &logger, enum loglevel type, const char *fmt, va_list *ap = 0);
        static void push(Logger &logger, enum loglevel type, const char *buffer, size_t buflen);

    private:
        static bool logtid_;
        static bool logms_;
    };

    class Syslog {
        static int  hook(void *self, int op, int pri, const char *msg, size_t msglen);
    public:
        static void attach(Logger &logger);
        static void detach();
    };

    static NTService::IDiagnostics& Get(Logger &logger) {
        static struct ServiceDiagnosticsIOImpl : public NTService::IDiagnostics {
            ServiceDiagnosticsIOImpl(Logger &logger) : logger_(logger)
            {
            }

            ///////////////////////////////////////////////////////////////////

            virtual void ferror(const char *fmt, ...)
            {
                va_list ap;
                va_start(ap, fmt);
                verror(fmt, ap);
                va_end(ap);
            }

            virtual void fwarning(const char *fmt, ...)
            {
                va_list ap;
                va_start(ap, fmt);
                vwarning(fmt, ap);
                va_end(ap);
            }

            virtual void finfo(const char *fmt, ...)
            {
                va_list ap;
                va_start(ap, fmt);
                vinfo(fmt, ap);
                va_end(ap);
            }

            virtual void fdebug(const char *fmt, ...)
            {
                va_list ap;
                va_start(ap, fmt);
                vdebug(fmt, ap);
                va_end(ap);
            }

            ///////////////////////////////////////////////////////////////////

            virtual void verror(const char *fmt, va_list ap)
            {
                fprint(Adapter::LLERROR, fmt, ap);
            }

            virtual void vwarning(const char *fmt, va_list ap)
            {
                fprint(Adapter::LLWARNING, fmt, ap);
            }

            virtual void vinfo(const char *fmt, va_list ap)
            {
                fprint(Adapter::LLINFO, fmt, ap);
            }

            virtual void vdebug(const char *fmt, va_list ap)
            {
                fprint(Adapter::LLDEBUG, fmt, ap);
            }

            ///////////////////////////////////////////////////////////////////

            virtual void error(const char *msg)
            {
                sprint(Adapter::LLERROR, msg);
            }

            virtual void warning(const char *msg)
            {
                sprint(Adapter::LLWARNING, msg);
            }

            virtual void info(const char *msg)
            {
                sprint(Adapter::LLINFO, msg);
            }

            virtual void debug(const char *msg)
            {
                sprint(Adapter::LLDEBUG, msg);
            }

        private:
            void fprint(enum Adapter::loglevel type, const char *fmt, va_list &ap)
            {
                Adapter::print(logger_, type, fmt, &ap);
            }

            void sprint(enum Adapter::loglevel type, const char *msg)
            {
                Adapter::print(logger_, type, msg);
            }

        private:
            Logger &logger_;

        } diag_(logger);

        return diag_;
    }
};

#endif  //SERVICEDIAGS_H_INCLUDED
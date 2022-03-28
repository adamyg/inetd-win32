#pragma once
#ifndef LOGGERSYSLOG_H_INCLUDED
#define LOGGERSYSLOG_H_INCLUDED
/* -*- mode: c; indent-width: 8; -*- */
/*
 * Logger syslog adapter
 *
 * Copyright (c) 2020 - 2022, Adam Young.
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

#include "syslog.h"

class Logger;

struct LoggerSyslog {
    static int
    syslog_hook(void *self, int op, int pri, const char *msg, size_t msglen) {
        Logger *logger = (Logger *)self;
        logger->push(msg, msglen);
        return 1;
    }

    static void
    attach(Logger *logger) {
        setlogproxy(&LoggerSyslog::syslog_hook, (void *) logger);
    }

    static void
    detach() {
        setlogproxy(NULL, NULL);
    }
};

#endif  //LOGGERSYSLOG_H_INCLUDED

//end
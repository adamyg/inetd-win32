#include <edidentifier.h>
__CIDENT_RCSID(ServiceDiags_cpp,"$Id: ServiceDiagsSyslog.cpp,v 1.3 2022/05/18 16:23:40 cvsuser Exp $")

/* -*- mode: c; indent-width: 8; -*- */
/*
 * Service diagnositics adapter -- syslog
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
#include "syslog.h"


//private
int
ServiceDiags::Syslog::hook(void *self, int op, int pri, const char *msg, size_t msglen)
{
        static enum Adapter::loglevel levels[] = {
                { Adapter::LLERROR   },  // 0 - LOG_EMERG
                { Adapter::LLERROR   },  // 1 - LOG_ALERT
                { Adapter::LLERROR   },  // 2 - LOG_CRIT
                { Adapter::LLERROR   },  // 3 - LOG_ERR
                { Adapter::LLWARNING },  // 4 - LOG_WARNING
                { Adapter::LLWARNING },  // 5 - LOG_NOTICE
                { Adapter::LLINFO,   },  // 6 - LOG_INFO
                { Adapter::LLDEBUG   }   // 7 - LOG_DEBUG
                };

        Logger &logger = *((Logger *)self);
        Adapter::push(logger, levels[LOG_PRI(pri)], msg, msglen);
        return 1;
}


//static
void
ServiceDiags::Syslog::attach(Logger &logger)
{
        setlogproxy(&ServiceDiags::Syslog::hook, (void *) &logger);
        setlogoption(LOG_NOHEADER | (getlogoption() & ~LOG_PERROR));
}


//static
void
ServiceDiags::Syslog::detach()
{
        setlogproxy(NULL, NULL);
}

/*end*/
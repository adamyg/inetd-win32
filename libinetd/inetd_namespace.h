#pragma once
/* -*- mode: c; indent-width: 8; -*- */
/*
 * inetd::Definitions and tweaks.
 * windows inetd service.
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
 * ==
 */

/*
 *  Permit client-side interfaces within older compilers:
 *
 *      ServiceGetOpt.h
 *      SocketShared.h
 *      ScopedHandle.h
 *      ScopedProcessId.h
 */

#ifndef INETD_DELETED_FUNCTION                  /* toolchain C+11 tweaks; emulate Boost */
#if defined(__WATCOMC__)
#define INETD_DELETED_FUNCTION(_f) _f;
#define INETD_NOEXCEPT
#define INETD_NOEXCEPT_OR_NOTHROW throw()
#define INETD_NOEXCEPT_IF(Predicate)
#define INETD_NOEXCEPT_EXPR(Expression) false
#define INETD_OUTCOME_NODISCARD
#else
#define INETD_DELETED_FUNCTION(_f) _f = delete;
#define INETD_NOEXCEPT noexcept
#define INETD_NOEXCEPT_OR_NOTHROW noexcept
#define INETD_NOEXCEPT_IF(Predicate) noexcept((Predicate))
#define INETD_NOEXCEPT_EXPR(Expression) noexcept((Expression))
#define INETD_OUTCOME_NODISCARD [[nodiscard]]   /* alt SAL _Must_inspect_result_ */
#endif
#endif  //INETD_DELETED_FUNCTION

#include "../libsthread/timespec.h"             /* struct timespec */

#if defined(__WATCOMC__)
#ifndef _countof
#define _countof(__type) (sizeof(__type)/sizeof(__type[0]))
#endif
#define terminate() abort()
#define nullptr NULL
#endif

/*end*/

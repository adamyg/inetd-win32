#pragma once
#if !defined(WSSUPPORT_H_INCLUDED)
#define WSSUPPORT_H_INCLUDED
/* -*- mode: c; indent-width: 8; -*- */
/*
 * Windows supplemental definitions / work arounds.
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

#if defined(__MINGW32__)
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#define _WIN32_VER _WIN32_WINNT
#endif

#if !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0600
#elif (_WIN32_WINNT < 0x0600)
#error _WIN32_WINNT < 0x0600                    /* general requirement */
#endif

#ifndef BOOST_DELETED_FUNCTION                  /* toolchain C+11 tweaks; emulate Boost */
#if defined(__WATCOMC__)
#define BOOST_DELETED_FUNCTION(_f) _f;
#define BOOST_NOEXCEPT
#define BOOST_NOEXCEPT_OR_NOTHROW throw()
#define BOOST_NOEXCEPT_IF(Predicate)
#define BOOST_NOEXCEPT_EXPR(Expression) false
#define BOOST_OUTCOME_NODISCARD
#else
#define BOOST_DELETED_FUNCTION(_f) _f = delete;
#define BOOST_NOEXCEPT noexcept
#define BOOST_NOEXCEPT_OR_NOTHROW noexcept
#define BOOST_NOEXCEPT_IF(Predicate) noexcept((Predicate))
#define BOOST_NOEXCEPT_EXPR(Expression) noexcept((Expression))
#define BOOST_OUTCOME_NODISCARD [[nodiscard]]   /* alt SAL _Must_inspect_result_ */
#endif
#endif  //BOOST_DELETED_FUNCTION

#if defined(__MINGW32__)
#if !defined(_MAX_PATH)
#define _MAX_PATH MAX_PATH
#endif
#include <strings.h>
#include <stdarg.h>
#define _strnicmp(__a,__b,__c) strncasecmp(__a,__b,__c)
#define _stricmp(__a,__b) strcasecmp(__a,__b)

#if !defined(__MINGW64_VERSION_MAJOR) && defined(__cplusplus)
extern "C" __stdcall int sprintf_s( char * __s, size_t __n, const char * __format, ... );
extern "C" __stdcall int vsprintf_s( char * __s, size_t __n, const char * __format, va_list __arg );
#endif  // __cplusplus
#endif

#if defined(__WATCOMC__) && (__WATCOMC__ <= 1300)
#if defined(__cplusplus)                        /* std::nothrow() emulation */
#include <stdarg.h>
#include <cstddef>
#include <cstdlib>
#pragma warning (push)
#pragma warning 14 9 // no reference to symbol 'operator new'
    namespace std {
        struct nothrow_t { };
        static nothrow_t const nothrow;
    }
    static void* operator new (size_t _Size, std::nothrow_t const&) {
        return std::malloc(_Size);              /* see: opnew.cpp/rtlibrary */
            // Note: OpenWatcom (1.9 or 2.0) wont throw std::bad_alloc.
    }
    static void* operator new [] (size_t _Size, std::nothrow_t const&) {
        return std::malloc(_Size);
    }
#pragma warning (pop)

extern "C" _WCRTLINK extern int sprintf_s( char * __s, size_t __n, const char * __format, ... );
extern "C" _WCRTLINK extern int vsprintf_s( char * __s, size_t __n, const char * __format, va_list __arg );
#endif  // __cplusplus
#endif  // __WATCOMC__

#endif  //WSSUPPORT_H_INCLUDED

//end
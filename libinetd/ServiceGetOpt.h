#pragma once
#ifndef LIBINETD_GETOPT_H_INCLUDED
#define LIBINETD_GETOPT_H_INCLUDED
/* -*- mode: c; indent-width: 8; -*- */
/*
 * Command line options.
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
 * ==end==
 */

/*
 *  Unix like getopt_long()
 *
 *  Diagnostic Messages:
 *
 *      o short
 *
 *          "%s: unknown option -- %c"
 *          "%s: option requires an argument -- %c"
 *
 *      o long
 *
 *          %s: unknown option -- %s"
 *          %s: option requires an argument -- %s"
 *          %s: option doesn't take an argument -- %s"
 *          %s: ambiguous option -- %s"
 */

#include "inetd_namespace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cassert>

#include <iostream>
#include <string>

#if defined(__WATCOMC__) && (__WATCOMC__ <= 1300)
// not visible under __cplusplus
extern "C" _WCRTLINK extern int sprintf_s( char * __s, size_t __n, const char * __format, ... );
#endif

namespace inetd {
class Getopt {
	INETD_DELETED_FUNCTION(Getopt(const class Getopt &))
	INETD_DELETED_FUNCTION(Getopt& operator=(const Getopt &))

public:
	enum argument_flag {
		argument_none = 1,
		argument_required,
		argument_optional,
	};

	enum error_code {
		OPT_UNKNOWN = 1,
		OPT_ARGUMENT_REQUIRED,
		OPT_NO_ARGUMENT,
		OPT_AMBIGUOUS
	};

	struct Option {
		const char *name;		/* name of long option */
		/*
		 *  one of no_argument, required_argument, and optional_argument:
		 *  whether option takes an argument
		 */
		enum argument_flag has_arg;
		int *flag;			/* if not NULL, set *flag to val when option found */
		int val;			/* if flag not NULL, value to set *flag to; else return value */
	};

public:
	Getopt(const char *ostr, const char *progname = NULL) :
		ostr_(ostr), long_options_(NULL),
		progname_(progname),
		place_(NULL), optarg_(NULL), optind_(1), longindex_(-1), optopt_(0), optret_(0),
		optmissing_('?'), opterr_(1)
	{
		assert(ostr_);
		if (ostr_ && ':' == ostr[0]) {
			optmissing_ = ':';
			opterr_ = 0;
			++ostr;
		}
	}

	Getopt(const char *ostr, struct Option *long_options, const char *progname = 0) :
		ostr_(ostr), long_options_(long_options),
		progname_(progname),
		place_(NULL), optarg_(NULL), optind_(1), longindex_(-1), optopt_(0), optret_(0),
		optmissing_('?'), opterr_(1)
	{
		assert(ostr_);
		if (ostr_ && ':' == ostr_[0]) {
			optmissing_ = ':';
			opterr_ = 0;
			++ostr_;
		}
	}

	const char *progname() const
	{
		return progname_;
	}

	int optret() const
	{
		return optret_;
	}

	int optind() const
	{
		return optind_;
	}

	int optopt() const
	{
		return optopt_;
	}

	const char *optarg() const
	{
		return optarg_;
	}

	void opterr(int flag)
	{
		opterr_ = flag;
	}

	int opterr() const
	{
		return opterr_;
	}

	int longindex() const
	{
		return longindex_;
	}

	int shift(int nargc, const char * const *nargv)
	{
		return (optret_ = pop_argument(nargc, nargv, NULL));
	}

	int shift(int nargc, const char * const *nargv, std::string &msg)
	{
		return (optret_ = pop_argument(nargc, nargv, &msg));
	}

public:
	virtual void error_report(enum error_code code, const char *message)
	{
		(void) code;
		if (msg_) {
			msg_->assign(message);
		} else {
			std::cerr << message << std::endl;
		}
	}

private:
	int pop_argument(int nargc, const char * const *nargv, std::string *msg)
	{
		assert(nargc >= 1);
		assert(nargv);

		msg_ = msg;			// optional message destination
		longindex_ = -1;
		int ret = short_argument(nargc, nargv);
		if (-100 == ret) {
			if (! long_options_) {
				msg_ = NULL;
				return EOF;	// "--", we are done
			}
			ret = long_argument(nargc, nargv);
		}
		msg_ = NULL;
		return ret;
	}

	int short_argument(int nargc, const char * const *nargv)
	{
		const char *oli = NULL;

		if (NULL == place_ || !*place_) {
			if (1 == optind_ && !progname_) {
				progname_ = (NULL != (progname_ = strrchr(nargv[0], '/'))) ||
				(NULL != (progname_ = strrchr(nargv[0], '\\'))) ? progname_ + 1 : nargv[0];
			}

			if (NULL == ostr_ || nargc < 1) {
				return EOF;
			}

			if (optind_ >= nargc || *(place_ = nargv[optind_]) != '-' || !*++place_) {
				place_ = NULL;
				return EOF;
			}

			if (*place_ == '-') { // "--", we are done
				++optind_;
				return -100;
			}
		}

		if ((optopt_ = (int)*place_++) == (int)':' || NULL == (oli = strchr(ostr_, optopt_))) {
			if (!*place_) ++optind_;
			short_fatal(OPT_UNKNOWN, "unknown option");
			return (int)'?';
		}

		if (*++oli != ':') {
			optarg_ = NULL;
			if (!*place_) ++optind_;
		} else {
			if (*place_) {
				optarg_ = place_;

			} else if (nargc <= ++optind_) {
				short_fatal(OPT_ARGUMENT_REQUIRED, "option requires an argument");
				place_ = optarg_ = NULL;
				return optmissing_;

			} else {
				optarg_ = nargv[optind_];
			}

			place_ = NULL;
			++optind_;
		}
		return optopt_;
	}

	int
	long_argument(int nargc, const char * const *nargv)
	{
		const char *arg, *has_equal;
		int ambiguous = 0, match = -1, arglen, i;

		(void) nargc;

		arg = place_ + 1;
		if (*arg == '\0') {
			return EOF;
		}

		match = -1;
		ambiguous = 0;
		place_ = "";

		if ((has_equal = strchr(arg, '=')) != NULL) {
			arglen = (int)(has_equal - arg);
			++has_equal;
		} else {
			arglen = (int)strlen(arg);
		}

		for (i = 0; long_options_[i].name; ++i) {
			/* find matching long option */
			if (strncmp(arg, long_options_[i].name, arglen))
				continue;

			if (strlen(long_options_[i].name) == (unsigned)arglen) {
				match = i;
				ambiguous = 0;
				break;
			}

			/* partial match */
			if (match == -1)
				match = i;
			else ambiguous = 1;
		}

		if (ambiguous) {
			long_fatal(OPT_AMBIGUOUS, "ambiguous option", arg, arglen);
			optopt_ = 0;
			return (int)'?';
		}

		if (match != -1) {
			if (long_options_[match].has_arg == argument_none && has_equal) {
				long_fatal(OPT_NO_ARGUMENT, "option doesn't take an argument", arg, arglen);
				optopt_ = (NULL == long_options_[match].flag ? long_options_[match].val : 0);
				return optmissing_;
			}

			if (long_options_[match].has_arg == argument_required ||
			    long_options_[match].has_arg == argument_optional) {
				if (has_equal) {
					optarg_ = has_equal;

				} else if (long_options_[match].has_arg == argument_required) {
					// optional argument doesn't use next nargv
					optarg_ = nargv[optind_++];
				}
			}

			if ((long_options_[match].has_arg == argument_required) && (NULL == optarg_)) {
				// Missing argument; leading ':' indicates no error should be generated
				long_fatal(OPT_ARGUMENT_REQUIRED, "option requires an argument", arg);
				optopt_ = (NULL == long_options_[match].flag ? long_options_[match].val : 0);
				--optind_;
				return optmissing_;
			}

		} else {
			long_fatal(OPT_UNKNOWN, "unknown option", arg);
			optopt_ = 0;
			return (int)'?';
		}

		longindex_ = match;
		if (long_options_[match].flag) {
			*long_options_[match].flag = long_options_[match].val;
			return 0;
		}

		return optopt_ = long_options_[match].val;
	}

private:
	void
	short_fatal(enum error_code code, const char *msg)
	{
		if (!opterr_) return;

		char buffer[1024];
		(void) sprintf_s(buffer, sizeof(buffer), "%s: %s -- %c", progname(), msg, optopt_);
		error_report(code, buffer);
	}

	void
	long_fatal(enum error_code code, const char *msg, const char *arg, int arglen = -1)
	{
		if (!opterr_) return;

		char buffer[1024];
		if (arglen < 0) arglen = (int)strlen(arg);
		(void) sprintf_s(buffer, sizeof(buffer), "%s: %s -- %.*s", progname(), msg, arglen, arg);
		error_report(code, buffer);
	}

private:
	const char *ostr_;
	struct Option *long_options_;
	const char *progname_;
	const char *place_;
	const char *optarg_;
	std::string *msg_;
	int optind_;
	int longindex_;
	int optopt_;
	int optret_;
	int optmissing_;
	int opterr_;
};

}   //namspace inetd

#endif //LIBINETD_GETOPT_H_INCLUDED


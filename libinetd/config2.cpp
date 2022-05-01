/* -*- mode: c; indent-width: 8; -*- */
/*
 * Configuration
 * windows inetd service -- xinetd style.
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

#include <sysexits.h>
#include <syslog.h>
#include <unistd.h>

#include "inetd.h"
#include "config2.h"
#include "xinetd.h"

static struct servconfig *nextconfigent2(const struct configparams *params);

static xinetd::Parser *parser;


int
setconfig2(const char *path)
{
	std::ifstream stream(path);
	return setconfig2(stream, path);
}


int
setconfig2(std::istream &stream, const char *path)
{
	endconfig2();
	parser = new xinetd::Parser(stream, path);
	return (parser->good() ? 1 : 0);
}


const char *
setconfig2status(int *error_code)
{
	if (parser) {
		int t_error_code = 0;
		const char *val = parser->status(t_error_code);
		if (error_code) {
			*error_code = t_error_code;
		}
		return val;
	}
	return nullptr;
}


const char *
getconfigdef2(const char *key, char &op, unsigned idx)
{
	if (parser) {
		return parser->default(key, op, idx);
	}
	return nullptr;
}


void
endconfig2(void)
{
	delete parser;
	parser = nullptr;
}


struct servconfig *
getconfigent2(const struct configparams *params, int *ret)
{
	struct servconfig *sc = nullptr;
	int t_ret = EX_SOFTWARE;

	try {
		if (parser && nullptr == (sc = parser->next(params))) {
			t_ret = 0;
		}

	} catch (int exit_code) {
		t_ret = exit_code;

	} catch (std::exception &msg) {
		syslog(LOG_ERR, "config error : exception, %s", msg.what());

	} catch (...) {
		syslog(LOG_ERR, "config error : exception");
	}

	if (ret) *ret = t_ret;
	return sc;
}

//end

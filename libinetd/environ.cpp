/* -*- mode: c; indent-width: 8; -*- */
/*
 * windows inetd service - process environment.
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

#include <algorithm>
#include <cstdlib>


static size_t
make(const std::vector<inetd::String> &passenv, const std::vector<inetd::String> &setenv, unsigned &elements, char *vars[], char *buf)
{
	unsigned ecount = 0, esize = 0;
#if defined(_MSC_VER) && (_MSC_VER >= 1900)
	char **cenv = *__p__environ();
#else
	char **cenv = environ;
#endif

	for (const char *var; nullptr != (var = *cenv); ++cenv) {
		const char *sep = strchr(var, '=');
		if (!sep)
			continue;

		const size_t keylen = sep - var;
		if (! passenv.empty()) {
			if (std::find_if(passenv.begin(), passenv.end(), [&](const auto &elm) {
					    return (elm.length() == keylen && 0 == memcmp(var, elm.data(), keylen));
					}) == passenv.end()) {
				continue; // omitted from passenv; ignore
			}
		}

		if (std::find_if(setenv.begin(), setenv.end(), [&](const auto &elm) {
				    return (elm.length() > keylen && 0 == memcmp(var, elm.data(), keylen + 1 /*=*/));
				}) != setenv.end()) {
			continue; // within setenv list; ignore
		}

		const size_t varlen = strlen(var) + 1 /*nul*/;
		if (vars) {
			assert(ecount < elements);
			vars[ecount] = buf;
			memcpy(buf, var, varlen);
			buf += varlen;
		}
		esize += varlen;
		++ecount;
	}

	if (! setenv.empty()) {
		for (const auto &var: setenv) {
			const size_t varlen = var.length() + 1 /*nul*/;
			if (vars) {
				assert(ecount < elements);
				vars[ecount] = buf;
				memcpy(buf, var.c_str(), varlen);
				buf += varlen;
			}
			esize += varlen;
			++ecount;
		}
	}

	if (vars) {
		assert(ecount < elements);
		vars[ecount] = nullptr;
	}
	++ecount; // terminator

	elements = ecount;
	return esize;
}


static const char **
make(const std::vector<inetd::String> &passenv, const std::vector<inetd::String> &setenv)
{
	unsigned size, elements = 0;

	if (0 == (size = make(passenv, setenv, elements, nullptr, nullptr)))
		return nullptr; // no content

	const size_t vsize = sizeof(char *) * elements;

	if (void *mem = (void *)::malloc(vsize + size)) {
		make(passenv, setenv, elements, (char **)mem, (char *)mem + vsize);
		return (const char **)mem;
	}
	return nullptr;
}


/////////////////////////////////////////////////////////////////////////////////////////
//  environment

static inetd::CriticalSection env_lock;


environment::environment()
	: env_(nullptr)
{
}


environment::environment(const environment &rhs)
	: env_(nullptr)
{
	passenv_ = rhs.passenv_;
	setenv_ = rhs.setenv_;
}


environment&
environment::operator=(environment &&rhs)
{
	if (this != &rhs) {
		passenv_ = std::move(rhs.passenv_);
		setenv_ = std::move(rhs.setenv_);
		rhs.reset();
		reset();
	}
	return *this;
}


environment::~environment()
{
	clear();
}


environment::Collection&
environment::passenv()
{
	return passenv_;
}


environment::Collection&
environment::setenv()
{
	return setenv_;
}


const char **
environment::get() const
{
	if (!passenv_.empty() || !setenv_.empty()) {
		if (nullptr == env_) {
			inetd::CriticalSection::Guard guard(env_lock);
			if (nullptr == env_) {
				env_ = make(passenv_, setenv_);
			}
		}
	}
	return env_;
}


bool
environment::empty() const
{
	return (passenv_.empty() && setenv_.empty());
}


void
environment::clear()
{
	passenv_.clear();
	setenv_.clear();
	reset();
}


void
environment::reset()
{
	::free(env_);
	env_ = nullptr;
}

//env

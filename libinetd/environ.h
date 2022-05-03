#pragma once
/* -*- mode: c; indent-width: 8; -*- */
/*
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

#include <vector>

#include "SimpleString.h"

class environment {
	environment operator=(const environment &) = delete;

public:
	typedef std::vector<inetd::String> Collection;

	environment();
	environment(const environment &rhs);
	environment& operator=(environment &&rhs);
	~environment();

	const char **get() const;
	Collection& passenv();
	Collection& setenv();
	bool empty() const;
	void clear();
	void reset();

private:
	Collection passenv_;
	Collection setenv_;
	mutable const char **env_;
};

//end
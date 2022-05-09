#pragma once
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

#include <istream>

#include "config.h"

struct servdefaults;

int setconfig2(const char *path);
int setconfig2(std::istream &stream, const char *path);
const char *setconfig2status(int *error_code);
const char *getconfigdef2(const char *key, char &op, unsigned idx = 0);
struct servconfig *getconfigent2(const struct configparams *params, int *ret);
void endconfig2(void);

//end
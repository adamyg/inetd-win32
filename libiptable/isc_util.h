#pragma once
#ifndef ISC_UTIL_H_INCLUDED
#define ISC_UTIL_H_INCLUDED
/*
 * Copyright (C) Internet Systems Consortium, Inc. ("ISC")
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, you can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * See the COPYRIGHT file distributed with this work for additional
 * information regarding copyright ownership.
 */

#include <sys/cdefs.h>

__BEGIN_DECLS

struct isc_prefix;

int isc_compare_eqprefix(const struct isc_prefix *a, const struct isc_prefix *b, unsigned int prefixlen);

__END_DECLS

#endif /*ISC_UTIL_H_INCLUDED*/

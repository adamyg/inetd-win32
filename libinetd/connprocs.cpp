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
 * ==end==
 */

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif

#include <exception>

#include "inetd.h"


struct connprocs::Guard : public inetd::SpinLock::Guard {
	Guard(connprocs &cps) : inetd::SpinLock::Guard(cps.cp_lock) { }
};


connprocs::connprocs(int maxperip)
{
	cp_procs.reserve(maxperip);
	cp_maxchild = maxperip;
}


bool
connprocs::resize(int maxperip /*bool trim*/)
{
	Guard spin_guard(*this);

	if (cp_maxchild == maxperip)
		return true;

//	const int numchild = procs.numchild();
//	if (trim && maxperip < numchild) {
//		for (int i = maxperip; i < numchild; ++i) {
//			if (struct procinfo *proc = procs.cp_procs[i]) {
//				assert(proc->pr_conn == conn);
//				proc->pr_conn = nullptr;
//				assert(nullptr != proc->pr_sep);
//			}
//		}
//		cp_procs.resize(maxperip);
//	}

	try {
		cp_procs.reserve(maxperip);
		cp_maxchild = maxperip;
		return true;
	} catch(...) { /*memory-error*/ }
	return false;
}


struct procinfo *
connprocs::newproc(struct conninfo *conn, int &maxchild)
{
	connprocs::Guard spin_guard(*this);

	if ((maxchild = cp_maxchild) > 0) {
		if (numchild() < maxchild) {
			if (struct procinfo *proc = new(std::nothrow) procinfo) {
				cp_procs.push_back(proc);
				proc->pr_conn = conn;
				return proc;
			}
			return (procinfo *)-1;
		}
		//overflow, ignore request
	}
	return nullptr;
}


bool
connprocs::unlink(struct procinfo *proc)
{
	connprocs::Guard spin_guard(*this);

	assert(proc->pr_conn != nullptr);
	for (int it = 0, end = numchild(); it < end; ++it) {
		if (cp_procs[it] == proc) {
			// remove element by reorging last
			if (end-- /*remove trail*/ && it != end) {
				cp_procs[it] = cp_procs[end];
			}
			cp_procs.resize(end);
			proc->pr_conn = nullptr;
			return true;
		}
	}
	assert(proc->pr_conn != nullptr);
	return false;
}


void
connprocs::clear(struct conninfo *conn)
{
	connprocs::Guard spin_guard(*this);

	for (int it = 0, end = numchild(); it < end; ++it) {
		if (struct procinfo *proc = cp_procs[it]) {
			assert(proc->pr_conn == conn);
			proc->pr_conn = nullptr;
		}
	}
	cp_procs.clear();
}

#pragma once
/* -*- mode: c; indent-width: 8; -*- */
/*
 * inetd::SimpleLock
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

#include <atomic>

namespace inetd {
struct SpinLock {
	SpinLock(const SpinLock &) = delete;
	SpinLock& operator=(const SpinLock &) = delete;

	struct Guard {
		Guard(SpinLock &lock) : lock_(lock) {
			while (lock.flag_.test_and_set(std::memory_order_acquire))
				; /* acquire-lock, spin */
		}
		~Guard() {
			lock_.flag_.clear(std::memory_order_release);
		}
		SpinLock &lock_;
	};

	SpinLock() : flag_{ATOMIC_FLAG_INIT} {
	}

	std::atomic_flag flag_;
};

struct CriticalSection {
	CriticalSection(const CriticalSection &) = delete;
	CriticalSection& operator=(const CriticalSection &) = delete;

	class Guard {
		Guard(const Guard &) = delete;
		Guard& operator=(const Guard &) = delete;
	public:
		Guard(CriticalSection &lock) : lock_(lock) {
			::EnterCriticalSection(&lock.cs_);
		}
		~Guard() {
			::LeaveCriticalSection(&lock_.cs_);
		}
	private:
		CriticalSection &lock_;
	};

	CriticalSection() {
		::InitializeCriticalSectionAndSpinCount(&cs_, 0x00000400);
	}

	~CriticalSection() {
		::DeleteCriticalSection(&cs_);
	}

	CRITICAL_SECTION cs_;
};

}   //namespace inetd

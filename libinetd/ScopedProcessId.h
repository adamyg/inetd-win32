#pragma once
/* -*- mode: c; indent-width: 8; -*- */
/*
 * RAII processid
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

#include <cassert>

#include "WindowStd.h"

namespace inetd {
class ScopedProcessId {
	ScopedProcessId(const ScopedProcessId &) = delete;
	ScopedProcessId& operator=(const ScopedProcessId &) = delete;

private:
	static bool dup_emplace(HANDLE &handle)
	{
		HANDLE current_process = ::GetCurrentProcess(), result = nullptr;
		if (::DuplicateHandle(current_process, handle, current_process,
				&result, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
			handle = result;
			return true;
		}
		return false;
	}

public:
	ScopedProcessId()
	{
		(void) memset(&pid_, 0, sizeof(pid_));
	}

	~ScopedProcessId()
	{
		Close();
	}

	ScopedProcessId& operator=(ScopedProcessId&& other)
	{
		Set(other.Take());
		return *this;
	}

	void Set(const PROCESS_INFORMATION &other)
	{
		if (&pid_ != &other) {
			Close();
			pid_ = other;
		}
	}

	bool Clone(const ScopedProcessId &other)
	{
		if (this != &other) {
			PROCESS_INFORMATION temp = other.pid_;
			if (dup_emplace(temp.hProcess)) {
				if (dup_emplace(temp.hThread)) {
					Close();
					pid_ = temp;
					return true;
				}
				DWORD last_error = ::GetLastError();
				::CloseHandle(temp.hProcess);
				::SetLastError(last_error);
			}
		}
		return false;
	}

	operator PROCESS_INFORMATION *()
	{
		return &pid_;
	}

	PROCESS_INFORMATION Take()
	{
		PROCESS_INFORMATION temp = pid_;
		(void) memset(&pid_, 0, sizeof(pid_));
		return temp;
	}

	HANDLE take_process_handle()
	{
		HANDLE temp = pid_.hProcess;
		pid_.hProcess = nullptr;
		return temp;
	}

	HANDLE process_handle() const
	{
		return pid_.hProcess;
	}

	DWORD process_id() const
	{
		return pid_.dwProcessId;
	}

	int pid() const
	{
		assert(((int)process_id()) > 0);
		return (int)process_id();
	}

	HANDLE take_process_thread()
	{
		HANDLE temp = pid_.hThread;
		pid_.hThread = nullptr;
		return temp;
	}

	HANDLE process_thread() const
	{
		return pid_.hThread;
	}

	DWORD process_tid() const
	{
		return pid_.dwThreadId;
	}

	bool IsValid() const
	{
		return (pid_.hProcess != nullptr && pid_.hProcess != INVALID_HANDLE_VALUE);
	}

	void Close()
	{
		if (pid_.hThread)  ::CloseHandle(pid_.hThread);
		if (pid_.hProcess) ::CloseHandle(pid_.hProcess);
		pid_.hThread = pid_.hProcess = 0;
		pid_.dwProcessId = pid_.dwThreadId = 0;
	}

private:
	PROCESS_INFORMATION pid_;
};

};  //namespace inetd

#pragma once
/* -*- mode: c; indent-width: 8; -*- */
/*
 * RAII handles
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

#include "inetd_namespace.h"

#include <cassert>

#include "WindowStd.h"

namespace inetd {
class ScopedHandle {
	INETD_DELETED_FUNCTION(ScopedHandle(const ScopedHandle &))
	INETD_DELETED_FUNCTION(ScopedHandle& operator=(const ScopedHandle &))

public:
	ScopedHandle(HANDLE handle) : handle_(handle)
	{
	}

	ScopedHandle() : handle_(nullptr)
	{
	}

	~ScopedHandle()
	{
		Close();
	}

#if defined(_MSC_VER)
	ScopedHandle& operator=(ScopedHandle&& other)
	{
		Set(other.Take());
		return *this;
	}
#else
	ScopedHandle& move_operator(ScopedHandle& other)
	{
		Set(other.Take());
		return *this;
	}
#endif

	void Set(HANDLE handle)
	{
		if (handle_ != handle) {
			Close();
			handle_ = handle;
		}
	}

	HANDLE Get() const
	{
		return handle_;
	}

	HANDLE Take()
	{
		HANDLE handle = handle_;
		handle_ = nullptr;
		return handle;
	}

	operator HANDLE() const
	{
		return handle_;
	}

	bool IsValid() const
	{
		return (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE);
	}

	void Close()
	{
		if (IsValid()) {
			::CloseHandle(handle_), handle_ = nullptr;
		}
	}

private:
	HANDLE handle_;
};

};  //namespace inetd

//end

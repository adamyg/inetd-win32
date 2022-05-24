#pragma once
/*
 * inetd::SimpleString - RAII character pointer interface.
 * windows inetd service.
 *
 * Copyright (c) 2022, Adam Young.
 * All rights reserved.
 *
 * This file is part of inetd-win32.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <new>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4127)		// conditional expression is constant
#pragma push_macro("new")
#undef  new
#endif

namespace inetd {

class String
{
	const char *buffer_;

public:
	String() : buffer_(nullptr)
	{
	}

	String(const char *buffer) : buffer_(nullptr)
	{
		assign(buffer);
	}

	String(const char *buffer, unsigned buflen) : buffer_(nullptr)
	{
		assign(buffer, buflen);
	}

	String(const String &other) : buffer_(nullptr)
	{
		assign(other.buffer_, other.length());
	}

#if defined(__MSC_VER)
	String(String &&other)
	{
		buffer_ = other.buffer_;
		other.buffer_ = nullptr;
	}
#endif        

	~String()
	{
		clear();
	}

	String& operator=(const String& other)
	{
		if (this != &other) {
			assign(other.buffer_, other.length());
		}
		return *this;
	}

#if defined(__MSC_VER)
	String& operator=(String&& other)
	{
		if (this != &other) {
			::free((void *)buffer_);
			buffer_ = other.buffer_;
			other.buffer_ = nullptr;
		}
		return *this;
	}
#endif

	String& operator=(const char *other)
	{
		assign(other, strlen(other));
		return *this;
	}

	String& operator+=(const String& other)
	{
		append(other.buffer_, other.length());
		return *this;
	}

	String& operator+=(const char *buffer)
	{
		append(buffer);
		return *this;
	}

	friend String& operator+(String a, const String &b)
	{
		a += b;
		return a;
	}

	bool operator==(const String& other) const
	{
		return ((buffer_ == other.buffer_) ||
			(buffer_ && other.buffer_ && 0 == ::strcmp(buffer_, other.buffer_)));
	}

	bool operator==(const char *other) const
	{
		return ((buffer_ == other) ||
			(buffer_ && other && 0 == ::strcmp(buffer_, other)));
	}

	bool operator!=(const String& other) const
	{
		return !(*this == other);
	}

	bool operator!=(const char *other) const
	{
		return !(*this == other);
	}

	operator const char *() const // implicit conversion
	{
		return (buffer_ ? buffer_ : "");
	}

	const char& operator [](unsigned pos) const
	{
		assert(pos < length());
		return buffer_[pos];
	}

	char& operator [](unsigned pos)
	{
		assert(pos < length());
		return const_cast<char &>(buffer_[pos]);
	}

	char *alloc(unsigned buflen)
	{
		char *t_buffer = (char *) ::calloc(buflen + 1 /*nul*/, 1);
		if (nullptr == t_buffer) {
			throw std::bad_alloc();
		}

		clear();
		buffer_ = t_buffer;
		return t_buffer;
	}

	char *realloc(unsigned buflen)
	{
		if (nullptr == buffer_) {
			return alloc(buflen);
		}

		char *t_buffer = (char *) ::realloc((void *)buffer_, ++buflen /*nul*/);
		if (nullptr == t_buffer) {
			throw std::bad_alloc();
		}

		const unsigned olength = length();
		memset(t_buffer + olength, 0, buflen - olength);
		buffer_ = t_buffer;
		return t_buffer;
	}

	void assign(const char *buffer, unsigned buflen)
	{
		if (nullptr == buffer) {
			clear();
			return;
		}

		char *t_buffer = (char *) ::malloc(buflen + 1 /*nul*/);
		if (nullptr == t_buffer) {
			throw std::bad_alloc();
		}
		memcpy(t_buffer, buffer, buflen);
		t_buffer[buflen] = 0;

		::free((void *)buffer_);
		buffer_ = t_buffer;
	}

	void assign(const char *buffer)
	{
		assign(buffer, strlen(buffer));
	}

	void append(const char *buffer, unsigned buflen)
	{
		const unsigned olength = length(), nlength = olength + buflen;
		char *t_buffer = (char *) ::malloc(nlength + 1 /*nul*/);
		if (nullptr == t_buffer) {
			throw std::bad_alloc();
		}

		memcpy(t_buffer, buffer_, olength);
		memcpy(t_buffer + olength, buffer, buflen);
		t_buffer[nlength] = 0;

		::free((void *)buffer_);
		buffer_ = t_buffer;
	}

	void append(const char *buffer)
	{
		append(buffer, strlen(buffer));
	}

	void clear()
	{
		::free((void *)buffer_);
		buffer_ = nullptr;
	}

	const char *c_str() const
	{ //always a non null value; variation from std::string
		return buffer_?buffer_:"";
	}

	const char *data() const
	{ //underlying buffer
		return buffer_;
	}

	unsigned length() const
	{
		return (buffer_ ? strlen(buffer_) : 0);
	}

	bool is_null() const
	{
		return (nullptr == buffer_);
	}

	bool empty() const
	{
		return (nullptr == buffer_ || !*buffer_);
	}
};

#if defined(_MSC_VER)
#pragma pop_macro("new")
#pragma warning(pop)
#endif

} //namespace inetd


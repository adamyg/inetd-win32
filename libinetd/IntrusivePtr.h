#pragma once
/* -*- mode: c; indent-width: 8; -*- */
/*
 * inetd::instrusive_ptr
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

 /*
  * Intrusive pointer container.
  *
  * Example usage:
  *
  *     struct rb_node : public inetd::intrusive::PtrMemberHook<rb_node> {
  *             static void intrusive_deleter(struct rb_node *node) {
  *                     delete node;
  *             }
  *     };
  *     typedef inetd::intrusive_ptr<rb_node> RBPtr;
  *
  */

#include <type_traits>
#include <cassert>
#include <atomic>

namespace inetd {
namespace intrusive {

template<class T>
struct PtrMemberHook {
	PtrMemberHook(const PtrMemberHook &) = delete;
	PtrMemberHook& operator=(const PtrMemberHook &) = delete;

	typedef T element_type;
	constexpr PtrMemberHook() : intrusive_ptr_references_(0) {
	}

	virtual ~PtrMemberHook() {
		assert(0 == intrusive_ptr_references_);
	}

	static void intrusive_ptr_add_ref(element_type *px) {
		assert(px);
		++(px->intrusive_ptr_references_);
	}

	static void intrusive_ptr_release(element_type *px) {
		assert(px);
		if (0 == --(px->intrusive_ptr_references_)) {
			/*if constexpr (std::enable_if<std::is_function<decltype(element_type::intrusive_deleter)>::value) {
				element_type::intrusive_deleter(p);
			} else {
				delete p;
			}*/
			element_type::intrusive_deleter(px);
		}
	}

	static unsigned intrusive_ptr_count(const element_type *px) {
		return px->intrusive_ptr_references_;
	}

	std::atomic<unsigned> intrusive_ptr_references_;
};

}   //namespace intrusive

template<class T>
class instrusive_ptr {
private:
	typedef T element_type;
	typedef intrusive::PtrMemberHook<element_type> hook_type;

public:
	constexpr instrusive_ptr() noexcept : px_(nullptr) { }

	instrusive_ptr(element_type *p, bool incref = true) : px_(p) {
		if (px_ && incref) hook_type::intrusive_ptr_add_ref(px_);
	}

	template<class U>
	instrusive_ptr(const instrusive_ptr<U> &rhs) noexcept : px_(rhs.get()) {
		if (px_) hook_type::intrusive_ptr_add_ref(px_);
	}

	instrusive_ptr(const instrusive_ptr &rhs) noexcept : px_(rhs.get()) {
		if (px_) hook_type::intrusive_ptr_add_ref(px_);
	}

	instrusive_ptr(instrusive_ptr &&rhs) noexcept : px_(rhs.px_) {
		rhs.px_ = nullptr;
	}

	~instrusive_ptr() {
		if (px_) hook_type::intrusive_ptr_release(px_);
	}

	template<class U>
	instrusive_ptr& operator=(const instrusive_ptr<U> &rhs) {
		instrusive_ptr(rhs).swap(*this);
		return *this;
	}

	instrusive_ptr& operator=(const instrusive_ptr &rhs) {
		instrusive_ptr(rhs).swap(*this);
		return *this;
	}

	instrusive_ptr& operator=(element_type *rhs) {
		instrusive_ptr(rhs).swap(*this);
		return *this;
	}

	void reset() {
		instrusive_ptr().swap(*this);
	}

	void reset(element_type *rhs) {
		instrusive_ptr(rhs).swap(*this);
	}

	void reset(element_type *rhs, bool incref) {
		instrusive_ptr(rhs, incref).swap(*this);
	}

	element_type* get() const noexcept {
		return px_;
	}

	element_type* detach() noexcept {
		element_type* ret = px_;
		px_ = 0;
		return ret;
	}

	element_type& operator*() const noexcept {
		assert(px_);
		return *px_;
	}

	element_type* operator->() const noexcept {
		assert(px_);
		return px_;
	}

	operator bool () {
		return (px_ != nullptr);
	}

	bool unique() const {
		return (1 == intrusive_ptr_count(px_));
	}

	unsigned use_count() const {
		return intrusive_ptr_count(px_);
	}

	void swap(instrusive_ptr &rhs) noexcept {
		element_type* p = px_;
		px_ = rhs.px_;
		rhs.px_ = p;
	}

private:
	element_type *px_;
};


namespace intrusive {
template<class T>
class enable_shared_from_this : public PtrMemberHook<T> {
public:
	instrusive_ptr<T>
	shared_from_this() {
		return instrusive_ptr<T>(static_cast<T*>(this));
	}
};
}   //intrusive

}   //inetd

//end

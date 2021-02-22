/* -*- mode: c; indent-width: 8; -*- */
/*
 * inetd::instrusive_ptr
 * windows inetd service.
 *
 * Copyright (c) 2020 - 2021, Adam Young.
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
  *     };
  *     typedef inetd::intrusive_ptr<rb_node> RBPtr;
  *
  */

#include <cassert>
#include <atomic>

namespace inetd {
namespace intrusive {

template<class T>
struct PtrMemberHook {
	typedef T element_type;
	constexpr PtrMemberHook() : references_(0) {
	}
	static void intrusive_ptr_add_ref(element_type *p) {
		assert(p);
		++(p->references_);
	}
	static void intrusive_ptr_release(element_type *p) {
		assert(p);
		if (0 == --(p->references)) {
			delete p;
		}
	}
private:
	std::atomic<unsigned> references_;
};

}   //namespace intrusive

template<class T>
class instrusive_ptr {
private:
	typedef T element_type;

public:
	constexpr instrusive_ptr() noexcept : px_(nullptr) { }

	instrusive_ptr(element_type *p, bool incref = true) : px_(p) {
		if (px_ && incref) intrusive_ptr_add_ref(px_);
	}

	template<class U>
	instrusive_ptr(const instrusive_ptr<U> &rhs) : px_(rhs.get()) {
		if (px_) intrusive_ptr_add_ref(px_);
	}

	instrusive_ptr(instrusive_ptr &&rhs) noexcept : px_(rhs.px_) {
		rhs.px_ = nullptr;
	}

	~instrusive_ptr() {
		if (px_) intrusive_ptr_release(px_);
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
		return px;
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

	void swap(instrusive_ptr &rhs) noexcept {
		element_type* p = px_;
		px_ = rhs.px_;
		rhs.px_ = p;
	}

private:
	element_type px_;

};

}   //inetd

//end

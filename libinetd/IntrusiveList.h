#pragma once
/* -*- mode: c; indent-width: 8; -*- */
/*
 * inetd::Instrusive::List
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
 * ==
 */

#include <sys/queue.h>

#include <cassert>

#include "SimpleLock.h"

namespace inetd {
namespace Intrusive {
template <typename Member>
struct MemberHook {
	struct Collection {
		Collection() {
			reset();
		}
		inline void reset() {
			LIST_INIT(&head);
			count = 0;
		}
		inline bool empty() const {
			return LIST_EMPTY(&head);
		}
		inline MemberHook *top() {
			return LIST_FIRST(&head);
		}
		inline unsigned push_front(Member *member, MemberHook *hook) {
			LIST_INSERT_HEAD(&head, hook, node_);
			hook->owner_  = this;
			hook->member_ = member;
			return ++count;
		}
		inline unsigned remove(MemberHook *hook) {
			hook->owner_  = nullptr;
			hook->member_ = nullptr;
			LIST_REMOVE(hook, node_);
			return --count;
		}
		inetd::CriticalSection cs_;
		_LIST_HEAD(, MemberHook, ) head;
		unsigned count;
	};

	MemberHook() : node_{}, member_(nullptr) {
	}
	_LIST_ENTRY(MemberHook, ) node_;
	Collection *owner_;
	Member *member_;
};

template <typename Member, typename Hook, Hook Member::* PtrToMemberHook>
struct List {
	List(const List &) = delete;
	List& operator=(const List &) = delete;

public:
	class Guard {
		Guard(const Guard &) = delete;
		Guard& operator=(const Guard &) = delete;
	public:
		Guard(typename Hook::Collection &collection) :
				guard_(collection.cs_) {
		}
	private:
		inetd::CriticalSection::Guard guard_;
	};
	friend class Guard;

private:
	typedef Hook MemberHook;

	static inline MemberHook *
	member_hook_raw(Member *member) {
		return &((member)->*(PtrToMemberHook));
	}

	static inline MemberHook *
	member_hook_unassigned(Member *member) {
		assert(member);
		assert((member->*PtrToMemberHook).member_ == nullptr);
		return &((member)->*(PtrToMemberHook));
	}

	static inline MemberHook *
	member_hook_assigned(Member *member) {
		assert(member);
		assert((member->*PtrToMemberHook).member_ == member);
		return &((member)->*(PtrToMemberHook));
	}

	static inline Member *
	hook_member(MemberHook *hook) {
		constexpr size_t hook_offset = offsetof(Member, *PtrToMemberHook);
		assert(hook);
		assert(hook->member_ == (void *)((const char *)hook - hook_offset));
		return static_cast<Member *>(hook->member_);
	}

public:
	List() { }

	~List() {
		assert(empty());
		assert(0 == count());
	}

	bool empty() const {
		return collection_.empty();
	}

	int count() const {
		return collection_.count;
	}

	Member *top() {
		if (MemberHook *hook = collection_.top()) {
			return hook_member(hook);
		}
		return nullptr;
	}

	unsigned push_front_r(Member *member) {
		Guard guard(collection_);
		push_front(member);
		return count();
	}

	void push_front(Member *member) {
#if defined(_DEBUG) && !defined(NDEBUG)
		assert(! exists(member));
#endif
		collection_.push_front(member, member_hook_unassigned(member));
	}

	unsigned push_back_r(Member *member) {
		Guard guard(collection_);
		push_back(member);
		return count();
	}

	void push_back(Member *member) {
#if defined(_DEBUG) && !defined(NDEBUG)
		assert(! exists(member));
#endif
		collection_.push_back(member, member_hook_unassigned(member));
	}

	bool exists(Member *member) const {
		MemberHook *hook = member_hook_raw(member), *existing = nullptr;
		LIST_FOREACH(existing, &collection_.head, node_) {
			if (existing == hook) {
				return true;
			}
		}
		return false;
	}

	unsigned remove_r(Member *member) {
		Guard guard(collection_);
		return remove(member);
	}

	unsigned remove(Member *member) {
#if defined(_DEBUG) && !defined(NDEBUG)
		assert(exists(member));
#endif
		return collection_.remove(member_hook_assigned(member));
	}

	static void remove_self_r(Member *member) {
		MemberHook *hook = member_hook_assigned(member);
		Hook::Collection *collection = hook->owner_;
		Guard guard(*collection);
		collection->remove(hook);
	}

	static void remove_self(Member *member) {
		MemberHook *hook = member_hook_assigned(member);
		Hook::Collection *collection = hook->owner_;
		collection->remove(hook);
	}

	void reset_r() {
		Guard guard(collection_);
		reset(member);
	}

	void reset() {
		collection_.reset();
	}

	template<class Functor, class... TParameter>
	int foreach_r(Functor functor, TParameter... params) {
		MemberHook *hook = nullptr;
		Guard guard(collection_);
		LIST_FOREACH(hook, &collection_.head, node_) {
			if (int ret = functor(hook_member(hook), params...)) {
				return ret;
			}
		}
		return 0;
	}

	template<class Functor, class... TParameter>
	int foreach(Functor functor, TParameter... params) {
		MemberHook *hook = nullptr;
		LIST_FOREACH(hook, &collection_.head, node_) {
			if (int ret = functor(hook_member(hook), params...)) {
				return ret;
			}
		}
		return 0;
	}

	template<class Functor, class... TParameter>
	int foreach_term_r(Functor functor, TParameter... params) {
		MemberHook *hook = nullptr;
		Guard guard(collection_);
		LIST_FOREACH(hook, &collection_.head, node_) {
			if (int ret = functor(hook_member(hook), params...)) {
				return ret;
			}
		}
		return functor(static_cast<Member *>(nullptr), params...);
	}

	template<class Functor, class... TParameter>
	int foreach_term(Functor functor, TParameter... params) {
		MemberHook *hook = nullptr;
		LIST_FOREACH(hook, &collection_.head, node_) {
			if (int ret = functor(hook_member(hook), params...)) {
				return ret;
			}
		}
		return functor(static_cast<Member *>(nullptr), params...);
	}

	template<class Functor, class... TParameter>
	int foreach_safe_r(Functor functor, TParameter... params) {
		MemberHook *hook, *t_hook = nullptr;
		Guard guard(collection_);
		LIST_FOREACH_SAFE(hook, &collection_.head, node_, t_hook) {
			if (int ret = functor(hook_member(hook), params...)) {
				return ret;
			}
		}
		return 0;
	}

	template<class Functor, class... TParameter>
	int foreach_safe(Functor functor, TParameter... params) {
		MemberHook *hook, *t_hook = nullptr;
		LIST_FOREACH_SAFE(hook, &collection_.head, node_, t_hook) {
			if (int ret = functor(hook_member(hook), params...)) {
				return ret;
			}
		}
		return 0;
	}

	template<class Functor, class... TParameter>
	int foreach_term_safe_r(Functor functor, TParameter... params) {
		MemberHook *hook, *t_hook = nullptr;
		Guard guard(collection_);
		LIST_FOREACH_SAFE(hook, &collection_.head, node_, t_hook) {
			if (int ret = functor(hook_member(hook), params...)) {
				return ret;
			}
		}
		return functor(static_cast<Member *>(nullptr), params...);
	}

	template<class Functor, class... TParameter>
	int foreach_term_safe(Functor functor, TParameter... params) {
		MemberHook *hook, *t_hook = nullptr;
		LIST_FOREACH_SAFE(hook, &collection_.head, node_, t_hook) {
			if (int ret = functor(hook_member(hook), params...)) {
				return ret;
			}
		}
		return functor(static_cast<Member *>(nullptr), params...);
	}

	template<class Functor, class... TParameter>
	void drain_r(Functor functor, TParameter... params) {
		Guard guard(collection_);
		while (! empty()) {
			Member *member = top();
			remove(member);
			functor(member);
		}
		assert(0 == count());
	}

	template<class Functor, class... TParameter>
	void drain(Functor functor, TParameter... params) {
		while (! empty()) {
			Member *member = top();
			remove(member);
			functor(member, params...);
		}
		assert(0 == count());
	}

private:
	typename Hook::Collection collection_;
};

}   //Intrusive
};  //inetd

//end

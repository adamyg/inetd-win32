#pragma once
/* -*- mode: c; indent-width: 8; -*- */
/*
 * inetd::instrusive_list
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

 /* 
  * Intrusive TAILQ and LIST based containers
  *
  * Example usage:
  *
  *     struct tailq_node {
  *         inetd::Intrusive::TailMemberHook<tailq_node> link_;
  *         int other_members_;
  *     };
  *     typedef inetd::intrusive_list<tailq_node, inetd::Intrusive::TailMemberHook<tailq_node>, &tailq_node::link_> MYTailq;
  *
  *
  *     struct list_node {
  *         inetd::Intrusive::ListMemberHook<list_node> link_;
  *         int other_members_;
  *     };
  *     typedef inetd::intrusive_list<list_node, inetd::Intrusive::ListMemberHook<list_node>, &list_node::link_> MYList;
  *
  */

#include <sys/queue.h>

#include <utility>
#include <cassert>

#include "SimpleLock.h"

namespace inetd {

/////////////////////////////////////////////////////////////////////////////////////////
//	List collection

#if defined(_DEBUG)
#define assert_value(...) __VA_ARGS__
#else
#define assert_value(...)
#endif

namespace Intrusive {
template <typename Member>
struct ListMemberHook {
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
		inline ListMemberHook *front() {
			return LIST_FIRST(&head);
		}
		inline unsigned push_front(Member *member, ListMemberHook *hook) {
			LIST_INSERT_HEAD(&head, hook, node_);
			hook->collection_ = this;
			assert_value(hook->member_ = member;)
			return ++count;
		}
		inline bool exists(ListMemberHook *hook) const {
			ListMemberHook *existing;
			LIST_FOREACH(existing, &head, node_) {
				if (existing == hook) {
					return true;
				}
			}
			return false;
		}
		template<class Parent, class Functor, class... TParameter>
		int foreach(Parent &parent, Functor functor, TParameter... params) {
			ListMemberHook *hook;
			LIST_FOREACH(hook, &head, node_) {
				if (int ret = functor(Parent::hook_member(hook), std::forward<TParameter>(params)...)) {
					return ret;
				}
			}
			return 0;
		}
		template<class Parent, class Functor, class... TParameter>
		int foreach_safe(Parent &parent, Functor functor, TParameter... params) {
			ListMemberHook *hook, *t_hook;
			LIST_FOREACH_SAFE(hook, &head, node_, t_hook) {
				if (int ret = functor(Parent::hook_member(hook), std::forward<TParameter>(params)...)) {
					return ret;
				}
			}
			return 0;
		}
		inline ListMemberHook *next(ListMemberHook *hook) {
			return LIST_NEXT(hook, node_);
		}
		inline unsigned remove(ListMemberHook *hook) {
			hook->collection_ = nullptr;
			assert_value(hook->member_ = nullptr;)
			LIST_REMOVE(hook, node_);
			return --count;
		}
		inetd::CriticalSection cs;
		LIST_HEAD(, ListMemberHook) head;
		unsigned count;
	};

	ListMemberHook() : node_{}, collection_(nullptr) assert_value(, member_(nullptr)) {
	}

        bool is_hooked() const { 
                return (collection_ != nullptr);
        }

	LIST_ENTRY(ListMemberHook) node_;
	Collection *collection_;
	assert_value(Member *member_;)
};
};  //namespace intrusive


/////////////////////////////////////////////////////////////////////////////////////////
//	Tail Queue collection

namespace Intrusive {
template <typename Member>
struct TailMemberHook {
	struct Collection {
		Collection() {
			reset();
		}
		inline void reset() {
			TAILQ_INIT(&head);
			count = 0;
		}
		inline bool empty() const {
			return TAILQ_EMPTY(&head);
		}
		inline TailMemberHook *front() {
			return TAILQ_FIRST(&head);
		}
		inline TailMemberHook *back() {
			return TAILQ_LAST(&head, TailHead);
		}
		inline unsigned push_front(Member *member, TailMemberHook *hook) {
			TAILQ_INSERT_HEAD(&head, hook, node_);
			hook->collection_ = this;
			assert_value(hook->member_ = member;)
			return ++count;
		}
		inline unsigned push_back(Member *member, TailMemberHook *hook) {
			TAILQ_INSERT_TAIL(&head, hook, node_);
			hook->collection_ = this;
			assert_value(hook->member_ = member);
			return ++count;
		}
		inline bool exists(TailMemberHook *hook) const {
			TailMemberHook *existing;
			TAILQ_FOREACH(existing, &head, node_) {
				if (existing == hook) {
					return true;
				}
			}
			return false;
		}
		template<class Parent, class Functor, class... TParameter>
		int foreach(Parent &parent, Functor functor, TParameter... params) {
			TailMemberHook *hook;
			TAILQ_FOREACH(hook, &head, node_) {
				if (int ret = functor(Parent::hook_member(hook), std::forward<TParameter>(params)...)) {
					return ret;
				}
			}
			return 0;
		}
		template<class Parent, class Functor, class... TParameter>
		int foreach_safe(Parent &parent, Functor functor, TParameter... params) {
			TailMemberHook *hook, *t_hook;
			TAILQ_FOREACH_SAFE(hook, &head, node_, t_hook) {
				if (int ret = functor(Parent::hook_member(hook), std::forward<TParameter>(params)...)) {
					return ret;
				}
			}
			return 0;
		}
		inline TailMemberHook *next(TailMemberHook *hook) {
			return TAILQ_NEXT(hook, node_);
		}
		inline unsigned remove(TailMemberHook *hook) {
			hook->collection_ = nullptr;
			assert_value(hook->member_ = nullptr;)
			TAILQ_REMOVE(&head, hook, node_);
			return --count;
		}
		inetd::CriticalSection cs;
		TAILQ_HEAD(TailHead, TailMemberHook) head;
		unsigned count;
	};

	TailMemberHook() : node_{}, collection_(nullptr) assert_value(, member_(nullptr)) {
	}

        bool is_hooked() const { 
                return (collection_ != nullptr);
        }

	TAILQ_ENTRY(TailMemberHook) node_;
	Collection *collection_;
	assert_value(Member *member_;)
};
};  //namespace intrusive


/////////////////////////////////////////////////////////////////////////////////////////
//	List container

template <typename Member, typename Hook, Hook Member::* PtrToMemberHook>
struct intrusive_list {
	intrusive_list(const intrusive_list &) = delete;
	intrusive_list operator=(const intrusive_list &) = delete;

public:
	typedef typename Hook::Collection Collection;

	class Guard {
		Guard(const Guard &) = delete;
		Guard& operator=(const Guard &) = delete;
	public:
		Guard(typename Hook::Collection &collection) :
				guard_(collection.cs) {
		}
	private:
		inetd::CriticalSection::Guard guard_;
	};
	friend class Guard;

public:
	typedef typename Hook::Collection Collection;
	typedef Hook MemberHook;

	struct iterator {
		using iterator_category = std::forward_iterator_tag;
		using difference_type = std::ptrdiff_t;
		using value_type = Member;
		using pointer = Member *;
		using reference = Member &;

		iterator(pointer ptr) : ptr_(ptr) { }

		reference operator*() const {
			assert(ptr_);
			return *ptr_;
		}
		pointer operator->() {
			assert(ptr_);
			return ptr_;
		}
		iterator& operator++() {
			if (pointer ptr = ptr_) {
				MemberHook *hook =
					ListContainer::member_hook_assigned(ptr);
				ptr = nullptr;
				if (nullptr != (hook = hook->collection_->next(hook))) {
					ptr = hook_member(hook);
				}
				ptr_ = ptr;
			}
			return *this;
		}
		iterator operator++(int) {
			iterator tmp(*this);
			++(*this);
			return tmp;
		}
		friend bool operator== (const iterator& a, const iterator& b) {
			return a.ptr_ == b.ptr_;
		};
		friend bool operator!= (const iterator& a, const iterator& b) {
			return a.ptr_ != b.ptr_;
		};
	private:
		pointer ptr_;
	};

private:
	static inline MemberHook *
	member_hook_naive(Member *member) {
		assert(member);
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

public:
	static inline Member *
	hook_member(MemberHook *hook) {
		constexpr size_t hook_offset = offsetof(Member, *PtrToMemberHook);
		assert(hook);
		assert(hook->member_ == (void *)((const char *)hook - hook_offset));
		return (Member *)((const char *)hook - hook_offset);
	}

public:
	intrusive_list() { }

	~intrusive_list() {
		assert(empty());
		assert(0 == count());
	}

	bool empty() const {
		return collection_.empty();
	}

	int count() const {
		return collection_.count;
	}

	Member *front() {
		if (MemberHook *hook = collection_.front()) {
			return hook_member(hook);
		}
		return nullptr;
	}

	template<typename = std::enable_if<
		std::is_member_function_pointer<decltype(&Hook::Collection::back)>::value>>
	Member *back() {
		if (MemberHook *hook = collection_.back()) {
			return hook_member(hook);
		}
		return nullptr;
	}

	unsigned push_front_r(Member &member) {
		Guard guard(collection_);
		push_front(member);
		return count();
	}

	void push_front(Member &member) {
#if defined(_DEBUG) && !defined(NDEBUG)
		assert(! exists(&member));
#endif
		collection_.push_front(&member, member_hook_unassigned(&member));
	}

	template<typename = std::enable_if<
		std::is_member_function_pointer<decltype(&Hook::Collection::push_back)>::value>>
	unsigned push_back_r(Member &member) {
		Guard guard(collection_);
		push_back(member);
		return count();
	}

	template<typename = std::enable_if<
		std::is_member_function_pointer<decltype(&Hook::Collection::push_back)>::value>>
	void push_back(Member &member) {
#if defined(_DEBUG) && !defined(NDEBUG)
		assert(! exists(&member));
#endif
		collection_.push_back(&member, member_hook_unassigned(&member));
	}

	bool exists_r(Member *member) const {
		Guard guard(collection_);
		return collection_.exists(member_hook_naive(member));
	}

	bool exists(Member *member) const {
		return collection_.exists(member_hook_naive(member));
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
		Hook::Collection *collection = hook->collection_;
		Guard guard(*collection);
		collection->remove(hook);
	}

	static void remove_self(Member *member) {
		MemberHook *hook = member_hook_assigned(member);
		Hook::Collection *collection = hook->collection_;
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
		Guard guard(collection_);
		return collection_.foreach<>(*this, functor, std::forward<TParameter>(params)...);
	}

	template<class Functor, class... TParameter>
	int foreach(Functor functor, TParameter... params) {
		return collection_.foreach<>(*this, functor, std::forward<TParameter>(params)...);
	}

	template<class Functor, class... TParameter>
	int foreach_term_r(Functor functor, TParameter... params) {
		Guard guard(collection_);
		if (int ret = collection_.foreach<>(*this, functor, std::forward<TParameter>(params)...)) {
			return ret;
		}
		return functor(static_cast<Member *>(nullptr), std::forward<TParameter>(params)...);
	}

	template<class Functor, class... TParameter>
	int foreach_term(Functor functor, TParameter... params) {
		if (int ret = collection_.foreach<>(*this, functor, std::forward<TParameter>(params)...)) {
			return ret;
		}
		return functor(static_cast<Member *>(nullptr), std::forward<TParameter>(params)...);
	}

	template<class Functor, class... TParameter>
	int foreach_safe_r(Functor functor, TParameter... params) {
		Guard guard(collection_);
		return collection_.foreach_safe<>(*this, functor, std::forward<TParameter>(params)...);
	}

	template<class Functor, class... TParameter>
	int foreach_safe(Functor functor, TParameter... params) {
		return collection_.foreach_safe<>(*this, functor, std::forward<TParameter>(params)...);
	}

	template<class Functor, class... TParameter>
	int foreach_term_safe_r(Functor functor, TParameter... params) {
		Guard guard(collection_);
		if (int ret = collection_.foreach_safe<>(*this, functor, std::forward<TParameter>(params)...)) {
			return ret;
		}
		return functor(static_cast<Member *>(nullptr), std::forward<TParameter>(params)...);
	}

	template<class Functor, class... TParameter>
	int foreach_term_safe(Functor functor, TParameter... params) {
		if (int ret = collection_.foreach_safe<>(*this, functor, std::forward<TParameter>(params)...)) {
			return ret;
		}
		return functor(static_cast<Member *>(nullptr), std::forward<TParameter>(params)...);
	}

	template<class Functor, class... TParameter>
	void drain_r(Functor functor, TParameter... params) {
		Guard guard(collection_);
		while (! empty()) {
			Member *member = front();
			remove(member);
			functor(member, std::forward<TParameter>(params)...);
		}
		assert(0 == count());
	}

	template<class Functor, class... TParameter>
	void drain(Functor functor, TParameter... params) {
		while (! empty()) {
			Member *member = front();
			remove(member);
			functor(member, std::forward<TParameter>(params)...);
		}
		assert(0 == count());
	}

public:
	iterator begin() {
		MemberHook *hook = collection_.front();
		return iterator(hook ? hook_member(hook) : nullptr);
	}

	iterator end() {
		return iterator(nullptr);
	}

	iterator iterator_to(Member *member) {
		assert(member);
		assert(member->collection_ == &collection_);
		return iterator(member);
	}

	void erase(iterator &it) {
		assert(it.ptr_);
		assert(it.ptr_->collection_ == &collection_);
		if (Member *member = it.ptr_) {
			member->collection_->remove(ListContainer::member_hook_assigned(member));
			it.ptr_ = nullptr;
		}
	}

private:
	Collection collection_;
};

#undef  assert_value

}   //namespace inetd

//end

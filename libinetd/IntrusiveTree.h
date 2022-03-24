/* -*- mode: c; indent-width: 8; -*- */
/*
 * inetd::Instrusive_tree
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
  * Intrusive RB and SPLAY based containers; see <sys/tree.h>
  *
  * Example usage:
  *
  *     struct rb_node {
  *         inetd::Intrusive::TreeMemberHook<rb_node> link_;
  *         struct compare {
  *             int operator()(const rb_node *a, const rb_node *b) const {
  *                 return strcmp(a->key_, b->key_);
  *             }
  *         }; 
  *         const char *key_;
  *         int other_members_;
  *     };
  *     typedef inetd::intrusive_tree<rb_node, rb_node::compare, inetd::Intrusive::TreeMemberHook<rb_node>, &rb_node::link_> RBTree;
  *
  *
  *     struct splay_node {
  *         inetd::Intrusive::PlayMemberHook<splay_node> link_;
  *         struct compare {
  *             int operator()(const rb_node *a, const rb_node *b) const {
  *                 return strcmp(a->key_, b->key_);
  *             }
  *         }; 
  *         int other_members_;
  *     };
  *     typedef inetd::intrusive_tree<splay_node, splay_node::compare, inetd::Intrusive::PlayMemberHook<splay_node>, &splay_node::link_> SPLAYTree;
  *
  */

#undef   bind
#include <sys/tree.h>

#include <utility>
#include <functional>
#include <cassert>

#include "SimpleLock.h"

namespace inetd {

/////////////////////////////////////////////////////////////////////////////////////////
//	Tree node collection

namespace Intrusive {
template <typename Member>
struct TreeMemberHook {
	struct IComparator {
		virtual inline int operator()(const TreeMemberHook *a, const TreeMemberHook *b) const = 0;
			// XXX: an unfortunately messy interface, as the RB implementation requires the comparator
			//  to be bound at compile-time, creating an unavoidable forward reference.
			//  The virtual interface should be optimised away.
	};

	typedef RB_HEAD(rb, TreeMemberHook) MemberHead;
	struct Collection {
		Collection(IComparator &comparator) : comparator_(comparator) {
			reset();
		}

		inline void reset() {
			RB_INIT(&head_);
			count_ = 0;
		}

		inline bool empty() const {
			return RB_EMPTY(&head_);
		}

		inline TreeMemberHook *front() {
			return RB_MIN(rb, &head_);
		}

		inline TreeMemberHook *root() {
			return RB_ROOT(&head_);
		}

		inline TreeMemberHook *back() {
			return RB_MAX(rb, &head_);
		}

		inline unsigned insert(Member *member, TreeMemberHook *hook) {
			RB_INSERT(rb, &head_, hook);
			hook->collection_ = this;
			hook->member_ = member;
			return ++count_;
		}

		inline TreeMemberHook *find(TreeMemberHook *hook) const {
			return RB_FIND(rb, const_cast<MemberHead *>(&head_), hook);
		}

		inline bool exists(TreeMemberHook *hook) const {
			const TreeMemberHook *existing = RB_FIND(rb, const_cast<MemberHead *>(&head_), hook);
			return (existing == hook);
		}

		template<class Parent, class Functor, class... TParameter>
		int foreach(Parent &parent, Functor functor, TParameter... params) {
			TreeMemberHook *hook;
			RB_FOREACH(hook, rb, &head) {
				if (int ret = functor(Parent::hook_member(hook), std::forward<TParameter>(params)...)) {
					return ret;
				}
			}
			return 0;
		}

		inline TreeMemberHook *next(TreeMemberHook *hook) {
			return RB_NEXT(rb, &head_, hook);
		}

		inline TreeMemberHook *prev(TreeMemberHook *hook) {
			return RB_PREV(rb, &head_, hook);
		}

		inline unsigned remove(TreeMemberHook *hook) {
			hook->collection_ = nullptr;
			hook->member_ = nullptr;
			RB_REMOVE(rb, &head_, hook);
			return --count_;
		}

		inetd::CriticalSection& cs() {
			return cs_;
		}

		unsigned count() const {
			return count_;
		}

	private:
		RB_GENERATE(rb, TreeMemberHook, node_, comparator_);

	private:
		inetd::CriticalSection cs_;
		IComparator &comparator_;
		MemberHead head_;
		unsigned count_;
	};

	TreeMemberHook() : node_{}, collection_(nullptr), member_(nullptr) {
	}

	RB_ENTRY(TreeMemberHook) node_;
	Collection *collection_;
	Member *member_;
};
};  //namespace intrusive


/////////////////////////////////////////////////////////////////////////////////////////
//	Tree container

template <typename Member, typename Comparator, typename Hook, Hook Member::* PtrToMemberHook>
struct intrusive_tree {
	intrusive_tree(const intrusive_tree &) = delete;
	intrusive_tree operator=(const intrusive_tree &) = delete;

public:
	typedef typename Hook::IComparator IComparator;
	typedef typename Hook::Collection Collection;
	typedef Hook MemberHook;

	class Guard {
		Guard(const Guard &) = delete;
		Guard& operator=(const Guard &) = delete;
	public:
		Guard(Collection &collection) : guard_(collection.cs) { }
	private:
		inetd::CriticalSection::Guard guard_;
	};
	friend class Guard;

public:
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
					intrusive_tree::member_hook_assigned(ptr);
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
	member_hook_naive(const Member *member) {
		assert(member);
		return (MemberHook *)&((member)->*(PtrToMemberHook));
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

	static inline const Member *
	hook_member_unassigned(const MemberHook *hook) {
		constexpr size_t hook_offset = offsetof(Member, *PtrToMemberHook);
		return (const Member *)((const char *)hook - hook_offset);
	}

public:
	intrusive_tree() : collection_(icomparator_) { }

	~intrusive_tree() {
		assert(empty());
		assert(0 == count());
	}

	bool empty() const {
		return collection_.empty();
	}

	int count() const {
		return collection_.count();
	}

	Member *front() {
		if (MemberHook *hook = collection_.front()) {
			return hook_member(hook);
		}
		return nullptr;
	}

	template<typename = std::enable_if<
		std::is_member_function_pointer<decltype(&Hook::Collection::root)>::value>>
	Member *root() {
		if (MemberHook *hook = collection_.root()) {
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

	inline void insert(Member &member) {
		collection_.insert(&member, member_hook_unassigned(&member));
	}

	inline Member *find_r(const Member &member) const {
		Guard guard(collection_);
		return find(member);
	}

	inline Member *find(const Member &member) const {
		if (MemberHook *hook = collection_.find(member_hook_naive(&member))) {
			return hook_member(hook);
		}
		return nullptr;
	}

	inline bool exists_r(Member *member) const {
		Guard guard(collection_);
		return collection_.exists(member_hook_naive(member));
	}

	inline bool exists(Member *member) const {
		return collection_.exists(member_hook_naive(member));
	}

	inline unsigned remove_r(Member *member) {
		Guard guard(collection_);
		return remove(member);
	}

	inline unsigned remove(Member *member) {
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

	inline void reset_r() {
		Guard guard(collection_);
		reset(member);
	}

	inline void reset() {
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
			member->collection_->remove(intrusive_tree::member_hook_assigned(member));
			it.ptr_ = nullptr;
		}
	}

private:
	struct ComparatorImpl : public IComparator {
		inline int operator()(const MemberHook *a, const MemberHook *b) const override final {
			return comparator_(hook_member_unassigned(a), hook_member_unassigned(b));
		}
		Comparator comparator_;
	};
	ComparatorImpl icomparator_;
	Collection collection_;
};

}  //inetd

//end

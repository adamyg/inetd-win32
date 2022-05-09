#pragma once
/* -*- mode: c; indent-width: 8; -*- */
/*
 * inetd::ObjectPool
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

#include <sys/queue.h>

#include <cstdint>
#include <cstdlib>
#include <vector>
#include <utility>
#include <memory>
#include <new>

namespace inetd {

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4127)		// conditional expression is constant
#pragma push_macro("new")
#undef  new
#endif

namespace object_pool {
struct default_allocator {
	static void *
	malloc(const size_t nodesize, const size_t nodecount) {
		return ::calloc(nodesize, nodecount);
	}

	static void
	free(void *block) {
		::free(block);
	}
};
};  //namespace object_pool

template <typename T, size_t ALIGNMENT = 16, typename Allocator = object_pool::default_allocator>
class ObjectPool {
	ObjectPool(const ObjectPool &) = delete;
	ObjectPool& operator=(const ObjectPool &) = delete;

private:
	static const uint32_t ISSLAB = 0xAcedBa5e;
	static const uint32_t ISFREE = 0xDeadFee0;
	static const uint32_t ISUSED = 0xCafeF00d;

	static constexpr inline std::size_t
	align_up(std::size_t value, std::size_t alignment) {
		return (value + alignment - 1) & ~(alignment - 1);
	}

	struct Node {
		uint32_t type_;
		TAILQ_ENTRY(Node) node_;
		ObjectPool *owner_;
		union {
			void *slab_;
			unsigned bucketsize_;
		};
	};

	typedef TAILQ_HEAD(FreeList, Node) NodeList;

public:
	typedef T element_type;
	typedef Allocator allocator_type;

public:
	ObjectPool(const size_t firstbucket = 32, const size_t maxbucket = 0, const bool prime = false) :
			bucket_first_(firstbucket),
			bucket_max_(maxbucket ? maxbucket : (size_t)-1),
			bucket_next_(firstbucket),
			meta_size_(align_up(sizeof(Node), ALIGNMENT)),
			node_size_(align_up(sizeof(Node), ALIGNMENT) + align_up(sizeof(element_type), ALIGNMENT)),
			capacity_(0),
			avail_(0)
	{
		assert(bucket_first_);
		assert(bucket_first_ <= bucket_max_);
		assert(meta_size_ >= sizeof(Node));
		assert(node_size_ >= (sizeof(Node) + sizeof(element_type)));

		TAILQ_INIT(&slabs_);
		TAILQ_INIT(&free_list_);
		TAILQ_INIT(&used_list_);
		if (prime) {
			(free)((malloc)());
		}
	}

	~ObjectPool()
	{
		size_t total = 0, used = 0, bucketsize = bucket_first_;

		Node *slabnode, *t_slabnode;
		TAILQ_FOREACH_SAFE(slabnode, &slabs_, node_, t_slabnode) {

			assert(ISSLAB == slabnode->type_);
			assert(this == slabnode->owner_);
			assert(bucketsize == slabnode->bucketsize_);

			for (char *cursor = reinterpret_cast<char *>(slabnode) + node_size_,
					*end = cursor + (node_size_ * bucketsize); cursor < end; cursor += node_size_) {
				Node *node = static_cast<Node *>((void *)cursor);

				assert(ISFREE == node->type_ || ISUSED == node->type_);
				assert(slabnode == node->slab_);
				if (ISUSED == node->type_) {
					assert(this == node->owner_);
					element_type *element = node_to_element(node);
					element->~T();
					++used;
				}
				++total;
			}

			if (bucketsize < bucket_max_)
				bucketsize <<= 1;

			TAILQ_REMOVE(&slabs_, slabnode, node_);
			(Allocator::free)((void *)slabnode);
		}

		assert(total == capacity_);
		assert(used == (total - avail_));
	}

	template<typename... Args>
	inline element_type *construct(Args&&... args)
	{
		Node *node = (malloc)();
		if (nullptr == node)
			throw std::bad_alloc();
		element_type *element = node_to_element(node);
		try {
			new(element) element_type(std::forward<Args>(args)...);
			assert(ISUSED == node->type_);
		} catch (...) {
			(free)(node);
			throw;
		}
		return element;
	}

	template<typename... Args>
	inline element_type *construct_nothrow(Args&&... args)
	{
		Node *node = (malloc)();
		if (nullptr == node)
			throw nullptr;
		element_type *element = node_to_element(node);
		new(element) element_type(std::forward<Args>(args)...);
		assert(ISUSED == node->type_);
		return element;
	}

	inline void
	destroy(element_type *element)
	{
		assert(element);
		if (element) {
			Node *node = element_to_node(element);
			assert(ISUSED == node->type_);
			assert(this == node->owner_);
			if (ISUSED == node->type_ && this == node->owner_) {
				element->~T();
				(free)(node);
			}
		}
	}

	inline bool
	is_from(const element_type *element) const
	{
		const Node *node = element_to_node(element);
		switch (node->type_) {
		case ISFREE:
		case ISUSED: {
				const Node *slabnode;
				TAILQ_FOREACH(slabnode, &slabs_, node_) {
					assert(ISSLAB == slabnode->type_);
					assert(this == slabnode->owner_);
					if (slabnode == node->slab_) {
						assert(ISUSED == node->type_);
						return true;
					}
				}
			}
			break;
		}
		return false;
	}

	inline void
	check() const
	{
		size_t total = 0, used = 0, bucketsize = bucket_first_;

		const Node *slabnode;
		TAILQ_FOREACH(slabnode, &slabs_, node_) {

			assert(ISSLAB == slabnode->type_);
			assert(this == slabnode->owner_);
			assert(bucketsize == slabnode->bucketsize_);

			for (const char *cursor = reinterpret_cast<const char *>(slabnode) + node_size_,
					*end = cursor + (node_size_ * bucketsize); cursor < end; cursor += node_size_) {
				const Node *node = static_cast<const Node *>((void *)cursor);

				assert(ISUSED == node->type_ || ISFREE == node->type_);
				assert(slabnode == node->slab_);
				if (ISUSED == node->type_) {
					assert(this == node->owner_);
					++used;
				}
				++total;
			}

			if (bucketsize < bucket_max_)
				bucketsize <<= 1;
		}
		assert(total == capacity_);
		assert(used == (total - avail_));
	}

	inline size_t avail() const
	{
		return avail_;
	}

	inline size_t capacity() const
	{
		return capacity_;
	}

	inline size_t size() const
	{
		return capacity_ - avail_;
	}

	inline size_t get_next_size() const
	{
		return bucket_next_;
	}

	inline void set_next_size(const size_t next)
	{
		assert(next);
		bucket_next_ = next;
	}

private:
	inline element_type *
	node_to_element(Node *node)
	{
		return static_cast<element_type *>((void *)(((char *)node) + meta_size_));
	}

	inline const element_type *
	node_to_element(Node *node) const
	{
		return static_cast<const element_type *>((const void *)(((const char *)node) + meta_size_));
	}

	inline Node *
	element_to_node(element_type *element)
	{
		return static_cast<Node *>((void *)(((char *)element) - meta_size_));
	}

	inline const Node *
	element_to_node(const element_type *element) const
	{
		return static_cast<const Node *>((void *)(((const char *)element) - meta_size_));
	}

	inline Node * malloc() noexcept
	{
		Node *node;

		if (nullptr == (node = TAILQ_FIRST(&free_list_))) {
			const size_t bucketsize = bucket_next_;
			char *slab;

			// new object slab
			assert(0 == avail_);
			if (nullptr == (slab =
					static_cast<char *>(Allocator::malloc(node_size_, 1 /*slabnode*/ + bucketsize)))) {
				return nullptr;
			}

			Node *slabnode = reinterpret_cast<Node *>(slab);
			slabnode->type_ = ISSLAB;
			slabnode->owner_ = this;
			slabnode->bucketsize_ = bucketsize;
			TAILQ_INSERT_TAIL(&slabs_, slabnode, node_);

			// push back new object buffers
			for (char *cursor = (slab + node_size_), *end = cursor + (node_size_ * bucketsize);
					cursor < end; cursor += node_size_) {
				Node *t_node = static_cast<Node *>((void *)cursor);

				t_node->type_ = ISFREE;   // node type and implied state.
				t_node->slab_ = slabnode; // owner reference.
				t_node->owner_ = nullptr; // pool reference, when allocated.
				TAILQ_INSERT_TAIL(&free_list_, t_node, node_);
			}

			capacity_ += bucketsize;
			avail_ += bucketsize;
			if (bucket_next_ < bucket_max_)
				bucket_next_ <<= 1;

			node = TAILQ_FIRST(&free_list_);
			assert(node);
		}

		assert(ISFREE == node->type_);
		assert(nullptr == node->owner_);
		TAILQ_REMOVE(&free_list_, node, node_);
		TAILQ_INSERT_TAIL(&used_list_, node, node_);
		node->type_ = ISUSED;
		node->owner_ = this;
		assert(avail_ > 0);
		--avail_;
		return node;
	}

	inline void
	free(Node *node)
	{
		assert(ISUSED == node->type_);
		assert(this == node->owner_);
		node->owner_ = nullptr;
		node->type_ = ISFREE;
		TAILQ_REMOVE(&used_list_, node, node_);
		TAILQ_INSERT_TAIL(&free_list_, node, node_);
#if defined(_DEBUG)
		(void) memset(node_to_element(node), 0xCC, node_size_ - meta_size_);
#endif
		assert(avail_ < capacity_);
		++avail_;
	}

private:
	const size_t bucket_first_, bucket_max_;
	size_t bucket_next_;
	const size_t meta_size_;
	const size_t node_size_;
	NodeList slabs_;
	NodeList free_list_;
	NodeList used_list_;
	size_t capacity_;
	size_t avail_;
};

#if defined(_MSC_VER)
#pragma pop_macro("new")
#pragma warning(pop)
#endif

}   //namespace inetd

//end

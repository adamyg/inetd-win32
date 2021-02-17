#ifndef BZSTD_POOL_OBJECT_POOL_H_INCLUDED
#define BZSTD_POOL_OBJECT_POOL_H_INCLUDED
#pragma once
//
//  non-locking object_pool
//

#include <bzstd/bzstdint.h>
#include <bzstd/bzalign.h>
#include <bzstd/queue/tailqueue.h>
#include <cstdlib>
#include <new>

namespace bzstd {
    
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4127)                 // conditional expression is constant
#endif

/*!
\file
\brief Provides a template type dhstd:object_pool<T> that can be used for fast and 
    efficient memory allocation of objects of type T. It also provides automatic 
    destruction of non-deallocated objects.
*/

struct default_bucket_allocator {
    static void *
    malloc(const size_t nodesize, const size_t nodecount)
        { return ::calloc(nodesize, nodecount); }

    static void
    free(void *block)
        { ::free(block); }
};


template <typename T, size_t ALIGNMENT = 16,
        typename Allocator = default_bucket_allocator>
class object_pool {  
private:
    static const uint32_t FREEMAGIC   = MKMAGIC32('O','p','F','r');
    static const uint32_t USEDMAGIC   = MKMAGIC32('O','p','U','s');
    static const uint32_t OBJECTMAGIC = MKMAGIC32('O','p','U','c');

    struct Node {
        uint32_t m_magic;                       // structure magic.
        void *m_slab;                           // aasociated slab.
        TAILQ_ENTRY(Node) m_node;               // free/used list.
#if defined(_DEBUG)
        void *m_owner;                          // associated pool.
#endif
    };

    typedef TAILQ_HEAD(FreeList, Node) NodeList_t;
    typedef std::vector<char *> SlabList_t;

public:
    typedef T element_type;                     //!< ElementType.
    typedef Allocator allocator_type;           //!< System allocator type.

private:
    object_pool(const object_pool &);
    object_pool& operator=(const object_pool &);

public:
    /// Constructs a new (empty by default) ObjectPool.
    /// \param firstbucket  Number of chunks to request from the system the next time that object needs to allocate system memory (default 32).
    /// \param maxbucket    Maximum size of chunks to ever request from the system - this puts a cap on the doubling algorithm used by the underlying pool.
    /// \param prime        If *true*, the initial bucket is primed.
    ///
    object_pool(const size_t firstbucket = 32, const size_t maxbucket = 0, const bool prime = false)
            : m_bucketfirst(firstbucket), m_bucketmax(maxbucket ? maxbucket : (size_t)-1),
                m_bucketnext(firstbucket),
                m_nodeheadersize(ALIGNTO(sizeof(Node), ALIGNMENT)),
                m_noderawsize(ALIGNTO(sizeof(Node), ALIGNMENT) + ALIGNTO(sizeof(element_type), ALIGNMENT)),
                m_capacity(0), m_avail(0)
        {
            assert(m_bucketfirst);
            assert(m_bucketfirst <= m_bucketmax);
            assert(m_nodeheadersize >= sizeof(Node));
            assert(m_noderawsize >= (sizeof(Node) + sizeof(element_type)));
            TAILQ_INIT(&m_freelist);
            TAILQ_INIT(&m_usedlist);
            if (prime) (free)((malloc)());
        }

    ~object_pool()
        {
            size_t total = 0, used = 0;

            if (m_slabs.size()) {
                size_t bucketsize = m_bucketfirst;
                for (SlabList_t::iterator it(m_slabs.begin()), end(m_slabs.end()); it != end; ++it) {
                    char *slab = *it;

                    for (char *cursor = slab, *end = cursor + (m_noderawsize * bucketsize); 
                                cursor < end; cursor += m_noderawsize) {
                        Node *node = static_cast<Node *>((void *)cursor);

                        assert(FREEMAGIC == node->m_magic || USEDMAGIC == node->m_magic || OBJECTMAGIC == node->m_magic);
                        assert(slab == node->m_slab);
                        if (OBJECTMAGIC == node->m_magic) {
                            element_type *element = toelement(node);
                            element->~T();
                            ++used;
                        }
                        ++total;
                    }

                    if (bucketsize < m_bucketmax) bucketsize <<= 1;
                    (Allocator::free)((void *)slab);
                }
            }
            assert(total == m_capacity);
            assert(used == (total - m_avail));
        }

    inline void
    check() const
        {
            size_t total = 0, used = 0;

            if (m_slabs.size()) {
                size_t bucketsize = m_bucketfirst;
                for (SlabList_t::const_iterator it(m_slabs.begin()), end(m_slabs.end()); it != end; ++it) {                  
                    const char *slab = *it;

                    for (const char *cursor = slab, *end = cursor + (m_noderawsize * bucketsize); 
                                cursor < end; cursor += m_noderawsize) { 
                        const Node *node = static_cast<const Node *>((void *)cursor);

                        assert(FREEMAGIC == node->m_magic || USEDMAGIC == node->m_magic || OBJECTMAGIC == node->m_magic);
                        assert(slab == node->m_slab);
                        if (FREEMAGIC != node->m_magic) ++used;
                        ++total;
                    }
                    if (bucketsize < m_bucketmax) bucketsize <<= 1;
                }
            }
            assert(total == m_capacity);
            assert(used == (total - m_avail));
        }

    /// \returns true if chunk was allocated from *this or may be returned as the result of a future allocation from *this.
    ///
    ///     Returns false if chunk was allocated from some other pool or may be returned as the result of a future allocation 
    ///     from some other pool. Otherwise, the return value is meaningless.
    ///
    /// \note This function may NOT be used to reliably test random pointer values!
    ///
    inline bool
    is_from(const element_type *element) const
        {
            const Node *node = tonode(element);
            switch (node->m_magic) {
            case FREEMAGIC:
            case USEDMAGIC:
            case OBJECTMAGIC:
                for (SlabList_t::const_iterator it(m_slabs.begin()), end(m_slabs.end()); it != end; ++it) {
                    if (node->m_slab == *it) {
                        assert(node->m_magic != FREEMAGIC);
                        return true;
                    }
                }
                break;
            }
            return false;
        }

#if defined(_MSC_VER)
#pragma push_macro("new")
#undef  new
#endif

    /// \returns A pointer to an object of type T, allocated in memory from the underlying pool and 
    ///     default constructed.  The returned object can be freed by a call to \ref destroy. 
    ///     Otherwise the returned object will be automatically destroyed when *this is destroyed.
    ///
    inline element_type *
    construct()
        {
            Node *node = (malloc)();
            element_type *element = toelement(node);
            if (0 == element) throw std::bad_alloc();
            try {
                new(element) element_type();
                node->m_magic = OBJECTMAGIC;
            } catch (...) {
                (free)(node);
                throw;
            }
            return element;
        }

    inline element_type *
    construct_nothrow()
        {
            Node *node = (malloc)();
            element_type *element = toelement(node);
            if (element) {
                new(element) element_type();
                node->m_magic = OBJECTMAGIC;
            }
            return element;
        }

    /// \returns A pointer to an object of type T, allocated in memory from the underlying pool and 
    ///     parameterised constructed.  The returned object can be freed by a call to \ref destroy. 
    ///     Otherwise the returned object will be automatically destroyed when *this is destroyed.
    ///
    template <typename Argument>
    inline element_type *
    construct(Argument argument)
        {
            Node *node = (malloc)();
            element_type *element = toelement(node);
            if (0 == element) throw std::bad_alloc();
            try {
                new(element) element_type(argument);
                node->m_magic = OBJECTMAGIC;
            } catch (...) {
                (free)(node);
                throw;
            }
            return element;
        }

    template <typename Argument1, typename Argument2>
    inline element_type *
    construct(Argument1 argument1, Argument2 argument2)
        {
            Node *node = (malloc)();
            element_type *element = toelement(node);
            if (0 == element) throw std::bad_alloc();
            try {
                new(element) element_type(argument1, argument2);
                node->m_magic = OBJECTMAGIC;
            } catch (...) {
                (free)(node);
                throw;
            }
            return element;
        }

    template <typename Argument1, typename Argument2, typename Argument3>
    inline element_type *
    construct(Argument1 argument1, Argument2 argument2, Argument3 argument3)
        {
            Node *node = (malloc)();
            element_type *element = toelement(node);
            if (0 == element) throw std::bad_alloc();
            try {
                new(element) element_type(argument1, argument2, argument3);
                node->m_magic = OBJECTMAGIC;
            } catch (...) {
                (free)(node);
                throw;
            }
            return element;
        }

    template <typename Argument>
    inline element_type *
    construct_nothrow(Argument argument)
        {
            Node *node = (malloc)();
            element_type *element = toelement(node);
            if (0 == element) return 0;
            new(element) element_type(argument);
            node->m_magic = OBJECTMAGIC;
            return element;
        }

    template <typename Argument1, typename Argument2>
    inline element_type *
    construct_nothrow(Argument1 argument1, Argument2 argument2)
        {
            Node *node = (malloc)();
            element_type *element = toelement(node);
            if (0 == element) return 0;
            new(element) element_type(argument1, argument2);
            node->m_magic = OBJECTMAGIC;
            return element;
        }

    template <typename Argument1, typename Argument2, typename Argument3>
    inline element_type *
    construct_nothrow(Argument1 argument1, Argument2 argument2, Argument3 argument3)
        {
            Node *node = (malloc)();
            element_type *element = toelement(node);
            if (0 == element) return 0;
            new(element) element_type(argument1, argument2, argument3);
            node->m_magic = OBJECTMAGIC;
            return element;
        }

#if defined(_MSC_VER)
#pragma pop_macro("new")
#endif

    /// Destroys an object allocated with \ref construct. 
    /// \param element  Object to destroyed.     
    /// \pre element    must have been previously allocated from *this via a call to \ref construct.
    ///
    inline void
    destroy(element_type *element)
        {
            assert(element);
            if (element) {
                Node *node = tonode(element);
                assert(OBJECTMAGIC == node->m_magic);
#if defined(_DEBUG)
                assert(this == node->m_owner);
#endif
                element->~T();
                node->m_magic = USEDMAGIC;
                (free)(node);
            }
        }

    /// \returns The number of available object, without additional slab allocation.
    inline size_t
    avail() const
        {
            return m_avail;
        }

    /// \returns The current capacity, without additional slab allocation.
    inline size_t
    capacity() const
        {
            return m_capacity;
        }

    /// \returns The derived number of allocation objects.
    inline size_t
    used() const
        {
            return m_capacity - m_avail;
        }

    /// \returns The number of chunks that will be allocated next time we run out of memory.
    inline size_t
    get_next_size() const
        {
            return m_bucketnext;
        }

    /// Set a new number of chunks to allocate the next time we run out of memory.
    /// \param next     Wanted bucketnext size (must not be zero).
    inline void
    set_next_size(const size_t next)
        {
            assert(next);
            m_bucketnext = next;
        }

private:
    inline element_type *
    toelement(Node *node)
        { return static_cast<element_type *>((void *)(((char *)node) + m_nodeheadersize)); }

    inline const element_type *
    toelement(Node *node) const
        { return static_cast<const element_type *>((const void *)(((const char *)node) + m_nodeheadersize)); }

    inline Node *
    tonode(element_type *element)
        { return static_cast<Node *>((void *)(((char *)element) - m_nodeheadersize)); }

    inline const Node *
    tonode(const element_type *element) const
        { return static_cast<const Node *>((void *)(((const char *)element) - m_nodeheadersize)); }

    inline Node *
    malloc()
        {
            Node *node;

            if (0 == (node = TAILQ_FIRST(&m_freelist))) {
                const size_t bucketsize = m_bucketnext;
                char *slab;

                assert(0 == m_avail);
                if (0 == (slab =                // new object slab
                        static_cast<char *>(Allocator::malloc(m_noderawsize, bucketsize)))) {
                    return 0;
                }

                if (m_bucketnext < m_bucketmax) m_bucketnext <<= 1;
                m_slabs.push_back(slab);
                                                // push back new object buffers
                for (char *cursor = slab, *end = cursor + (m_noderawsize * bucketsize);
                                cursor < end; cursor += m_noderawsize) {
                    Node *t_node = static_cast<Node *>((void *)cursor);

                    t_node->m_magic = FREEMAGIC;
                    t_node->m_slab = slab;      // owner reference
                    TAILQ_INSERT_TAIL(&m_freelist, t_node, m_node);
                    ++m_capacity;
                    ++m_avail;
                }

                node = TAILQ_FIRST(&m_freelist);
                assert(node);
            }

            assert(node->m_magic == FREEMAGIC);
            TAILQ_REMOVE(&m_freelist, node, m_node);
            TAILQ_INSERT_TAIL(&m_usedlist, node, m_node);
            node->m_magic = USEDMAGIC;
#if defined(_DEBUG)
            assert(0 == node->m_owner);
            node->m_owner = this;
#endif
            assert(m_avail > 0);
            --m_avail;
            return node;
        }

    inline void
    free(Node *node)
        {
            assert(USEDMAGIC == node->m_magic);
#if defined(_DEBUG)
            assert(this == node->m_owner);
            node->m_owner = 0;
#endif
            TAILQ_REMOVE(&m_usedlist, node, m_node);
            TAILQ_INSERT_TAIL(&m_freelist, node, m_node);
            node->m_magic = FREEMAGIC;
            assert(m_avail < m_capacity);
            ++m_avail;
        }

private:
    const size_t m_bucketfirst, m_bucketmax;
    size_t m_bucketnext;
    const size_t m_nodeheadersize;
    const size_t m_noderawsize;
    SlabList_t m_slabs;
    NodeList_t m_freelist;
    NodeList_t m_usedlist;
    size_t m_capacity;
    size_t m_avail;
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

}   //namespace bzstd

#endif  //BZSTD_POOL_OBJECT_POOL_H_INCLUDED

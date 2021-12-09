// By Emil Ernerfeldt 2014-2017
// LICENSE:
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.

#pragma once

#include <cstdlib>
#include <cstring>
#include <iterator>
#include <utility>
//#include <cassert>

#ifdef _MSC_VER
#  include <intrin.h>
#ifndef __clang__
#  include <zmmintrin.h>
#endif
#else
#  include <x86intrin.h>
#endif

// likely/unlikely
#if (__GNUC__ >= 4 || __clang__)
#    define EMH_LIKELY(condition)   __builtin_expect(condition, 1)
#    define EMH_UNLIKELY(condition) __builtin_expect(condition, 0)
#else
#    define EMH_LIKELY(condition)   condition
#    define EMH_UNLIKELY(condition) condition
#endif

namespace emilib {

#define KEYHASH_MASK(key_hash) (((uint8_t)(key_hash >> 24) << 1) | State::EFILLED)

#ifndef AVX2_EHASH
    const static auto simd_empty  = _mm_set1_epi8(1);
    const static auto simd_delete = _mm_set1_epi8(3);
    constexpr static uint8_t simd_gaps = sizeof(simd_empty) / sizeof(uint8_t);

    #define SET1_EPI8      _mm_set1_epi8
    #define LOADU_EPI8     _mm_loadu_si128
    #define MOVEMASK_EPI8  _mm_movemask_epi8
    #define CMPEQ_EPI8     _mm_cmpeq_epi8
#elif 1
    const static auto simd_empty  = _mm256_set1_epi8(1);
    const static auto simd_delete = _mm256_set1_epi8(3);
    constexpr static uint8_t simd_gaps = sizeof(simd_empty) / sizeof(uint8_t);
    #define SET1_EPI8      _mm256_set1_epi8
    #define LOADU_EPI8     _mm256_loadu_si256
    #define MOVEMASK_EPI8  _mm256_movemask_epi8
    #define CMPEQ_EPI8     _mm256_cmpeq_epi8
#elif AVX512_EHASH
    const static auto simd_empty  = _mm512_set1_epi8(1);
    const static auto simd_delete = _mm512_set1_epi8(3);
    constexpr static uint8_t simd_gaps = sizeof(simd_empty) / sizeof(uint8_t);
    #define SET1_EPI8      _mm512_set1_epi8
    #define LOADU_EPI8     _mm512_loadu_si512
    #define MOVEMASK_EPI8  _mm512_movemask_epi8 //avx512 error
    #define CMPEQ_EPI8     _mm512_test_epi8_mask
#else
    //TODO arm neon
#endif

//find filled or empty
constexpr static uint8_t stat_bits = sizeof(uint8_t) * 8;
constexpr static uint8_t stat_gaps = sizeof(uint64_t) / sizeof(uint8_t);

inline static uint32_t CTZ(uint64_t n)
{
#if defined(__x86_64__) || defined(_WIN32) || (__BYTE_ORDER__ && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)

#elif __BIG_ENDIAN__ || (__BYTE_ORDER__ && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    n = __builtin_bswap64(n);
#else
    static uint32_t endianness = 0x12345678;
    const auto is_big = *(const char *)&endianness == 0x12;
    if (is_big)
    n = __builtin_bswap64(n);
#endif

#if _WIN32
    unsigned long index;
    #if defined(_WIN64)
    _BitScanForward64(&index, n);
    #else
    _BitScanForward(&index, n);
    #endif
#elif defined (__LP64__) || (SIZE_MAX == UINT64_MAX) || defined (__x86_64__)
    uint32_t index = __builtin_ctzll(n);
#elif 1
    uint32_t index = __builtin_ctzl(n);
#endif

    return (uint32_t)index;
}

/// A cache-friendly hash table with open addressing, linear probing and power-of-two capacity
template <typename KeyT, typename ValueT, typename HashT = std::hash<KeyT>, typename EqT = std::equal_to<KeyT>>
class HashMap
{
private:
    using htype = HashMap<KeyT, ValueT, HashT, EqT>;

    using PairT = std::pair<KeyT, ValueT>;
public:
    using size_t          = uint32_t;
    using value_type      = PairT;
    using reference       = PairT&;
    using const_reference = const PairT&;
    typedef ValueT mapped_type;
    typedef ValueT val_type;
    typedef KeyT   key_type;

    class iterator
    {
    public:
        using iterator_category = std::forward_iterator_tag;
        using difference_type   = std::ptrdiff_t;
        using value_type        = std::pair<KeyT, ValueT>;
        using pointer           = value_type*;
        using reference         = value_type&;

        iterator(htype* hash_map, size_t bucket) : _map(hash_map), _bucket(bucket) { }

        size_t operator - (iterator& r)
        {
            return _bucket - r._bucket;
        }

        size_t bucket() const
        {
            return _bucket;
        }

        iterator& operator++()
        {
            goto_next_element();
            return *this;
        }

        iterator operator++(int)
        {
            size_t old_index = _bucket;
            goto_next_element();
            return iterator(_map, old_index);
        }

        reference operator*() const
        {
            return _map->_pairs[_bucket];
        }

        pointer operator->() const
        {
            return _map->_pairs + _bucket;
        }

        bool operator==(const iterator& rhs) const
        {
            return _bucket == rhs._bucket;
        }

        bool operator!=(const iterator& rhs) const
        {
            return _bucket != rhs._bucket;
        }

    private:
        void goto_next_element()
        {
            _bucket = _map->find_filled_slot(_bucket + 1);
        }

    public:
        htype*  _map;
        size_t  _bucket;
    };

    class const_iterator
    {
    public:
        using iterator_category = std::forward_iterator_tag;
        using difference_type   = std::ptrdiff_t;
        using value_type        = const std::pair<KeyT, ValueT>;
        using pointer           = value_type*;
        using reference         = value_type&;

        const_iterator(iterator proto) : _map(proto._map), _bucket(proto._bucket)  { }
        const_iterator(const htype* hash_map, size_t bucket) : _map(hash_map), _bucket(bucket)
        {
        }

        size_t bucket() const
        {
            return _bucket;
        }

        size_t operator - (const_iterator& r) const
        {
            return _bucket - r._bucket;
        }

        const_iterator& operator++()
        {
            goto_next_element();
            return *this;
        }

        const_iterator operator++(int)
        {
            size_t old_index = _bucket;
            goto_next_element();
            return const_iterator(_map, old_index);
        }

        reference operator*() const
        {
            return _map->_pairs[_bucket];
        }

        pointer operator->() const
        {
            return _map->_pairs + _bucket;
        }

        bool operator==(const const_iterator& rhs) const
        {
            //DCHECK_EQ_F(_map, rhs._map);
            return _bucket == rhs._bucket;
        }

        bool operator!=(const const_iterator& rhs) const
        {
            return _bucket != rhs._bucket;
        }

    private:
        void goto_next_element()
        {
            _bucket = _map->find_filled_slot(_bucket + 1);
        }

    public:
        const htype*  _map;
        size_t        _bucket;
    };

    // ------------------------------------------------------------------------

    HashMap() = default;

    HashMap(size_t n)
    {
        rehash(n);
    }

    HashMap(const HashMap& other)
    {
        clone(other);
    }

    HashMap(HashMap&& other)
    {
        if (this != &other) {
            *this = std::move(other);
            if (!is_triviall_destructable())
                other.clear();
            else
                other._num_filled = 0;
        }
    }

    HashMap(std::initializer_list<std::pair<KeyT, ValueT>> il)
    {
        reserve(il.size());
        for (auto it = il.begin(); it != il.end(); ++it)
            insert(*it);
    }

    HashMap& operator=(const HashMap& other)
    {
        if (this != &other)
            clone(other);
         return *this;
    }

    HashMap& operator=(HashMap&& other)
    {
        if (this != &other) {
            swap(other);
            if (!is_triviall_destructable())
                other.clear();
            else
                other._num_filled = 0;
        }
        return *this;
    }

    ~HashMap()
    {
        if (!is_triviall_destructable())
            clear();

        _num_filled = 0;
        free(_states);
    }

    static constexpr bool is_copy_trivially()
    {
#if __cplusplus >= 201103L || _MSC_VER > 1600
        return (std::is_trivially_copyable<KeyT>::value && std::is_trivially_copyable<ValueT>::value);
#else
        return (std::is_pod<KeyT>::value && std::is_pod<ValueT>::value);
#endif
    }

    void clone(const HashMap& other)
    {
        if (other.size() == 0) {
            clear();
            return;
        }

        _hasher     = other._hasher;
        if (is_copy_trivially()) {
            _num_filled = _num_buckets = 0;
            reserve(other._num_buckets / 2);
            memcpy(_pairs,  other._pairs,  _num_buckets * sizeof(_pairs[0]));
        } else {
            clear();
            reserve(other._num_buckets / 2);
            for (auto it = other.cbegin();  it != other.cend(); ++it)
                new(_pairs + it.bucket()) PairT(*it);
        }
        //assert(_num_buckets == other._num_buckets);
        _num_filled = other._num_filled;
        _max_probe_length = other._max_probe_length;
        memcpy(_states, other._states, (_num_buckets + simd_gaps) * sizeof(_states[0]));
    }

    void swap(HashMap& other)
    {
        std::swap(_hasher,           other._hasher);
        std::swap(_eq,               other._eq);
        std::swap(_states,           other._states);
        std::swap(_pairs,            other._pairs);
        std::swap(_num_buckets,      other._num_buckets);
        std::swap(_num_filled,       other._num_filled);
        std::swap(_max_probe_length, other._max_probe_length);
        std::swap(_mask,             other._mask);
    }

    // -------------------------------------------------------------

    iterator begin()
    {
        return { this, find_filled_slot(0) };
    }

    const_iterator cbegin() const
    {
        return { this, find_filled_slot(0) };
    }

    const_iterator begin() const
    {
        return cbegin();
    }

    iterator end()
    {
        return { this, _num_buckets };
    }

    const_iterator cend() const
    {
        return { this, _num_buckets };
    }

    const_iterator end() const
    {
        return cend();
    }

    size_t size() const
    {
        return _num_filled;
    }

    bool empty() const
    {
        return _num_filled==0;
    }

    // Returns the number of buckets.
    size_t bucket_count() const
    {
        return _num_buckets;
    }

    /// Returns average number of elements per bucket.
    float load_factor() const
    {
        return _num_filled / static_cast<float>(_num_buckets);
    }

    void max_load_factor(float lf = 8.0f/9)
    {
    }

    // ------------------------------------------------------------

    template<typename KeyLike>
    iterator find(const KeyLike& key)
    {
        return iterator(this, find_filled_bucket(key));
    }

    template<typename KeyLike>
    const_iterator find(const KeyLike& key) const
    {
        return const_iterator(this, find_filled_bucket(key));
    }

    template<typename KeyLike>
    bool contains(const KeyLike& k) const
    {
        return find_filled_bucket(k) != _num_buckets;
    }

    template<typename KeyLike>
    size_t count(const KeyLike& k) const
    {
        return find_filled_bucket(k) != _num_buckets;
    }

    /// Returns the matching ValueT or nullptr if k isn't found.
    template<typename KeyLike>
    ValueT* try_get(const KeyLike& k)
    {
        auto bucket = find_filled_bucket(k);
        return &_pairs[bucket].second;
    }

    /// Const version of the above
    template<typename KeyLike>
    ValueT* try_get(const KeyLike& k) const
    {
        auto bucket = find_filled_bucket(k);
        return &_pairs[bucket].second;
    }

    /// Convenience function.
    template<typename KeyLike>
    ValueT get_or_return_default(const KeyLike& k) const
    {
        const ValueT* ret = try_get(k);
        return ret ? *ret : ValueT();
    }

    // -----------------------------------------------------

    /// Returns a pair consisting of an iterator to the inserted element
    /// (or to the element that prevented the insertion)
    /// and a bool denoting whether the insertion took place.
    std::pair<iterator, bool> insert(const KeyT& key, const ValueT& value)
    {
        check_expand_need();

        const auto key_hash = _hasher(key);
        const auto bucket = find_or_allocate(key, key_hash);

        if (_states[bucket] % 2 == State::EFILLED) {
            return { iterator(this, bucket), false };
        } else {
            _states[bucket] = KEYHASH_MASK(key_hash);
            new(_pairs + bucket) PairT(key, value); _num_filled++;
            return { iterator(this, bucket), true };
        }
    }

    std::pair<iterator, bool> insert(const KeyT& key, ValueT&& value)
    {
        check_expand_need();

        const auto key_hash = _hasher(key);
        const auto bucket = find_or_allocate(key, key_hash);

        if (_states[bucket] % 2 == State::EFILLED) {
            return { iterator(this, bucket), false };
        }
        else {
            _states[bucket] = KEYHASH_MASK(key_hash);
            new(_pairs + bucket) PairT(key, std::move(value)); _num_filled++;
            return { iterator(this, bucket), true };
        }
    }

    std::pair<iterator, bool> emplace(const KeyT& key, const ValueT& value)
    {
        return insert(key, value);
    }

    std::pair<iterator, bool> emplace(const KeyT& key, ValueT&& value)
    {
        return insert(key, std::move(value));
    }

    std::pair<iterator, bool> insert(const std::pair<KeyT, ValueT>& p)
    {
        return insert(p.first, p.second);
    }

    std::pair<iterator, bool> insert(iterator it, const std::pair<KeyT, ValueT>& p)
    {
        return insert(p.first, p.second);
    }

    void insert(const_iterator beginc, const_iterator endc)
    {
        reserve(endc - beginc + _num_filled);
        for (; beginc != endc; ++beginc) {
            insert(beginc->first, beginc->second);
        }
    }

    void insert_unique(const_iterator beginc, const_iterator endc)
    {
        reserve(endc - beginc + _num_filled);
        for (; beginc != endc; ++beginc) {
            insert_unique(beginc->first, beginc->second);
        }
    }

    /// Same as above, but contains(key) MUST be false
    void insert_unique(const KeyT& key, const ValueT& value)
    {
        check_expand_need();

        const auto key_hash = _hasher(key);
        const auto bucket = find_empty_slot(key_hash & _mask, 0);

        _states[bucket] = KEYHASH_MASK(key_hash);
        new(_pairs + bucket) PairT(key, value); _num_filled++;
    }

    void insert_unique(KeyT&& key, ValueT&& value)
    {
        check_expand_need();

        const auto key_hash = _hasher(key);
        const auto bucket = find_empty_slot(key_hash & _mask, 0);

        _states[bucket] = KEYHASH_MASK(key_hash);
        new(_pairs + bucket) PairT(std::move(key), std::move(value)); _num_filled++;
    }

    void insert_unique(std::pair<KeyT, ValueT>&& p)
    {
        insert_unique(std::move(p.first), std::move(p.second));
    }

    void insert_unique(std::pair<KeyT, ValueT>& p)
    {
        insert_unique(p.first, p.second);
    }

    void insert_or_assign(const KeyT& key, ValueT&& value)
    {
        check_expand_need();

        const auto key_hash = _hasher(key);
        const auto bucket = find_or_allocate(key, key_hash);

        // Check if inserting a new value rather than overwriting an old entry
        if (_states[bucket] % 2 == State::EFILLED) {
            _pairs[bucket].second = value;
        } else {
            _states[bucket] = KEYHASH_MASK(key_hash);
            new(_pairs + bucket) PairT(key, value); _num_filled++;
        }
    }

    /// Like std::map<KeyT,ValueT>::operator[].
    ValueT& operator[](const KeyT& key)
    {
        check_expand_need();

        const auto key_hash = _hasher(key);
        const auto bucket = find_or_allocate(key, key_hash);

        /* Check if inserting a new value rather than overwriting an old entry */
        if (_states[bucket] % 2 != State::EFILLED) {
            _states[bucket] = KEYHASH_MASK(key_hash);
            new(_pairs + bucket) PairT(key, ValueT()); _num_filled++;
        }
        return _pairs[bucket].second;
    }

    // -------------------------------------------------------

    /// Erase an element from the hash table.
    /// return false if element was not found
    size_t erase(const KeyT& key)
    {
        auto bucket = find_filled_bucket(key);
        if (bucket == _num_buckets)
            return 0;

        _erase(bucket);
        return 1;
    }

    iterator erase(const_iterator cit)
    {
        _erase(cit._bucket);
        iterator it(this, cit._bucket);
        return ++it;
    }

    iterator erase(iterator it)
    {
        _erase(it._bucket);
        return ++it;
    }

    void _erase(iterator it)
    {
        _erase(it._bucket);
    }

    void _erase(size_t bucket)
    {
        if (is_triviall_destructable())
            _pairs[bucket].~PairT();
        auto state = _states[bucket] = (_states[bucket + 1] % 4) == State::EEMPTY ? State::EEMPTY : State::EDELETE;
        if (state == State::EEMPTY) {
            while (bucket > 1 && _states[--bucket] == State::EDELETE)
                _states[bucket] = State::EEMPTY;
        }
        _num_filled -= 1;
    }

    static constexpr bool is_triviall_destructable()
    {
#if __cplusplus >= 201402L || _MSC_VER > 1600 || __clang__
        return (std::is_trivially_destructible<KeyT>::value && std::is_trivially_destructible<ValueT>::value);
#else
        return (std::is_pod<KeyT>::value && std::is_pod<ValueT>::value);
#endif
    }

    /// Remove all elements, keeping full capacity.
    void clear()
    {
        if (!is_triviall_destructable()) {
            for (size_t bucket=0; _num_filled; ++bucket) {
                if (_states[bucket] % 2 == State::EFILLED) {
                    _pairs[bucket].~PairT();
                    _num_filled --;
                }
                _states[bucket] = State::EEMPTY;
            }
        } else if (_num_filled)
            std::fill_n(_states, _num_buckets, State::EEMPTY);

        _num_filled = 0;
        _max_probe_length = -1;
    }

    void shrink_to_fit()
    {
        rehash(_num_filled);
    }

    bool reserve(size_t num_elems)
    {
        size_t required_buckets = num_elems + num_elems / 8;
        if (EMH_LIKELY(required_buckets < _num_buckets))
            return false;

        rehash(required_buckets + 2);
        return true;
    }

    /// Make room for this many elements
    void rehash(size_t num_elems)
    {
        const size_t required_buckets = num_elems;
        if (EMH_UNLIKELY(required_buckets < _num_filled))
            return;

        auto num_buckets = _num_filled > (1u << 16) ? (1u << 16) : 4;
        while (num_buckets < required_buckets) { num_buckets *= 2; }

        auto status_size = (simd_gaps + num_buckets) * sizeof(uint8_t);
        status_size += (8 - status_size % 8) % 8;

        auto new_states = (uint8_t*)malloc(status_size + (num_buckets + 1) * sizeof(PairT));
        auto new_pairs  = (PairT*)(new_states + status_size);
            //(PairT*)malloc((num_buckets + 1) * sizeof(PairT));

        auto old_num_filled  = _num_filled;
        auto old_num_buckets = _num_buckets;
        auto old_states      = _states;
        auto old_pairs       = _pairs;
        auto max_probe_length = _max_probe_length;

        _num_filled  = 0;
        _num_buckets = num_buckets;
        _mask        = num_buckets - 1;
        _states      = new_states;
        _pairs       = new_pairs;

        std::fill_n(_states, num_buckets, State::EEMPTY);
        //find empty tombstone
        std::fill_n(_states + num_buckets, simd_gaps / 2, State::EFILLED + 4);
        //find filled tombstone
        std::fill_n(_states + num_buckets + simd_gaps / 2, simd_gaps / 2, State::EEMPTY + 4);
        //fill last packet zero
        memset(new_pairs + num_buckets, 0, sizeof(new_pairs[0]));

        _max_probe_length = -1;
        auto collision = 0;

        for (size_t src_bucket=0; _num_filled < old_num_filled; src_bucket++) {
            if (old_states[src_bucket] % 2 == State::EFILLED) {
                auto& src_pair = old_pairs[src_bucket];

                const auto key_hash = _hasher(src_pair.first);
                const auto dst_bucket = find_empty_slot(key_hash & _mask, 0);
                collision += _states[key_hash & _mask] % 2 == State::EFILLED;

                _states[dst_bucket] = KEYHASH_MASK(key_hash);
                new(_pairs + dst_bucket) PairT(std::move(src_pair));
                _num_filled += 1;
                src_pair.~PairT();
            }
        }

        if (_num_filled > 1000000)
            printf("\t\t\tmax_probe_length/_max_probe_length = %d/%d, collsions = %d, collision = %.2f%%\n",
                max_probe_length, _max_probe_length, collision, collision * 100.0f / _num_buckets);

        free(old_states);
    }

private:
    // Can we fit another element?
    void check_expand_need()
    {
        reserve(_num_filled);
    }

    // Find the bucket with this key, or return (size_t)-1
    template<typename KeyLike>
    size_t find_filled_bucket(const KeyLike& key) const
    {
        const auto key_hash = _hasher(key);
        auto next_bucket = (size_t)(key_hash & _mask);
        if (0 && _max_probe_length < simd_gaps / 4) {
            for (int offset = 0; offset <= _max_probe_length; ) {
                if (_states[next_bucket] % 2 == State::EFILLED) {
                    if (_eq(_pairs[next_bucket].first, key)) {
                        return next_bucket;
                    }
                }
                else if (_states[next_bucket] % 4 == State::EEMPTY) {
                    return _num_buckets; // End of the chain!
                }
                next_bucket = (key_hash + ++offset) & _mask;
            }
        } else {
            const char keymask = KEYHASH_MASK(key_hash);
            const auto filled = SET1_EPI8(keymask);

            for (int i = _max_probe_length; i >= 0; i -= simd_gaps) {
                const auto vec = LOADU_EPI8((decltype(&simd_empty))((char*)_states + next_bucket));
                auto maskf = MOVEMASK_EPI8(CMPEQ_EPI8(vec, filled));

                while (maskf != 0) {
                    const auto fbucket = next_bucket + CTZ(maskf);
                    if (EMH_UNLIKELY(fbucket >= _num_buckets))
                        break; //overflow
                    else if (_eq(_pairs[fbucket].first, key))
                        return fbucket;
                    maskf &= maskf - 1;
                }

                if (_max_probe_length >= simd_gaps) {
                const auto maske = MOVEMASK_EPI8(CMPEQ_EPI8(vec, simd_empty));
                if (maske != 0)
                    break;
                }

                next_bucket += simd_gaps;
                if (EMH_UNLIKELY(next_bucket >= _num_buckets)) {
                    i += next_bucket - _num_buckets;
                    next_bucket = 0;
                }
            }
        }
        return _num_buckets;
    }

    // Find the bucket with this key, or return a good empty bucket to place the key in.
    // In the latter case, the bucket is expected to be filled.
    template<typename KeyLike>
    size_t find_or_allocate(const KeyLike& key, uint64_t key_hash)
    {
        size_t hole = (size_t)-1;
        int offset = 0;
        if (0 && _max_probe_length <= simd_gaps / 2) {
            for (; offset <= _max_probe_length; ++offset) {
                auto bucket = (key_hash + offset) & _mask;
                if (_states[bucket] % 2 == State::EFILLED) {
                    if (_eq(_pairs[bucket].first, key)) {
                        return bucket;
                    }
                }
                else if (_states[bucket] % 4 == State::EEMPTY) {
                    return bucket;
                }
                else if (hole == (size_t)-1) {
                    hole = bucket;
                }
            }

            if (hole != (size_t)-1)
                return hole;
            return find_empty_slot((key_hash + offset) & _mask, offset);
        }

        const char keymask = KEYHASH_MASK(key_hash);
        const auto filled = SET1_EPI8(keymask);
        const auto bucket = (size_t)(key_hash & _mask);
        const auto round  = bucket + _max_probe_length;
        auto next_bucket  = bucket, i = bucket;

        for (; i <= round; i += simd_gaps) {
            const auto vec  = LOADU_EPI8((decltype(&simd_empty))((char*)_states + next_bucket));
            auto maskf  = MOVEMASK_EPI8(CMPEQ_EPI8(vec, filled));

            //1. find filled
            while (maskf != 0) {
                const auto fbucket = next_bucket + CTZ(maskf);
                if (EMH_UNLIKELY(fbucket >= _num_buckets))
                    break;
                else if (_eq(_pairs[fbucket].first, key))
                    return fbucket;
                maskf &= maskf - 1;
            }


            //2. find empty
            const auto maske = MOVEMASK_EPI8(CMPEQ_EPI8(vec, simd_empty));
            if (maske != 0) {
                const auto ebucket = std::min(hole, next_bucket + CTZ(maske));
                const int offset = (ebucket - bucket + _num_buckets) & _mask;
                if (EMH_UNLIKELY(offset > _max_probe_length))
                    _max_probe_length = offset;
                return ebucket;
            }

            //3. find erased
            if (hole == -1) {
                const auto maskd = MOVEMASK_EPI8(CMPEQ_EPI8(vec, simd_delete));
                if (maskd != 0)
                    hole = next_bucket + CTZ(maskd);
            }

            //4. next round
            next_bucket += simd_gaps;
            if (EMH_UNLIKELY(next_bucket >= _num_buckets)) {
                i -= next_bucket - _num_buckets;
                next_bucket = 0;
            }
        }

        if (EMH_LIKELY(hole != (size_t)-1))
            return hole;

        return find_empty_slot(next_bucket, i - bucket);
    }

    size_t find_empty_slot(size_t next_bucket, int offset)
    {
        constexpr uint64_t EMPTY_MASK = 0x0101010101010101ull;

        while (true) {
            const auto maske = *(uint64_t*)(_states + next_bucket) & EMPTY_MASK;
            if (EMH_LIKELY(maske != 0)) {
                const auto probe = CTZ(maske) / stat_bits;
                offset += probe;
                if (EMH_UNLIKELY(offset > _max_probe_length))
                    _max_probe_length = offset;
                const auto ebucket = next_bucket + probe;
                return ebucket;
            }
            next_bucket += stat_gaps;
            offset      += stat_gaps;
            if (EMH_UNLIKELY(next_bucket >= _num_buckets)) {
                offset -= next_bucket - _num_buckets;
                next_bucket = 0;
            }
        }
        return 0;
    }

    size_t find_filled_slot(size_t next_bucket) const
    {
#if 0
        if (_num_filled * 4 > _num_buckets) {
            while ((_states[next_bucket++] % 2 != State::EFILLED));
            return next_bucket - 1;
        }
#endif

        constexpr uint64_t EFILLED_FIND = 0xfefefefefefefefeull;
        while (true) {
            const auto maske = ~(*(uint64_t*)(_states + next_bucket) | EFILLED_FIND);
            if (EMH_LIKELY(maske != 0))
                return next_bucket + CTZ(maske) / stat_bits;
            next_bucket += stat_gaps;
        }
        return _num_buckets;
    }

private:
    enum State : uint8_t
    {
        EFILLED   = 0, // Is set with key/value
        EDELETE   = 3, // Is inside a search-chain, but is empty
        EEMPTY    = 1, // Never been touched empty
    };

    HashT   _hasher;
    EqT     _eq;
    uint8_t*_states           = nullptr;
    PairT*  _pairs            = nullptr;
    size_t  _num_buckets      =  0;
    size_t  _mask             =  0; // _num_buckets minus one
    size_t  _num_filled       =  0;
    int     _max_probe_length = -1; // Our longest bucket-brigade is this long. ONLY when we have zero elements is this ever negative (-1).
};

} // namespace emilib

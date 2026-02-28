#pragma once
// ===========================================================================
// std/unordered_map_impl.h - Out-of-line template definitions
//
// DO NOT include directly. Always include unordered_map.h instead.
// ===========================================================================

namespace std {

// ===== Placement helpers ===================================================

template<class T, class... Args>
inline T* _construct(void* p, Args&&... args) {
    return new (p) T(forward<Args>(args)...);
}

template<class T>
inline void _destroy(T* p) { p->~T(); }

// ###########################################################################
// Iterator
// ###########################################################################

// --- iterator --------------------------------------------------------------

template<class K,class V,class H,class E>
void unordered_map<K,V,H,E>::iterator::advance() {
    if (node_) node_ = node_->next;
    while (!node_ && ++bucket_idx_ < bucket_cnt_)
        node_ = buckets_[bucket_idx_];
}

template<class K,class V,class H,class E>
unordered_map<K,V,H,E>::iterator::iterator()
    : buckets_(nullptr), bucket_cnt_(0), bucket_idx_(0), node_(nullptr) {}

template<class K,class V,class H,class E>
unordered_map<K,V,H,E>::iterator::iterator(Node** b, size_type bc, size_type bi, Node* n)
    : buckets_(b), bucket_cnt_(bc), bucket_idx_(bi), node_(n) {}

template<class K,class V,class H,class E>
typename unordered_map<K,V,H,E>::value_type&
unordered_map<K,V,H,E>::iterator::operator*() const { return node_->kv; }

template<class K,class V,class H,class E>
typename unordered_map<K,V,H,E>::value_type*
unordered_map<K,V,H,E>::iterator::operator->() const { return &node_->kv; }

template<class K,class V,class H,class E>
typename unordered_map<K,V,H,E>::iterator&
unordered_map<K,V,H,E>::iterator::operator++() { advance(); return *this; }

template<class K,class V,class H,class E>
typename unordered_map<K,V,H,E>::iterator
unordered_map<K,V,H,E>::iterator::operator++(int) { auto t = *this; advance(); return t; }

template<class K,class V,class H,class E>
bool unordered_map<K,V,H,E>::iterator::operator==(const iterator& o) const
{ return node_ == o.node_; }

template<class K,class V,class H,class E>
bool unordered_map<K,V,H,E>::iterator::operator!=(const iterator& o) const
{ return node_ != o.node_; }

// --- const_iterator --------------------------------------------------------

template<class K,class V,class H,class E>
void unordered_map<K,V,H,E>::const_iterator::advance() {
    if (node_) node_ = node_->next;
    while (!node_ && ++bucket_idx_ < bucket_cnt_)
        node_ = buckets_[bucket_idx_];
}

template<class K,class V,class H,class E>
unordered_map<K,V,H,E>::const_iterator::const_iterator()
    : buckets_(nullptr), bucket_cnt_(0), bucket_idx_(0), node_(nullptr) {}

template<class K,class V,class H,class E>
unordered_map<K,V,H,E>::const_iterator::const_iterator(Node** b, size_type bc, size_type bi, Node* n)
    : buckets_(b), bucket_cnt_(bc), bucket_idx_(bi), node_(n) {}

template<class K,class V,class H,class E>
unordered_map<K,V,H,E>::const_iterator::const_iterator(const iterator& it)
    : buckets_(it.buckets_), bucket_cnt_(it.bucket_cnt_),
      bucket_idx_(it.bucket_idx_), node_(it.node_) {}

template<class K,class V,class H,class E>
const typename unordered_map<K,V,H,E>::value_type&
unordered_map<K,V,H,E>::const_iterator::operator*() const { return node_->kv; }

template<class K,class V,class H,class E>
const typename unordered_map<K,V,H,E>::value_type*
unordered_map<K,V,H,E>::const_iterator::operator->() const { return &node_->kv; }

template<class K,class V,class H,class E>
typename unordered_map<K,V,H,E>::const_iterator&
unordered_map<K,V,H,E>::const_iterator::operator++() { advance(); return *this; }

template<class K,class V,class H,class E>
typename unordered_map<K,V,H,E>::const_iterator
unordered_map<K,V,H,E>::const_iterator::operator++(int) { auto t = *this; advance(); return t; }

template<class K,class V,class H,class E>
bool unordered_map<K,V,H,E>::const_iterator::operator==(const const_iterator& o) const
{ return node_ == o.node_; }

template<class K,class V,class H,class E>
bool unordered_map<K,V,H,E>::const_iterator::operator!=(const const_iterator& o) const
{ return node_ != o.node_; }

// ###########################################################################
// Internal helpers
// ###########################################################################

template<class K,class V,class H,class E>
typename unordered_map<K,V,H,E>::size_type
unordered_map<K,V,H,E>::bucket_index(const K& k) const {
    return hasher_(k) % bucket_cnt_;
}

template<class K,class V,class H,class E>
void unordered_map<K,V,H,E>::alloc_buckets(size_type n) {
    buckets_ = allocate<Node*>(n);
    memset(buckets_, 0, n * sizeof(Node*));
    bucket_cnt_ = n;
}

template<class K,class V,class H,class E>
void unordered_map<K,V,H,E>::free_buckets() {
    if (buckets_) { deallocate(buckets_); buckets_ = nullptr; }
    bucket_cnt_ = 0;
}

template<class K,class V,class H,class E>
void unordered_map<K,V,H,E>::rehash_internal(size_type new_count) {
    if (new_count < INITIAL_BUCKETS) new_count = INITIAL_BUCKETS;

    Node** old = buckets_;
    size_type old_cnt = bucket_cnt_;

    alloc_buckets(new_count);

    for (size_type i = 0; i < old_cnt; ++i) {
        Node* cur = old[i];
        while (cur) {
            Node* nxt = cur->next;
            size_type idx = hasher_(cur->kv.first) % bucket_cnt_;
            cur->next = buckets_[idx];
            buckets_[idx] = cur;
            cur = nxt;
        }
    }
    if (old) mem_free(old);
}

template<class K,class V,class H,class E>
void unordered_map<K,V,H,E>::maybe_rehash() {
    if (bucket_cnt_ == 0) { alloc_buckets(INITIAL_BUCKETS); return; }
    if (static_cast<float>(size_ + 1) / bucket_cnt_ > max_load_)
        rehash_internal(bucket_cnt_ * 2);
}

template<class K,class V,class H,class E>
typename unordered_map<K,V,H,E>::Node*
unordered_map<K,V,H,E>::find_node(const K& k) const {
    if (bucket_cnt_ == 0) return nullptr;
    Node* cur = buckets_[hasher_(k) % bucket_cnt_];
    while (cur) {
        if (eq_(cur->kv.first, k)) return cur;
        cur = cur->next;
    }
    return nullptr;
}

template<class K,class V,class H,class E>
template<class... Args>
typename unordered_map<K,V,H,E>::Node*
unordered_map<K,V,H,E>::create_node(Args&&... args) {
    void* mem = mem_alloc(sizeof(Node));
    return _construct<Node>(mem, forward<Args>(args)...);
}

template<class K,class V,class H,class E>
void unordered_map<K,V,H,E>::destroy_node(Node* n) {
    _destroy(n);
    mem_free(n);
}

template<class K,class V,class H,class E>
template<class KK, class VV>
typename unordered_map<K,V,H,E>::Node*
unordered_map<K,V,H,E>::insert_default(KK&& k, VV&& v) {
    maybe_rehash();
    Node* n = create_node(pair<const K, V>(forward<KK>(k), forward<VV>(v)));
    size_type idx = bucket_index(n->kv.first);
    n->next = buckets_[idx];
    buckets_[idx] = n;
    ++size_;
    return n;
}

// ###########################################################################
// Lifecycle
// ###########################################################################

template<class K,class V,class H,class E>
unordered_map<K,V,H,E>::unordered_map() = default;

template<class K,class V,class H,class E>
unordered_map<K,V,H,E>::unordered_map(size_type hint, const H& h, const E& eq)
    : hasher_(h), eq_(eq)
{
    size_type n = INITIAL_BUCKETS;
    while (n < hint) n *= 2;
    alloc_buckets(n);
}

template<class K,class V,class H,class E>
unordered_map<K,V,H,E>::unordered_map(const unordered_map& o) {
    if (o.size_ == 0) return;
    alloc_buckets(o.bucket_cnt_);
    for (size_type i = 0; i < o.bucket_cnt_; ++i) {
        Node* src = o.buckets_[i];
        while (src) {
            Node* n = create_node(src->kv);
            n->next = buckets_[i];
            buckets_[i] = n;
            ++size_;
            src = src->next;
        }
    }
    max_load_ = o.max_load_;
    hasher_ = o.hasher_;
    eq_ = o.eq_;
}

template<class K,class V,class H,class E>
unordered_map<K,V,H,E>::unordered_map(unordered_map&& o) noexcept
    : buckets_(o.buckets_), bucket_cnt_(o.bucket_cnt_),
      size_(o.size_), max_load_(o.max_load_),
      hasher_(move(o.hasher_)), eq_(move(o.eq_))
{
    o.buckets_ = nullptr; o.bucket_cnt_ = 0; o.size_ = 0;
}

template<class K,class V,class H,class E>
unordered_map<K,V,H,E>&
unordered_map<K,V,H,E>::operator=(const unordered_map& o) {
    if (this != &o) { unordered_map tmp(o); swap(*this, tmp); }
    return *this;
}

template<class K,class V,class H,class E>
unordered_map<K,V,H,E>&
unordered_map<K,V,H,E>::operator=(unordered_map&& o) noexcept {
    if (this != &o) {
        clear(); free_buckets();
        buckets_ = o.buckets_; bucket_cnt_ = o.bucket_cnt_;
        size_ = o.size_; max_load_ = o.max_load_;
        hasher_ = move(o.hasher_); eq_ = move(o.eq_);
        o.buckets_ = nullptr; o.bucket_cnt_ = 0; o.size_ = 0;
    }
    return *this;
}

template<class K,class V,class H,class E>
unordered_map<K,V,H,E>::~unordered_map() {
    clear(); free_buckets();
}

// ###########################################################################
// Capacity
// ###########################################################################

template<class K,class V,class H,class E>
bool unordered_map<K,V,H,E>::empty() const noexcept { return size_ == 0; }

template<class K,class V,class H,class E>
typename unordered_map<K,V,H,E>::size_type
unordered_map<K,V,H,E>::size() const noexcept { return size_; }

template<class K,class V,class H,class E>
typename unordered_map<K,V,H,E>::size_type
unordered_map<K,V,H,E>::bucket_count() const noexcept { return bucket_cnt_; }

// ###########################################################################
// Iterators
// ###########################################################################

template<class K,class V,class H,class E>
typename unordered_map<K,V,H,E>::iterator
unordered_map<K,V,H,E>::begin() noexcept {
    for (size_type i = 0; i < bucket_cnt_; ++i)
        if (buckets_[i]) return iterator(buckets_, bucket_cnt_, i, buckets_[i]);
    return end();
}

template<class K,class V,class H,class E>
typename unordered_map<K,V,H,E>::iterator
unordered_map<K,V,H,E>::end() noexcept {
    return iterator(buckets_, bucket_cnt_, bucket_cnt_, nullptr);
}

template<class K,class V,class H,class E>
typename unordered_map<K,V,H,E>::const_iterator
unordered_map<K,V,H,E>::begin() const noexcept {
    for (size_type i = 0; i < bucket_cnt_; ++i)
        if (buckets_[i])
            return const_iterator(const_cast<Node**>(buckets_), bucket_cnt_, i, buckets_[i]);
    return end();
}

template<class K,class V,class H,class E>
typename unordered_map<K,V,H,E>::const_iterator
unordered_map<K,V,H,E>::end() const noexcept {
    return const_iterator(const_cast<Node**>(buckets_), bucket_cnt_, bucket_cnt_, nullptr);
}

template<class K,class V,class H,class E>
typename unordered_map<K,V,H,E>::const_iterator
unordered_map<K,V,H,E>::cbegin() const noexcept { return begin(); }

template<class K,class V,class H,class E>
typename unordered_map<K,V,H,E>::const_iterator
unordered_map<K,V,H,E>::cend() const noexcept { return end(); }

// ###########################################################################
// Lookup
// ###########################################################################

template<class K,class V,class H,class E>
typename unordered_map<K,V,H,E>::iterator
unordered_map<K,V,H,E>::find(const K& k) {
    if (bucket_cnt_ == 0) return end();
    size_type idx = bucket_index(k);
    for (Node* c = buckets_[idx]; c; c = c->next)
        if (eq_(c->kv.first, k)) return iterator(buckets_, bucket_cnt_, idx, c);
    return end();
}

template<class K,class V,class H,class E>
typename unordered_map<K,V,H,E>::const_iterator
unordered_map<K,V,H,E>::find(const K& k) const {
    if (bucket_cnt_ == 0) return end();
    size_type idx = hasher_(k) % bucket_cnt_;
    for (Node* c = buckets_[idx]; c; c = c->next)
        if (eq_(c->kv.first, k))
            return const_iterator(const_cast<Node**>(buckets_), bucket_cnt_, idx, c);
    return end();
}

template<class K,class V,class H,class E>
typename unordered_map<K,V,H,E>::size_type
unordered_map<K,V,H,E>::count(const K& k) const { return find_node(k) ? 1 : 0; }

template<class K,class V,class H,class E>
bool unordered_map<K,V,H,E>::contains(const K& k) const { return find_node(k) != nullptr; }

// ###########################################################################
// Element access
// ###########################################################################

template<class K,class V,class H,class E>
V& unordered_map<K,V,H,E>::operator[](const K& k) {
    Node* n = find_node(k);
    if (n) return n->kv.second;
    return insert_default(k, V{})->kv.second;
}

template<class K,class V,class H,class E>
V& unordered_map<K,V,H,E>::operator[](K&& k) {
    Node* n = find_node(k);
    if (n) return n->kv.second;
    return insert_default(move(k), V{})->kv.second;
}

template<class K,class V,class H,class E>
V& unordered_map<K,V,H,E>::at(const K& k) { return find_node(k)->kv.second; }

template<class K,class V,class H,class E>
const V& unordered_map<K,V,H,E>::at(const K& k) const { return find_node(k)->kv.second; }

// ###########################################################################
// Modifiers
// ###########################################################################

template<class K,class V,class H,class E>
pair<typename unordered_map<K,V,H,E>::iterator, bool>
unordered_map<K,V,H,E>::insert(const value_type& kv) {
    Node* ex = find_node(kv.first);
    if (ex) return { iterator(buckets_, bucket_cnt_, bucket_index(kv.first), ex), false };
    maybe_rehash();
    Node* n = create_node(kv);
    size_type idx = bucket_index(n->kv.first);
    n->next = buckets_[idx]; buckets_[idx] = n; ++size_;
    return { iterator(buckets_, bucket_cnt_, idx, n), true };
}

template<class K,class V,class H,class E>
pair<typename unordered_map<K,V,H,E>::iterator, bool>
unordered_map<K,V,H,E>::insert(value_type&& kv) {
    Node* ex = find_node(kv.first);
    if (ex) return { iterator(buckets_, bucket_cnt_, bucket_index(kv.first), ex), false };
    maybe_rehash();
    Node* n = create_node(move(kv));
    size_type idx = bucket_index(n->kv.first);
    n->next = buckets_[idx]; buckets_[idx] = n; ++size_;
    return { iterator(buckets_, bucket_cnt_, idx, n), true };
}

template<class K,class V,class H,class E>
template<class... Args>
pair<typename unordered_map<K,V,H,E>::iterator, bool>
unordered_map<K,V,H,E>::emplace(Args&&... args) {
    Node* n = create_node(forward<Args>(args)...);
    Node* ex = find_node(n->kv.first);
    if (ex) {
        destroy_node(n);
        return { iterator(buckets_, bucket_cnt_, bucket_index(ex->kv.first), ex), false };
    }
    maybe_rehash();
    size_type idx = bucket_index(n->kv.first);
    n->next = buckets_[idx]; buckets_[idx] = n; ++size_;
    return { iterator(buckets_, bucket_cnt_, idx, n), true };
}

template<class K,class V,class H,class E>
template<class M>
pair<typename unordered_map<K,V,H,E>::iterator, bool>
unordered_map<K,V,H,E>::insert_or_assign(const K& k, M&& val) {
    Node* ex = find_node(k);
    if (ex) {
        ex->kv.second = forward<M>(val);
        return { iterator(buckets_, bucket_cnt_, bucket_index(k), ex), false };
    }
    maybe_rehash();
    Node* n = create_node(pair<const K, V>(k, forward<M>(val)));
    size_type idx = bucket_index(n->kv.first);
    n->next = buckets_[idx]; buckets_[idx] = n; ++size_;
    return { iterator(buckets_, bucket_cnt_, idx, n), true };
}

template<class K,class V,class H,class E>
typename unordered_map<K,V,H,E>::size_type
unordered_map<K,V,H,E>::erase(const K& k) {
    if (bucket_cnt_ == 0) return 0;
    size_type idx = bucket_index(k);
    Node* cur = buckets_[idx]; Node* prev = nullptr;
    while (cur) {
        if (eq_(cur->kv.first, k)) {
            if (prev) prev->next = cur->next; else buckets_[idx] = cur->next;
            destroy_node(cur); --size_;
            return 1;
        }
        prev = cur; cur = cur->next;
    }
    return 0;
}

template<class K,class V,class H,class E>
typename unordered_map<K,V,H,E>::iterator
unordered_map<K,V,H,E>::erase(iterator it) {
    if (!it.node_) return end();
    iterator nxt = it; ++nxt;
    size_type idx = it.bucket_idx_;
    Node* cur = buckets_[idx]; Node* prev = nullptr;
    while (cur) {
        if (cur == it.node_) {
            if (prev) prev->next = cur->next; else buckets_[idx] = cur->next;
            destroy_node(cur); --size_;
            return nxt;
        }
        prev = cur; cur = cur->next;
    }
    return end();
}

template<class K,class V,class H,class E>
void unordered_map<K,V,H,E>::clear() noexcept {
    for (size_type i = 0; i < bucket_cnt_; ++i) {
        Node* cur = buckets_[i];
        while (cur) { Node* nxt = cur->next; destroy_node(cur); cur = nxt; }
        buckets_[i] = nullptr;
    }
    size_ = 0;
}

// ###########################################################################
// Hash policy
// ###########################################################################

template<class K,class V,class H,class E>
float unordered_map<K,V,H,E>::load_factor() const noexcept {
    return bucket_cnt_ == 0 ? 0.f : static_cast<float>(size_) / bucket_cnt_;
}

template<class K,class V,class H,class E>
float unordered_map<K,V,H,E>::max_load_factor() const noexcept { return max_load_; }

template<class K,class V,class H,class E>
void unordered_map<K,V,H,E>::max_load_factor(float ml) { max_load_ = ml; }

template<class K,class V,class H,class E>
void unordered_map<K,V,H,E>::rehash(size_type count) {
    size_type needed = static_cast<size_type>(static_cast<float>(size_) / max_load_) + 1;
    if (count < needed) count = needed;
    size_type n = INITIAL_BUCKETS;
    while (n < count) n *= 2;
    if (n != bucket_cnt_) rehash_internal(n);
}

template<class K,class V,class H,class E>
void unordered_map<K,V,H,E>::reserve(size_type count) {
    rehash(static_cast<size_type>(static_cast<float>(count) / max_load_) + 1);
}

} // namespace std

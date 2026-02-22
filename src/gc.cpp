#include "muslisp/gc.hpp"

#include <algorithm>

#include "muslisp/env.hpp"
#include "muslisp/value.hpp"

namespace muslisp {

gc::gc() = default;

gc::~gc() {
    gc_node* cursor = head_;
    while (cursor) {
        gc_node* next = cursor->next;
        delete cursor;
        cursor = next;
    }
}

void gc::link_node(gc_node* node, std::size_t bytes) {
    node->next = head_;
    head_ = node;

    ++allocated_objects_current_;
    ++total_allocated_objects_;
    bytes_allocated_ += bytes;

    if (allocated_objects_current_ > next_gc_threshold_) {
        collection_requested_ = true;
    }
}

void gc::request_collection() {
    collection_requested_ = true;
}

void gc::maybe_collect() {
    if (collection_requested_) {
        collect();
    }
}

void gc::collect() {
    for (value* slot : root_slots_) {
        if (slot) {
            mark_value(*slot);
        }
    }

    for (env_ptr root : root_envs_) {
        mark_env(root);
    }

    mark_interned_symbols(*this);

    sweep();
}

void gc::mark_node(gc_node* node) {
    if (!node || node->marked) {
        return;
    }

    node->marked = true;
    node->gc_mark_children(*this);
}

void gc::mark_value(value v) {
    mark_node(v);
}

void gc::mark_env(env_ptr env) {
    mark_node(env);
}

void gc::sweep() {
    gc_node** link = &head_;
    std::size_t live_count = 0;
    std::size_t live_bytes = 0;

    while (*link) {
        gc_node* node = *link;
        if (!node->marked) {
            *link = node->next;
            delete node;
            continue;
        }

        node->marked = false;
        ++live_count;
        live_bytes += node->gc_size_bytes();
        link = &node->next;
    }

    allocated_objects_current_ = live_count;
    live_objects_after_last_gc_ = live_count;
    bytes_allocated_ = live_bytes;

    const std::size_t min_threshold = 256;
    const std::size_t grown = live_count * 2;
    next_gc_threshold_ = grown > min_threshold ? grown : min_threshold;

    collection_requested_ = false;
}

void gc::add_root_slot(value* slot) {
    if (!slot) {
        return;
    }
    root_slots_.push_back(slot);
}

void gc::remove_root_slot(value* slot) {
    const auto it = std::find(root_slots_.rbegin(), root_slots_.rend(), slot);
    if (it != root_slots_.rend()) {
        root_slots_.erase(std::next(it).base());
    }
}

void gc::register_root_env(env_ptr env) {
    if (!env) {
        return;
    }
    const auto it = std::find(root_envs_.begin(), root_envs_.end(), env);
    if (it == root_envs_.end()) {
        root_envs_.push_back(env);
    }
}

void gc::unregister_root_env(env_ptr env) {
    root_envs_.erase(std::remove(root_envs_.begin(), root_envs_.end(), env), root_envs_.end());
}

gc_stats_snapshot gc::stats() const noexcept {
    gc_stats_snapshot snapshot;
    snapshot.total_allocated_objects = total_allocated_objects_;
    snapshot.live_objects_after_last_gc = live_objects_after_last_gc_;
    snapshot.bytes_allocated = bytes_allocated_;
    snapshot.next_gc_threshold = next_gc_threshold_;
    return snapshot;
}

gc& default_gc() {
    static gc heap;
    return heap;
}

gc_root_scope::gc_root_scope(gc& heap) : heap_(heap), begin_index_(heap.root_slots_.size()) {}

gc_root_scope::~gc_root_scope() {
    heap_.root_slots_.resize(begin_index_);
}

void gc_root_scope::add(value* slot) {
    heap_.root_slots_.push_back(slot);
}

}  // namespace muslisp

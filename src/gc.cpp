#include "muslisp/gc.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>

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
        requested_reason_ = gc_collection_reason::threshold;
    }
}

void gc::request_collection() {
    collection_requested_ = true;
    requested_reason_ = gc_collection_reason::requested;
}

void gc::maybe_collect() {
    if (!collection_requested_) {
        return;
    }
    if (policy_ == gc_policy::manual) {
        return;
    }
    if (policy_ == gc_policy::between_ticks && in_tick()) {
        return;
    }
    if (policy_ == gc_policy::fail_on_tick_gc && in_tick()) {
        throw std::runtime_error("gc: collection requested during tick under fail-on-tick-gc policy");
    }
    const gc_collection_reason reason =
        policy_ == gc_policy::between_ticks ? gc_collection_reason::between_ticks : requested_reason_;
    collect_impl(reason, false);
}

void gc::collect() {
    if (policy_ == gc_policy::fail_on_tick_gc && in_tick()) {
        throw std::runtime_error("gc: forced collection during tick under fail-on-tick-gc policy");
    }
    collect_impl(gc_collection_reason::forced, true);
}

void gc::collect_impl(gc_collection_reason reason, bool forced) {
    const auto pause_start = std::chrono::steady_clock::now();
    const std::uint64_t collection_id = collection_count_ + 1;
    const std::size_t live_objects_before = allocated_objects_current_;
    const std::size_t heap_live_bytes_before = bytes_allocated_;

    gc_lifecycle_event begin;
    begin.begin = true;
    begin.collection_id = collection_id;
    begin.reason = reason;
    begin.policy = policy_;
    begin.forced = forced;
    begin.in_tick = in_tick();
    begin.heap_live_bytes_before = heap_live_bytes_before;
    begin.live_objects_before = live_objects_before;
    emit_lifecycle(begin);

    const auto mark_start = std::chrono::steady_clock::now();
    for (value* slot : root_slots_) {
        if (slot) {
            mark_value(*slot);
        }
    }

    for (env_ptr root : root_envs_) {
        mark_env(root);
    }

    mark_interned_symbols(*this);

    const auto mark_end = std::chrono::steady_clock::now();
    const sweep_result swept = sweep();
    const auto sweep_end = std::chrono::steady_clock::now();

    const auto mark_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(mark_end - mark_start).count();
    const auto sweep_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(sweep_end - mark_end).count();
    const auto pause_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(sweep_end - pause_start).count();

    collection_count_ = collection_id;
    total_pause_ns_ += static_cast<std::uint64_t>(pause_ns);
    freed_objects_total_ += swept.freed_count;

    gc_lifecycle_event end;
    end.begin = false;
    end.collection_id = collection_id;
    end.reason = reason;
    end.policy = policy_;
    end.forced = forced;
    end.in_tick = in_tick();
    end.heap_live_bytes_before = heap_live_bytes_before;
    end.heap_live_bytes_after = swept.live_bytes;
    end.live_objects_before = live_objects_before;
    end.live_objects_after = swept.live_count;
    end.freed_objects = swept.freed_count;
    end.mark_time_ns = static_cast<std::uint64_t>(mark_ns);
    end.sweep_time_ns = static_cast<std::uint64_t>(sweep_ns);
    end.pause_time_ns = static_cast<std::uint64_t>(pause_ns);
    emit_lifecycle(end);
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

gc::sweep_result gc::sweep() {
    gc_node** link = &head_;
    std::size_t live_count = 0;
    std::size_t live_bytes = 0;
    std::size_t freed_count = 0;

    while (*link) {
        gc_node* node = *link;
        if (!node->marked) {
            *link = node->next;
            delete node;
            ++freed_count;
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
    requested_reason_ = gc_collection_reason::requested;

    return sweep_result{.live_count = live_count, .live_bytes = live_bytes, .freed_count = freed_count};
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
    root_envs_.push_back(env);
}

void gc::unregister_root_env(env_ptr env) {
    const auto it = std::find(root_envs_.rbegin(), root_envs_.rend(), env);
    if (it != root_envs_.rend()) {
        root_envs_.erase(std::next(it).base());
    }
}

gc_stats_snapshot gc::stats() const noexcept {
    gc_stats_snapshot snapshot;
    snapshot.total_allocated_objects = total_allocated_objects_;
    snapshot.live_objects_after_last_gc = live_objects_after_last_gc_;
    snapshot.bytes_allocated = bytes_allocated_;
    snapshot.next_gc_threshold = next_gc_threshold_;
    snapshot.collection_count = collection_count_;
    snapshot.total_pause_ns = total_pause_ns_;
    snapshot.freed_objects_total = freed_objects_total_;
    return snapshot;
}

void gc::set_policy(gc_policy policy) noexcept {
    policy_ = policy;
}

gc_policy gc::policy() const noexcept {
    return policy_;
}

void gc::enter_tick() noexcept {
    ++tick_depth_;
}

void gc::exit_tick() {
    if (tick_depth_ > 0) {
        --tick_depth_;
    }
    if (tick_depth_ == 0 && collection_requested_ && policy_ == gc_policy::between_ticks) {
        collect_impl(gc_collection_reason::between_ticks, false);
    }
}

bool gc::in_tick() const noexcept {
    return tick_depth_ > 0;
}

void gc::set_lifecycle_listener(lifecycle_listener listener) {
    lifecycle_listener_ = std::move(listener);
}

void gc::clear_lifecycle_listener() {
    lifecycle_listener_ = {};
}

std::string_view gc::policy_name(gc_policy policy) noexcept {
    switch (policy) {
        case gc_policy::default_policy:
            return "default";
        case gc_policy::between_ticks:
            return "between-ticks";
        case gc_policy::manual:
            return "manual";
        case gc_policy::fail_on_tick_gc:
            return "fail-on-tick-gc";
    }
    return "default";
}

std::string_view gc::collection_reason_name(gc_collection_reason reason) noexcept {
    switch (reason) {
        case gc_collection_reason::threshold:
            return "threshold";
        case gc_collection_reason::requested:
            return "requested";
        case gc_collection_reason::forced:
            return "forced";
        case gc_collection_reason::between_ticks:
            return "between-ticks";
    }
    return "requested";
}

bool gc::parse_policy(std::string_view text, gc_policy& out) noexcept {
    if (!text.empty() && text.front() == ':') {
        text.remove_prefix(1);
    }
    if (text == "default") {
        out = gc_policy::default_policy;
        return true;
    }
    if (text == "between-ticks") {
        out = gc_policy::between_ticks;
        return true;
    }
    if (text == "manual") {
        out = gc_policy::manual;
        return true;
    }
    if (text == "fail-on-tick-gc") {
        out = gc_policy::fail_on_tick_gc;
        return true;
    }
    return false;
}

void gc::emit_lifecycle(const gc_lifecycle_event& event) {
    if (lifecycle_listener_) {
        lifecycle_listener_(event);
    }
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

gc_tick_scope::gc_tick_scope(gc& heap) : heap_(heap) {
    heap_.enter_tick();
}

gc_tick_scope::~gc_tick_scope() {
    heap_.exit_tick();
}

}  // namespace muslisp

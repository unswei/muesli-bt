#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace muslisp {

struct object;
struct env;

using value = object*;
using env_ptr = env*;

class gc;

struct gc_node {
    bool marked = false;
    gc_node* next = nullptr;

    virtual ~gc_node() = default;
    virtual void gc_mark_children(gc& heap) = 0;
    [[nodiscard]] virtual std::size_t gc_size_bytes() const = 0;
};

struct gc_stats_snapshot {
    std::size_t total_allocated_objects = 0;
    std::size_t live_objects_after_last_gc = 0;
    std::size_t bytes_allocated = 0;
    std::size_t next_gc_threshold = 0;
    std::uint64_t collection_count = 0;
    std::uint64_t total_pause_ns = 0;
    std::size_t freed_objects_total = 0;
};

enum class gc_policy {
    default_policy,
    between_ticks,
    manual,
    fail_on_tick_gc,
};

enum class gc_collection_reason {
    threshold,
    requested,
    forced,
    between_ticks,
};

struct gc_lifecycle_event {
    bool begin = true;
    std::uint64_t collection_id = 0;
    gc_collection_reason reason = gc_collection_reason::forced;
    gc_policy policy = gc_policy::default_policy;
    bool forced = false;
    bool in_tick = false;
    std::size_t heap_live_bytes_before = 0;
    std::size_t heap_live_bytes_after = 0;
    std::size_t live_objects_before = 0;
    std::size_t live_objects_after = 0;
    std::size_t freed_objects = 0;
    std::uint64_t mark_time_ns = 0;
    std::uint64_t sweep_time_ns = 0;
    std::uint64_t pause_time_ns = 0;
};

class gc {
public:
    using lifecycle_listener = std::function<void(const gc_lifecycle_event&)>;

    gc();
    ~gc();

    gc(const gc&) = delete;
    gc& operator=(const gc&) = delete;

    template <typename T, typename... Args>
    T* allocate(Args&&... args) {
        static_assert(std::is_base_of_v<gc_node, T>, "GC can allocate only gc_node-derived types");
        T* node = new T(std::forward<Args>(args)...);
        link_node(node, node->gc_size_bytes());
        return node;
    }

    void request_collection();
    void maybe_collect();
    void collect();

    void mark_value(value v);
    void mark_env(env_ptr env);

    void add_root_slot(value* slot);
    void remove_root_slot(value* slot);
    void register_root_env(env_ptr env);
    void unregister_root_env(env_ptr env);

    [[nodiscard]] gc_stats_snapshot stats() const noexcept;
    void set_policy(gc_policy policy) noexcept;
    [[nodiscard]] gc_policy policy() const noexcept;
    void enter_tick() noexcept;
    void exit_tick();
    [[nodiscard]] bool in_tick() const noexcept;
    void set_lifecycle_listener(lifecycle_listener listener);
    void clear_lifecycle_listener();

    [[nodiscard]] static std::string_view policy_name(gc_policy policy) noexcept;
    [[nodiscard]] static std::string_view collection_reason_name(gc_collection_reason reason) noexcept;
    [[nodiscard]] static bool parse_policy(std::string_view text, gc_policy& out) noexcept;

private:
    struct sweep_result {
        std::size_t live_count = 0;
        std::size_t live_bytes = 0;
        std::size_t freed_count = 0;
    };

    void link_node(gc_node* node, std::size_t bytes);
    void mark_node(gc_node* node);
    void collect_impl(gc_collection_reason reason, bool forced);
    [[nodiscard]] sweep_result sweep();
    void emit_lifecycle(const gc_lifecycle_event& event);

    gc_node* head_ = nullptr;
    std::size_t allocated_objects_current_ = 0;
    std::size_t total_allocated_objects_ = 0;
    std::size_t live_objects_after_last_gc_ = 0;
    std::size_t bytes_allocated_ = 0;
    std::size_t next_gc_threshold_ = 256;
    bool collection_requested_ = false;
    gc_collection_reason requested_reason_ = gc_collection_reason::requested;
    gc_policy policy_ = gc_policy::default_policy;
    std::size_t tick_depth_ = 0;
    std::uint64_t collection_count_ = 0;
    std::uint64_t total_pause_ns_ = 0;
    std::size_t freed_objects_total_ = 0;
    lifecycle_listener lifecycle_listener_{};

    std::vector<value*> root_slots_;
    std::vector<env_ptr> root_envs_;

    friend class gc_root_scope;
};

gc& default_gc();

class gc_root_scope {
public:
    explicit gc_root_scope(gc& heap);
    ~gc_root_scope();

    gc_root_scope(const gc_root_scope&) = delete;
    gc_root_scope& operator=(const gc_root_scope&) = delete;

    void add(value* slot);

private:
    gc& heap_;
    std::size_t begin_index_ = 0;
};

class gc_tick_scope {
public:
    explicit gc_tick_scope(gc& heap);
    ~gc_tick_scope();

    gc_tick_scope(const gc_tick_scope&) = delete;
    gc_tick_scope& operator=(const gc_tick_scope&) = delete;

private:
    gc& heap_;
};

}  // namespace muslisp

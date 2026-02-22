#pragma once

#include <cstddef>
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
};

class gc {
public:
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

private:
    void link_node(gc_node* node, std::size_t bytes);
    void mark_node(gc_node* node);
    void sweep();

    gc_node* head_ = nullptr;
    std::size_t allocated_objects_current_ = 0;
    std::size_t total_allocated_objects_ = 0;
    std::size_t live_objects_after_last_gc_ = 0;
    std::size_t bytes_allocated_ = 0;
    std::size_t next_gc_threshold_ = 256;
    bool collection_requested_ = false;

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

}  // namespace muslisp

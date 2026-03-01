#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "bt/ast.hpp"
#include "bt/scheduler.hpp"
#include "bt/status.hpp"

namespace bt {

enum class trace_event_kind {
    tick_begin,
    tick_end,
    node_enter,
    node_exit,
    bb_write,
    bb_read,
    scheduler_submit,
    scheduler_start,
    scheduler_finish,
    scheduler_cancel,
    node_halt,
    node_preempt,
    warning,
    error
};

struct trace_event {
    trace_event_kind kind = trace_event_kind::tick_begin;
    std::uint64_t sequence = 0;
    std::uint64_t tick_index = 0;
    std::chrono::steady_clock::time_point ts{};

    node_id node = 0;
    status node_status = status::failure;
    std::chrono::nanoseconds duration{0};

    job_id job = 0;
    job_status job_st = job_status::unknown;

    std::string key;
    std::string value_repr;
    std::string message;
};

class trace_buffer {
public:
    explicit trace_buffer(std::size_t capacity_events);

    void push(trace_event ev);
    std::vector<trace_event> snapshot() const;
    std::size_t size() const;
    std::size_t capacity() const;
    void clear();

private:
    std::size_t capacity_;
    std::vector<trace_event> events_;
    std::uint64_t sequence_ = 0;
    mutable std::mutex mutex_;
};

const char* trace_event_kind_name(trace_event_kind kind) noexcept;

}  // namespace bt

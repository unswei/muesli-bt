#include "bt/trace.hpp"

namespace bt {

trace_buffer::trace_buffer(std::size_t capacity_events) : capacity_(capacity_events) {
    events_.reserve(capacity_);
}

void trace_buffer::push(trace_event ev) {
    std::lock_guard<std::mutex> lock(mutex_);
    ev.sequence = ++sequence_;

    if (capacity_ == 0) {
        return;
    }

    if (events_.size() == capacity_) {
        events_.erase(events_.begin());
    }
    events_.push_back(std::move(ev));
}

std::vector<trace_event> trace_buffer::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return events_;
}

std::size_t trace_buffer::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return events_.size();
}

std::size_t trace_buffer::capacity() const {
    return capacity_;
}

void trace_buffer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.clear();
}

const char* trace_event_kind_name(trace_event_kind kind) noexcept {
    switch (kind) {
        case trace_event_kind::tick_begin:
            return "tick_begin";
        case trace_event_kind::tick_end:
            return "tick_end";
        case trace_event_kind::node_enter:
            return "node_enter";
        case trace_event_kind::node_exit:
            return "node_exit";
        case trace_event_kind::bb_write:
            return "bb_write";
        case trace_event_kind::bb_read:
            return "bb_read";
        case trace_event_kind::scheduler_submit:
            return "scheduler_submit";
        case trace_event_kind::scheduler_start:
            return "scheduler_start";
        case trace_event_kind::scheduler_finish:
            return "scheduler_finish";
        case trace_event_kind::scheduler_cancel:
            return "scheduler_cancel";
        case trace_event_kind::warning:
            return "warning";
        case trace_event_kind::error:
            return "error";
    }
    return "unknown";
}

}  // namespace bt

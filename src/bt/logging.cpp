#include "bt/logging.hpp"

namespace bt {

memory_log_sink::memory_log_sink(std::size_t capacity_records) : capacity_(capacity_records) {
    records_.reserve(capacity_);
}

void memory_log_sink::write(const log_record& rec) {
    std::lock_guard<std::mutex> lock(mutex_);

    log_record copy = rec;
    copy.sequence = ++sequence_;

    if (capacity_ == 0) {
        return;
    }

    if (records_.size() == capacity_) {
        records_.erase(records_.begin());
    }
    records_.push_back(std::move(copy));
}

std::vector<log_record> memory_log_sink::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return records_;
}

std::size_t memory_log_sink::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return records_.size();
}

std::size_t memory_log_sink::capacity() const {
    return capacity_;
}

void memory_log_sink::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    records_.clear();
}

const char* log_level_name(log_level level) noexcept {
    switch (level) {
        case log_level::debug:
            return "debug";
        case log_level::info:
            return "info";
        case log_level::warn:
            return "warn";
        case log_level::error:
            return "error";
    }
    return "unknown";
}

}  // namespace bt

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "bt/ast.hpp"

namespace bt {

enum class log_level {
    debug,
    info,
    warn,
    error
};

struct log_record {
    std::uint64_t sequence = 0;
    std::chrono::steady_clock::time_point ts{};
    log_level level = log_level::info;
    std::uint64_t tick_index = 0;
    node_id node = 0;
    std::string category;
    std::string message;
};

class log_sink {
public:
    virtual ~log_sink() = default;
    virtual void write(const log_record& rec) = 0;
};

class memory_log_sink final : public log_sink {
public:
    explicit memory_log_sink(std::size_t capacity_records);

    void write(const log_record& rec) override;
    std::vector<log_record> snapshot() const;
    std::size_t size() const;
    std::size_t capacity() const;
    void clear();

private:
    std::size_t capacity_;
    std::vector<log_record> records_;
    std::uint64_t sequence_ = 0;
    mutable std::mutex mutex_;
};

const char* log_level_name(log_level level) noexcept;

}  // namespace bt

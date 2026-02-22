#pragma once

#include <any>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "bt/profile.hpp"

namespace bt {

using job_id = std::uint64_t;

enum class job_status {
    queued,
    running,
    done,
    failed,
    cancelled,
    unknown
};

struct job_timing {
    std::chrono::steady_clock::time_point submitted_at{};
    std::chrono::steady_clock::time_point started_at{};
    std::chrono::steady_clock::time_point finished_at{};
};

struct job_info {
    job_status status = job_status::unknown;
    job_timing timing{};
    std::string task_name;
    std::string error_text;
};

struct job_result {
    std::any payload;
};

struct job_request {
    std::string task_name;
    std::function<job_result()> fn;
    std::optional<std::chrono::milliseconds> timeout;
};

class scheduler {
public:
    virtual ~scheduler() = default;

    virtual job_id submit(job_request req) = 0;
    virtual job_info get_info(job_id id) const = 0;
    virtual bool try_get_result(job_id id, job_result& out) = 0;
    virtual bool cancel(job_id id) = 0;

    virtual scheduler_profile_stats stats_snapshot() const = 0;
};

class thread_pool_scheduler final : public scheduler {
public:
    explicit thread_pool_scheduler(std::size_t worker_count = 0);
    ~thread_pool_scheduler() override;

    thread_pool_scheduler(const thread_pool_scheduler&) = delete;
    thread_pool_scheduler& operator=(const thread_pool_scheduler&) = delete;

    job_id submit(job_request req) override;
    job_info get_info(job_id id) const override;
    bool try_get_result(job_id id, job_result& out) override;
    bool cancel(job_id id) override;
    scheduler_profile_stats stats_snapshot() const override;

private:
    struct job_state {
        job_id id = 0;
        job_status status = job_status::queued;
        job_timing timing{};
        std::string task_name;
        std::string error_text;
        job_request request;
        std::optional<job_result> result;
        bool cancel_requested = false;
    };

    void worker_loop();

    std::size_t worker_count_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stopping_ = false;
    job_id next_job_id_ = 1;

    std::queue<job_id> queue_;
    std::unordered_map<job_id, std::shared_ptr<job_state>> jobs_;
    scheduler_profile_stats stats_{};
    std::vector<std::thread> workers_;
};

const char* job_status_name(job_status st) noexcept;

}  // namespace bt

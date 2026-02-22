#include "bt/scheduler.hpp"

#include <exception>

namespace bt {
namespace {

std::size_t effective_worker_count(std::size_t requested) {
    if (requested > 0) {
        return requested;
    }
    const auto hc = std::thread::hardware_concurrency();
    if (hc == 0) {
        return 2;
    }
    return hc > 4 ? 4 : hc;
}

}  // namespace

thread_pool_scheduler::thread_pool_scheduler(std::size_t worker_count) : worker_count_(effective_worker_count(worker_count)) {
    workers_.reserve(worker_count_);
    for (std::size_t i = 0; i < worker_count_; ++i) {
        workers_.emplace_back([this] { worker_loop(); });
    }
}

thread_pool_scheduler::~thread_pool_scheduler() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_all();
    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

job_id thread_pool_scheduler::submit(job_request req) {
    if (!req.fn) {
        throw std::invalid_argument("scheduler submit: empty job function");
    }

    auto state = std::make_shared<job_state>();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        state->id = next_job_id_++;
        state->status = job_status::queued;
        state->timing.submitted_at = std::chrono::steady_clock::now();
        state->task_name = req.task_name;
        state->request = std::move(req);

        jobs_[state->id] = state;
        queue_.push(state->id);
        ++stats_.submitted;
    }

    cv_.notify_one();
    return state->id;
}

job_info thread_pool_scheduler::get_info(job_id id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = jobs_.find(id);
    if (it == jobs_.end()) {
        return job_info{};
    }

    job_info info;
    info.status = it->second->status;
    info.timing = it->second->timing;
    info.task_name = it->second->task_name;
    info.error_text = it->second->error_text;
    return info;
}

bool thread_pool_scheduler::try_get_result(job_id id, job_result& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = jobs_.find(id);
    if (it == jobs_.end()) {
        return false;
    }
    if (it->second->status != job_status::done || !it->second->result.has_value()) {
        return false;
    }
    out = *(it->second->result);
    return true;
}

bool thread_pool_scheduler::cancel(job_id id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = jobs_.find(id);
    if (it == jobs_.end()) {
        return false;
    }

    auto& state = *(it->second);
    if (state.status == job_status::done || state.status == job_status::failed || state.status == job_status::cancelled) {
        return false;
    }

    state.cancel_requested = true;
    if (state.status == job_status::queued) {
        state.status = job_status::cancelled;
        state.timing.finished_at = std::chrono::steady_clock::now();
        ++stats_.cancelled;
    }

    return true;
}

scheduler_profile_stats thread_pool_scheduler::stats_snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

void thread_pool_scheduler::worker_loop() {
    while (true) {
        std::shared_ptr<job_state> state;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty()) {
                return;
            }

            const job_id id = queue_.front();
            queue_.pop();

            const auto it = jobs_.find(id);
            if (it == jobs_.end()) {
                continue;
            }
            state = it->second;

            if (state->status == job_status::cancelled) {
                continue;
            }

            const auto start = std::chrono::steady_clock::now();
            state->status = job_status::running;
            state->timing.started_at = start;
            ++stats_.started;
            stats_.queue_delay.observe(std::chrono::duration_cast<std::chrono::nanoseconds>(start - state->timing.submitted_at));
        }

        try {
            job_result result = state->request.fn();

            std::lock_guard<std::mutex> lock(mutex_);
            const auto finish = std::chrono::steady_clock::now();
            state->timing.finished_at = finish;
            stats_.run_time.observe(std::chrono::duration_cast<std::chrono::nanoseconds>(finish - state->timing.started_at));

            if (state->cancel_requested) {
                state->status = job_status::cancelled;
                ++stats_.cancelled;
            } else {
                state->status = job_status::done;
                state->result = std::move(result);
                ++stats_.completed;
            }
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto finish = std::chrono::steady_clock::now();
            state->timing.finished_at = finish;
            stats_.run_time.observe(std::chrono::duration_cast<std::chrono::nanoseconds>(finish - state->timing.started_at));
            state->status = job_status::failed;
            state->error_text = e.what();
            ++stats_.failed;
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto finish = std::chrono::steady_clock::now();
            state->timing.finished_at = finish;
            stats_.run_time.observe(std::chrono::duration_cast<std::chrono::nanoseconds>(finish - state->timing.started_at));
            state->status = job_status::failed;
            state->error_text = "unknown exception";
            ++stats_.failed;
        }
    }
}

const char* job_status_name(job_status st) noexcept {
    switch (st) {
        case job_status::queued:
            return "queued";
        case job_status::running:
            return "running";
        case job_status::done:
            return "done";
        case job_status::failed:
            return "failed";
        case job_status::cancelled:
            return "cancelled";
        case job_status::unknown:
            return "unknown";
    }
    return "unknown";
}

}  // namespace bt

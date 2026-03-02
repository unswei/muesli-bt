#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>
#include <utility>
#include <vector>

#include "bt/runtime.hpp"
#include "bt/vla.hpp"

namespace conformance {

class stepped_clock final : public bt::clock_interface {
public:
    stepped_clock() = default;

    std::chrono::steady_clock::time_point now() const override { return now_; }

    void advance_ms(std::int64_t ms) {
        if (ms <= 0) {
            return;
        }
        now_ += std::chrono::milliseconds(ms);
    }

private:
    std::chrono::steady_clock::time_point now_{};
};

struct scripted_vla_behaviour {
    std::int64_t steps_before_complete = 8;
    std::int64_t step_sleep_ms = 1;
    bool honour_cancel = true;
    bool return_ok_after_cancel = false;
    double action_value = 0.25;
};

class scripted_vla_backend final : public bt::vla_backend {
public:
    explicit scripted_vla_backend(scripted_vla_behaviour behaviour) : behaviour_(std::move(behaviour)) {}

    bt::vla_response infer(const bt::vla_request& request,
                           std::function<bool(const bt::vla_partial&)> on_partial,
                           std::atomic<bool>& cancel_flag) override {
        ++infer_calls_;
        bt::vla_response out;
        out.model = request.model;
        out.action.type = bt::vla_action_type::continuous;
        out.action.u = {behaviour_.action_value};
        out.confidence = 0.8;
        out.explanation = "scripted backend";

        const std::int64_t steps = std::max<std::int64_t>(1, behaviour_.steps_before_complete);
        for (std::int64_t i = 0; i < steps; ++i) {
            bt::vla_partial partial;
            partial.sequence = static_cast<std::uint64_t>(i + 1);
            partial.text_chunk = "partial";
            partial.confidence = 0.5;
            partial.action_candidate = out.action;
            if (!on_partial(partial)) {
                cancel_flag.store(true);
            }

            if (cancel_flag.load() && behaviour_.honour_cancel) {
                out.status = bt::vla_status::cancelled;
                out.explanation = "cancelled";
                return out;
            }

            if (behaviour_.step_sleep_ms > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(behaviour_.step_sleep_ms));
            }
        }

        if (cancel_flag.load() && !behaviour_.return_ok_after_cancel) {
            out.status = bt::vla_status::cancelled;
            out.explanation = "cancelled";
            return out;
        }

        out.status = bt::vla_status::ok;
        return out;
    }

    [[nodiscard]] std::int64_t infer_calls() const noexcept { return infer_calls_.load(); }

private:
    scripted_vla_behaviour behaviour_;
    std::atomic<std::int64_t> infer_calls_{0};
};

}  // namespace conformance

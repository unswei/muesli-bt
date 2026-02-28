#include "bt/vla.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

namespace bt {
namespace {

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

double elapsed_ms(std::chrono::steady_clock::time_point start, std::chrono::steady_clock::time_point end) {
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;
}

bool is_terminal(vla_job_status status) {
    return status == vla_job_status::done || status == vla_job_status::error || status == vla_job_status::timeout ||
           status == vla_job_status::cancelled;
}

vla_job_status to_job_status(vla_status status) {
    switch (status) {
        case vla_status::ok:
            return vla_job_status::done;
        case vla_status::timeout:
            return vla_job_status::timeout;
        case vla_status::error:
        case vla_status::invalid:
            return vla_job_status::error;
        case vla_status::cancelled:
            return vla_job_status::cancelled;
    }
    return vla_job_status::error;
}

std::string json_escape(std::string_view input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (char c : input) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

std::string action_to_json(const vla_action& action) {
    std::ostringstream out;
    out << "{\"type\":\"" << vla_action_type_name(action.type) << "\"";
    if (action.type == vla_action_type::continuous) {
        out << ",\"u\":[";
        for (std::size_t i = 0; i < action.u.size(); ++i) {
            if (i != 0) {
                out << ',';
            }
            out << action.u[i];
        }
        out << ']';
    } else if (action.type == vla_action_type::discrete) {
        out << ",\"id\":\"" << json_escape(action.discrete_id) << "\"";
    } else {
        out << ",\"steps\":[";
        for (std::size_t i = 0; i < action.steps.size(); ++i) {
            if (i != 0) {
                out << ',';
            }
            out << action_to_json(action.steps[i]);
        }
        out << ']';
    }
    out << '}';
    return out.str();
}

std::string response_to_json_local(const vla_response& response) {
    std::ostringstream out;
    out << "{\"status\":\"" << vla_status_name(response.status) << "\",\"action\":" << action_to_json(response.action)
        << ",\"confidence\":" << response.confidence << ",\"model\":{\"name\":\"" << json_escape(response.model.name)
        << "\",\"version\":\"" << json_escape(response.model.version) << "\"}";
    if (!response.explanation.empty()) {
        out << ",\"explanation\":\"" << json_escape(response.explanation) << "\"";
    }
    if (!response.stats.empty()) {
        out << ",\"stats\":{";
        bool first = true;
        for (const auto& [k, v] : response.stats) {
            if (!first) {
                out << ',';
            }
            first = false;
            out << "\"" << json_escape(k) << "\":" << v;
        }
        out << '}';
    }
    out << '}';
    return out.str();
}

std::string observation_summary(const vla_request& request) {
    std::ostringstream out;
    out << "state_dims=" << request.observation.state.size();
    if (request.observation.image.has_value()) {
        out << " image_id=" << request.observation.image->id;
    }
    if (request.observation.blob.has_value()) {
        out << " blob_id=" << request.observation.blob->id;
    }
    out << " frame_id=" << request.observation.frame_id << " ts_ms=" << request.observation.timestamp_ms;
    return out.str();
}

std::optional<std::string> validate_request_shape(const vla_request& request) {
    if (request.task_id.empty()) {
        return "request.task_id is required";
    }
    if (request.instruction.empty()) {
        return "request.instruction is required";
    }
    if (request.deadline_ms <= 0) {
        return "request.deadline_ms must be > 0";
    }
    if (request.action_space.type.empty()) {
        return "request.action_space.type is required";
    }
    if (request.action_space.dims <= 0) {
        return "request.action_space.dims must be > 0";
    }
    if (request.action_space.bounds.size() != static_cast<std::size_t>(request.action_space.dims)) {
        return "request.action_space.bounds length must match dims";
    }
    for (const auto& [lo, hi] : request.action_space.bounds) {
        if (!std::isfinite(lo) || !std::isfinite(hi) || lo > hi) {
            return "request.action_space.bounds entries must be finite and ordered";
        }
    }
    if (request.model.name.empty() || request.model.version.empty()) {
        return "request.model.name and request.model.version are required";
    }
    return std::nullopt;
}

bool validate_and_clamp_action(const vla_request& request, vla_action& action, std::string& reason) {
    if (action.type != vla_action_type::continuous) {
        return true;
    }

    const std::size_t dims = static_cast<std::size_t>(request.action_space.dims);
    if (action.u.size() != dims) {
        reason = "response.action dimensions do not match action space";
        return false;
    }

    for (std::size_t i = 0; i < dims; ++i) {
        if (!std::isfinite(action.u[i])) {
            reason = "response.action contains non-finite value";
            return false;
        }
        const auto [lo, hi] = request.action_space.bounds[i];
        action.u[i] = std::clamp(action.u[i], lo, hi);

        if (std::fabs(action.u[i]) > request.constraints.max_abs_value) {
            action.u[i] = std::copysign(request.constraints.max_abs_value, action.u[i]);
        }

        for (const auto& [f_lo, f_hi] : request.constraints.forbidden_ranges) {
            if (action.u[i] >= f_lo && action.u[i] <= f_hi) {
                reason = "response.action intersects forbidden range";
                return false;
            }
        }
    }

    if (!request.observation.state.empty()) {
        const std::size_t n = std::min(request.observation.state.size(), action.u.size());
        for (std::size_t i = 0; i < n; ++i) {
            const double delta = std::fabs(action.u[i] - request.observation.state[i]);
            if (delta > request.constraints.max_delta) {
                action.u[i] = request.observation.state[i] +
                              std::copysign(request.constraints.max_delta, action.u[i] - request.observation.state[i]);
                const auto [lo, hi] = request.action_space.bounds[i];
                action.u[i] = std::clamp(action.u[i], lo, hi);
            }
        }
    }

    return true;
}

vla_action make_continuous_action(const std::vector<double>& u) {
    vla_action action;
    action.type = vla_action_type::continuous;
    action.u = u;
    return action;
}

class rt2_stub_backend final : public vla_backend {
public:
    vla_response infer(const vla_request& request,
                       std::function<bool(const vla_partial&)> on_partial,
                       std::atomic<bool>& cancel_flag) override {
        const auto started = std::chrono::steady_clock::now();

        const std::size_t dims = static_cast<std::size_t>(request.action_space.dims);
        std::vector<double> proposal(dims, 0.0);

        auto emit_partial = [&](std::uint64_t seq, std::string text, double confidence) {
            vla_partial part;
            part.sequence = seq;
            part.text_chunk = std::move(text);
            part.confidence = confidence;
            part.action_candidate = make_continuous_action(proposal);
            if (!on_partial(part)) {
                cancel_flag.store(true);
            }
        };

        // Lightweight staged decode simulation for streaming.
        for (std::uint64_t i = 0; i < 3; ++i) {
            if (cancel_flag.load()) {
                vla_response cancelled;
                cancelled.status = vla_status::cancelled;
                cancelled.model = request.model;
                cancelled.explanation = "cancelled before completion";
                cancelled.stats["latency_ms"] = elapsed_ms(started, std::chrono::steady_clock::now());
                return cancelled;
            }

            if (i == 0) {
                proposal.assign(dims, 0.0);
                emit_partial(1, "encode observation", 0.20);
            } else if (i == 1) {
                for (std::size_t d = 0; d < dims; ++d) {
                    if (request.observation.state.empty()) {
                        proposal[d] = 0.0;
                    } else if (dims == 1) {
                        const double x = request.observation.state[0];
                        proposal[0] = std::clamp(1.0 - x, -1.0, 1.0);
                    } else if (request.observation.state.size() >= 4 && d < 2) {
                        const double target = request.observation.state[2 + d];
                        proposal[d] = std::clamp(target, -0.35, 0.35);
                    } else {
                        proposal[d] = std::clamp(request.observation.state[d], -1.0, 1.0);
                    }
                }
                emit_partial(2, "decode action prior", 0.55);
            } else {
                for (std::size_t d = 0; d < dims; ++d) {
                    proposal[d] *= 0.9;
                }
                emit_partial(3, "finalize structured action", 0.75);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            const auto elapsed = elapsed_ms(started, std::chrono::steady_clock::now());
            if (elapsed > static_cast<double>(request.deadline_ms)) {
                vla_response timeout;
                timeout.status = vla_status::timeout;
                timeout.model = request.model;
                timeout.action = make_continuous_action(proposal);
                timeout.confidence = 0.0;
                timeout.explanation = "deadline exceeded";
                timeout.stats["latency_ms"] = elapsed;
                timeout.stats["partials"] = static_cast<double>(i + 1);
                return timeout;
            }
        }

        vla_response out;
        out.status = vla_status::ok;
        out.model = request.model;
        out.action = make_continuous_action(proposal);
        out.confidence = 0.75;
        out.explanation = "rt2-style stub output";
        out.stats["latency_ms"] = elapsed_ms(started, std::chrono::steady_clock::now());
        out.stats["partials"] = 3.0;
        return out;
    }
};

class replay_backend final : public vla_backend {
public:
    explicit replay_backend(std::function<std::optional<vla_response>(std::uint64_t)> lookup_fn)
        : lookup_fn_(std::move(lookup_fn)) {}

    vla_response infer(const vla_request& request,
                       std::function<bool(const vla_partial&)>,
                       std::atomic<bool>&) override {
        const std::uint64_t hash = vla_service::hash_request(request);
        const std::optional<vla_response> replayed = lookup_fn_(hash);
        if (replayed.has_value()) {
            return *replayed;
        }
        vla_response out;
        out.status = vla_status::error;
        out.model = request.model;
        out.explanation = "replay miss";
        return out;
    }

private:
    std::function<std::optional<vla_response>(std::uint64_t)> lookup_fn_;
};

}  // namespace

void capability_registry::register_capability(capability_descriptor descriptor) {
    if (descriptor.name.empty()) {
        throw std::invalid_argument("register_capability: capability name must not be empty");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    capabilities_[descriptor.name] = std::move(descriptor);
}

std::vector<std::string> capability_registry::list() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> out;
    out.reserve(capabilities_.size());
    for (const auto& [name, _] : capabilities_) {
        out.push_back(name);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::optional<capability_descriptor> capability_registry::describe(std::string_view name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = capabilities_.find(std::string(name));
    if (it == capabilities_.end()) {
        return std::nullopt;
    }
    return it->second;
}

vla_service::vla_service(scheduler* sched) : sched_(sched) {
    if (!sched_) {
        throw std::invalid_argument("vla_service: scheduler pointer must not be null");
    }
    records_.reserve(record_capacity_);

    capability_descriptor cap;
    cap.name = "vla.rt2";
    cap.safety_class = "restricted";
    cap.cost_category = "high";
    cap.request_schema = {
        {"task_id", "string", true},
        {"instruction", "string", true},
        {"observation", "map", true},
        {"action_space", "map", true},
        {"constraints", "map", true},
        {"deadline_ms", "int", true},
        {"model", "map", true},
    };
    cap.response_schema = {
        {"status", "keyword", true},
        {"action", "map", true},
        {"confidence", "float", true},
        {"model", "map", true},
        {"stats", "map", true},
    };
    capabilities_.register_capability(std::move(cap));

    register_backend("rt2-stub", std::make_shared<rt2_stub_backend>());
    register_backend(
        "replay", std::make_shared<replay_backend>([this](std::uint64_t hash) -> std::optional<vla_response> {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = replay_store_.find(hash);
            if (it == replay_store_.end()) {
                return std::nullopt;
            }
            return it->second;
        }));
}

vla_service::vla_job_id vla_service::submit(const vla_request& request) {
    auto state = std::make_shared<job_state>();
    const auto now = std::chrono::steady_clock::now();
    state->submitted_at = now;
    state->request = request;
    state->request_hash = hash_request(request);

    vla_record immediate_record;
    bool emit_immediate_record = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        state->id = next_job_id_++;
        jobs_[state->id] = state;

        const auto request_err = validate_request_shape(request);
        if (request_err.has_value()) {
            vla_response err;
            err.status = vla_status::error;
            err.model = request.model;
            err.explanation = *request_err;

            state->status = vla_job_status::error;
            state->final = err;
            state->finished_at = now;
            state->error = *request_err;

            immediate_record.ts_ms = now_ms();
            immediate_record.run_id = request.run_id;
            immediate_record.tick_index = request.tick_index;
            immediate_record.node_name = request.node_name;
            immediate_record.task_id = request.task_id;
            immediate_record.capability = request.capability;
            immediate_record.model_name = request.model.name;
            immediate_record.model_version = request.model.version;
            immediate_record.request_hash = state->request_hash;
            immediate_record.observation_summary = observation_summary(request);
            immediate_record.deadline_ms = request.deadline_ms;
            immediate_record.seed = request.seed.value_or(0);
            immediate_record.status = "error";
            immediate_record.latency_ms = 0.0;
            immediate_record.response_json = response_to_json_local(err);
            emit_immediate_record = true;
        }

        if (!emit_immediate_record && request.observation.image.has_value()) {
            if (images_.find(request.observation.image->id) == images_.end()) {
                vla_response err;
                err.status = vla_status::error;
                err.model = request.model;
                err.explanation = "observation.image handle does not exist";
                state->status = vla_job_status::error;
                state->final = err;
                state->finished_at = now;
                state->error = err.explanation;
                immediate_record.ts_ms = now_ms();
                immediate_record.run_id = request.run_id;
                immediate_record.tick_index = request.tick_index;
                immediate_record.node_name = request.node_name;
                immediate_record.task_id = request.task_id;
                immediate_record.capability = request.capability;
                immediate_record.model_name = request.model.name;
                immediate_record.model_version = request.model.version;
                immediate_record.request_hash = state->request_hash;
                immediate_record.observation_summary = observation_summary(request);
                immediate_record.deadline_ms = request.deadline_ms;
                immediate_record.seed = request.seed.value_or(0);
                immediate_record.status = "error";
                immediate_record.latency_ms = 0.0;
                immediate_record.response_json = response_to_json_local(err);
                emit_immediate_record = true;
            }
        }

        if (!emit_immediate_record && request.observation.blob.has_value()) {
            if (blobs_.find(request.observation.blob->id) == blobs_.end()) {
                vla_response err;
                err.status = vla_status::error;
                err.model = request.model;
                err.explanation = "observation.blob handle does not exist";
                state->status = vla_job_status::error;
                state->final = err;
                state->finished_at = now;
                state->error = err.explanation;
                immediate_record.ts_ms = now_ms();
                immediate_record.run_id = request.run_id;
                immediate_record.tick_index = request.tick_index;
                immediate_record.node_name = request.node_name;
                immediate_record.task_id = request.task_id;
                immediate_record.capability = request.capability;
                immediate_record.model_name = request.model.name;
                immediate_record.model_version = request.model.version;
                immediate_record.request_hash = state->request_hash;
                immediate_record.observation_summary = observation_summary(request);
                immediate_record.deadline_ms = request.deadline_ms;
                immediate_record.seed = request.seed.value_or(0);
                immediate_record.status = "error";
                immediate_record.latency_ms = 0.0;
                immediate_record.response_json = response_to_json_local(err);
                emit_immediate_record = true;
            }
        }

        if (!emit_immediate_record) {
            const std::string owner_key = make_owner_key(request);
            const auto active_it = active_owner_jobs_.find(owner_key);
            if (active_it != active_owner_jobs_.end()) {
                const auto old_it = jobs_.find(active_it->second);
                if (old_it != jobs_.end() && !is_terminal(old_it->second->status)) {
                    old_it->second->cancel_requested.store(true);
                    old_it->second->superseded = true;
                    if (old_it->second->scheduler_job_id != 0) {
                        (void)sched_->cancel(old_it->second->scheduler_job_id);
                    }
                }
            }
            active_owner_jobs_[owner_key] = state->id;

            const auto cache_it = cache_.find(state->request_hash);
            if (cache_it != cache_.end() && now < cache_it->second.expires_at) {
                state->status = vla_job_status::done;
                state->cache_hit = true;
                state->started_at = now;
                state->finished_at = now;
                state->final = cache_it->second.response;

                immediate_record.ts_ms = now_ms();
                immediate_record.run_id = request.run_id;
                immediate_record.tick_index = request.tick_index;
                immediate_record.node_name = request.node_name;
                immediate_record.task_id = request.task_id;
                immediate_record.capability = request.capability;
                immediate_record.model_name = request.model.name;
                immediate_record.model_version = request.model.version;
                immediate_record.request_hash = state->request_hash;
                immediate_record.observation_summary = observation_summary(request);
                immediate_record.deadline_ms = request.deadline_ms;
                immediate_record.seed = request.seed.value_or(0);
                immediate_record.status = "done";
                immediate_record.latency_ms = 0.0;
                immediate_record.response_json = response_to_json_local(*state->final);
                immediate_record.cache_hit = true;
                emit_immediate_record = true;
            }
        }
    }

    if (emit_immediate_record) {
        append_record(immediate_record);
        return state->id;
    }

    const std::shared_ptr<vla_backend> backend = resolve_backend(request);
    if (!backend) {
        vla_record rec;
        rec.ts_ms = now_ms();
        rec.run_id = request.run_id;
        rec.tick_index = request.tick_index;
        rec.node_name = request.node_name;
        rec.task_id = request.task_id;
        rec.capability = request.capability;
        rec.model_name = request.model.name;
        rec.model_version = request.model.version;
        rec.request_hash = state->request_hash;
        rec.observation_summary = observation_summary(request);
        rec.deadline_ms = request.deadline_ms;
        rec.seed = request.seed.value_or(0);
        rec.status = "error";
        vla_response err;
        err.status = vla_status::error;
        err.model = request.model;
        err.explanation = "backend not found";
        rec.response_json = response_to_json_local(err);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            state->status = vla_job_status::error;
            state->final = err;
            state->finished_at = std::chrono::steady_clock::now();
        }
        append_record(rec);
        return state->id;
    }

    job_request req;
    req.task_name = "vla.submit";
    req.fn = [this, state, backend] {
        vla_record rec;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state->status = vla_job_status::running;
            state->started_at = std::chrono::steady_clock::now();
        }

        auto on_partial = [this, state](const vla_partial& partial) -> bool {
            std::lock_guard<std::mutex> lock(mutex_);
            if (state->cancel_requested.load()) {
                return false;
            }
            state->latest_partial = partial;
            ++state->partial_count;
            state->status = vla_job_status::streaming;
            return true;
        };

        vla_response response;
        try {
            response = backend->infer(state->request, on_partial, state->cancel_requested);
        } catch (const std::exception& e) {
            response.status = vla_status::error;
            response.model = state->request.model;
            response.explanation = std::string("backend exception: ") + e.what();
        } catch (...) {
            response.status = vla_status::error;
            response.model = state->request.model;
            response.explanation = "backend unknown exception";
        }

        const auto finish = std::chrono::steady_clock::now();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            const double latency = elapsed_ms(state->submitted_at, finish);
            if (state->cancel_requested.load() && response.status == vla_status::ok) {
                response.status = vla_status::cancelled;
                response.explanation = "cancelled";
            }
            if (latency > static_cast<double>(state->request.deadline_ms) && response.status == vla_status::ok) {
                response.status = vla_status::timeout;
                response.explanation = "deadline exceeded";
            }

            std::string invalid_reason;
            if (response.status == vla_status::ok) {
                if (!validate_and_clamp_action(state->request, response.action, invalid_reason)) {
                    response.status = vla_status::invalid;
                    response.explanation = invalid_reason;
                }
            }

            response.stats["latency_ms"] = latency;
            response.stats["partials"] = static_cast<double>(state->partial_count);
            state->final = response;
            state->finished_at = finish;
            state->status = to_job_status(response.status);

            const std::string owner_key = make_owner_key(state->request);
            const auto owner_it = active_owner_jobs_.find(owner_key);
            if (owner_it != active_owner_jobs_.end() && owner_it->second == state->id) {
                active_owner_jobs_.erase(owner_it);
            }

            if (response.status == vla_status::ok) {
                cache_[state->request_hash] = cache_entry{
                    .response = response,
                    .expires_at = finish + std::chrono::milliseconds(cache_ttl_ms_),
                };
                replay_store_[state->request_hash] = response;
                evict_cache_if_needed();
            }

            rec.ts_ms = now_ms();
            rec.run_id = state->request.run_id;
            rec.tick_index = state->request.tick_index;
            rec.node_name = state->request.node_name;
            rec.task_id = state->request.task_id;
            rec.capability = state->request.capability;
            rec.model_name = state->request.model.name;
            rec.model_version = state->request.model.version;
            rec.request_hash = state->request_hash;
            rec.observation_summary = observation_summary(state->request);
            rec.deadline_ms = state->request.deadline_ms;
            rec.seed = state->request.seed.value_or(0);
            rec.status = vla_job_status_name(state->status);
            rec.latency_ms = latency;
            rec.response_json = response_to_json_local(response);
            rec.cache_hit = state->cache_hit;
            rec.replay_hit = state->replay_hit;
            rec.superseded = state->superseded;
        }

        append_record(rec);
        return job_result{};
    };

    const job_id sid = sched_->submit(std::move(req));
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state->scheduler_job_id = sid;
    }
    return state->id;
}

vla_poll vla_service::poll(vla_job_id id) {
    vla_poll out;
    std::shared_ptr<job_state> state;
    job_id scheduler_id_to_cancel = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = jobs_.find(id);
        if (it == jobs_.end()) {
            out.status = vla_job_status::error;
            return out;
        }
        state = it->second;

        if (!is_terminal(state->status) && state->request.deadline_ms > 0) {
            const auto now = std::chrono::steady_clock::now();
            if (elapsed_ms(state->submitted_at, now) > static_cast<double>(state->request.deadline_ms) &&
                !state->cancel_requested.load()) {
                state->cancel_requested.store(true);
                state->status = vla_job_status::timeout;
                scheduler_id_to_cancel = state->scheduler_job_id;
            }
        }
        out.status = state->status;
        out.partial = state->latest_partial;
        out.final = state->final;

        const auto end = (state->finished_at.time_since_epoch().count() == 0) ? std::chrono::steady_clock::now()
                                                                               : state->finished_at;
        out.stats["latency_ms"] = elapsed_ms(state->submitted_at, end);
        out.stats["partial_count"] = static_cast<double>(state->partial_count);
        out.stats["request_hash"] = static_cast<double>(state->request_hash & 0xffffffffu);
    }

    if (scheduler_id_to_cancel != 0) {
        (void)sched_->cancel(scheduler_id_to_cancel);
    }

    return out;
}

bool vla_service::cancel(vla_job_id id) {
    std::shared_ptr<job_state> state;
    vla_record rec;
    bool emit_record = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = jobs_.find(id);
        if (it == jobs_.end()) {
            return false;
        }
        state = it->second;
        if (is_terminal(state->status)) {
            return false;
        }

        state->cancel_requested.store(true);
        if (state->scheduler_job_id != 0) {
            (void)sched_->cancel(state->scheduler_job_id);
        }

        if (state->status == vla_job_status::queued) {
            state->status = vla_job_status::cancelled;
            state->finished_at = std::chrono::steady_clock::now();
            vla_response response;
            response.status = vla_status::cancelled;
            response.model = state->request.model;
            response.explanation = "cancelled while queued";
            response.stats["latency_ms"] = elapsed_ms(state->submitted_at, state->finished_at);
            state->final = response;

            rec.ts_ms = now_ms();
            rec.run_id = state->request.run_id;
            rec.tick_index = state->request.tick_index;
            rec.node_name = state->request.node_name;
            rec.task_id = state->request.task_id;
            rec.capability = state->request.capability;
            rec.model_name = state->request.model.name;
            rec.model_version = state->request.model.version;
            rec.request_hash = state->request_hash;
            rec.observation_summary = observation_summary(state->request);
            rec.deadline_ms = state->request.deadline_ms;
            rec.seed = state->request.seed.value_or(0);
            rec.status = "cancelled";
            rec.latency_ms = elapsed_ms(state->submitted_at, state->finished_at);
            rec.response_json = response_to_json_local(response);
            emit_record = true;
        }
    }

    if (emit_record) {
        append_record(rec);
    }
    return true;
}

void vla_service::register_backend(std::string name, std::shared_ptr<vla_backend> backend) {
    if (name.empty()) {
        throw std::invalid_argument("register_backend: backend name must not be empty");
    }
    if (!backend) {
        throw std::invalid_argument("register_backend: backend pointer is null");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    backends_[std::move(name)] = std::move(backend);
}

void vla_service::set_default_backend(std::string name) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (backends_.find(name) == backends_.end()) {
        throw std::invalid_argument("set_default_backend: unknown backend: " + name);
    }
    default_backend_ = std::move(name);
}

std::string vla_service::default_backend() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return default_backend_;
}

capability_registry& vla_service::capabilities() noexcept {
    return capabilities_;
}

const capability_registry& vla_service::capabilities() const noexcept {
    return capabilities_;
}

image_handle_ref vla_service::create_image(std::int64_t width,
                                           std::int64_t height,
                                           std::int64_t channels,
                                           std::string encoding,
                                           std::int64_t timestamp_ms,
                                           std::string frame_id) {
    if (width <= 0 || height <= 0 || channels <= 0) {
        throw std::invalid_argument("create_image: dimensions/channels must be > 0");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const std::int64_t id = next_image_id_++;
    images_[id] = image_info{
        .id = id,
        .width = width,
        .height = height,
        .channels = channels,
        .encoding = std::move(encoding),
        .timestamp_ms = timestamp_ms,
        .frame_id = std::move(frame_id),
    };
    return image_handle_ref{.id = id};
}

blob_handle_ref vla_service::create_blob(std::int64_t size_bytes,
                                         std::string mime_type,
                                         std::int64_t timestamp_ms,
                                         std::string tag) {
    if (size_bytes < 0) {
        throw std::invalid_argument("create_blob: size_bytes must be >= 0");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const std::int64_t id = next_blob_id_++;
    blobs_[id] = blob_info{
        .id = id,
        .size_bytes = size_bytes,
        .mime_type = std::move(mime_type),
        .timestamp_ms = timestamp_ms,
        .tag = std::move(tag),
    };
    return blob_handle_ref{.id = id};
}

std::optional<image_info> vla_service::get_image_info(image_handle_ref handle) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = images_.find(handle.id);
    if (it == images_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<blob_info> vla_service::get_blob_info(blob_handle_ref handle) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = blobs_.find(handle.id);
    if (it == blobs_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::string vla_service::dump_recent_records(std::size_t max_count) const {
    const std::vector<vla_record> recs = recent_records(max_count);
    std::ostringstream out;
    for (const vla_record& rec : recs) {
        out << record_to_json(rec) << '\n';
    }
    return out.str();
}

std::vector<vla_record> vla_service::recent_records(std::size_t max_count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (max_count >= records_.size()) {
        return records_;
    }
    return std::vector<vla_record>(records_.end() - static_cast<std::ptrdiff_t>(max_count), records_.end());
}

void vla_service::clear_records() {
    std::lock_guard<std::mutex> lock(mutex_);
    records_.clear();
}

void vla_service::set_log_path(std::string path) {
    if (path.empty()) {
        throw std::invalid_argument("set_log_path: path must not be empty");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    log_path_ = std::move(path);
}

const std::string& vla_service::log_path() const noexcept {
    return log_path_;
}

void vla_service::set_log_enabled(bool enabled) noexcept {
    log_enabled_ = enabled;
}

bool vla_service::log_enabled() const noexcept {
    return log_enabled_;
}

void vla_service::set_cache_ttl_ms(std::int64_t ttl_ms) {
    if (ttl_ms < 0) {
        throw std::invalid_argument("set_cache_ttl_ms: ttl must be non-negative");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    cache_ttl_ms_ = ttl_ms;
}

std::int64_t vla_service::cache_ttl_ms() const noexcept {
    return cache_ttl_ms_;
}

void vla_service::set_cache_capacity(std::size_t capacity) {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_capacity_ = capacity;
    evict_cache_if_needed();
}

std::size_t vla_service::cache_capacity() const noexcept {
    return cache_capacity_;
}

std::uint64_t vla_service::hash64(std::string_view text) noexcept {
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : text) {
        hash ^= static_cast<std::uint64_t>(c);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::uint64_t vla_service::hash_request(const vla_request& request) {
    std::ostringstream out;
    out << request.capability << '\n' << request.task_id << '\n' << request.instruction << '\n'
        << request.observation.timestamp_ms << '\n' << request.observation.frame_id << '\n';
    if (request.observation.image.has_value()) {
        out << "img:" << request.observation.image->id << '\n';
    }
    if (request.observation.blob.has_value()) {
        out << "blob:" << request.observation.blob->id << '\n';
    }
    out << "state:";
    for (double v : request.observation.state) {
        out << v << ',';
    }
    out << '\n' << "dims:" << request.action_space.dims << '\n';
    for (const auto& [lo, hi] : request.action_space.bounds) {
        out << lo << ':' << hi << ';';
    }
    out << '\n' << "model:" << request.model.name << ':' << request.model.version << '\n';
    out << "deadline:" << request.deadline_ms << '\n';
    out << "max_abs:" << request.constraints.max_abs_value << " max_delta:" << request.constraints.max_delta << '\n';
    return hash64(out.str());
}

void vla_service::append_record(const vla_record& record) {
    std::string json_line;
    bool write_json = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (record_capacity_ > 0 && records_.size() == record_capacity_) {
            records_.erase(records_.begin());
        }
        records_.push_back(record);
        write_json = log_enabled_;
        if (write_json) {
            json_line = record_to_json(record);
        }
    }

    if (write_json) {
        append_jsonl_line(json_line);
    }
}

std::string vla_service::record_to_json(const vla_record& record) const {
    std::ostringstream out;
    out << '{' << "\"ts_ms\":" << record.ts_ms << ','
        << "\"run_id\":\"" << json_escape(record.run_id) << "\","
        << "\"tick_index\":" << record.tick_index << ','
        << "\"node_name\":\"" << json_escape(record.node_name) << "\","
        << "\"task_id\":\"" << json_escape(record.task_id) << "\","
        << "\"capability\":\"" << json_escape(record.capability) << "\","
        << "\"model_name\":\"" << json_escape(record.model_name) << "\","
        << "\"model_version\":\"" << json_escape(record.model_version) << "\","
        << "\"request_hash\":" << record.request_hash << ','
        << "\"observation\":\"" << json_escape(record.observation_summary) << "\","
        << "\"deadline_ms\":" << record.deadline_ms << ','
        << "\"seed\":" << record.seed << ','
        << "\"status\":\"" << json_escape(record.status) << "\","
        << "\"latency_ms\":" << record.latency_ms << ','
        << "\"cache_hit\":" << (record.cache_hit ? "true" : "false") << ','
        << "\"replay_hit\":" << (record.replay_hit ? "true" : "false") << ','
        << "\"superseded\":" << (record.superseded ? "true" : "false") << ','
        << "\"response\":" << record.response_json << '}';
    return out.str();
}

void vla_service::append_jsonl_line(const std::string& line) {
    const std::string path = log_path_;
    std::lock_guard<std::mutex> lock(file_mutex_);
    const std::filesystem::path fs_path(path);
    if (fs_path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(fs_path.parent_path(), ec);
    }
    std::ofstream out(path, std::ios::app);
    if (!out) {
        return;
    }
    out << line << '\n';
}

std::string vla_service::response_to_json(const vla_response& response) const {
    return response_to_json_local(response);
}

std::shared_ptr<vla_backend> vla_service::resolve_backend(const vla_request& request) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!request.model.name.empty()) {
        const auto by_model = backends_.find(request.model.name);
        if (by_model != backends_.end()) {
            return by_model->second;
        }
    }
    const auto by_default = backends_.find(default_backend_);
    if (by_default != backends_.end()) {
        return by_default->second;
    }
    return nullptr;
}

std::string vla_service::make_owner_key(const vla_request& request) const {
    return request.run_id + "::" + request.node_name;
}

void vla_service::evict_cache_if_needed() {
    while (cache_.size() > cache_capacity_) {
        auto it = cache_.begin();
        if (it == cache_.end()) {
            return;
        }
        cache_.erase(it);
    }
}

const char* vla_status_name(vla_status status) noexcept {
    switch (status) {
        case vla_status::ok:
            return "ok";
        case vla_status::timeout:
            return "timeout";
        case vla_status::error:
            return "error";
        case vla_status::cancelled:
            return "cancelled";
        case vla_status::invalid:
            return "invalid";
    }
    return "error";
}

const char* vla_job_status_name(vla_job_status status) noexcept {
    switch (status) {
        case vla_job_status::queued:
            return "queued";
        case vla_job_status::running:
            return "running";
        case vla_job_status::streaming:
            return "streaming";
        case vla_job_status::done:
            return "done";
        case vla_job_status::error:
            return "error";
        case vla_job_status::timeout:
            return "timeout";
        case vla_job_status::cancelled:
            return "cancelled";
    }
    return "error";
}

std::string vla_action_type_name(vla_action_type type) {
    switch (type) {
        case vla_action_type::continuous:
            return "continuous";
        case vla_action_type::discrete:
            return "discrete";
        case vla_action_type::sequence:
            return "sequence";
    }
    return "continuous";
}

}  // namespace bt

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "bt/runtime_host.hpp"

namespace bt {

struct racecar_state {
    std::string state_schema = "racecar_state.v1";
    std::vector<double> state_vec{};
    double x = 0.0;
    double y = 0.0;
    double yaw = 0.0;
    double speed = 0.0;
    std::vector<double> rays{};
    std::vector<double> goal{};
    bool collision_imminent = false;
    std::int64_t collision_count = 0;
    std::int64_t t_ms = 0;
};

struct racecar_tick_record {
    std::string schema_version = "racecar_demo.v1";
    std::string run_id = "run";
    std::int64_t tick_index = 0;
    double sim_time_s = 0.0;
    double wall_time_s = 0.0;
    std::string mode = "bt";
    racecar_state state{};
    double distance_to_goal = 0.0;
    double steering = 0.0;
    double throttle = 0.0;
    std::int64_t collisions_total = 0;
    bool goal_reached = false;
    std::string bt_status = "failure";
    std::string planner_meta_json{};
    bool used_fallback = false;
    bool is_error_record = false;
    std::string error_reason{};
};

class racecar_sim_adapter {
public:
    virtual ~racecar_sim_adapter() = default;

    [[nodiscard]] virtual racecar_state get_state() = 0;
    virtual void apply_action(double steering, double throttle) = 0;
    virtual void step(std::int64_t steps) = 0;
    virtual void reset() = 0;
    virtual void debug_draw() {}
    [[nodiscard]] virtual bool stop_requested() const { return false; }
    virtual void on_tick_record(const racecar_tick_record&) {}
};

void set_racecar_sim_adapter(std::shared_ptr<racecar_sim_adapter> adapter);
[[nodiscard]] std::shared_ptr<racecar_sim_adapter> racecar_sim_adapter_ptr();
void clear_racecar_demo_state();

[[nodiscard]] racecar_state racecar_get_state();
void racecar_apply_action(double steering, double throttle);
void racecar_step(std::int64_t steps);
void racecar_reset();
void racecar_debug_draw();

struct racecar_loop_options {
    double tick_hz = 20.0;
    std::int64_t max_ticks = 700;
    std::string state_key = "state";
    std::string action_key = "action";
    std::int64_t steps_per_tick = 12;
    std::array<double, 2> safe_action{0.0, 0.0};
    bool draw_debug = false;
    std::string mode = "bt";
    std::string planner_meta_key = "plan-meta";
    std::string run_id = "run";
    double goal_tolerance = 0.6;
};

enum class racecar_loop_status {
    ok,
    stopped,
    error
};

[[nodiscard]] const char* racecar_loop_status_name(racecar_loop_status status) noexcept;

struct racecar_loop_result {
    racecar_loop_status status = racecar_loop_status::error;
    std::int64_t ticks = 0;
    std::string reason;
    racecar_state final_state{};
    bool goal_reached = false;
    std::int64_t collisions_total = 0;
    std::int64_t fallback_count = 0;
};

[[nodiscard]] racecar_loop_result run_racecar_loop(runtime_host& host,
                                                   std::int64_t instance_handle,
                                                   const racecar_loop_options& options);

void install_racecar_demo_callbacks(runtime_host& host);

}  // namespace bt

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace air_hockey_demo {

inline constexpr std::size_t kObservationDimension = 19;
inline constexpr std::size_t kActionDimension = 2;

struct host_configuration {
    std::int64_t blackout_start_step = 8;
    std::int64_t blackout_length_steps = 4;
    std::int64_t timeout_steps = 40;
    std::int64_t action_lock_steps = 0;
    std::vector<std::int64_t> replace_track_steps;
    std::optional<std::int64_t> terminate_at_step;
};

struct host_info {
    std::string protocol_version;
    std::string backend;
    std::string observation_schema;
    std::string action_schema;
    std::int64_t observation_dimension = 0;
    std::int64_t action_dimension = 0;
    std::int64_t control_period_ms = 0;
    std::int64_t max_deadline_ms = 0;
    bool privileged_fields_available = true;
};

struct public_state {
    std::string observation_schema;
    std::array<double, kObservationDimension> observation{};
    std::int64_t observation_step = 0;
    bool puck_visible = false;
    bool action_locked = false;
    bool episode_active = false;
    bool terminated = false;
    bool truncated = false;
    std::string defence_context_id;
    std::string episode_id;
};

struct host_step_result {
    public_state state;
    double reward = 0.0;
};

class host_protocol_error final : public std::runtime_error {
public:
    host_protocol_error(std::string code, std::string message);

    [[nodiscard]] const std::string& code() const noexcept;

private:
    std::string code_;
};

class host_client {
public:
    explicit host_client(std::filesystem::path socket_path);

    [[nodiscard]] host_info info();
    [[nodiscard]] host_configuration configure(const host_configuration& configuration);
    [[nodiscard]] public_state reset(std::optional<std::uint32_t> seed);
    [[nodiscard]] public_state observe();
    void act(const std::array<double, kActionDimension>& action);
    [[nodiscard]] host_step_result step();
    void close();

private:
    [[nodiscard]] std::string exchange(const std::string& operation,
                                       const std::string& payload_json);

    std::filesystem::path socket_path_;
    std::uint64_t next_request_id_ = 1;
};

}  // namespace air_hockey_demo

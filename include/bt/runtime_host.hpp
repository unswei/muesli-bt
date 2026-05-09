#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "bt/compiler.hpp"
#include "bt/event_log.hpp"
#include "bt/model_service.hpp"
#include "bt/planner.hpp"
#include "bt/runtime.hpp"
#include "bt/vla.hpp"

namespace bt {

class runtime_host {
public:
    runtime_host();

    std::int64_t store_definition(definition def);
    std::int64_t create_instance(std::int64_t definition_handle);

    definition* find_definition(std::int64_t handle);
    const definition* find_definition(std::int64_t handle) const;
    instance* find_instance(std::int64_t handle);
    const instance* find_instance(std::int64_t handle) const;

    status tick_instance(std::int64_t handle);
    void reset_instance(std::int64_t handle);

    registry& callbacks() noexcept;
    const registry& callbacks() const noexcept;

    scheduler& scheduler_ref();
    const scheduler& scheduler_ref() const;

    planner_service& planner_ref();
    const planner_service& planner_ref() const;
    vla_service& vla_ref();
    const vla_service& vla_ref() const;
    void set_model_service_client(model_service_config config, std::unique_ptr<model_service_client> client);
    void clear_model_service_client() noexcept;
    [[nodiscard]] bool model_service_configured() const noexcept;
    [[nodiscard]] const model_service_config& model_service_config_ref() const noexcept;
    [[nodiscard]] model_service_response call_model_service(const model_service_request& request);
    [[nodiscard]] model_service_compatibility_result check_model_service_compatibility();

    memory_log_sink& logs() noexcept;
    const memory_log_sink& logs() const noexcept;
    event_log& events() noexcept;
    const event_log& events() const noexcept;

    void set_clock_interface(clock_interface* clock) noexcept;
    clock_interface* clock_interface_ptr() noexcept;
    const clock_interface* clock_interface_ptr() const noexcept;

    void set_robot_interface(robot_interface* robot) noexcept;
    robot_interface* robot_interface_ptr() noexcept;
    const robot_interface* robot_interface_ptr() const noexcept;

    void enable_deterministic_test_mode(std::uint64_t planner_seed = 0x4d6f6f736c694254ull,
                                        std::string run_id = "deterministic-run",
                                        std::int64_t unix_ms_start = 1735689600000,
                                        std::int64_t unix_ms_step = 1);
    void disable_deterministic_test_mode() noexcept;
    [[nodiscard]] bool deterministic_test_mode_enabled() const noexcept;

    void clear_logs();
    void clear_all();

    std::string dump_instance_stats(std::int64_t handle) const;
    std::string dump_instance_trace(std::int64_t handle) const;
    std::string dump_instance_blackboard(std::int64_t handle) const;
    std::string dump_scheduler_stats() const;
    std::string dump_logs() const;
    std::string dump_planner_records(std::size_t max_count = 200) const;
    std::string dump_vla_records(std::size_t max_count = 200) const;

private:
    std::int64_t next_definition_handle_ = 1;
    std::int64_t next_instance_handle_ = 1;

    std::unordered_map<std::int64_t, definition> definitions_;
    std::unordered_map<std::int64_t, std::unique_ptr<instance>> instances_;

    registry registry_;
    thread_pool_scheduler scheduler_;
    memory_log_sink logs_;
    event_log events_;
    planner_service planner_;
    vla_service vla_;
    model_service_config model_service_config_{};
    std::unique_ptr<model_service_client> model_service_client_;
    std::size_t model_service_fault_index_ = 0;

    std::unique_ptr<clock_interface> owned_clock_;
    std::unique_ptr<robot_interface> owned_robot_;
    clock_interface* clock_ = nullptr;
    robot_interface* robot_ = nullptr;

    bool deterministic_test_mode_enabled_ = false;
};

runtime_host& default_runtime_host();
void install_demo_callbacks(runtime_host& host);

}  // namespace bt

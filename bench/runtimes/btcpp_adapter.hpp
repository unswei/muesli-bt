#pragma once

#include <filesystem>
#include <memory>

#include "runtimes/runtime_adapter.hpp"

namespace muesli_bt::bench {

class btcpp_adapter final : public runtime_adapter {
public:
    btcpp_adapter();

    std::string name() const override;
    std::string version() const override;
    std::string commit() const override;
    bool supports_scenario(const scenario_definition& scenario) const override;
    std::unique_ptr<compiled_tree> compile_tree(const tree_fixture& fixture) override;
    std::unique_ptr<lifecycle_case> new_lifecycle_case(const tree_fixture& fixture,
                                                       const scenario_definition& scenario,
                                                       const std::filesystem::path& scratch_dir) override;
    std::unique_ptr<instance_handle> new_instance(const compiled_tree& compiled,
                                                  const scenario_definition& scenario) override;
    void prepare_for_timed_run(instance_handle& instance,
                               std::size_t estimated_tick_count,
                               std::size_t repetition_index) override;
    run_status tick(instance_handle& instance) override;
    run_counters read_counters(const instance_handle& instance) const override;
    void teardown(instance_handle& instance) override;

private:
    class compiled_tree_impl;
    class instance_impl;
    class lifecycle_case_impl;
};

}  // namespace muesli_bt::bench

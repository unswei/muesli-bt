#pragma once

#include <memory>
#include <unordered_map>

#include "bt/instance.hpp"
#include "bt/registry.hpp"
#include "runtimes/runtime_adapter.hpp"

namespace muesli_bt::bench {

class muesli_adapter final : public runtime_adapter {
public:
    muesli_adapter();

    std::string name() const override;
    std::string version() const override;
    std::string commit() const override;
    std::unique_ptr<compiled_tree> compile_tree(const tree_fixture& fixture) override;
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

    bt::registry* registry() noexcept;
    const bt::registry* registry() const noexcept;
    instance_impl& state_for(bt::instance& instance) const;
    const instance_impl& state_for(const bt::instance& instance) const;
    void register_callbacks();

    std::unique_ptr<bt::registry> registry_;
    mutable std::unordered_map<const bt::instance*, instance_impl*> instance_index_;
};

}  // namespace muesli_bt::bench

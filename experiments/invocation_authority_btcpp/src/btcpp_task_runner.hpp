#pragma once

#include "common_task.hpp"
#include "effect_recorder.hpp"
#include "variant.hpp"

#include <behaviortree_cpp/basic_types.h>

#include <cstddef>
#include <memory>
#include <vector>

namespace muesli_bt::experiments::controlled_authority
{

class btcpp_task_runner
{
public:
  btcpp_task_runner(deterministic_coordinator& coordinator, effect_recorder& recorder,
                    std::unique_ptr<authority_variant> variant,
                    logical_time request_deadline = logical_time{500});
  ~btcpp_task_runner();

  btcpp_task_runner(const btcpp_task_runner&) = delete;
  btcpp_task_runner& operator=(const btcpp_task_runner&) = delete;

  void request_submission(std::size_t count = 1);
  variant_update cancel_request(std::uint64_t request_id);
  BT::NodeStatus tick();
  void reset();

  [[nodiscard]] const authority_variant& variant() const noexcept;
  [[nodiscard]] authority_variant& variant() noexcept;
  [[nodiscard]] const std::vector<request_record>& submitted_requests() const noexcept;
  [[nodiscard]] std::vector<std::string> task_events() const;
  [[nodiscard]] std::vector<std::string> variant_events() const;

private:
  class implementation;
  std::unique_ptr<implementation> implementation_;
};

} // namespace muesli_bt::experiments::controlled_authority

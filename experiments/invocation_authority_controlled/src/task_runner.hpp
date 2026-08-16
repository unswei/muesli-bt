#pragma once

#include "bt/status.hpp"
#include "common_task.hpp"
#include "effect_recorder.hpp"
#include "variant.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace muesli_bt::experiments::controlled_authority
{

struct task_runner_config
{
  logical_time request_deadline{500};
};

class shared_lisp_task_runner
{
public:
  shared_lisp_task_runner(deterministic_coordinator& coordinator, effect_recorder& recorder,
                          std::unique_ptr<authority_variant> variant,
                          std::string common_task_source,
                          task_runner_config config = task_runner_config{});
  ~shared_lisp_task_runner();

  shared_lisp_task_runner(const shared_lisp_task_runner&) = delete;
  shared_lisp_task_runner& operator=(const shared_lisp_task_runner&) = delete;

  void request_submission(std::size_t count = 1);
  variant_update pump();
  variant_update cancel_request(std::uint64_t request_id);
  bt::status tick();
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

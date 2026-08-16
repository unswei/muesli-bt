#pragma once

#include "variant.hpp"

#include <memory>
#include <vector>

namespace muesli_bt::experiments::controlled_authority
{

class invocation_scoped_variant final : public authority_variant
{
public:
  invocation_scoped_variant(std::shared_ptr<proposal_provider> provider, effect_recorder& recorder,
                            logical_now now, logical_time request_deadline = logical_time{500},
                            proposal_validation_config validation = proposal_validation_config{});
  ~invocation_scoped_variant() override;

  invocation_scoped_variant(const invocation_scoped_variant&) = delete;
  invocation_scoped_variant& operator=(const invocation_scoped_variant&) = delete;

  [[nodiscard]] const variant_descriptor& descriptor() const noexcept override;
  variant_update submit(const request_record& request) override;
  variant_update poll(logical_time admission_at) override;
  bool dispatch(logical_time dispatch_at) override;
  variant_update cancel(std::uint64_t request_id, logical_time cancelled_at) override;
  void synchronise(const task_snapshot& task) override;
  void halt(logical_time halted_at, std::string_view reason) override;
  void reset(logical_time reset_at) override;
  [[nodiscard]] std::vector<std::string> canonical_events() const override;
  [[nodiscard]] std::size_t active_jobs() const override;

private:
  class implementation;
  std::unique_ptr<implementation> implementation_;
};

} // namespace muesli_bt::experiments::controlled_authority

#pragma once

#include "variant.hpp"

#include <memory>

namespace muesli_bt::experiments::controlled_authority
{

class btcpp_asynchronous_variant final : public authority_variant
{
public:
  btcpp_asynchronous_variant(
      std::shared_ptr<proposal_provider> provider, effect_recorder& recorder, logical_now now,
      proposal_validation_config validation = proposal_validation_config{});
  ~btcpp_asynchronous_variant() override;

  btcpp_asynchronous_variant(const btcpp_asynchronous_variant&) = delete;
  btcpp_asynchronous_variant& operator=(const btcpp_asynchronous_variant&) = delete;

  [[nodiscard]] const variant_descriptor& descriptor() const noexcept override;
  variant_update submit(const request_record& request) override;
  variant_update poll(logical_time admission_at) override;
  bool dispatch(logical_time dispatch_at) override;
  void synchronise(const task_snapshot& task) override;
  void halt(logical_time halted_at, std::string_view reason) override;
  void reset(logical_time reset_at) override;
  [[nodiscard]] std::vector<std::string> canonical_events() const override;
  [[nodiscard]] std::size_t active_jobs() const override;

private:
  class implementation;
  std::unique_ptr<implementation> implementation_;
};

class btcpp_invocation_scoped_variant final : public authority_variant
{
public:
  btcpp_invocation_scoped_variant(
      std::shared_ptr<proposal_provider> provider, effect_recorder& recorder, logical_now now,
      proposal_validation_config validation = proposal_validation_config{});
  ~btcpp_invocation_scoped_variant() override;

  btcpp_invocation_scoped_variant(const btcpp_invocation_scoped_variant&) = delete;
  btcpp_invocation_scoped_variant& operator=(const btcpp_invocation_scoped_variant&) = delete;

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

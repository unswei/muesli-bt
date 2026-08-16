#pragma once

#include "common_task.hpp"
#include "effect_recorder.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace muesli_bt::experiments::controlled_authority
{

struct variant_descriptor
{
  std::string variant_id;
  std::string short_label;
  std::string reader_label;
  bool blocking = false;
};

enum class provider_status
{
  ok,
  disconnected,
  failed,
};

struct task_proposal
{
  std::string response_id;
  std::string frame_id = "task_context";
  std::array<double, 3> pose{0.0, 0.0, 0.0};
  bool schema_valid = true;
};

struct provider_result
{
  provider_status status = provider_status::ok;
  task_proposal proposal;
  std::string reason;
  std::size_t completion_copies = 1;
};

class proposal_provider
{
public:
  virtual ~proposal_provider() = default;
  virtual provider_result infer(const request_record& request) = 0;
  virtual bool cancel(const request_record& request);
};

struct proposal_validation_config
{
  std::string frame_id = "task_context";
  std::array<double, 3> minimum{-1.0, -1.0, -3.141593};
  std::array<double, 3> maximum{1.0, 1.0, 3.141593};
};

[[nodiscard]] std::optional<std::string>
validate_provider_result(const provider_result& result, const proposal_validation_config& config);

using logical_now = std::function<logical_time()>;

struct variant_update
{
  std::size_t provider_completions = 0;
  std::size_t commits = 0;
  std::size_t rejections = 0;
  std::string last_reason;

  [[nodiscard]] bool has_terminal_decision() const noexcept
  {
    return commits > 0 || rejections > 0;
  }
};

class authority_variant
{
public:
  virtual ~authority_variant() = default;

  [[nodiscard]] virtual const variant_descriptor& descriptor() const noexcept = 0;
  virtual variant_update submit(const request_record& request) = 0;
  virtual variant_update poll(logical_time admission_at) = 0;
  virtual bool dispatch(logical_time dispatch_at) = 0;
  virtual variant_update cancel(std::uint64_t request_id, logical_time cancelled_at);
  virtual void synchronise(const task_snapshot& task);
  virtual void halt(logical_time halted_at, std::string_view reason);
  virtual void reset(logical_time reset_at);
  [[nodiscard]] virtual std::vector<std::string> canonical_events() const;
  [[nodiscard]] virtual std::size_t active_jobs() const = 0;
};

class blocking_variant final : public authority_variant
{
public:
  blocking_variant(std::shared_ptr<proposal_provider> provider, effect_recorder& recorder,
                   logical_now now,
                   proposal_validation_config validation = proposal_validation_config{});
  ~blocking_variant() override;

  blocking_variant(const blocking_variant&) = delete;
  blocking_variant& operator=(const blocking_variant&) = delete;

  [[nodiscard]] const variant_descriptor& descriptor() const noexcept override;
  variant_update submit(const request_record& request) override;
  variant_update poll(logical_time admission_at) override;
  bool dispatch(logical_time dispatch_at) override;
  [[nodiscard]] std::size_t active_jobs() const override;

private:
  class implementation;
  std::unique_ptr<implementation> implementation_;
};

class asynchronous_variant final : public authority_variant
{
public:
  asynchronous_variant(std::shared_ptr<proposal_provider> provider, effect_recorder& recorder,
                       logical_now now,
                       proposal_validation_config validation = proposal_validation_config{});
  ~asynchronous_variant() override;

  asynchronous_variant(const asynchronous_variant&) = delete;
  asynchronous_variant& operator=(const asynchronous_variant&) = delete;

  [[nodiscard]] const variant_descriptor& descriptor() const noexcept override;
  variant_update submit(const request_record& request) override;
  variant_update poll(logical_time admission_at) override;
  bool dispatch(logical_time dispatch_at) override;
  [[nodiscard]] std::size_t active_jobs() const override;

private:
  class implementation;
  std::unique_ptr<implementation> implementation_;
};

class timeout_variant final : public authority_variant
{
public:
  timeout_variant(std::shared_ptr<proposal_provider> provider, effect_recorder& recorder,
                  logical_now now,
                  proposal_validation_config validation = proposal_validation_config{});
  ~timeout_variant() override;

  timeout_variant(const timeout_variant&) = delete;
  timeout_variant& operator=(const timeout_variant&) = delete;

  [[nodiscard]] const variant_descriptor& descriptor() const noexcept override;
  variant_update submit(const request_record& request) override;
  variant_update poll(logical_time admission_at) override;
  bool dispatch(logical_time dispatch_at) override;
  variant_update cancel(std::uint64_t request_id, logical_time cancelled_at) override;
  [[nodiscard]] std::size_t active_jobs() const override;

private:
  class implementation;
  std::unique_ptr<implementation> implementation_;
};

} // namespace muesli_bt::experiments::controlled_authority

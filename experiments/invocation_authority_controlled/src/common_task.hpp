#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace muesli_bt::experiments::controlled_authority
{

using logical_time = std::chrono::milliseconds;

enum class task_branch
{
  model,
  fallback,
  safe_stand,
};

enum class task_event_kind
{
  enter_model_branch,
  leave_model_branch,
  reenter_model_branch,
  context_changed,
  emergency_activated,
  emergency_cleared,
  runtime_reset,
};

struct task_event
{
  std::uint64_t sequence = 0;
  logical_time at{};
  task_event_kind kind = task_event_kind::enter_model_branch;
  std::string context_id;
};

struct request_record
{
  std::uint64_t request_id = 0;
  std::uint64_t branch_epoch = 0;
  std::uint64_t generation = 0;
  std::uint64_t reset_epoch = 0;
  std::string captured_context_id;
  logical_time submitted_at{};
  logical_time deadline{};
};

struct task_snapshot
{
  task_branch branch = task_branch::fallback;
  bool model_branch_active = false;
  bool emergency = false;
  std::uint64_t branch_epoch = 0;
  std::uint64_t generation = 0;
  std::uint64_t reset_epoch = 0;
  std::string context_id;
  logical_time last_event_at{};
};

class common_task
{
public:
  explicit common_task(std::string initial_context_id);

  common_task(const common_task&) = delete;
  common_task& operator=(const common_task&) = delete;

  void apply(const task_event& event);
  request_record submit_request(logical_time now, logical_time deadline);
  [[nodiscard]] task_snapshot snapshot() const;

private:
  mutable std::mutex mutex_;
  task_snapshot state_;
  std::uint64_t next_request_id_ = 1;
  std::uint64_t last_event_sequence_ = 0;
  bool has_event_ = false;
};

enum class authority_reason
{
  current,
  branch_revoked,
  superseded,
  context_changed,
  deadline_expired,
};

[[nodiscard]] std::string_view to_string(authority_reason reason) noexcept;

struct authority_assessment
{
  bool current = false;
  authority_reason reason = authority_reason::branch_revoked;
};

class authority_oracle
{
public:
  explicit authority_oracle(std::string initial_context_id);

  authority_oracle(const authority_oracle&) = delete;
  authority_oracle& operator=(const authority_oracle&) = delete;

  void apply(const task_event& event);
  void register_request(const request_record& request);
  [[nodiscard]] authority_assessment assess(const request_record& request,
                                            logical_time effect_at) const;
  [[nodiscard]] task_snapshot snapshot() const;

private:
  mutable std::mutex mutex_;
  task_snapshot state_;
  std::uint64_t last_event_sequence_ = 0;
  bool has_event_ = false;
};

class deterministic_coordinator
{
public:
  deterministic_coordinator(std::string initial_context_id, std::vector<task_event> events);

  deterministic_coordinator(const deterministic_coordinator&) = delete;
  deterministic_coordinator& operator=(const deterministic_coordinator&) = delete;

  void advance_to(logical_time target);
  request_record submit_request(logical_time deadline_after_submission);
  [[nodiscard]] authority_assessment assess(const request_record& request,
                                            logical_time effect_at) const;
  [[nodiscard]] task_snapshot task_state() const;
  [[nodiscard]] task_snapshot oracle_state() const;
  [[nodiscard]] logical_time now() const;
  [[nodiscard]] std::size_t remaining_events() const;

private:
  static void validate_schedule(const std::vector<task_event>& events);

  mutable std::mutex mutex_;
  common_task task_;
  authority_oracle oracle_;
  std::vector<task_event> events_;
  std::size_t next_event_ = 0;
  logical_time now_{};
};

}  // namespace muesli_bt::experiments::controlled_authority

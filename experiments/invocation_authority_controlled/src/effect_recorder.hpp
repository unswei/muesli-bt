#pragma once

#include "common_task.hpp"

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace muesli_bt::experiments::controlled_authority
{

enum class effect_kind
{
  request_submitted,
  provider_completed,
  result_committed,
  result_rejected,
  cancellation_requested,
  capability_dispatched,
  capability_dispatch_rejected,
  fallback_activated,
  safe_stand_activated,
};

[[nodiscard]] std::string_view to_string(effect_kind kind) noexcept;

struct effect_record
{
  std::uint64_t sequence = 0;
  effect_kind kind = effect_kind::request_submitted;
  std::string variant_id;
  logical_time at{};
  std::optional<request_record> request;
  std::string response_id;
  std::string reason;
  bool authority_assessed = false;
  authority_assessment authority;
};

struct effect_summary
{
  std::size_t requests_submitted = 0;
  std::size_t provider_completions = 0;
  std::size_t current_commits = 0;
  std::size_t obsolete_commits = 0;
  std::size_t result_rejections = 0;
  std::size_t cancellation_requests = 0;
  std::size_t terminal_decisions = 0;
  std::size_t current_dispatches = 0;
  std::size_t obsolete_dispatches = 0;
  std::size_t dispatch_rejections = 0;
  std::size_t fallback_activations = 0;
  std::size_t safe_stand_activations = 0;

  [[nodiscard]] bool has_obsolete_effect() const noexcept
  {
    return obsolete_commits > 0 || obsolete_dispatches > 0;
  }
};

using authority_assessor =
    std::function<authority_assessment(const request_record&, logical_time)>;

class effect_recorder
{
public:
  explicit effect_recorder(authority_assessor assessor);

  effect_recorder(const effect_recorder&) = delete;
  effect_recorder& operator=(const effect_recorder&) = delete;

  void record_request(std::string_view variant_id, const request_record& request);
  void record_provider_completion(std::string_view variant_id, const request_record& request,
                                  std::string_view response_id, logical_time completed_at);
  void record_commit(std::string_view variant_id, const request_record& request,
                     std::string_view response_id, logical_time committed_at);
  void record_rejection(std::string_view variant_id, const request_record& request,
                        std::string_view response_id, std::string_view reason,
                        logical_time rejected_at);
  void record_cancellation(std::string_view variant_id, const request_record& request,
                           logical_time requested_at, std::string_view reason);
  void record_dispatch(std::string_view variant_id, const request_record& request,
                       std::string_view response_id, logical_time dispatched_at);
  void record_dispatch_rejection(std::string_view variant_id, const request_record& request,
                                 std::string_view response_id, std::string_view reason,
                                 logical_time rejected_at);
  void record_fallback(std::string_view variant_id, logical_time activated_at,
                       std::string_view reason);
  void record_safe_stand(std::string_view variant_id, logical_time activated_at,
                         std::string_view reason);

  [[nodiscard]] std::vector<effect_record> snapshot() const;
  [[nodiscard]] effect_summary summary(std::string_view variant_id) const;

private:
  void append(effect_record record);
  [[nodiscard]] authority_assessment assess(const request_record& request,
                                            logical_time effect_at) const;

  authority_assessor assessor_;
  mutable std::mutex mutex_;
  std::vector<effect_record> records_;
  std::uint64_t next_sequence_ = 1;
};

}  // namespace muesli_bt::experiments::controlled_authority

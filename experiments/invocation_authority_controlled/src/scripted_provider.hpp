#pragma once

#include "variant.hpp"

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace muesli_bt::experiments::controlled_authority
{

struct scripted_provider_job
{
  std::string request_label;
  provider_result result;
};

class scripted_provider final : public proposal_provider
{
public:
  explicit scripted_provider(std::vector<scripted_provider_job> jobs);
  ~scripted_provider() override;

  scripted_provider(const scripted_provider&) = delete;
  scripted_provider& operator=(const scripted_provider&) = delete;

  provider_result infer(const request_record& request) override;
  bool cancel(const request_record& request) override;

  void wait_until_started(std::string_view request_label);
  void release(std::string_view request_label);
  void wait_until_finished(std::string_view request_label);
  void release_all() noexcept;

private:
  struct job_state
  {
    scripted_provider_job script;
    std::uint64_t request_id = 0;
    bool started = false;
    bool released = false;
    bool finished = false;
    bool cancellation_requested = false;
  };

  [[nodiscard]] job_state& find_label(std::string_view request_label);
  [[nodiscard]] job_state* find_request(std::uint64_t request_id);

  std::mutex mutex_;
  std::condition_variable condition_;
  std::vector<std::unique_ptr<job_state>> jobs_;
  std::size_t next_job_ = 0;
};

} // namespace muesli_bt::experiments::controlled_authority

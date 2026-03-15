#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace muesli_bt::bench {

struct environment_info {
    std::string machine_id;
    std::string hostname;
    std::string os_name;
    std::string os_version;
    std::string kernel_version;
    std::string cpu_model;
    std::string cpu_arch;
    std::size_t physical_cores = 0;
    std::size_t logical_cores = 0;
    std::string cpu_governor;
    std::string cpu_pinning = "off";
    std::uint64_t ram_bytes_total = 0;
    std::string compiler_name;
    std::string compiler_version;
    std::string build_type;
    std::string build_flags;
    std::string runtime_name = "muesli-bt";
    std::string runtime_version;
    std::string runtime_commit;
    std::string harness_commit;
    std::string clock_source = "std::chrono::steady_clock";
    std::string allocator_mode = "global_new_counter";
    std::string notes;
};

environment_info collect_environment();
std::string timestamp_utc_now();
std::string timestamp_utc_slug_now();
std::uint64_t peak_rss_bytes() noexcept;
std::filesystem::path default_results_root();

}  // namespace muesli_bt::bench

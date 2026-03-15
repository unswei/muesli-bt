#include "harness/metadata.hpp"

#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <sys/resource.h>
#include <sys/utsname.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

#include "bench_config.hpp"

namespace muesli_bt::bench {
namespace {

std::string trim_copy(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    std::size_t index = 0;
    while (index < value.size() && (value[index] == ' ' || value[index] == '\t')) {
        ++index;
    }
    return value.substr(index);
}

std::string hostname() {
    std::array<char, 256> buffer{};
    if (::gethostname(buffer.data(), buffer.size()) != 0) {
        return "unknown";
    }
    buffer.back() = '\0';
    return buffer.data();
}

std::uint64_t fnv1a64(std::string_view text) noexcept {
    std::uint64_t hash = 1469598103934665603ull;
    for (const unsigned char byte : text) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string hash64_hex(std::string_view text) {
    std::ostringstream out;
    out << "fnv1a64:" << std::hex << std::setw(16) << std::setfill('0') << fnv1a64(text);
    return out.str();
}

std::size_t logical_core_count() {
    const long count = ::sysconf(_SC_NPROCESSORS_ONLN);
    return count > 0 ? static_cast<std::size_t>(count) : 1u;
}

std::size_t physical_core_count() {
#if defined(__APPLE__)
    std::uint32_t value = 0;
    std::size_t size = sizeof(value);
    if (::sysctlbyname("hw.physicalcpu", &value, &size, nullptr, 0) == 0 && value > 0u) {
        return static_cast<std::size_t>(value);
    }
    return logical_core_count();
#else
    std::ifstream in("/proc/cpuinfo");
    if (!in) {
        return logical_core_count();
    }

    std::set<std::pair<std::string, std::string>> seen;
    std::string line;
    std::string physical_id;
    std::string core_id;
    while (std::getline(in, line)) {
        if (line.rfind("physical id", 0) == 0) {
            physical_id = trim_copy(line.substr(line.find(':') + 1u));
        } else if (line.rfind("core id", 0) == 0) {
            core_id = trim_copy(line.substr(line.find(':') + 1u));
        } else if (line.empty()) {
            if (!physical_id.empty() || !core_id.empty()) {
                seen.emplace(physical_id, core_id);
            }
            physical_id.clear();
            core_id.clear();
        }
    }
    if (!physical_id.empty() || !core_id.empty()) {
        seen.emplace(physical_id, core_id);
    }
    return seen.empty() ? logical_core_count() : seen.size();
#endif
}

std::string cpu_arch(const utsname& uts) {
    return uts.machine[0] == '\0' ? "unknown" : std::string(uts.machine);
}

std::string cpu_model() {
#if defined(__APPLE__)
    char buffer[256]{};
    std::size_t size = sizeof(buffer);
    if (::sysctlbyname("machdep.cpu.brand_string", buffer, &size, nullptr, 0) == 0 && size > 1u) {
        return trim_copy(buffer);
    }
    return "unknown";
#else
    std::ifstream in("/proc/cpuinfo");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("model name", 0) == 0) {
            return trim_copy(line.substr(line.find(':') + 1u));
        }
    }
    return "unknown";
#endif
}

std::string cpu_governor() {
#if defined(__APPLE__)
    return "";
#else
    std::ifstream in("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
    if (!in) {
        return "";
    }
    std::string line;
    std::getline(in, line);
    return trim_copy(line);
#endif
}

std::uint64_t ram_bytes_total() {
#if defined(__APPLE__)
    std::uint64_t value = 0u;
    std::size_t size = sizeof(value);
    if (::sysctlbyname("hw.memsize", &value, &size, nullptr, 0) == 0) {
        return value;
    }
    return 0u;
#else
    std::ifstream in("/proc/meminfo");
    if (!in) {
        return 0u;
    }

    std::string key;
    std::uint64_t value_kib = 0u;
    std::string unit;
    while (in >> key >> value_kib >> unit) {
        if (key == "MemTotal:") {
            return value_kib * 1024ull;
        }
    }
    return 0u;
#endif
}

std::string environment_notes(std::string_view cpu_governor_value) {
    std::vector<std::string> parts;
    if (cpu_governor_value.empty()) {
        parts.push_back("cpu governor unavailable");
    }
    parts.push_back("cpu pinning not enabled");

    std::ostringstream out;
    for (std::size_t index = 0; index < parts.size(); ++index) {
        if (index != 0u) {
            out << "; ";
        }
        out << parts[index];
    }
    return out.str();
}

}  // namespace

environment_info collect_environment() {
    struct utsname uts {};
    if (::uname(&uts) != 0) {
        std::memset(&uts, 0, sizeof(uts));
    }

    environment_info info;
    info.hostname = hostname();
    info.os_name = uts.sysname[0] == '\0' ? "unknown" : uts.sysname;
    info.os_version = uts.version[0] == '\0' ? "unknown" : uts.version;
    info.kernel_version = uts.release[0] == '\0' ? "unknown" : uts.release;
    info.cpu_model = cpu_model();
    info.cpu_arch = cpu_arch(uts);
    info.physical_cores = physical_core_count();
    info.logical_cores = logical_core_count();
    info.cpu_governor = cpu_governor();
    info.cpu_pinning = "off";
    info.ram_bytes_total = ram_bytes_total();
    info.compiler_name = MUESLI_BT_BENCH_COMPILER_ID;
    info.compiler_version = MUESLI_BT_BENCH_COMPILER_VERSION;
    info.build_type = MUESLI_BT_BENCH_BUILD_TYPE;
    info.build_flags = MUESLI_BT_BENCH_BUILD_FLAGS;
    info.runtime_version = MUESLI_BT_BENCH_PROJECT_VERSION;
    info.runtime_commit = MUESLI_BT_BENCH_GIT_COMMIT;
    info.harness_commit = MUESLI_BT_BENCH_GIT_COMMIT;
    info.clock_source = "std::chrono::steady_clock";
    info.allocator_mode = "global_new_counter";
    info.notes = environment_notes(info.cpu_governor);
    info.machine_id = hash64_hex(info.hostname + "|" + info.cpu_model + "|" + info.os_name + "|" + info.kernel_version);
    return info;
}

std::string timestamp_utc_now() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string timestamp_utc_slug_now() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y%m%dT%H%M%SZ");
    return out.str();
}

std::uint64_t peak_rss_bytes() noexcept {
    struct rusage usage {};
    if (::getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0u;
    }
#if defined(__APPLE__)
    return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
    return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024ull;
#endif
}

std::filesystem::path default_results_root() {
    return std::filesystem::path(MUESLI_BT_BENCH_SOURCE_DIR) / "bench" / "results";
}

}  // namespace muesli_bt::bench

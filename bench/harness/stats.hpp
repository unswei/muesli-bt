#pragma once

#include <cstdint>
#include <vector>

namespace muesli_bt::bench {

struct latency_summary {
    std::uint64_t median = 0;
    std::uint64_t p95 = 0;
    std::uint64_t p99 = 0;
    std::uint64_t p999 = 0;
    std::uint64_t max = 0;
    double jitter_ratio_p99_over_median = 0.0;
};

latency_summary summarise_latencies(const std::vector<std::uint64_t>& samples);
std::uint64_t percentile_u64(std::vector<std::uint64_t> samples, double fraction);
double percentile_double(std::vector<double> samples, double fraction);
double mean_double(const std::vector<double>& samples);
double stddev_double(const std::vector<double>& samples);

}  // namespace muesli_bt::bench

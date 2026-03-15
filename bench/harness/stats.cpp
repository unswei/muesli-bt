#include "harness/stats.hpp"

#include <algorithm>
#include <cmath>

namespace muesli_bt::bench {
namespace {

template <typename T>
T percentile_sorted(const std::vector<T>& sorted, double fraction) {
    if (sorted.empty()) {
        return T{};
    }

    if (fraction <= 0.0) {
        return sorted.front();
    }
    if (fraction >= 1.0) {
        return sorted.back();
    }

    const double scaled = fraction * static_cast<double>(sorted.size());
    std::size_t index = static_cast<std::size_t>(std::ceil(scaled));
    if (index == 0u) {
        index = 1u;
    }
    if (index > sorted.size()) {
        index = sorted.size();
    }
    return sorted[index - 1u];
}

}  // namespace

latency_summary summarise_latencies(const std::vector<std::uint64_t>& samples) {
    if (samples.empty()) {
        return {};
    }

    std::vector<std::uint64_t> sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    const std::uint64_t median = percentile_sorted(sorted, 0.50);
    const std::uint64_t p99 = percentile_sorted(sorted, 0.99);
    return latency_summary{
        .median = median,
        .p95 = percentile_sorted(sorted, 0.95),
        .p99 = p99,
        .p999 = percentile_sorted(sorted, 0.999),
        .max = sorted.back(),
        .jitter_ratio_p99_over_median = median == 0u ? 0.0 : static_cast<double>(p99) / static_cast<double>(median),
    };
}

std::uint64_t percentile_u64(std::vector<std::uint64_t> samples, double fraction) {
    if (samples.empty()) {
        return 0u;
    }
    std::sort(samples.begin(), samples.end());
    return percentile_sorted(samples, fraction);
}

double percentile_double(std::vector<double> samples, double fraction) {
    if (samples.empty()) {
        return 0.0;
    }
    std::sort(samples.begin(), samples.end());
    return percentile_sorted(samples, fraction);
}

double mean_double(const std::vector<double>& samples) {
    if (samples.empty()) {
        return 0.0;
    }

    double total = 0.0;
    for (const double sample : samples) {
        total += sample;
    }
    return total / static_cast<double>(samples.size());
}

double stddev_double(const std::vector<double>& samples) {
    if (samples.size() < 2u) {
        return 0.0;
    }

    const double mean = mean_double(samples);
    double accum = 0.0;
    for (const double sample : samples) {
        const double delta = sample - mean;
        accum += delta * delta;
    }
    return std::sqrt(accum / static_cast<double>(samples.size()));
}

}  // namespace muesli_bt::bench

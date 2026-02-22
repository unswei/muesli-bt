#include "bt/profile.hpp"

namespace bt {

void duration_stats::observe(std::chrono::nanoseconds sample, std::chrono::nanoseconds budget) {
    ++count;
    last = sample;
    total += sample;
    if (sample > max) {
        max = sample;
    }
    if (budget.count() > 0 && sample > budget) {
        ++over_budget_count;
    }
}

}  // namespace bt

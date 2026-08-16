#pragma once

#include "campaign_trial.hpp"

#include <filesystem>
#include <iosfwd>

namespace muesli_bt::experiments::controlled_authority
{

void write_trial_result(const std::filesystem::path& output_directory, std::ostream& raw_results,
                        const trial_result& result);

} // namespace muesli_bt::experiments::controlled_authority

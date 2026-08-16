#include "timing_engine.hpp"
#include "timing_plan.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace controlled = muesli_bt::experiments::controlled_authority;

int main(int argc, char** argv)
{
  if (argc != 3)
  {
    std::cerr << "usage: muesli_bt_controlled_authority_timing <plan.tsv> <output-directory>\n";
    return 2;
  }
  try
  {
    const controlled::timing_plan plan = controlled::read_timing_plan(argv[1]);
    const controlled::timing_engine_result result =
        controlled::execute_timing_plan(plan, std::filesystem::path(argv[2]));
    std::cout << "wrote " << result.trials_written << " timing trials after "
              << result.warmups_executed << " warm-ups to " << result.raw_results_path << '\n';
    return 0;
  }
  catch (const std::exception& error)
  {
    std::cerr << "controlled-authority timing campaign failed: " << error.what() << '\n';
    return 2;
  }
}

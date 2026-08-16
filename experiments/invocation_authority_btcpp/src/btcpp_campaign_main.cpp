#include "btcpp_campaign_engine.hpp"
#include "campaign_plan.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace controlled = muesli_bt::experiments::controlled_authority;

int main(int argc, char** argv)
{
  if (argc == 2 && std::string_view(argv[1]) == "--help")
  {
    std::cerr << "usage: muesli_bt_controlled_authority_btcpp_campaign <plan.tsv> "
                 "<output-directory>\n";
    return 0;
  }
  if (argc != 3)
  {
    std::cerr << "usage: muesli_bt_controlled_authority_btcpp_campaign <plan.tsv> "
                 "<output-directory>\n";
    return 2;
  }
  try
  {
    const controlled::campaign_plan plan =
        controlled::read_campaign_plan(std::filesystem::path(argv[1]));
    const controlled::campaign_engine_result result = controlled::execute_btcpp_comparison_plan(
        plan, std::filesystem::path(argv[2]));
    std::cout << "wrote " << result.trials_written << " external-comparison trials to "
              << result.raw_results_path << '\n';
    return 0;
  }
  catch (const std::exception& error)
  {
    std::cerr << "BehaviorTree.CPP comparison campaign failed: " << error.what() << '\n';
    return 1;
  }
}

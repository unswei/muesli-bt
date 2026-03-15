#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "harness/runner.hpp"

namespace {

void print_usage() {
    std::cout << "usage:\n"
              << "  bench list\n"
              << "  bench run <scenario-id> [--output-dir DIR] [--warmup-ms N] [--run-ms N] [--repetitions N] [--seed N]\n"
              << "  bench run-group <group-id> [--output-dir DIR] [--warmup-ms N] [--run-ms N] [--repetitions N] [--seed N]\n"
              << "  bench run-all [--output-dir DIR] [--warmup-ms N] [--run-ms N] [--repetitions N] [--seed N]\n";
}

std::string require_value(const std::vector<std::string>& args, std::size_t& index, const std::string& option) {
    if (index + 1u >= args.size()) {
        throw std::invalid_argument("missing value for " + option);
    }
    ++index;
    return args[index];
}

}  // namespace

int main(int argc, char** argv) {
    using namespace muesli_bt::bench;

    try {
        std::vector<std::string> args(argv + 1, argv + argc);
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            print_usage();
            return 0;
        }

        run_request request;
        std::string command = args[0];

        if (command == "list") {
            for (const scenario_definition& scenario : default_scenarios()) {
                std::cout << scenario.scenario_id << '\n';
            }
            return 0;
        }

        std::size_t cursor = 1u;
        if (command == "run") {
            if (cursor >= args.size()) {
                throw std::invalid_argument("run requires a scenario id");
            }
            const scenario_definition* scenario = find_scenario(args[cursor]);
            if (!scenario) {
                throw std::invalid_argument("unknown scenario id: " + args[cursor]);
            }
            request.scenarios.push_back(*scenario);
            ++cursor;
        } else if (command == "run-group") {
            if (cursor >= args.size()) {
                throw std::invalid_argument("run-group requires a group id");
            }
            request.scenarios = scenarios_for_group(args[cursor]);
            if (request.scenarios.empty()) {
                throw std::invalid_argument("unknown group id: " + args[cursor]);
            }
            ++cursor;
        } else if (command == "run-all") {
            request.scenarios = default_scenarios();
        } else {
            throw std::invalid_argument("unknown command: " + command);
        }

        for (; cursor < args.size(); ++cursor) {
            const std::string& arg = args[cursor];
            if (arg == "--output-dir") {
                request.output_dir = require_value(args, cursor, arg);
            } else if (arg == "--warmup-ms") {
                request.warmup_override = std::chrono::milliseconds(std::stoll(require_value(args, cursor, arg)));
            } else if (arg == "--run-ms") {
                request.run_override = std::chrono::milliseconds(std::stoll(require_value(args, cursor, arg)));
            } else if (arg == "--repetitions") {
                request.repetitions_override = static_cast<std::size_t>(std::stoull(require_value(args, cursor, arg)));
            } else if (arg == "--seed") {
                request.seed_override = std::stoull(require_value(args, cursor, arg));
            } else if (arg == "--runtime") {
                request.runtime_name = require_value(args, cursor, arg);
            } else {
                throw std::invalid_argument("unknown option: " + arg);
            }
        }

        request.progress_callback = [](const progress_event& event) {
            if (event.kind == progress_event_kind::suite_started) {
                std::cout << event.total_scenarios << " benchmarks queued"
                          << " | est. " << event.expected_duration_text << '\n';
            } else if (event.kind == progress_event_kind::scenario_started) {
                std::cout << '[' << event.scenario_index << '/' << event.total_scenarios << "] "
                          << event.description << " | "
                          << event.started_at_local_minute << " | est. "
                          << event.expected_duration_text << '\n';
            }
            std::cout.flush();
        };

        benchmark_runner runner;
        const run_result result = runner.run(request);
        std::cout << "wrote benchmark results to " << result.output_dir << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "bench: " << e.what() << '\n';
        return 1;
    }
}

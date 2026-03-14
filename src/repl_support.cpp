#include "repl_support.hpp"

#include <cstdlib>

namespace muslisp::repl_support {

namespace {

constexpr std::string_view kHistoryFileName = ".muesli_bt_history";

}  // namespace

bool should_exit_repl(std::string_view line, bool buffer_empty) {
    return buffer_empty && (line == ":q" || line == ":quit" || line == ":exit");
}

bool is_clear_command(std::string_view line) {
    return line == ":clear";
}

std::optional<std::filesystem::path> history_path_from_home(std::string_view home) {
    if (home.empty()) {
        return std::nullopt;
    }
    return std::filesystem::path(std::string(home)) / std::string(kHistoryFileName);
}

std::optional<std::filesystem::path> history_path_from_env() {
    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') {
        return std::nullopt;
    }
    return history_path_from_home(home);
}

std::string normalise_history_entry(std::string_view buffer) {
    if (buffer.empty()) {
        return {};
    }

    std::string entry(buffer);
    while (!entry.empty() && (entry.back() == '\n' || entry.back() == '\r')) {
        entry.pop_back();
    }
    return entry;
}

}  // namespace muslisp::repl_support

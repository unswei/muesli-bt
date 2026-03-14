#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace muslisp::repl_support {

bool should_exit_repl(std::string_view line, bool buffer_empty);
bool is_clear_command(std::string_view line);
std::optional<std::filesystem::path> history_path_from_home(std::string_view home);
std::optional<std::filesystem::path> history_path_from_env();
std::string normalise_history_entry(std::string_view buffer);

}  // namespace muslisp::repl_support

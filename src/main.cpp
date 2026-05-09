#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "muslisp/error.hpp"
#include "muslisp/eval.hpp"
#include "muslisp/gc.hpp"
#include "muslisp/printer.hpp"
#include "muslisp/reader.hpp"
#include "repl_support.hpp"

#if defined(MUESLI_BT_HAVE_LINENOISE)
#include <unistd.h>

#include "linenoise.h"
#endif

namespace {

constexpr const char* kReplPrompt = "muslisp> ";
constexpr const char* kReplContinuationPrompt = "......> ";
constexpr int kReplHistoryMaxLen = 256;

struct model_service_start_options {
    std::optional<std::filesystem::path> service_dir;
    std::string host = "127.0.0.1";
    std::string port = "8765";
    std::string log_level = "info";
    std::optional<std::filesystem::path> replay_path;
    bool enable_mock_backend = true;
    bool dry_run = false;
};

enum class repl_read_status {
    line,
    interrupted,
    eof,
};

struct repl_read_result {
    repl_read_status status = repl_read_status::eof;
    std::string line;
};

std::string read_text_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw muslisp::lisp_error("failed to open file: " + path);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string shell_quote(std::string_view input) {
    std::string out = "'";
    for (const char ch : input) {
        if (ch == '\'') {
            out += "'\\''";
        } else {
            out.push_back(ch);
        }
    }
    out.push_back('\'');
    return out;
}

std::optional<std::filesystem::path> model_service_dir_from_env() {
    const char* env = std::getenv("MUESLI_MODEL_SERVICE_DIR");
    if (env == nullptr || env[0] == '\0') {
        return std::nullopt;
    }
    return std::filesystem::path(env);
}

std::optional<std::filesystem::path> model_service_dir_from_sibling() {
    const std::filesystem::path candidate = std::filesystem::current_path().parent_path() / "muesli-model-service";
    if (std::filesystem::exists(candidate / "pyproject.toml")) {
        return candidate;
    }
    return std::nullopt;
}

std::string next_option_value(int argc, char** argv, int& index, const std::string& option) {
    if (index + 1 >= argc) {
        throw muslisp::lisp_error(option + ": missing value");
    }
    ++index;
    return argv[index];
}

model_service_start_options parse_model_service_start_options(int argc, char** argv, int start_index) {
    model_service_start_options opts;
    for (int i = start_index; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--model-service-dir") {
            opts.service_dir = std::filesystem::path(next_option_value(argc, argv, i, arg));
        } else if (arg == "--host") {
            opts.host = next_option_value(argc, argv, i, arg);
        } else if (arg == "--port") {
            opts.port = next_option_value(argc, argv, i, arg);
        } else if (arg == "--log-level") {
            opts.log_level = next_option_value(argc, argv, i, arg);
        } else if (arg == "--replay-path") {
            opts.replay_path = std::filesystem::path(next_option_value(argc, argv, i, arg));
        } else if (arg == "--no-mock") {
            opts.enable_mock_backend = false;
        } else if (arg == "--dry-run") {
            opts.dry_run = true;
        } else {
            throw muslisp::lisp_error("unknown model-service option: " + arg);
        }
    }
    if (!opts.service_dir.has_value()) {
        opts.service_dir = model_service_dir_from_env();
    }
    if (!opts.service_dir.has_value()) {
        opts.service_dir = model_service_dir_from_sibling();
    }
    return opts;
}

std::string build_model_service_start_command(const model_service_start_options& opts) {
    std::ostringstream cmd;
    if (opts.service_dir.has_value()) {
        cmd << "uv --directory " << shell_quote(opts.service_dir->string()) << " run muesli-model-service serve";
    } else {
        cmd << "muesli-model-service serve";
    }
    cmd << " --host " << shell_quote(opts.host);
    cmd << " --port " << shell_quote(opts.port);
    cmd << " --log-level " << shell_quote(opts.log_level);
    if (opts.replay_path.has_value()) {
        cmd << " --replay-path " << shell_quote(opts.replay_path->string());
    }
    if (!opts.enable_mock_backend) {
        cmd << " --no-enable-mock-backend";
    }
    return cmd.str();
}

int run_model_service_start(int argc, char** argv) {
    const model_service_start_options opts = parse_model_service_start_options(argc, argv, 2);
    const std::string command = build_model_service_start_command(opts);
    std::cout << command << '\n';
    if (opts.dry_run) {
        return 0;
    }
    const int rc = std::system(command.c_str());
    if (rc != 0) {
        std::cerr << "model-service start failed with exit code " << rc << '\n';
        return 1;
    }
    return 0;
}

int print_usage() {
    std::cout
        << "usage:\n"
        << "  muslisp [script.lisp]\n"
        << "  muslisp --model-service-start [--model-service-dir DIR] [--host HOST] [--port PORT]\n"
        << "                                [--log-level LEVEL] [--replay-path PATH] [--no-mock]\n"
        << "\n"
        << "model service discovery:\n"
        << "  --model-service-dir DIR, MUESLI_MODEL_SERVICE_DIR, ../muesli-model-service, then PATH\n";
    return 0;
}

int run_script(const std::string& path, muslisp::env_ptr env) {
    const auto source = read_text_file(path);
    std::vector<muslisp::value> exprs = muslisp::read_all(source);

    muslisp::gc_root_scope roots(muslisp::default_gc());
    for (muslisp::value& expr : exprs) {
        roots.add(&expr);
    }

    muslisp::value result = muslisp::make_nil();
    roots.add(&result);

    for (muslisp::value expr : exprs) {
        result = muslisp::eval(expr, env);
        std::cout << muslisp::print_value(result) << '\n';
        muslisp::default_gc().maybe_collect();
    }

    return 0;
}

#if defined(MUESLI_BT_HAVE_LINENOISE)
class repl_history_store {
  public:
    repl_history_store() : path_(muslisp::repl_support::history_path_from_env()) {
        linenoiseSetMultiLine(1);
        linenoiseHistorySetMaxLen(kReplHistoryMaxLen);
        if (path_.has_value()) {
            const std::string path_string = path_->string();
            (void)linenoiseHistoryLoad(path_string.c_str());
        }
    }

    void add(std::string_view entry) const {
        if (entry.empty()) {
            return;
        }

        std::string owned_entry(entry);
        (void)linenoiseHistoryAdd(owned_entry.c_str());
        if (path_.has_value()) {
            const std::string path_string = path_->string();
            (void)linenoiseHistorySave(path_string.c_str());
        }
    }

  private:
    std::optional<std::filesystem::path> path_;
};

bool repl_can_use_linenoise() {
    return ::isatty(STDIN_FILENO) != 0 && ::isatty(STDOUT_FILENO) != 0;
}

repl_read_result read_line_with_linenoise(const char* prompt) {
    errno = 0;
    char* raw = linenoise(prompt);
    if (raw == nullptr) {
        if (errno == EAGAIN) {
            return {.status = repl_read_status::interrupted};
        }
        return {.status = repl_read_status::eof};
    }

    std::string line(raw);
    linenoiseFree(raw);
    return {.status = repl_read_status::line, .line = std::move(line)};
}
#endif

repl_read_result read_line_with_getline(const char* prompt) {
    std::cout << prompt;
    std::cout.flush();

    std::string line;
    if (!std::getline(std::cin, line)) {
        std::cout << '\n';
        return {.status = repl_read_status::eof};
    }
    return {.status = repl_read_status::line, .line = std::move(line)};
}

int run_repl(muslisp::env_ptr env) {
    std::string buffer;
#if defined(MUESLI_BT_HAVE_LINENOISE)
    const bool use_linenoise = repl_can_use_linenoise();
    const repl_history_store history_store;
#else
    const bool use_linenoise = false;
#endif

    const auto add_history_entry = [&](std::string_view entry) {
#if defined(MUESLI_BT_HAVE_LINENOISE)
        if (use_linenoise) {
            history_store.add(entry);
        }
#else
        (void)entry;
#endif
    };

    while (true) {
        const char* prompt = buffer.empty() ? kReplPrompt : kReplContinuationPrompt;
        const repl_read_result input =
#if defined(MUESLI_BT_HAVE_LINENOISE)
            use_linenoise ? read_line_with_linenoise(prompt) :
#endif
                            read_line_with_getline(prompt);

        if (input.status == repl_read_status::interrupted) {
            std::cout << '\n';
            buffer.clear();
            continue;
        }
        if (input.status == repl_read_status::eof) {
            break;
        }

        if (muslisp::repl_support::should_exit_repl(input.line, buffer.empty())) {
            break;
        }
        if (muslisp::repl_support::is_clear_command(input.line)) {
            buffer.clear();
            continue;
        }

        buffer += input.line;
        buffer.push_back('\n');

        try {
            std::vector<muslisp::value> exprs = muslisp::read_all(buffer);
            add_history_entry(muslisp::repl_support::normalise_history_entry(buffer));

            muslisp::gc_root_scope roots(muslisp::default_gc());
            for (muslisp::value& expr : exprs) {
                roots.add(&expr);
            }

            muslisp::value result = muslisp::make_nil();
            roots.add(&result);

            for (muslisp::value expr : exprs) {
                result = muslisp::eval(expr, env);
                std::cout << muslisp::print_value(result) << '\n';
                muslisp::default_gc().maybe_collect();
            }
            buffer.clear();
        } catch (const muslisp::parse_error& e) {
            if (e.incomplete()) {
                continue;
            }
            add_history_entry(muslisp::repl_support::normalise_history_entry(buffer));
            std::cerr << "parse error: " << e.what() << '\n';
            buffer.clear();
        } catch (const muslisp::lisp_error& e) {
            std::cerr << "error: " << e.what() << '\n';
            buffer.clear();
        }
    }

    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc > 1 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
            return print_usage();
        }
        if (argc > 1 && std::string(argv[1]) == "--model-service-start") {
            return run_model_service_start(argc, argv);
        }
        muslisp::env_ptr env = muslisp::create_global_env();
        if (argc > 1) {
            return run_script(argv[1], env);
        }
        return run_repl(env);
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << '\n';
        return 1;
    }
}

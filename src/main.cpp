#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "muslisp/error.hpp"
#include "muslisp/eval.hpp"
#include "muslisp/gc.hpp"
#include "muslisp/printer.hpp"
#include "muslisp/reader.hpp"

namespace {

std::string read_text_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw muslisp::lisp_error("failed to open file: " + path);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
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

int run_repl(muslisp::env_ptr env) {
    std::string buffer;
    while (true) {
        std::cout << (buffer.empty() ? "muslisp> " : "......> ");
        std::cout.flush();

        std::string line;
        if (!std::getline(std::cin, line)) {
            std::cout << '\n';
            break;
        }

        if (buffer.empty() && (line == ":q" || line == ":quit" || line == ":exit")) {
            break;
        }

        buffer += line;
        buffer.push_back('\n');

        try {
            std::vector<muslisp::value> exprs = muslisp::read_all(buffer);

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

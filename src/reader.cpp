#include "muslisp/reader.hpp"

#include <cstddef>
#include <charconv>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <string>

#include "muslisp/error.hpp"

namespace muslisp {
namespace {

class parser {
public:
    explicit parser(std::string_view source) : source_(source) {}

    std::vector<value> read_all_expressions() {
        std::vector<value> exprs;
        while (true) {
            skip_ws_and_comments();
            if (eof()) {
                break;
            }
            exprs.push_back(read_expr());
        }
        return exprs;
    }

private:
    [[nodiscard]] parse_error parse_error_at(const std::string& message, bool incomplete, std::size_t position) const {
        std::size_t line = 1;
        std::size_t column = 1;
        const std::size_t end = position < source_.size() ? position : source_.size();
        for (std::size_t i = 0; i < end; ++i) {
            if (source_[i] == '\n') {
                ++line;
                column = 1;
                continue;
            }
            ++column;
        }
        return parse_error("line " + std::to_string(line) + ", column " + std::to_string(column) + ": " + message, incomplete);
    }

    [[nodiscard]] parse_error parse_error_here(const std::string& message, bool incomplete) const {
        return parse_error_at(message, incomplete, pos_);
    }

    [[nodiscard]] bool eof() const {
        return pos_ >= source_.size();
    }

    [[nodiscard]] char peek() const {
        return source_[pos_];
    }

    [[nodiscard]] char get() {
        return source_[pos_++];
    }

    void skip_ws_and_comments() {
        while (!eof()) {
            const char c = peek();
            if (std::isspace(static_cast<unsigned char>(c))) {
                ++pos_;
                continue;
            }
            if (c == ';') {
                while (!eof() && peek() != '\n') {
                    ++pos_;
                }
                continue;
            }
            break;
        }
    }

    [[nodiscard]] value read_expr() {
        skip_ws_and_comments();
        if (eof()) {
            throw parse_error_here("unexpected end of input", true);
        }

        const char c = get();
        if (c == '(') {
            return read_list();
        }
        if (c == ')') {
            throw parse_error_here("unexpected ')'", false);
        }
        if (c == '\'') {
            value quoted = read_expr();
            return list_from_vector({make_symbol("quote"), quoted});
        }
        if (c == '`') {
            value qq = read_expr();
            return list_from_vector({make_symbol("quasiquote"), qq});
        }
        if (c == ',') {
            if (eof()) {
                throw parse_error_here("unexpected end of input after ','", true);
            }
            if (peek() == '@') {
                ++pos_;
                value splice = read_expr();
                return list_from_vector({make_symbol("unquote-splicing"), splice});
            }
            value unquoted = read_expr();
            return list_from_vector({make_symbol("unquote"), unquoted});
        }
        if (c == '"') {
            return read_string();
        }
        return read_atom(c);
    }

    [[nodiscard]] value read_list() {
        std::vector<value> items;
        while (true) {
            skip_ws_and_comments();
            if (eof()) {
                throw parse_error_here("unterminated list", true);
            }
            if (peek() == ')') {
                ++pos_;
                return list_from_vector(items);
            }
            items.push_back(read_expr());
        }
    }

    [[nodiscard]] value read_string() {
        std::string out;
        while (true) {
            if (eof()) {
                throw parse_error_here("unterminated string", true);
            }

            const char c = get();
            if (c == '"') {
                return make_string(out);
            }
            if (c == '\\') {
                if (eof()) {
                    throw parse_error_here("unterminated string escape", true);
                }
                const char escaped = get();
                switch (escaped) {
                    case 'n':
                        out.push_back('\n');
                        break;
                    case 't':
                        out.push_back('\t');
                        break;
                    case 'r':
                        out.push_back('\r');
                        break;
                    case '"':
                        out.push_back('"');
                        break;
                    case '\\':
                        out.push_back('\\');
                        break;
                    default:
                        throw parse_error_here("unknown string escape", false);
                }
                continue;
            }
            out.push_back(c);
        }
    }

    [[nodiscard]] static bool delimiter(char c) {
        return std::isspace(static_cast<unsigned char>(c)) || c == '(' || c == ')' || c == ';';
    }

    [[nodiscard]] value read_atom(char first_char) {
        std::string token(1, first_char);
        while (!eof() && !delimiter(peek())) {
            token.push_back(get());
        }

        if (token == "#t") {
            return make_boolean(true);
        }
        if (token == "#f") {
            return make_boolean(false);
        }
        if (token == "nil") {
            return make_nil();
        }

        const bool maybe_float = token.find('.') != std::string::npos || token.find('e') != std::string::npos ||
                                 token.find('E') != std::string::npos;

        if (!maybe_float) {
            std::int64_t integer = 0;
            const char* begin = token.data();
            const char* end = token.data() + token.size();
            const auto result = std::from_chars(begin, end, integer);
            if (result.ec == std::errc{} && result.ptr == end) {
                return make_integer(integer);
            }
        }

        if (maybe_float) {
            errno = 0;
            char* parse_end = nullptr;
            const double parsed = std::strtod(token.c_str(), &parse_end);
            if (parse_end == token.c_str() + token.size() && errno != ERANGE) {
                return make_float(parsed);
            }
            if (parse_end == token.c_str() + token.size() && errno == ERANGE) {
                return make_float(parsed);
            }
        }

        return make_symbol(token);
    }

    std::string_view source_;
    std::size_t pos_ = 0;
};

}  // namespace

value read_one(std::string_view source) {
    auto exprs = read_all(source);
    if (exprs.empty()) {
        throw parse_error("line 1, column 1: expected one expression, got none", false);
    }
    if (exprs.size() != 1) {
        throw parse_error("line 1, column 1: expected one expression, got multiple", false);
    }
    return exprs.front();
}

std::vector<value> read_all(std::string_view source) {
    parser p(source);
    return p.read_all_expressions();
}

}  // namespace muslisp

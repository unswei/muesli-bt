#pragma once

#include <stdexcept>
#include <string>

namespace muslisp {

class lisp_error : public std::runtime_error {
public:
    explicit lisp_error(const std::string& message) : std::runtime_error(message) {}
};

class parse_error : public lisp_error {
public:
    parse_error(const std::string& message, bool incomplete)
        : lisp_error(message), incomplete_(incomplete) {}

    [[nodiscard]] bool incomplete() const noexcept { return incomplete_; }

private:
    bool incomplete_;
};

class eval_error : public lisp_error {
public:
    explicit eval_error(const std::string& message) : lisp_error(message) {}
};

class type_error : public eval_error {
public:
    explicit type_error(const std::string& message) : eval_error(message) {}
};

class name_error : public eval_error {
public:
    explicit name_error(const std::string& message) : eval_error(message) {}
};

}  // namespace muslisp

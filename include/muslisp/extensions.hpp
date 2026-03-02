#pragma once

#include <memory>
#include <string>
#include <vector>

#include "muslisp/env.hpp"
#include "muslisp/value.hpp"

namespace bt {
class runtime_host;
}

namespace muslisp {

class registrar {
public:
    explicit registrar(env_ptr global_env);

    void register_builtin(const std::string& full_name,
                          primitive_fn fn,
                          const std::string& docstring = {},
                          const std::string& arity_or_signature = {});
    void register_value(const std::string& full_name, value bound_value, const std::string& docstring = {});

private:
    void ensure_registerable_name(const std::string& full_name, const char* where) const;

    env_ptr global_env_;
};

class extension {
public:
    virtual ~extension() = default;

    [[nodiscard]] virtual std::string name() const = 0;
    virtual void register_lisp(registrar& reg) const = 0;
    virtual void register_bt(bt::runtime_host& host) const {
        (void)host;
    }
};

class runtime_config {
public:
    void register_extension(std::unique_ptr<extension> ext);
    [[nodiscard]] const std::vector<std::unique_ptr<extension>>& extensions() const noexcept;

private:
    std::vector<std::unique_ptr<extension>> extensions_;
};

}  // namespace muslisp

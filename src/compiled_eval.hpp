#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "muslisp/gc.hpp"
#include "muslisp/value.hpp"

namespace muslisp {

enum class compiled_opcode {
    push_const,
    load_local,
    load_name,
    store_local,
    pop,
    jump,
    jump_if_false,
    jump_if_false_keep,
    jump_if_truthy_keep,
    call,
    tail_call,
    return_value,
};

struct compiled_instruction {
    compiled_opcode opcode = compiled_opcode::push_const;
    std::size_t index = 0;
    value literal = nullptr;
    std::string text;
};

struct compiled_closure {
    std::vector<compiled_instruction> code;
    std::size_t local_count = 0;
};

[[nodiscard]] const std::shared_ptr<compiled_closure>& closure_compiled(value v);
void set_closure_compiled(value v, std::shared_ptr<compiled_closure> compiled);
std::shared_ptr<compiled_closure> try_compile_closure(const std::vector<std::string>& params,
                                                      const std::vector<value>& body);
void compiled_closure_mark_children(const std::shared_ptr<compiled_closure>& compiled, gc& heap);
value execute_compiled_closure(value fn_value, const std::vector<value>& args);

}  // namespace muslisp

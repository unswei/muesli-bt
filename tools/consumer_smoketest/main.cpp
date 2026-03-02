#include "muslisp/eval.hpp"
#include "muslisp/value.hpp"

#include <stdexcept>

int main()
{
  muslisp::env_ptr env = muslisp::create_global_env();
  const muslisp::value out = muslisp::eval_source("(+ 1 2 3)", env);
  if (!muslisp::is_integer(out) || muslisp::integer_value(out) != 6)
  {
    throw std::runtime_error("unexpected eval result");
  }
  return 0;
}

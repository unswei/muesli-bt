#include "bt/instance.hpp"

namespace bt {

instance::instance(const definition* definition_ptr, std::size_t trace_capacity) : def(definition_ptr), trace(trace_capacity) {}

}  // namespace bt

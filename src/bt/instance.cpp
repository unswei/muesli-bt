#include "bt/instance.hpp"

namespace bt {

instance::instance(const definition* definition_ptr, std::size_t trace_capacity) : def(definition_ptr), trace(trace_capacity) {
    if (!def) {
        return;
    }

    const std::size_t node_count = def->nodes.size();
    memory.reserve(node_count);
    active_vla_jobs.reserve(node_count);
    halt_warning_emitted.reserve(node_count);
    node_stats.reserve(node_count);
    halt_stack.reserve(node_count);
}

}  // namespace bt

#include "fixtures/source_factory.hpp"

#include <sstream>
#include <stdexcept>

namespace muesli_bt::bench {
namespace {

void append_fixture_node(std::ostringstream& out, const fixture_node& node) {
    switch (node.kind) {
        case fixture_node_kind::seq:
            out << "(seq";
            break;
        case fixture_node_kind::sel:
            out << "(sel";
            break;
        case fixture_node_kind::reactive_sel:
            out << "(reactive-sel";
            break;
        case fixture_node_kind::cond:
            out << "(cond " << node.leaf_name << ')';
            return;
        case fixture_node_kind::act:
            out << "(act " << node.leaf_name << ')';
            return;
    }

    for (const fixture_node& child : node.children) {
        out << ' ';
        append_fixture_node(out, child);
    }
    out << ')';
}

}  // namespace

std::string make_fixture_dsl(const tree_fixture& fixture) {
    std::ostringstream out;
    append_fixture_node(out, fixture.root);
    return out.str();
}

}  // namespace muesli_bt::bench

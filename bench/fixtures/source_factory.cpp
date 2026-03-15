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

void append_btcpp_xml_node(std::ostringstream& out, const fixture_node& node, int indent) {
    const std::string padding(static_cast<std::size_t>(indent), ' ');
    auto append_container = [&](std::string_view tag) {
        out << padding << '<' << tag << ">\n";
        for (const fixture_node& child : node.children) {
            append_btcpp_xml_node(out, child, indent + 2);
        }
        out << padding << "</" << tag << ">\n";
    };

    switch (node.kind) {
        case fixture_node_kind::seq:
            append_container("Sequence");
            return;
        case fixture_node_kind::sel:
            append_container("Fallback");
            return;
        case fixture_node_kind::reactive_sel:
            append_container("ReactiveFallback");
            return;
        case fixture_node_kind::cond:
        case fixture_node_kind::act:
            out << padding << '<' << node.leaf_name << "/>\n";
            return;
    }
}

}  // namespace

std::string make_fixture_dsl(const tree_fixture& fixture) {
    std::ostringstream out;
    append_fixture_node(out, fixture.root);
    return out.str();
}

std::string make_fixture_btcpp_xml(const tree_fixture& fixture, std::string_view tree_id) {
    std::ostringstream out;
    out << "<root BTCPP_format=\"4\">\n";
    out << "  <BehaviorTree ID=\"" << tree_id << "\">\n";
    append_btcpp_xml_node(out, fixture.root, 4);
    out << "  </BehaviorTree>\n";
    out << "</root>\n";
    return out.str();
}

}  // namespace muesli_bt::bench

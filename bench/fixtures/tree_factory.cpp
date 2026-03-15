#include "fixtures/tree_factory.hpp"

#include <cmath>
#include <functional>
#include <stdexcept>
#include <utility>

namespace muesli_bt::bench {
namespace {

std::size_t levels_for_perfect_tree(std::size_t node_count) {
    if (node_count == 0) {
        throw std::invalid_argument("tree fixture: node count must be positive");
    }

    std::size_t levels = 0;
    std::size_t total = 0;
    while (total < node_count) {
        ++levels;
        total = (1ull << levels) - 1ull;
    }
    if (total != node_count) {
        throw std::invalid_argument("tree fixture: only perfect binary tree sizes are supported");
    }
    return levels;
}

fixture_node make_boolean_leaf(bool success, std::size_t leaf_index) {
    const bool use_condition = (leaf_index % 2u) == 0u;
    if (success) {
        return fixture_node{
            .kind = use_condition ? fixture_node_kind::cond : fixture_node_kind::act,
            .leaf_name = use_condition ? "cond_ok" : "act_ok",
            .children = {},
        };
    }

    return fixture_node{
        .kind = use_condition ? fixture_node_kind::cond : fixture_node_kind::act,
        .leaf_name = use_condition ? "cond_fail" : "act_fail",
        .children = {},
    };
}

fixture_node_kind static_composite_kind(tree_family family, std::size_t depth) {
    switch (family) {
        case tree_family::seq:
            return fixture_node_kind::seq;
        case tree_family::sel:
            return fixture_node_kind::sel;
        case tree_family::alt:
            return (depth % 2u) == 0u ? fixture_node_kind::sel : fixture_node_kind::seq;
        case tree_family::single_leaf:
        case tree_family::reactive:
            break;
    }
    throw std::invalid_argument("tree fixture: invalid static family");
}

fixture_node make_static_subtree(tree_family family, std::size_t levels, std::size_t depth, bool success, std::size_t& leaf_index) {
    if (levels == 1u) {
        return make_boolean_leaf(success, leaf_index++);
    }

    const fixture_node_kind kind = static_composite_kind(family, depth);
    fixture_node node{.kind = kind};
    if (kind == fixture_node_kind::seq) {
        node.children.push_back(make_static_subtree(family, levels - 1u, depth + 1u, true, leaf_index));
        node.children.push_back(make_static_subtree(family, levels - 1u, depth + 1u, success, leaf_index));
        return node;
    }

    node.children.push_back(make_static_subtree(family, levels - 1u, depth + 1u, false, leaf_index));
    node.children.push_back(make_static_subtree(family, levels - 1u, depth + 1u, success, leaf_index));
    return node;
}

fixture_node make_single_leaf_tree() {
    return fixture_node{
        .kind = fixture_node_kind::act,
        .leaf_name = "act_ok",
        .children = {},
    };
}

fixture_node make_perfect_seq_tree_from_leaf_provider(std::size_t levels,
                                                      const std::function<fixture_node(std::size_t, std::size_t)>& provider,
                                                      std::size_t& leaf_index,
                                                      std::size_t leaf_count) {
    if (levels == 1u) {
        return provider(leaf_index++, leaf_count);
    }

    fixture_node node{.kind = fixture_node_kind::seq};
    node.children.push_back(make_perfect_seq_tree_from_leaf_provider(levels - 1u, provider, leaf_index, leaf_count));
    node.children.push_back(make_perfect_seq_tree_from_leaf_provider(levels - 1u, provider, leaf_index, leaf_count));
    return node;
}

tree_fixture make_static_fixture(tree_family family, std::size_t node_count) {
    if (family == tree_family::single_leaf) {
        return tree_fixture{
            .root = make_single_leaf_tree(),
            .node_count = 1,
            .leaf_count = 1,
            .async_leaf_count = 0,
        };
    }

    const std::size_t levels = levels_for_perfect_tree(node_count);
    std::size_t leaf_index = 0;
    tree_fixture fixture;
    fixture.root = make_static_subtree(family, levels, 0u, true, leaf_index);
    fixture.node_count = node_count;
    fixture.leaf_count = (node_count + 1u) / 2u;
    fixture.async_leaf_count = 0;
    return fixture;
}

tree_fixture make_reactive_fixture(std::size_t node_count) {
    if ((node_count % 2u) == 0u || node_count < 3u) {
        throw std::invalid_argument("reactive fixture: tree size must be odd and at least 3");
    }

    const std::size_t branch_nodes = (node_count - 1u) / 2u;
    const std::size_t branch_levels = levels_for_perfect_tree(branch_nodes);
    const std::size_t branch_leaf_count = (branch_nodes + 1u) / 2u;

    std::size_t guard_leaf_index = 0;
    const auto guard_provider = [](std::size_t leaf_index, std::size_t leaf_count) {
        if (leaf_index == 0u) {
            return fixture_node{.kind = fixture_node_kind::cond, .leaf_name = "cond_emergency_stop", .children = {}};
        }
        if (leaf_index + 1u == leaf_count) {
            return fixture_node{.kind = fixture_node_kind::act, .leaf_name = "act_stop", .children = {}};
        }
        return make_boolean_leaf(true, leaf_index);
    };

    std::size_t work_leaf_index = 0;
    const auto work_provider = [](std::size_t leaf_index, std::size_t leaf_count) {
        if (leaf_index == 0u) {
            return fixture_node{.kind = fixture_node_kind::cond, .leaf_name = "cond_path_valid", .children = {}};
        }
        if (leaf_index + 1u == leaf_count) {
            return fixture_node{.kind = fixture_node_kind::act, .leaf_name = "act_follow_path_async", .children = {}};
        }
        return make_boolean_leaf(true, leaf_index);
    };

    fixture_node root{.kind = fixture_node_kind::reactive_sel};
    root.children.push_back(
        make_perfect_seq_tree_from_leaf_provider(branch_levels, guard_provider, guard_leaf_index, branch_leaf_count));
    root.children.push_back(
        make_perfect_seq_tree_from_leaf_provider(branch_levels, work_provider, work_leaf_index, branch_leaf_count));

    return tree_fixture{
        .root = std::move(root),
        .node_count = node_count,
        .leaf_count = (node_count + 1u) / 2u,
        .async_leaf_count = 1,
    };
}

}  // namespace

tree_fixture make_fixture(const scenario_definition& scenario) {
    switch (scenario.kind) {
        case benchmark_kind::single_leaf:
        case benchmark_kind::static_tick:
        case benchmark_kind::compile_lifecycle:
            return make_static_fixture(scenario.family, scenario.tree_size_nodes);
        case benchmark_kind::reactive_interrupt:
            return make_reactive_fixture(scenario.tree_size_nodes);
    }
    throw std::invalid_argument("tree fixture: unsupported scenario kind");
}

}  // namespace muesli_bt::bench

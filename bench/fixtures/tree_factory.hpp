#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "harness/scenario.hpp"

namespace muesli_bt::bench {

enum class fixture_node_kind {
    seq,
    sel,
    reactive_sel,
    cond,
    act
};

struct fixture_node {
    fixture_node_kind kind = fixture_node_kind::seq;
    std::string leaf_name;
    std::vector<fixture_node> children;
};

struct tree_fixture {
    fixture_node root;
    std::size_t node_count = 0;
    std::size_t leaf_count = 0;
    std::size_t async_leaf_count = 0;
};

tree_fixture make_fixture(const scenario_definition& scenario);

}  // namespace muesli_bt::bench

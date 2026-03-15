#pragma once

#include <string>
#include <string_view>

#include "fixtures/tree_factory.hpp"

namespace muesli_bt::bench {

std::string make_fixture_dsl(const tree_fixture& fixture);
std::string make_fixture_btcpp_xml(const tree_fixture& fixture, std::string_view tree_id = "MainTree");

}  // namespace muesli_bt::bench

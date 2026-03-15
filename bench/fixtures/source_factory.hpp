#pragma once

#include <string>

#include "fixtures/tree_factory.hpp"

namespace muesli_bt::bench {

std::string make_fixture_dsl(const tree_fixture& fixture);

}  // namespace muesli_bt::bench

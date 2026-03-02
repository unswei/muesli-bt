#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "muslisp/extensions.hpp"

namespace bt {
class runtime_host;
}

namespace webots {
class Robot;
}

namespace bt::integrations::webots {

void install_callbacks(runtime_host& host);

}  // namespace bt::integrations::webots

namespace muslisp::integrations::webots {

struct attach_options {
    std::int64_t steps_per_tick = 1;
    std::string demo = "obstacle";
    std::string obs_schema = "epuck.obstacle.obs.v1";
};

std::unique_ptr<extension> make_extension(::webots::Robot* robot, attach_options options = {});

}  // namespace muslisp::integrations::webots

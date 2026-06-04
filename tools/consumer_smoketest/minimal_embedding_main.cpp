#include "bt/compiler.hpp"
#include "bt/event_log.hpp"
#include "bt/registry.hpp"
#include "bt/runtime.hpp"
#include "bt/scheduler.hpp"
#include "bt/status.hpp"
#include "muslisp/reader.hpp"
#include "muslisp/value.hpp"

#include <stdexcept>

int main() {
    bt::definition tree = bt::compile_definition(muslisp::read_one(
        "(sel (seq (cond obstacle-clear) (act drive-forward)) (act safe-stop))"));
    bt::instance inst(&tree);
    bt::registry callbacks;
    bt::thread_pool_scheduler scheduler(0);
    bt::event_log events(16);
    bool obstacle_clear = true;
    double command = 0.0;

    callbacks.register_condition("obstacle-clear", [&](bt::tick_context&, std::span<const muslisp::value>) {
        return obstacle_clear;
    });
    callbacks.register_action("drive-forward", [&](bt::tick_context&, bt::node_id, bt::node_memory&, std::span<const muslisp::value>) {
        command = 1.0;
        return bt::status::success;
    });
    callbacks.register_action("safe-stop", [&](bt::tick_context&, bt::node_id, bt::node_memory&, std::span<const muslisp::value>) {
        command = 0.0;
        return bt::status::success;
    });

    bt::services services;
    services.sched = &scheduler;
    services.obs.events = &events;

    if (bt::tick(inst, callbacks, services) != bt::status::success || command != 1.0) {
        throw std::runtime_error("drive-forward branch did not run");
    }
    obstacle_clear = false;
    if (bt::tick(inst, callbacks, services) != bt::status::success || command != 0.0) {
        throw std::runtime_error("safe-stop branch did not run");
    }
    return 0;
}

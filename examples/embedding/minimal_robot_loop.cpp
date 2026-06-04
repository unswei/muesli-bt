#include "bt/compiler.hpp"
#include "bt/event_log.hpp"
#include "bt/registry.hpp"
#include "bt/runtime.hpp"
#include "bt/scheduler.hpp"
#include "bt/status.hpp"
#include "muslisp/reader.hpp"
#include "muslisp/value.hpp"

#include <iostream>
#include <stdexcept>

namespace {

bt::definition compile_minimal_tree() {
    const muslisp::value form = muslisp::read_one(
        "(sel"
        "  (seq"
        "    (cond obstacle-clear)"
        "    (act drive-forward))"
        "  (act safe-stop))");
    return bt::compile_definition(form);
}

}  // namespace

int main() {
    bt::definition tree = compile_minimal_tree();
    bt::instance inst(&tree);
    bt::registry callbacks;
    bt::thread_pool_scheduler scheduler(0);
    bt::event_log events(128);
    events.set_line_listener([](const std::string& line) {
        std::cout << line << '\n';
    });

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

    bt::status first = bt::tick(inst, callbacks, services);
    if (first != bt::status::success || command != 1.0) {
        throw std::runtime_error("expected drive-forward command");
    }

    obstacle_clear = false;
    bt::status second = bt::tick(inst, callbacks, services);
    if (second != bt::status::success || command != 0.0) {
        throw std::runtime_error("expected safe-stop command");
    }

    return 0;
}

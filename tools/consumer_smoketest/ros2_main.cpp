#include "muslisp/env_api.hpp"
#include "muslisp/eval.hpp"
#include "muslisp/value.hpp"
#include "ros2/extension.hpp"

#include <stdexcept>

int main() {
    muslisp::runtime_config config;
    config.register_extension(muslisp::integrations::ros2::make_extension());
    muslisp::env_ptr env = muslisp::create_global_env(std::move(config));

    (void)muslisp::eval_source("(env.attach \"ros2\")", env);
    (void)muslisp::eval_source(
        "(begin "
        "  (define cfg (map.make)) "
        "  (map.set! cfg 'control_hz 50) "
        "  (map.set! cfg 'obs_source \"odom\") "
        "  (map.set! cfg 'action_sink \"cmd_vel\") "
        "  (map.set! cfg 'reset_mode \"stub\") "
        "  (env.configure cfg))",
        env);

    const muslisp::value info = muslisp::eval_source("(env.info)", env);
    if (!muslisp::is_map(info)) {
        throw std::runtime_error("env.info did not return a map");
    }
    if (muslisp::string_value(muslisp::eval_source("(map.get (env.info) 'backend \"\")", env)) != "ros2") {
        throw std::runtime_error("unexpected ros2 backend id");
    }

    (void)muslisp::eval_source("(env.reset 7)", env);
    (void)muslisp::eval_source(
        "(begin "
        "  (define action (map.make)) "
        "  (define u (map.make)) "
        "  (map.set! action 'action_schema \"ros2.action.v1\") "
        "  (map.set! action 't_ms 1) "
        "  (map.set! u 'linear_x 0.1) "
        "  (map.set! u 'linear_y 0.0) "
        "  (map.set! u 'angular_z 0.2) "
        "  (map.set! action 'u u) "
        "  (env.act action))",
        env);
    (void)muslisp::eval_source("(env.step)", env);

    if (muslisp::string_value(muslisp::eval_source("(map.get (env.observe) 'obs_schema \"\")", env)) != "ros2.obs.v1") {
        throw std::runtime_error("unexpected ros2 observation schema");
    }
    if (muslisp::string_value(muslisp::eval_source("(map.get (map.get (env.observe) 'state (map.make)) 'state_schema \"\")", env)) !=
        "ros2.state.v1") {
        throw std::runtime_error("unexpected ros2 state schema");
    }

    muslisp::env_api_reset();
    return 0;
}

#include "muslisp/eval.hpp"
#include "muslisp/value.hpp"
#include "pybullet/extension.hpp"
#include "pybullet/racecar_demo.hpp"

#include <memory>
#include <stdexcept>

namespace {

class smoke_racecar_adapter final : public bt::racecar_sim_adapter {
public:
    [[nodiscard]] bt::racecar_state get_state() override {
        ++state_reads_;
        bt::racecar_state out;
        out.state_schema = "racecar_state.v1";
        out.state_vec = {0.0, 0.0, 0.0, 0.0};
        out.goal = {5.0, 0.0};
        out.rays = {10.0, 10.0, 10.0};
        out.t_ms = t_ms_;
        return out;
    }

    void apply_action(double steering, double throttle) override {
        (void)steering;
        (void)throttle;
        ++apply_calls_;
    }

    void step(std::int64_t steps) override {
        (void)steps;
        ++step_calls_;
        t_ms_ += 50;
    }

    void reset() override {
        t_ms_ = 0;
    }

    [[nodiscard]] std::int64_t apply_calls() const noexcept {
        return apply_calls_;
    }

    [[nodiscard]] std::int64_t state_reads() const noexcept {
        return state_reads_;
    }

    [[nodiscard]] std::int64_t step_calls() const noexcept {
        return step_calls_;
    }

private:
    std::int64_t t_ms_ = 0;
    std::int64_t apply_calls_ = 0;
    std::int64_t state_reads_ = 0;
    std::int64_t step_calls_ = 0;
};

}  // namespace

int main() {
    auto adapter = std::make_shared<smoke_racecar_adapter>();
    bt::set_racecar_sim_adapter(adapter);

    muslisp::runtime_config config;
    config.register_extension(muslisp::integrations::pybullet::make_extension());
    muslisp::env_ptr env = muslisp::create_global_env(std::move(config));

    (void)muslisp::eval_source("(env.attach \"pybullet\")", env);
    (void)muslisp::eval_source("(env.reset 1)", env);
    (void)muslisp::eval_source("(env.observe)", env);
    (void)muslisp::eval_source(
        "(begin "
        "  (define action (map.make)) "
        "  (map.set! action 'action_schema \"action.u.v1\") "
        "  (map.set! action 'u (list 0.0 0.0)) "
        "  (env.act action))",
        env);
    (void)muslisp::eval_source("(env.step)", env);
    (void)muslisp::eval_source(
        "(define tree (bt.compile '(seq (act constant-drive action 0.0 0.0) (act apply-action action) (succeed))))", env);
    (void)muslisp::eval_source("(define inst (bt.new-instance tree))", env);

    const muslisp::value out = muslisp::eval_source("(bt.tick inst)", env);
    if (!muslisp::is_symbol(out) || muslisp::symbol_name(out) != "success") {
        throw std::runtime_error("unexpected bt.tick result");
    }
    if (adapter->state_reads() < 1 || adapter->apply_calls() < 1 || adapter->step_calls() < 1) {
        throw std::runtime_error("pybullet adapter attach path was not exercised");
    }

    bt::clear_racecar_demo_state();
    return 0;
}

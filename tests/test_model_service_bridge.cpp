#include "bt/model_service.hpp"
#include "bt/runtime_host.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

class fake_vla_model_service_client final : public bt::model_service_client {
public:
    explicit fake_vla_model_service_client(
        std::string step_output =
            "{\"actions\":[{\"type\":\"joint_targets\",\"values\":[0.2,-0.1],\"dt_ms\":200}]}",
        bool fail_on_call = false)
        : step_output_(std::move(step_output)), fail_on_call_(fail_on_call) {}

    bt::model_service_response call(const bt::model_service_request& request) override {
        if (fail_on_call_) {
            std::cerr << "FAIL: replay mode unexpectedly contacted fake service for op "
                      << bt::model_service_operation_name(request.op) << '\n';
            std::exit(1);
        }
        ops.push_back(bt::model_service_operation_name(request.op));
        refs_seen += request.refs_json.size();
        bt::model_service_response response;
        response.id = request.id;
        response.metadata_json = "{\"service\":\"fake\"}";
        if (request.op == bt::model_service_operation::start) {
            response.status = bt::model_service_status::running;
            response.session_id = "sess-test";
            return response;
        }
        if (request.op == bt::model_service_operation::step) {
            response.status = bt::model_service_status::action_chunk;
            response.session_id = request.session_id;
            response.output_json = step_output_;
            return response;
        }
        if (request.op == bt::model_service_operation::cancel || request.op == bt::model_service_operation::close) {
            response.status = bt::model_service_status::success;
            response.session_id = request.session_id;
            return response;
        }
        response.status = bt::model_service_status::success;
        return response;
    }

    std::vector<std::string> ops;
    std::size_t refs_seen = 0;

private:
    std::string step_output_;
    bool fail_on_call_ = false;
};

bt::model_service_response validate_action_chunk_output(const std::string& output_json) {
    bt::model_service_request request;
    request.id = "validate-action-chunk";
    request.op = bt::model_service_operation::step;
    request.capability = "cap.vla.action_chunk.v1";
    request.deadline_ms = 100;

    bt::model_service_response response;
    response.id = request.id;
    response.status = bt::model_service_status::action_chunk;
    response.output_json = output_json;
    response.host_reached = true;

    bt::validate_model_service_response(request, response);
    return response;
}

bt::vla_request make_model_service_vla_request() {
    bt::vla_request vla;
    vla.capability = "cap.vla.action_chunk.v1";
    vla.task_id = "test-task";
    vla.instruction = "move a little";
    vla.deadline_ms = 1000;
    vla.model.name = "model-service";
    vla.model.version = "test";
    vla.observation.state = {0.0, 0.0};
    vla.observation.frame_id = "frame://camera1/123456789";
    vla.action_space.type = "continuous";
    vla.action_space.dims = 2;
    vla.action_space.bounds = {{-1.0, 1.0}, {-1.0, 1.0}};
    vla.constraints.max_abs_value = 1.0;
    vla.constraints.max_delta = 1.0;
    return vla;
}

bt::vla_poll wait_for_terminal_vla(bt::runtime_host& host, bt::vla_service::vla_job_id job) {
    bt::vla_poll poll;
    for (int i = 0; i < 100; ++i) {
        poll = host.vla_ref().poll(job);
        if (poll.status == bt::vla_job_status::done || poll.status == bt::vla_job_status::error ||
            poll.status == bt::vla_job_status::timeout || poll.status == bt::vla_job_status::cancelled) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return poll;
}

bt::vla_poll run_model_service_vla(std::string output_json) {
    bt::runtime_host host;
    auto fake = std::make_unique<fake_vla_model_service_client>(std::move(output_json));
    bt::model_service_config fake_cfg;
    host.set_model_service_client(fake_cfg, std::move(fake));

    const bt::vla_service::vla_job_id job = host.vla_ref().submit(make_model_service_vla_request());
    return wait_for_terminal_vla(host, job);
}

}  // namespace

int main() {
    bt::model_service_config cfg;
    cfg.endpoint = "ws://127.0.0.1:1/v1/ws";
    cfg.connect_timeout_ms = 10;
    cfg.request_timeout_ms = 10;

    auto client = bt::make_websocket_model_service_client(cfg);
    bt::model_service_request request;
    request.id = "req-unavailable";
    request.op = bt::model_service_operation::describe;

    const bt::model_service_response response = client->call(request);
    check(response.id == "req-unavailable", "response should preserve request id on unavailable result");
    check(response.status == bt::model_service_status::unavailable,
          "unreachable optional bridge should return unavailable");
    check(response.error_retryable, "unreachable optional bridge should be retryable");
    check(!response.host_reached, "unreachable optional bridge must not reach host execution");

    if (const char* endpoint = std::getenv("MUESLI_BT_MODEL_SERVICE_TEST_ENDPOINT")) {
        bt::model_service_config live_cfg;
        live_cfg.endpoint = endpoint;
        live_cfg.connect_timeout_ms = 1000;
        live_cfg.request_timeout_ms = 5000;
        auto live_client = bt::make_websocket_model_service_client(live_cfg);
        bt::model_service_request describe;
        describe.id = "req-describe";
        describe.op = bt::model_service_operation::describe;
        const bt::model_service_response live_response = live_client->call(describe);
        check(live_response.id == "req-describe", "live response should preserve request id");
        check(live_response.status == bt::model_service_status::success, "live describe should succeed");
        check(live_response.output_json.find("cap.vla.action_chunk.v1") != std::string::npos,
              "live describe should include VLA action chunk capability");
        const bt::model_service_compatibility_result compatibility =
            bt::check_model_service_compatibility(*live_client);
        check(compatibility.compatible, "live describe compatibility check should pass");
        check(compatibility.missing_capabilities.empty(), "live compatibility check should not report missing caps");
    }

    bt::runtime_host host;
    auto fake = std::make_unique<fake_vla_model_service_client>();
    fake_vla_model_service_client* fake_ptr = fake.get();
    bt::model_service_config fake_cfg;
    host.set_model_service_client(fake_cfg, std::move(fake));

    const bt::vla_service::vla_job_id job = host.vla_ref().submit(make_model_service_vla_request());
    bt::vla_poll poll = wait_for_terminal_vla(host, job);
    check(poll.status == bt::vla_job_status::done, "model-service VLA job should complete");
    check(poll.final.has_value(), "model-service VLA job should return final response");
    check(poll.final->status == bt::vla_status::ok, "model-service VLA final response should be ok");
    check(poll.final->action.u.size() == 2, "model-service VLA action should preserve action dimensions");
    check(poll.final->action.u[0] == 0.2, "model-service VLA action should parse first value");
    check(poll.final->action.u[1] == -0.1, "model-service VLA action should parse second value");
    check(fake_ptr->ops.size() >= 3, "model-service VLA backend should call start, step, and close");
    check(fake_ptr->ops[0] == "start", "model-service VLA backend should start a session first");
    check(fake_ptr->ops[1] == "step", "model-service VLA backend should step the session");
    check(fake_ptr->ops.back() == "close", "model-service VLA backend should close the session");
    check(fake_ptr->refs_seen >= 3, "model-service VLA calls should carry frame refs");
    check(!poll.final->model_service_request_hashes.empty(), "model-service VLA final should include request hashes");
    check(!poll.final->model_service_response_hashes.empty(), "model-service VLA final should include response hashes");
    check(!poll.final->frame_refs.empty(), "model-service VLA final should include frame refs");
    check(poll.final->frame_refs[0] == "frame://camera1/123456789",
          "model-service VLA final should preserve frame ref");

    const std::filesystem::path replay_dir =
        std::filesystem::temp_directory_path() /
        ("muesli-bt-vla-replay-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    {
        bt::runtime_host record_host;
        auto record_fake = std::make_unique<fake_vla_model_service_client>();
        bt::model_service_config record_cfg;
        record_cfg.replay_mode = "record";
        record_cfg.replay_cache_path = replay_dir.string();
        record_host.set_model_service_client(record_cfg, std::move(record_fake));
        const bt::vla_poll record_poll =
            wait_for_terminal_vla(record_host, record_host.vla_ref().submit(make_model_service_vla_request()));
        check(record_poll.status == bt::vla_job_status::done, "recorded VLA session should complete");
        check(record_poll.final.has_value(), "recorded VLA session should produce final response");
        check(!record_poll.final->model_service_request_hashes.empty(),
              "recorded VLA session should expose request hashes");
        check(!record_poll.final->model_service_response_hashes.empty(),
              "recorded VLA session should expose response hashes");
    }
    {
        bt::runtime_host replay_host;
        auto replay_fake = std::make_unique<fake_vla_model_service_client>(
            "{\"actions\":[{\"type\":\"joint_targets\",\"values\":[9.0,9.0],\"dt_ms\":200}]}", true);
        bt::model_service_config replay_cfg;
        replay_cfg.replay_mode = "replay";
        replay_cfg.replay_cache_path = replay_dir.string();
        replay_host.set_model_service_client(replay_cfg, std::move(replay_fake));
        const bt::vla_poll replay_poll =
            wait_for_terminal_vla(replay_host, replay_host.vla_ref().submit(make_model_service_vla_request()));
        check(replay_poll.status == bt::vla_job_status::done, "replayed VLA session should complete");
        check(replay_poll.final.has_value(), "replayed VLA session should produce final response");
        check(replay_poll.final->model_service_replay_cache_hit,
              "replayed VLA session should report model-service replay cache hit");
        check(replay_poll.final->action.u[0] == 0.2 && replay_poll.final->action.u[1] == -0.1,
              "replayed VLA session should use cached action output");
        const std::string records = replay_host.dump_vla_records();
        check(records.find("response_hashes") != std::string::npos,
              "VLA records should include model-service response hashes");
        check(records.find("frame://camera1/123456789") != std::string::npos,
              "VLA records should include frame refs");
    }
    std::filesystem::remove_all(replay_dir);

    const bt::model_service_response malformed =
        validate_action_chunk_output("{\"actions\":[{\"type\":\"joint_targets\",\"values\":[0.2,\"bad\"],\"dt_ms\":200}]}");
    check(malformed.status == bt::model_service_status::invalid_output, "malformed action values should be rejected");
    check(!malformed.host_reached, "malformed action values must not reach host execution");
    check(malformed.validation_reason_code == "model_service_action_values_invalid",
          "malformed action values should report a specific validation reason");

    const bt::model_service_response stale = validate_action_chunk_output(
        "{\"actions\":[{\"type\":\"joint_targets\",\"values\":[0.2,-0.1],\"dt_ms\":200}],\"stale\":true}");
    check(stale.status == bt::model_service_status::invalid_output, "stale action chunks should be rejected");
    check(!stale.host_reached, "stale action chunks must not reach host execution");
    check(stale.validation_reason_code == "model_service_stale_result",
          "stale action chunks should report a stale-result reason");

    const bt::model_service_response unsafe = validate_action_chunk_output(
        "{\"actions\":[{\"type\":\"joint_targets\",\"values\":[0.2,-0.1],\"dt_ms\":200}],\"unsafe\":true}");
    check(unsafe.status == bt::model_service_status::unsafe_output, "unsafe action chunks should be rejected");
    check(!unsafe.host_reached, "unsafe action chunks must not reach host execution");
    check(unsafe.validation_reason_code == "model_service_unsafe_output",
          "unsafe action chunks should report an unsafe-output reason");

    const bt::model_service_response late = validate_action_chunk_output(
        "{\"actions\":[{\"type\":\"joint_targets\",\"values\":[0.2,-0.1],\"dt_ms\":200}],\"late\": true}");
    check(late.status == bt::model_service_status::invalid_output, "late action chunks should be rejected");
    check(!late.host_reached, "late action chunks must not reach host execution");
    check(late.validation_reason_code == "model_service_late_result",
          "late action chunks should report a late-result reason");

    const bt::model_service_response policy = validate_action_chunk_output(
        "{\"actions\":[{\"type\":\"joint_targets\",\"values\":[0.2,-0.1],\"dt_ms\":200}],\"policy_violation\":true}");
    check(policy.status == bt::model_service_status::unsafe_output,
          "policy-violating action chunks should be rejected");
    check(!policy.host_reached, "policy-violating action chunks must not reach host execution");
    check(policy.validation_reason_code == "model_service_policy_violation",
          "policy-violating action chunks should report a policy reason");

    const bt::vla_poll invalid_poll =
        run_model_service_vla("{\"actions\":[{\"type\":\"joint_targets\",\"values\":[0.2,\"bad\"],\"dt_ms\":200}]}");
    check(invalid_poll.status == bt::vla_job_status::error,
          "malformed model-service VLA output should become an error job");
    check(invalid_poll.final.has_value(), "malformed model-service VLA output should produce a final response");
    check(invalid_poll.final->status == bt::vla_status::invalid,
          "malformed model-service VLA output should produce invalid final status");

    return 0;
}

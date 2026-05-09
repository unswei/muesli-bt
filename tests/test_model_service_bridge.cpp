#include "bt/model_service.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
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
    }
    return 0;
}

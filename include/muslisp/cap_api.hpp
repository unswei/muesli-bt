#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "bt/vla.hpp"
#include "muslisp/value.hpp"

namespace muslisp {

class cap_backend {
public:
    virtual ~cap_backend() = default;

    [[nodiscard]] virtual bt::capability_descriptor describe() const = 0;
    [[nodiscard]] virtual value call(value request_map) = 0;
};

void cap_api_reset();
void cap_api_register_backend(const std::string& capability, std::shared_ptr<cap_backend> backend);
std::vector<std::string> cap_api_registered_capabilities();
[[nodiscard]] std::optional<bt::capability_descriptor> cap_api_describe(const std::string& capability);
[[nodiscard]] bool cap_api_has_backend(const std::string& capability);
[[nodiscard]] value cap_api_call(const std::string& capability, value request_map);
[[nodiscard]] value cap_call(value request_map);

}  // namespace muslisp

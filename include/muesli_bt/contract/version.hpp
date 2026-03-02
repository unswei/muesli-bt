#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace muesli_bt::contract {

struct semantic_version {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;
};

inline constexpr semantic_version kRuntimeContractSemver{1, 0, 0};
inline constexpr std::string_view kRuntimeContractName = "runtime-contract";
inline constexpr std::string_view kRuntimeContractVersion = "1.0.0";
inline constexpr std::string_view kRuntimeContractId = "runtime-contract-v1.0.0";

[[nodiscard]] constexpr std::array<std::uint32_t, 3> runtime_contract_version_triplet() noexcept {
    return {kRuntimeContractSemver.major, kRuntimeContractSemver.minor, kRuntimeContractSemver.patch};
}

}  // namespace muesli_bt::contract

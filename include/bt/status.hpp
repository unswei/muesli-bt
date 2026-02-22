#pragma once

namespace bt {

enum class status {
    success,
    failure,
    running
};

const char* status_name(status st) noexcept;

}  // namespace bt

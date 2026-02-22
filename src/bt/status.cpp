#include "bt/status.hpp"

namespace bt {

const char* status_name(status st) noexcept {
    switch (st) {
        case status::success:
            return "success";
        case status::failure:
            return "failure";
        case status::running:
            return "running";
    }
    return "unknown";
}

}  // namespace bt

#pragma once

namespace muesli_bt::contract {

enum class node_outcome {
    success,
    failure,
    running
};

enum class async_lifecycle_state {
    idle,
    running,
    succeeded,
    failed,
    cancel_requested,
    cancelled,
    deadline_exceeded
};

}  // namespace muesli_bt::contract

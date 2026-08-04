# approach pose host validation

## what this is

`bt::approach_pose_validator` is an SDK-independent host policy for a planar
approach pose represented by the three-component vector `[x_m, y_m, yaw_rad]`.
It implements the existing `bt::vla_commit_validator` interface.

The validator checks the current ball context, robot stability, requested and
reported action frames, vector shape, finite values and configured pose bounds.
It does not send a walking command. A separate walking-target dispatcher
provides the audited hand-off to the host controller.

## when to use it

Use this validator when an asynchronous planner or model proposes an approach
pose and a BT `vla-wait` node must validate the proposal before writing it to
the blackboard.

Use it with `:acceptance_policy invocation_scoped`. That policy checks request
authority, generation, context and deadline before the host policy runs.

The validator does not require the Booster SDK. A Booster integration can
provide the latest perception and stability snapshot through the host-state
callback later.

## how it works

The runtime captures `:action_frame` when `vla-request` submits the job. A
backend reports the result frame in `vla_action::frame_id`. At commit time, the
validator accepts only when both frames equal the configured frame.

The host-state provider is called at commit time. It returns the current ball
context ID and current robot stability state. The checks run in this order:

1. non-empty captured, BT-current and host-current ball context IDs;
2. equality of all three ball context IDs;
3. stable robot state;
4. a continuous action with exactly three components;
5. matching requested, reported and configured frames; and
6. finite, inclusive bounds for `x_m`, `y_m` and `yaw_rad`.

Rejection uses stable reasons:

| condition | reason |
| --- | --- |
| Missing current ball context | `ball_stale` |
| Changed ball context | `context_changed` |
| Unstable robot | `robot_unstable` |
| Wrong action type or dimensions | `invalid_schema` |
| Wrong or missing frame | `invalid_frame` |
| Non-finite or out-of-bounds pose | `invalid_pose` |

After commit, `runtime_host::dispatch_walking_target` verifies that the
invocation was accepted, the ball context is still current, the frame matches,
the target exactly matches the accepted three-component action, and no target
has already been dispatched for that invocation. The method then calls the
registered `walking_target_dispatcher`.

Every dispatch attempt emits `walking_target_dispatch` in `mbt.evt.v1`. An
accepted event means the host callback reported that the target reached its
walking-controller boundary. A rejected event records the stable reason and
does not call the controller when the runtime check failed.

## api / syntax

The validation types are declared in `bt/approach_pose_validator.hpp`. The
walking hand-off types are declared in `bt/walking_target_dispatch.hpp`:

```cpp
struct approach_pose_bounds;
struct approach_pose_host_state;
struct approach_pose_validator_config;
using approach_pose_host_state_provider =
    std::function<approach_pose_host_state()>;

class approach_pose_validator final : public vla_commit_validator;

class walking_target_dispatcher;
struct walking_target;
struct walking_target_dispatch_context;
struct walking_target_dispatch_result;
```

Configure the associated BT request as a three-component action in the same
frame:

```lisp
(vla-request
  :name "ball-approach"
  :job_key approach-job
  :instruction "choose an approach pose"
  :state_key ball-state
  :dims 3
  :action_frame ball_context
  :deadline_ms 3500
  :acceptance_policy invocation_scoped
  :context_key ball-context-id)
```

A backend must preserve the frame reported by the proposer:

```cpp
response.action.type = bt::vla_action_type::continuous;
response.action.frame_id = "ball_context";
response.action.u = {-0.45, 0.08, 0.0};
```

## example

Register one validator with the runtime host before ticking the tree:

```cpp
bt::approach_pose_validator validator(
    bt::approach_pose_validator_config{
        .frame_id = "ball_context",
        .bounds = {
            .min_x_m = -1.0,
            .max_x_m = 0.0,
            .min_y_m = -0.5,
            .max_y_m = 0.5,
            .min_yaw_rad = -3.141593,
            .max_yaw_rad = 3.141593,
        },
    },
    [&robot_state] {
        const auto snapshot = robot_state.snapshot();
        return bt::approach_pose_host_state{
            .ball_context_id = snapshot.ball_context_id,
            .robot_stable = snapshot.stable,
        };
    });

host.set_vla_commit_validator(&validator);
```

Register the walking-controller hand-off separately:

```cpp
class booster_walking_dispatcher final : public bt::walking_target_dispatcher {
public:
    bt::walking_target_dispatch_result dispatch(
        const bt::walking_target_dispatch_context& context,
        const bt::walking_target& target) override {
        if (!walking_controller.accept_target(target)) {
            return {
                .accepted = false,
                .reason = "walking_controller_rejected",
            };
        }
        return {.accepted = true, .reason = {}};
    }
};

booster_walking_dispatcher walking_dispatcher;
host.set_walking_target_dispatcher(&walking_dispatcher);

const auto result = host.dispatch_walking_target(
    instance_handle,
    job_id,
    dispatching_node_id,
    bt::walking_target{
        .frame_id = "ball_context",
        .x_m = -0.45,
        .y_m = 0.08,
        .yaw_rad = 0.0,
    });
```

The host retains no ownership of either callback. Each callback must outlive
every call that can use it. Passing `nullptr` unregisters a callback.

## gotchas

- The vector is planar `[x_m, y_m, yaw_rad]`; it is not an XYZ position.
- Bounds are inclusive and must be finite and ordered.
- The host-state provider must return a coherent, thread-safe snapshot.
- A missing result frame is rejected when this validator is used.
- The accepted blackboard value remains the numeric vector. Configure the
  downstream walking adapter for the same frame and do not bypass validation.
- Keep the accepted invocation's job ID until the downstream action calls
  `dispatch_walking_target`. For a BT callback that reads the job key, configure
  `vla-wait` with `:clear_job #f`, then clear the key after dispatch.
- A callback must return `accepted` only after the walking-controller boundary
  has accepted the target. Acceptance does not mean the robot reached the pose.
- Successful walking-target dispatch is exactly once per invocation.
- Ball observation age and operating-area checks remain the responsibility of
  the host adapter. They can reject before dispatch even after this validator
  accepts the local pose contract.
- An exception from the host-state provider is caught by the runtime commit
  gate and becomes `host_policy_rejected`.

## see also

- [invocation-scoped authority](invocation-scoped-authority.md)
- [VLA BT nodes](vla-nodes.md)
- [VLA request/response schema](vla-request-response.md)
- [humanoid model-mediated approach experiment](../project/humanoid-model-mediated-approach-contract.md)

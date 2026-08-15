# booster studio bridge

## what this is

The Booster Studio bridge is an SDK-independent C++ integration between a
muesli-bt runtime and the local Booster host adapter. It carries live ball and
robot safety snapshots into the Behaviour Tree. It also carries an authorised
approach pose to the final walking-controller gate.

The bridge does not link against ROS 2 or BoosterOS. Those dependencies remain
inside the Booster Studio agent. The only external runtime evidence remains the
canonical `mbt.evt.v1` stream emitted by muesli-bt.

## when to use it

Use the bridge when the Behaviour Tree and Booster host run as separate local
processes. It is suitable for the virtual K1 trial and, after robot safety
review, the device trial.

Use the recording dispatcher for the frozen SDK-independent experiment matrix.
That matrix injects deterministic observations and cannot produce motion.

## how it works

`muesli_bt::booster::bridge_client` opens one Unix-domain socket connection per
operation. It sends one bounded JSON line and requires one complete response
line before the configured deadline. The client rejects malformed JSON,
duplicate object keys, non-finite numbers, oversized responses and incomplete
responses.

The native humanoid runner polls `snapshot()` before every BT tick. A valid
snapshot updates:

- current ball context and position;
- ball availability;
- robot stability; and
- software emergency state.

A transport or protocol fault fails closed by making the ball unavailable and
asserting the safe branch. The runner records `bridge-available` and
`bridge-fault-reason` as canonical blackboard writes. The
`bridge_walking_target_dispatcher` forwards job ID, generation, requesting and
authority nodes, dispatching node, job key, captured context, current context
and target. The Booster adapter then rechecks current context, observation age,
robot stability, operating bounds and exactly-once admission.

The existing runtime emits the synchronous outcome as
`walking_target_dispatch` in `mbt.evt.v1`. No bridge-specific log is created.

The Studio agent also owns a fail-closed native-process supervisor. Before
launch, it verifies a manifest-bound ELF64 x86-64 runner and all frozen BT,
configuration and evidence files. It waits for fresh ball and robot pose data,
normal stability and no emergency. A process fault removes the active target
and latches the software emergency state.

## api / syntax

Build and link the optional integration target:

```cmake
find_package(muesli_bt CONFIG REQUIRED)

add_executable(host main.cpp)
target_link_libraries(host PRIVATE
  muesli_bt::runtime
  muesli_bt::integration_booster)
```

Create a client and dispatcher:

```cpp
#include <booster/bridge_client.hpp>

muesli_bt::booster::bridge_client client({
    .socket_path = "/tmp/muesli-booster-bridge.sock",
    .timeout = std::chrono::milliseconds(100),
});

const auto snapshot = client.snapshot();
if (!snapshot.ok) {
  // Enter the host's fail-closed state.
}

muesli_bt::booster::bridge_walking_target_dispatcher dispatcher({
    .socket_path = "/tmp/muesli-booster-bridge.sock",
});
host.set_walking_target_dispatcher(&dispatcher);
```

The public operations are:

- `ping()` for protocol availability;
- `snapshot()` for the current perception and safety envelope; and
- `dispatch(context, target)` for synchronous walking-target admission.

The native experiment runner accepts:

```text
--booster-bridge-socket ABSOLUTE_PATH
--booster-bridge-timeout-ms POSITIVE_INTEGER
--physical-motion-enabled true|false
```

The runner requires the CLI motion flag to match the adapter snapshot. This
prevents an evidence manifest from claiming a different motion state from the
host process.

The generated payload and live-trial manifests use
`humanoid.booster_native_payload.v1` and `humanoid.booster_live_trial.v1`.
Their JSON Schemas are under `schemas/humanoid_booster/v1/`. The live manifest
binds the payload digest, exact runner command, canonical event hash and derived
overlay hash.

## example

Run the complete local bridge checks without ROS 2, BoosterOS or a simulator:

```bash
cmake --preset dev
cmake --build --preset dev --target \
  muesli_bt_booster_bridge_tests humanoid_model_mediated_trial
ctest --test-dir build/dev --output-on-failure \
  -R '^muesli_bt_booster_(bridge|bridge_runner|studio_adapter)$'
```

The runner smoke test starts the pure-Python adapter state, submits a real
invocation-scoped VLA request, accepts one host dispatch and checks the
canonical event. It never creates a Booster robot object or velocity publisher.

## gotchas

- Unix-domain sockets are unavailable on the Windows build. Client operations
  fail closed there.
- The socket is a local trust boundary. Keep its default owner-only file mode
  and use an absolute path.
- Client timeouts must be at most 60 seconds and responses must be at most
  16 KiB. The default timeout is 100 milliseconds.
- `motion_disabled`, stale robot pose and operating-area failures are reduced to
  `host_policy_rejected` at the public walking-dispatch API. Detailed adapter
  diagnostics stay inside the host process.
- One transient snapshot failure can revoke an active invocation. Restoring the
  socket does not restore authority to the old result.
- `MUESLI_BT_BUILD_INTEGRATION_BOOSTER=OFF` removes the integration target and
  live runner option. The SDK-independent experiment remains available.
- Passing local tests is not evidence of a virtual K1 or physical K1 run.
- The checked-in Studio target is `sim_x86_64` only. Real K1 targets need an
  independently built and verified payload plus the physical safety review.

## see also

- [humanoid model-mediated approach](../examples/humanoid-model-mediated-approach.md)
- [approach-pose validation](../bt/approach-pose-validation.md)
- [experiment contract](../project/humanoid-model-mediated-approach-contract.md)
- [consume as a package](../getting-started-consume.md)

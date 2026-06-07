# fixture bundles

Runtime-contract fixture bundles are stored as:

- `fixtures/<name>/config.json`
- `fixtures/<name>/seed.json`
- `fixtures/<name>/events.jsonl`
- `fixtures/<name>/expected_metrics.json`
- `fixtures/<name>/manifest.json`

Current bundles:

- `budget-warning-case`
- `deadline-cancel-case`
- `late-completion-drop-case`
- `determinism-replay-case`
- `async-cancel-before-start-case`
- `async-cancel-while-running-case`
- `async-cancel-after-timeout-case`
- `async-repeated-cancel-case`
- `async-late-completion-after-cancel-case`
- `ros2-observe-act-step-case`
- `ros2-invalid-action-fallback-case`
- `ros2-reset-unsupported-case`
- `ros2-deadline-fallback-case`
- `ros2-preemption-fallback-case`

The `async-*` bundles cover deterministic cancellation edges: cancellation before start, cancellation while running, cancellation after timeout, repeated cancellation, and late completion after cancellation.

The `ros2-*` bundles are pre-Linux surrogate fixtures. They define the canonical event-log expectations for ROS2-shaped scenarios before real ROS2 transport and rosbag-backed `L2` conformance exist.

The `ros2/nav2_capability_fake_server` bundle covers the optional ROS2/Nav2 `cap.navigation.v1` action-client evidence path. It runs Lisp `cap.call` requests against an in-process fake `NavigateToPose` action server and checks the report, manifest, and representative canonical capability-call logs.

The `dsl/generated-fragment-negative` fixtures cover generated Lisp BT fragments that must be rejected before execution. They are validated with `tools/validate_generated_bt_fragment.py` and cover unknown node types, unknown callbacks, unsupported capabilities, invalid budgets, malformed subtrees, and missing fallbacks around long-running async/model calls.

The `dsl/generated_guarded_recovery` fixtures cover the first accepted generated subtree evidence slice and the first agent proposal path. They include a deterministic blocked-path generator context, one accepted guarded recovery fragment, rejected unsafe/incomplete variants, proposal-envelope fixtures, manifests, canonical hashes, semantic diff and dry-run output, rollback evidence, a replay report, and a schema-valid `events.jsonl` lifecycle.

Update and verify using:

```bash
python3 tools/fixtures/update_fixture.py
python3 tools/fixtures/verify_fixture.py
python3 tools/validate_generated_bt_fragment.py fixtures/dsl/generated-fragment-negative
python3 tools/generate_guarded_recovery_subtree.py
python3 tests/check_generated_guarded_recovery.py
python3 tools/validate_generated_bt_fragment.py --export-manifests build/agent-manifests
python3 tools/run_nav2_capability_evidence.py --helper build/linux-ros2/nav2_capability_evidence --check
```

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

Update and verify using:

```bash
python3 tools/fixtures/update_fixture.py
python3 tools/fixtures/verify_fixture.py
```

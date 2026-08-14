# air-hockey host protocol

## overview

`airhockey.host.v1` is the local request/reply boundary between the future C++
air-hockey runner and a Python process that owns the simulation. It provides
the `info`, `configure`, `reset`, `observe`, `act`, `step` and `close`
operations required by the `env.*` lifecycle.

WP1 supplies a deterministic `fake_direct_launch` implementation for testing.
It has the same public protocol but does not import MuJoCo, load a policy or
model physics. A later work package will adapt the real ACRA
`DirectLaunchBackend` behind this boundary.

## when to use

Use this protocol when developing or testing the air-hockey paper example. Use
the fake host for lifecycle, context-transition, schema, replay and failure
tests that should run on any development machine.

Do not use fake-host trajectories as task evidence. Do not add air-hockey
fields to the generic `env.api.v1` or model-service capability contracts unless
they are independently generic.

## public surface

The authoritative schemas are:

- `schemas/air_hockey_host/v1/airhockey.host.request.v1.schema.json`
- `schemas/air_hockey_host/v1/airhockey.host.response.v1.schema.json`

Requests and replies are strict JSON objects. Unknown properties, duplicate
keys, non-finite numbers, invalid dimensions and values outside declared bounds
fail closed.

Each Unix-domain socket connection carries one request terminated by EOF or a
newline and one newline-terminated reply. Requests larger than 32 KiB are
rejected. Replies use canonical compact JSON with sorted keys so that a fixed
request sequence produces byte-identical output from a fresh fake host.

## protocol

Every request has this envelope:

```json
{
  "schema_version": "airhockey.host.request.v1",
  "request_id": "trial-01-step-0001",
  "op": "observe",
  "payload": {}
}
```

A successful response echoes the request identity:

```json
{
  "schema_version": "airhockey.host.response.v1",
  "request_id": "trial-01-step-0001",
  "op": "observe",
  "ok": true,
  "result": {
    "state": {}
  }
}
```

The abbreviated empty `state` above illustrates the envelope only; it is not a
schema-valid complete response.

The operation lifecycle is:

| Operation | Valid state | Effect |
| --- | --- | --- |
| `info` | host open | Reports versions, dimensions and timing constants. |
| `configure` | no active episode | Replaces provided scenario controls and reports the complete configuration. |
| `reset` | host open | Starts a new episode and defence context. |
| `observe` | after the first reset | Reports current public state without advancing. Final state remains observable. |
| `act` | active episode | Accepts one bounded two-value normalised mallet target for the next step. |
| `step` | active episode with an accepted action | Applies the action when unlocked, advances once and reports reward and public state. |
| `close` | any state | Closes the host; repeated close requests are harmless. |

Exactly one `act` is required before every active `step`. An action received
while `action_locked` is acknowledged but the host holds the current mallet
position for that step.

## inputs and outputs

The action uses `airhockey.normalised_mallet_target.v1`: exactly two finite
values in `[-1, 1]`.

The public observation uses `airhockey.public_observation.v1`: exactly 19
finite values in `[-1, 1]`, ordered as seven joint positions, seven joint
velocities, two current mallet-position values, two visible-puck values and one
puck-visibility flag. During blackout, the two visible-puck values are zero and
the flag is zero.

The state also includes the observation step, episode status and host-owned
`defence_context_id`. Blackout onset preserves the current context. A
false-to-true puck reacquisition or explicit track replacement increments the
track epoch. Reset starts a new episode epoch.

The wire contract has no general metadata object. Privileged puck state,
outcome labels, target labels, shot IDs and alias-family IDs cannot cross this
boundary. Evaluation tooling may record privileged scoring data in a separate
artefact, but the BT and provider must not receive it.

## errors and failure modes

Errors use `ok: false` and a stable code. Framing and schema failures use the
synthetic request identity `unknown` when a safe identity cannot be recovered.
The v1 codes cover malformed or oversized requests, schema and configuration
errors, invalid episode sequencing, a closed host and internal response
validation failure.

The processor validates generated replies before sending them. If a backend
attempts to return an undeclared or privileged field, the caller receives
`internal_error`; the invalid backend output is not forwarded.

The server creates its socket with mode `0600`, removes only an existing socket
at that exact path, and refuses to replace a regular file. A partial connection
produces an `invalid_json` reply where the client remains available, and does
not affect later connections.

## determinism

The fake host has no clock or random-number dependency. `reset.seed` is accepted
for lifecycle parity but does not alter the synthetic trajectory in WP1. Fresh
hosts given the same canonical request bytes return the same canonical response
bytes.

The deterministic property applies to the fake protocol host only. It does not
make scheduling, MuJoCo or a learned provider deterministic.

## example

Start the host from the repository root:

```bash
uv run --with 'jsonschema>=4.20,<5' \
  python examples/air_hockey_model_mediated_defence/host/run_fake_host.py \
  --socket /tmp/muesli-air-hockey.sock
```

The runnable source is under
`examples/air_hockey_model_mediated_defence/host/`. The C++ client added in WP2
will own normal request sequencing; the socket protocol is not intended as a
manual operator interface.

## testing

Run the MuJoCo-free gate locally:

```bash
uv run --with 'jsonschema>=4.20,<5' \
  python -m unittest discover \
  -s examples/air_hockey_model_mediated_defence/host/tests \
  -p 'test_*.py' -v
```

When CMake's selected Python interpreter can import `jsonschema`, the same suite
is registered as `muesli_bt_air_hockey_host` in CTest.

## related pages

- [env API](env-api.md)
- [model-service bridge](model-service-bridge.md)
- [writing a backend](writing-a-backend.md)

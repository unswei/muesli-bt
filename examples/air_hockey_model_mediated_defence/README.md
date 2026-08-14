# air-hockey model-mediated defence

This example is the staged integration for the muesli paper's dynamic
air-hockey demonstration. WP1 contains a versioned local protocol and a pure
fake host. It deliberately does not import MuJoCo or the ACRA air-hockey
package.

The fake host is useful for protocol, lifecycle and information-boundary tests.
It is not a physics simulator and its observations must not be used as task
evidence.

## run the contract tests

From the repository root:

```bash
uv run --with 'jsonschema>=4.20,<5' \
  python -m unittest discover \
  -s examples/air_hockey_model_mediated_defence/host/tests \
  -p 'test_*.py' -v
```

These tests require no GPU, MuJoCo installation or remote machine.

## run the fake host

```bash
uv run --with 'jsonschema>=4.20,<5' \
  python examples/air_hockey_model_mediated_defence/host/run_fake_host.py \
  --socket /tmp/muesli-air-hockey.sock
```

The process creates a mode `0600` Unix-domain socket and refuses to replace a
non-socket path. Each connection carries one bounded JSON request and one JSON
reply.

The authoritative request and response shapes are in
`schemas/air_hockey_host/v1/`. See the
[air-hockey host protocol](../../docs/integration/air-hockey-host-protocol.md)
for the lifecycle and field boundary.

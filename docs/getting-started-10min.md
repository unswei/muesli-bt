# first 10 minutes

This page is the fastest route from clone to a working runtime and a validated canonical event log.

It avoids ROS2, Webots, Isaac, VLA, and benchmark material.

## prerequisites

- CMake 3.20+
- Ninja
- C++20 compiler
- Python 3.11 for validation tooling

## build

```bash
git clone https://github.com/unswei/muesli-bt.git
cd muesli-bt
cmake --preset dev
cmake --build --preset dev -j
```

Expected result:

```text
[100%] Built target muslisp
```

The exact target list can differ by platform and enabled integrations.

## run the smallest BT

```bash
./build/dev/muslisp examples/bt/hello_bt.lisp
```

You should see approximately:

```text
<bt_def:1>
<bt_instance:1>
running
success
("...tick_end..." "...tick_ok...")
```

The first two object ids can vary. The important result is that the first tick returns `running`, the second tick returns `success`, and the final line contains canonical event-log records from the in-memory ring.

## validate a log

Create the Python environment once:

```bash
./scripts/setup-python-env.sh
```

Then run:

```bash
make verify-install
```

Expected output:

```text
verify-install passed: /path/to/muesli-bt/logs/verify-install.mbt.evt.v1.jsonl
```

Expected artefact:

- `logs/verify-install.mbt.evt.v1.jsonl`

That file is a canonical `mbt.evt.v1` JSONL event log validated against the checked-in schema.

## next

- [Hello BT](examples/hello-bt.md)
- [Lisp basics](examples/lisp-basics.md)
- [Runtime contract in practice](tutorials/runtime-contract-in-practice.md)
- [PyBullet e-puck-style goal seeking](examples/pybullet-epuck-goal.md)

# minimal real BT

Path: `examples/bt/minimal_real_bt.lisp`

This is the smallest BT example that still looks like task logic. It chooses between a guarded movement command and a fallback stop command using only built-in test callbacks and blackboard helpers.

## run it

```bash
./build/dev/muslisp examples/bt/minimal_real_bt.lisp
```

## expected output

You should see approximately:

```text
nil
nil
nil
<bt_def:1>
<bt_instance:1>
success
1
nil
success
0
("...tick_begin..." "...tick_end...")
```

Object ids and event details can vary. The important result is that the first tick writes command `1`, the reset clears the blackboard, and the second tick writes fallback command `0`.

## expected artefacts

- `/tmp/muesli-bt-minimal-real-bt.mbt.evt.v1.jsonl`

Validate it with:

```bash
python3 tools/validate_log.py \
  --schema schemas/event_log/v1/mbt.evt.v1.schema.json \
  /tmp/muesli-bt-minimal-real-bt.mbt.evt.v1.jsonl
```

Expected output:

```text
event log validation passed
```

## source

```lisp
--8<-- "examples/bt/minimal_real_bt.lisp"
```

## what this demonstrates

- `sel` as a fallback structure;
- `seq` as a guarded branch;
- blackboard input passed into `bt.tick`;
- a fallback action when the guard is absent;
- canonical event-log output.

## why this is a Behaviour Tree

The example is not only an `if` statement. It uses BT status propagation and a fallback branch:

- the guarded `seq` succeeds only if `obstacle-clear` is present;
- the enclosing `sel` chooses the first successful child;
- when the guard fails, the fallback action still runs and writes a safe command.

## next

- [runtime contract in practice](../tutorials/runtime-contract-in-practice.md)
- [Hello BT](hello-bt.md)
- [Behaviour Trees: introduction](../bt/intro.md)

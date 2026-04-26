# observability first

`muesli-bt` is not only a way to run Behaviour Trees. It is a way to make robot task execution inspectable.

## what gets logged

The canonical event stream is `mbt.evt.v1`.

Depending on the run mode, the stream can include:

- run start and runtime metadata;
- BT definition metadata;
- tick begin and tick end;
- node enter, exit, and status events;
- planner lifecycle events;
- async and VLA lifecycle events;
- host action validation outcomes;
- runtime outcome summaries such as timeout, fallback, and late-result-drop events.

Small excerpt:

```json
{"schema":"mbt.evt.v1","type":"tick_begin","tick":2}
{"schema":"mbt.evt.v1","type":"node_exit","tick":2,"data":{"node_id":1,"status":"success"}}
{"schema":"mbt.evt.v1","type":"tick_end","tick":2,"data":{"root_status":"success"}}
```

## why canonical event logs matter

Canonical logs make traces comparable across core-only runs, simulator runs, and ROS2-backed runs. A tool can read one external format instead of one format per backend.

## how replay works

Replay validation uses the event stream to check ordering, completed tick structure, runtime outcomes, and first divergence when two traces differ.

Start with:

- [canonical event log](../observability/event-log.md)
- [runtime contract v1](../contracts/runtime-contract-v1.md)
- [conformance levels](../contracts/conformance.md)

## how conformance levels use traces

`L0`, `L1`, and `L2` conformance separate core runtime semantics from simulator and ROS2 evidence. The point is to make heavier integrations useful without letting them redefine the runtime semantics.

## how this supports debugging

The trace records what was ticked, which nodes returned which statuses, when work timed out, and where fallback happened. This helps identify whether a failure came from BT structure, planner latency, host action validation, or backend IO.

## how this supports paper artefacts

Paper evidence should come from reproducible artefacts: commands, manifests, logs, replay reports, and benchmark bundles. The event stream is the shared evidence format that lets those artefacts be inspected after the run.

## where muesli-studio fits

[`muesli-studio`](https://github.com/unswei/muesli-studio) is a tooling consumer of runtime data. Its integration contract is documented in [muesli-studio integration](../contracts/muesli-studio-integration.md).

## see also

- [logging](../observability/logging.md)
- [tracing](../observability/tracing.md)
- [profiling](../observability/profiling.md)
- [tick audit record](../observability/tick-audit.md)
- [trace validation tools](../observability/event-log.md#validation)

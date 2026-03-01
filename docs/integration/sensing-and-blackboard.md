# Sensing And Blackboard

This page explains how sensor data reaches BT logic.

The [host](../terminology.md#host) (backend) owns sensor I/O. Lisp/BT code consumes normalised observation maps and writes decision outputs into the blackboard.

## End-To-End Pattern

1. backend reads sensors
2. backend publishes observation map via `env.observe`
3. Lisp/BT glue writes selected fields to blackboard keys
4. BT nodes consume those keys (`cond`, `act`, `plan-action`)

## Example: e-puck Proximity -> Blackboard

Webots e-puck logs show fields like:

- `obs.proximity` (8 proximity channels)
- `obs.min_obstacle`
- `obs.line_error` (line-follow world)
- `obs.obs_schema` (for example `epuck.line.obs.v1`)

Typical mapping logic in Lisp:

```lisp
(define obs (env.observe))
(bb.put "proximity" (map.get obs 'proximity (list)))
(bb.put "line_error" (map.get obs 'line_error 0.0))
(bb.put "min_obstacle" (map.get obs 'min_obstacle 1.0))
```

BT conditions then read those keys to pick branches.

## Example: Racecar Rays -> Blackboard

Racecar backend exposes state and rays each tick, then the run loop writes BT state inputs.

Useful fields include:

- pose/speed (`x`, `y`, `yaw`, `speed`)
- `rays` (raycast distances)
- goal coordinates
- `collision_imminent` flag

Planner and safety branches consume those values to decide action.

## Practical Guidance

- keep observation schemas explicit and versioned
- normalise units/ranges close to the backend boundary
- prefer small, stable blackboard keys over raw huge payloads
- validate required keys before BT tick and log missing-field errors clearly

## See Also

- [Integration Overview](overview.md)
- [Writing A Backend](writing-a-backend.md)
- [Blackboard](../bt/blackboard.md)

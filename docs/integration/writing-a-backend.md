# Writing A Backend

This guide shows the minimum work needed to connect a new simulator or robot.

A backend is your [host](../terminology.md#host) integration layer for `env.*`.

## Minimal Responsibilities

1. expose `env.info` with backend name + schema ids
2. implement `env.observe` and return a stable observation map
3. implement `env.act` with action validation/clamping
4. implement `env.step` for your timing model
5. provide a safe fallback action path
6. optionally implement `env.reset`

## Observation Path (`env.observe`)

- read sensors (camera, lidar, proprioception, state estimator)
- build observation map with explicit schema id/version
- include timestamp/tick metadata where useful
- keep shapes and units stable across runs

Example map shape:

```lisp
(map.make
  'schema_version "racecar.obs.v1"
  'state_vec (list x y yaw speed)
  'rays ray_distances
  't_ms now_ms)
```

## Action Path (`env.act`)

- define canonical action schema (`action.u.v1` or backend-specific id)
- validate dimension and numeric finiteness
- clamp to bounds before applying
- reject malformed actions and apply fallback safely

## Step Path (`env.step`)

Simulation backend:

- advance simulation by N physics steps per control tick

Real-time backend:

- run one cycle at control frequency and enforce pacing

Hardware backend:

- execute one sensor-read -> control-write iteration with watchdogs

## Reset Semantics (`env.reset`, optional)

If reset exists, document exactly what resets:

- world/robot state
- controller integrators/state estimators
- episode counters/logging context

## Safe Fallback

Always define a backend-side safe action (for example zero throttle + neutral steering).

Use it when:

- control branch returns failure and no action was published
- planner status is non-`ok`
- deadlines are missed
- action payload is invalid

## Backend Checklist

- [ ] `env.info` advertises schemas and capabilities
- [ ] `env.observe` returns stable schema
- [ ] `env.act` validates + clamps + reports errors
- [ ] `env.step` advances exactly one control increment
- [ ] fallback action is defined and tested
- [ ] log records include tick index, action, and budget stats
- [ ] one reproducible example script exists

## Minimal Template

```cpp
struct my_backend {
  obs_map observe();
  void act(const action_vec& u);
  void step();
  void reset(); // optional

  action_vec safe_action() const { return {0.0, 0.0}; }
};

// loop shape
while (running) {
  auto obs = backend.observe();
  blackboard_put("obs", obs);

  auto st = bt_tick(inst);
  auto action = blackboard_get_or("action", backend.safe_action());

  backend.act(action);
  backend.step();
}
```

## See Also

- [Integration Overview](overview.md)
- [Environment API (`env.*`)](env-api.md)
- [Sensing And Blackboard](sensing-and-blackboard.md)

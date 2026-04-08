# same-robot strict comparison track

## what this is

This page defines the stricter comparison track for the `v0.5.0` flagship.

The current PyBullet racecar and Webots e-puck pairing is useful for portability evidence.
It is not strong enough for the stricter claim that the decision logic should be almost identical under the same circumstances.

That stricter claim needs the same robot embodiment on both sides.

## when to use it

Use this page when you:

- decide whether a comparison should support a strict same-robot claim or only a portability claim
- plan the next PyBullet implementation after the current racecar flagship path
- review whether a proposed PyBullet robot is close enough to the Webots e-puck to justify stronger behavioural comparisons

## how it works

### two comparison tracks

The flagship now needs two clearly separated tracks.

Portability track:

- Webots e-puck
- PyBullet racecar
- same BT semantics
- same task class
- same shared blackboard and command contract
- weaker expected evidence: safe success and recognisable branch logic

Strict comparison track:

- Webots e-puck
- PyBullet e-puck-style differential robot
- same BT semantics
- same robot embodiment class
- stronger expected evidence: near-identical branch families and similar task completion behaviour

The current racecar path stays useful, but it should not be used to argue for almost identical decisions under the same circumstances.

The repository now includes an initial strict-track surrogate at `examples/pybullet_epuck_goal/`.
That path is still a surrogate rather than a full robot clone, but it keeps the embodiment class and command surface much closer to the Webots e-puck family.
The checked-in clutter layout for that path now also reaches the goal, so the strict track can use a successful cluttered-goal run rather than only a clean-path smoke test.

### source of truth

The Webots e-puck should be the source of truth for the strict track.

Reference points visible in the Webots `E-puck.proto`:

- wheel centre offset: `0.026 m` from body centre on each side
- wheel radius: `0.02 m`
- eight perimeter proximity sensors around the body
- compact circular body on the order of `0.07 m` diameter

For the strict PyBullet path, reproduce the embodiment numerically.
Do not treat the current racecar steering/throttle model as the strict baseline.

### implementation shape

The cleanest implementation is:

1. add a second PyBullet demo path for an e-puck-style differential robot
2. keep the existing racecar path intact as a separate portability/demo path
3. reuse the same shared flagship BT and shared planner model contract
4. use wheel-speed or differential-drive actuation on the strict path rather than steering/throttle

Current checked-in path:

- `examples/pybullet_epuck_goal/`
- shared BT source at `examples/pybullet_epuck_goal/bt/flagship_entry.lisp`
- differential-drive mapping in `examples/pybullet_epuck_goal/run_demo.py`

Recommended PyBullet strict-track shape:

- body: simple cylinder or capsule surrogate with e-puck-like footprint
- wheels: differential-drive left and right wheel actuation
- observation mapping:
  - pose `x`, `y`, `yaw`
  - forward speed
  - eight proximity-style or ray-derived obstacle channels mapped into the same shared `obstacle_front`
- action mapping:
  - shared `[linear_x, angular_z]`
  - converted to left/right wheel velocities through the same family of mapping already used on the Webots side

### what not to do

- Do not import or copy Webots licensed assets into the repo just to get the same shape.
- Do not widen the strict comparison around the racecar just because it already exists.
- Do not try to compare Ackermann steering against differential drive as though they were the same physical robot.

## api / syntax

Recommended implementation slices:

- `integrations/pybullet/`
  - add a new differential-drive adapter beside the current racecar adapter
- `examples/`
  - add a new PyBullet e-puck-style flagship entrypoint instead of mutating `pybullet_racecar/` into two incompatible meanings
- `docs/`
  - keep the strict comparison protocol separate from the portability protocol

Recommended first constants for the PyBullet strict track:

- wheel radius: `0.02`
- half track: `0.026`
- max wheel speed aligned to the Webots e-puck family

These should be validated against behaviour, but they are already close enough to support an initial same-robot surrogate.

The first checked-in strict-track protocol now lives here:

- [same-robot strict comparison protocol](same-robot-strict-comparison-protocol.md)

## example

Good strict-track comparison:

- Webots e-puck cluttered goal
- PyBullet e-puck-style differential robot in a geometrically similar cluttered goal arena
- same shared BT
- same shared thresholds
- same branch meanings

Weak strict-track comparison:

- Webots e-puck cluttered goal
- PyBullet racecar
- same BT

That second pair is still useful, but only for portability.

## gotchas

- “Same physical robot” in this project should mean same embodiment class and kinematics, not just same task label.
- Exact trajectory identity is still not required, even on the strict track.
- The stricter claim is about decision logic under similar embodiment and circumstances, not about pixel-perfect replay across simulators.

## see also

- [cross-transport flagship for v0.5](cross-transport-flagship.md)
- [cross-transport comparison protocol](cross-transport-comparison-protocol.md)
- [cross-transport shared contract](cross-transport-shared-contract.md)

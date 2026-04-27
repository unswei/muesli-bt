# same-robot strict comparison protocol

## what this is

This page defines the first concrete protocol for the stricter same-robot comparison track.

It pairs:

- the Webots e-puck flagship
- the PyBullet e-puck-style differential-drive surrogate

Unlike the portability protocol, this track is meant to support a stronger claim about branch logic and task progress under a closely matched embodiment class.

## when to use it

Use this page when you want to:

- compare the Webots e-puck flagship against the PyBullet e-puck-style surrogate
- collect a stricter same-robot evidence pair than the racecar portability lane
- check whether the shared flagship BT remains behaviourally similar when the robot embodiment class is held close

Do not use this page for the PyBullet racecar path.

## how it works

The current strict-track protocol treats the Webots e-puck as the reference run.

The PyBullet surrogate should:

- run the same flagship BT branch order
- consume the same shared flagship command surface
- reach the goal as well
- show similar branch families and progress trends

The checked-in protocol artefact is:

- `examples/flagship_wheeled/configs/same_robot_strict_protocol.json`

The current PyBullet strict-track runner is:

- `examples/pybullet_epuck_goal/run_demo.py`

The current Webots strict-track runner is:

- `examples/webots_epuck_goal/lisp/flagship_entry.lisp`

## api / syntax

### collection commands

PyBullet e-puck-style surrogate:

```bash
PYTHONPATH=build/dev/python \
  .venv-py311/bin/python examples/pybullet_epuck_goal/run_demo.py --headless
```

Optional clutter layout:

```bash
PYTHONPATH=build/dev/python \
  .venv-py311/bin/python examples/pybullet_epuck_goal/run_demo.py --headless --with-default-obstacles
```

Webots e-puck:

```bash
MUESLI_BT_WEBOTS_LISP_ENTRY=lisp/flagship_entry.lisp \
  "$WEBOTS_HOME/webots" --batch --mode=fast --stdout --stderr \
  examples/webots_epuck_goal/worlds/epuck_goal_cluttered.wbt
```

### normalise and compare

```bash
python3 examples/flagship_wheeled/tools/normalise_run.py \
  --backend webots \
  --output examples/flagship_wheeled/out/webots_epuck_strict.json \
  examples/webots_epuck_goal/logs/flagship_goal.jsonl

python3 examples/flagship_wheeled/tools/normalise_run.py \
  --backend pybullet \
  --output examples/flagship_wheeled/out/pybullet_epuck_strict.json \
  examples/pybullet_epuck_goal/logs/<run_id>.jsonl

python3 examples/flagship_wheeled/tools/compare_runs.py \
  --max-ticks 320 \
  examples/flagship_wheeled/out/webots_epuck_strict.json \
  examples/flagship_wheeled/out/pybullet_epuck_strict.json
```

## example

For the current strict-track protocol, a good result looks like this:

- both runs finish with `goal_reached = true`
- both runs finish with `status = success`
- branch-family overlap is high
- progress-ratio delta stays small
- aligned shared-action differences stay bounded

The exact trajectories do not need to be identical.

## gotchas

- The current PyBullet strict-track path is still a surrogate, not a literal Webots asset clone.
- The default PyBullet scene is a clean success path; the optional clutter flag is useful for manual inspection but is not yet the default evidence lane.
- Do not mix this page with the racecar portability protocol.
- Keep the shared flagship BT unchanged between the two runs.

## see also

- [same-robot strict comparison track](same-robot-strict-comparison.md)
- [cross-transport comparison protocol](cross-transport-comparison-protocol.md)
- [PyBullet: e-puck-style Goal Seeking](../examples/pybullet-epuck-goal.md)
- [Webots: e-puck Goal Seeking](../examples/webots-epuck-goal-seeking.md)

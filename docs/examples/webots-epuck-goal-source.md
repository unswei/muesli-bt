# Webots e-puck Goal: Full Source

## `bt_goal_seek.lisp`

```lisp
--8<-- "examples/webots_epuck_goal/lisp/bt_goal_seek.lisp"
```

## `flagship_entry.lisp`

```lisp
--8<-- "examples/webots_epuck_goal/lisp/flagship_entry.lisp"
```

## `main.lisp`

```lisp
--8<-- "examples/webots_epuck_goal/lisp/main.lisp"
```

## notes

- `bt_goal_seek.lisp` is the earlier reusable goal BT for the Webots example
- `flagship_entry.lisp` is the shared-contract wrapper used for the cross-transport flagship path
- `main.lisp` is the legacy entrypoint that still shows the original Webots-side shaping directly

## see also

- [Webots: e-puck Goal Seeking](webots-epuck-goal-seeking.md)
- [Webots: From Educational Goal BT To Reusable Wrapper](webots-epuck-goal-transition.md)

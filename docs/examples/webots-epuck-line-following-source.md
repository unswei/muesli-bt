# Webots e-puck Line Following: Full Source

## `main.lisp`

```lisp
--8<-- "examples/webots_epuck_line/lisp/main.lisp"
```

## `bt_line_follow.lisp`

```lisp
--8<-- "examples/webots_epuck_line/lisp/bt_line_follow.lisp"
```

## Walkthrough

- `main.lisp` wires environment loop configuration, logging path, and BT startup.
- `bt_line_follow.lisp` defines the tree structure for reacquire/follow transitions.
- DOT export from `main.lisp` is used by the docs and local debug flow.

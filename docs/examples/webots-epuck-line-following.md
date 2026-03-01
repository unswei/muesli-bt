# Webots: e-puck Line Following

## What It Demonstrates

- line reacquisition and line-follow branch logic in BT language
- planner-assisted follow branch with confidence/top-k diagnostics
- observation mapping (`line_error`, `ground`, `proximity`) into behaviour decisions

## How To Run

Detailed setup/build steps:

- [example README (Webots e-puck line)](https://github.com/unswei/muesli-bt/blob/main/examples/webots_epuck_line/README.md)

Direct run (after building the Webots controller target):

```bash
"$WEBOTS_HOME/webots" --batch --mode=fast --stdout --stderr \
  examples/webots_epuck_line/worlds/epuck_line_follow.wbt
```

## Logs And Plots

- log file: `examples/webots_epuck_line/logs/line.jsonl`
- planner/signal plots:

```bash
python3 examples/_tools/plot_planner_root.py \
  examples/webots_epuck_line/logs/line.jsonl \
  --every 40 --k 5 \
  --out_dir examples/webots_epuck_line/out
```

## Key BT Files

- `examples/webots_epuck_line/lisp/main.lisp`
- `examples/webots_epuck_line/lisp/bt_line_follow.lisp`

## BT Source (Inline)

```lisp
--8<-- "examples/webots_epuck_line/lisp/bt_line_follow.lisp"
```

Full source and walkthrough:

- [Webots e-puck line full source page](webots-epuck-line-following-source.md)

## Render BT DOT

`main.lisp` exports `out/tree.dot` at startup.

```bash
dot -Tsvg examples/webots_epuck_line/out/tree.dot \
  -o examples/webots_epuck_line/out/tree.svg
```

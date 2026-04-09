# Example: muslisp Basics

Path: `examples/repl_scripts/lisp-basics.lisp`

This is the best first runnable example if you are new to Lisp syntax.
Use it before [Example: Hello BT](hello-bt.md), because it shows the list-shaped syntax and symbol handling without introducing BT semantics yet.

Run via:

```bash
./build/dev/muslisp examples/repl_scripts/lisp-basics.lisp
```

This page does not cover BT syntax.
Its job is to make the first BT examples easier to read.

```lisp
--8<-- "examples/repl_scripts/lisp-basics.lisp"
```

## What To Notice

- `define` binds names to values
- arithmetic still uses the same list syntax as every other form
- quoted symbols stay as data instead of being evaluated
- lists are ordinary values, not a special BT-only syntax

## What This Shows

- `define` for values
- arithmetic with mixed numeric types
- comparison returning booleans
- quoting symbols
- list construction

Read next:

- [Brief Lisp Introduction](../lisp-basics.md)
- [Example: Hello BT](hello-bt.md)

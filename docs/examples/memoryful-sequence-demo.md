# Example: Memoryful Sequence Demo

This script contrasts memoryless `seq` with `mem-seq` using the same long-running child pattern.

Script path:

- `examples/repl_scripts/memoryful-sequence-demo.lisp`

Run:

```bash
./build/dev/muslisp examples/repl_scripts/memoryful-sequence-demo.lisp
```

## Source

```lisp
--8<-- "examples/repl_scripts/memoryful-sequence-demo.lisp"
```

## What To Look For

- `seq` keeps restarting from child 0 and stays `running`
- `mem-seq` resumes from the stored child index and reaches `success`
- traces make the different child re-entry patterns explicit

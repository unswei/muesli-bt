# Example: Memoryful Sequence Demo

This script contrasts memoryless `seq` with `mem-seq` using the same long-running child pattern.

Script path:

- `examples/repl_scripts/memoryful-sequence-demo.lisp`

## run it

```bash
./build/dev/muslisp examples/repl_scripts/memoryful-sequence-demo.lisp
```

## expected output

You should see approximately:

```text
memoryful-sequence-demo
(seq-t1 running)
(seq-t2 running)
(seq-t3 running)
(seq-t4 success)
(mem-t1 running)
(mem-t2 running)
(mem-t3 success)
(seq-events ("...tick_end..." ...))
(mem-events ("...tick_end..." ...))
nil
```

The event records include run ids and timings, so they will not match byte for byte.

## expected artefacts

This example prints event excerpts from memory and does not write files.

## source

```lisp
--8<-- "examples/repl_scripts/memoryful-sequence-demo.lisp"
```

## what this demonstrates

- `seq` keeps restarting from child 0 and stays `running`
- `mem-seq` resumes from the stored child index and reaches `success`
- traces make the different child re-entry patterns explicit

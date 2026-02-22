# Example: muslisp Basics

```lisp
(begin
  (define x 10)
  (define y 2.5)
  (print (+ x y))
  (print (< x 12))
  (print (list 'a 'b 'c))
  (+ x 1))
```

What this shows:

- `define` for values
- arithmetic with mixed numeric types
- comparison returning booleans
- quoting symbols
- list construction

Run via:

```bash
./build/dev/muslisp examples/repl_scripts/lisp-basics.lisp
```

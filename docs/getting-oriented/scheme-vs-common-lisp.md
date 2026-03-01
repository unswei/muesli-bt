# Scheme vs Common Lisp

muesli-bt mostly follows Scheme conventions.

If you are coming from Common Lisp, this runtime will feel Lisp-familiar but intentionally smaller and more constrained.

## What To Expect

- lexical scoping and closures
- Scheme-style special forms (`define`, `lambda`, `let`, `cond`, quote/quasiquote forms)
- prefix function calls
- small runtime surface oriented toward embedded control tasks

## What Is Intentionally Omitted

- full Common Lisp package system and macro ecosystem
- broad implementation-dependent CL libraries
- large dynamic object systems

The design goal is predictable behaviour and straightforward embedding, not language-complete CL compatibility.

## What Is Added For Robotics

The [host](../terminology.md#host) (backend) exposes robotics-specific built-ins and services:

- BT language forms (`bt`, `defbt`) and BT runtime built-ins
- `env.*` backend capability interface
- planner and VLA capability APIs

## Quick Syntax Contrast

```lisp
; Scheme-style conditional (used in muesli-bt)
(if (> x 0) 'pos 'non-pos)

; muesli-bt BT language form (purpose-built DSL)
(defbt patrol
  (sel
    (seq (cond target-visible) (act approach-target))
    (act search-target)))
```

## See Also

- [Language Syntax](../language/syntax.md)
- [Language Semantics](../language/semantics.md)
- [BT Syntax](../bt/syntax.md)

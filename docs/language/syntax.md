# Language Syntax

This page documents the concrete syntax accepted by the current reader.

For exact signatures, argument behaviour, and error details, use the [Language Reference Index](reference/index.md).

## Literals

### Integers

```lisp
0
42
-42
```

### Floats

```lisp
3.14
1e-3
2.
```

### Booleans

```lisp
#t
#f
```

### Nil

```lisp
nil
```

### Strings

```lisp
"hello"
"line\\nnext"
"quote: \"ok\""
```

Supported escapes:

- `\\n`, `\\t`, `\\r`
- `\\"`, `\\\\`

## Symbols

Anything not parsed as another literal token is a symbol:

```lisp
x
target-visible
bt.compile
```

## Lists

Lists are parenthesised forms:

```lisp
(define x 10)
(+ 1 2 3)
(list 1 2 3)
```

## Comments

A semicolon starts a line comment:

```lisp
(+ 1 2) ; this is ignored
```

## Quote And Quasiquote Syntax

Quote sugar expands to `(quote ...)`:

```lisp
'x
'(1 2 3)
```

Backquote expands to `(quasiquote ...)`:

```lisp
`x
`(a ,x ,@xs)
```

Unquote forms:

```lisp
,x
,@xs
```

Equivalent explicit forms:

```lisp
(quote x)
(quasiquote (a (unquote x) (unquote-splicing xs)))
```

## Deliberate Omissions

- no rationals (`3/7`)
- no complex numbers
- no hex or exactness prefixes
- no mutation forms (`set!`, `set-car!`, `set-cdr!`)

## See Also

- [Language Semantics](semantics.md)
- [Built-ins Overview](builtins.md)
- [Language Reference Index](reference/index.md)

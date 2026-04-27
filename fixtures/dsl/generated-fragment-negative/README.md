# generated fragment negative fixtures

These fixtures are rejected generated BT fragments for the Lisp DSL defensibility baseline.

They are intentionally not executable robot logic. Each `fragment.lisp` file is treated as untrusted generated data and checked by `tools/validate_generated_bt_fragment.py` before it could be compiled or installed.

The fixtures cover:

- unknown node type;
- unknown host callback;
- unsupported host/model capability;
- invalid budget;
- malformed subtree shape;
- missing fallback around a long-running async/model call.

Run:

```bash
python3 tools/validate_generated_bt_fragment.py fixtures/dsl/generated-fragment-negative
```

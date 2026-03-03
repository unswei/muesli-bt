PYTHON ?= $(CURDIR)/.venv-py311/bin/python
MODE ?= bt_planner
ARGS ?=
VERIFY_LOG ?= $(CURDIR)/logs/verify-install.mbt.evt.v1.jsonl
VERIFY_SCHEMA ?= $(CURDIR)/schemas/event_log/v1/mbt.evt.v1.schema.json

.PHONY: demo-setup demo-run verify-install

demo-setup:
	./scripts/demo-setup.sh "$(PYTHON)"

demo-run:
	@if [ ! -x "$(PYTHON)" ]; then \
		echo "error: $(PYTHON) not found. Run 'make demo-setup' first."; \
		exit 1; \
	fi
	PYTHONPATH="$(CURDIR)/build/dev/python:$$PYTHONPATH" \
	"$(PYTHON)" examples/pybullet_racecar/run_demo.py --mode "$(MODE)" $(ARGS)

verify-install:
	@if [ ! -x "$(CURDIR)/build/dev/muslisp" ]; then \
		echo "error: $(CURDIR)/build/dev/muslisp not found. Build first: cmake --preset dev && cmake --build --preset dev -j"; \
		exit 1; \
	fi
	@if [ ! -x "$(PYTHON)" ]; then \
		echo "error: $(PYTHON) not found. Run 'make demo-setup' first."; \
		exit 1; \
	fi
	rm -f "$(VERIFY_LOG)"
	./build/dev/muslisp examples/repl_scripts/verify-install.lisp >/dev/null
	"$(PYTHON)" tools/validate_log.py --schema "$(VERIFY_SCHEMA)" "$(VERIFY_LOG)"
	@echo "verify-install passed: $(VERIFY_LOG)"

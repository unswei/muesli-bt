PYTHON ?= $(CURDIR)/.venv-py311/bin/python
MODE ?= bt_planner
ARGS ?=

.PHONY: demo-setup demo-run

demo-setup:
	./scripts/demo-setup.sh "$(PYTHON)"

demo-run:
	@if [ ! -x "$(PYTHON)" ]; then \
		echo "error: $(PYTHON) not found. Run 'make demo-setup' first."; \
		exit 1; \
	fi
	PYTHONPATH="$(CURDIR)/build/dev/python:$$PYTHONPATH" \
	"$(PYTHON)" examples/pybullet_racecar/run_demo.py --mode "$(MODE)" $(ARGS)

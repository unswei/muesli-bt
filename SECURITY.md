# security policy

## reporting vulnerabilities or unsafe behaviour

Please do not open a public issue for a suspected security vulnerability, unsafe robotics behaviour, or a way to bypass documented runtime validation.

Report it by emailing the maintainers with:

- affected version or commit;
- platform and build configuration;
- a minimal reproduction, if possible;
- whether robot, simulator, ROS2, logging, replay, or model/VLA paths are involved;
- any safety impact or mitigation you already know.

## scope

Security and safety-sensitive reports can include:

- malformed input that crashes the runtime;
- event-log or replay validation bypasses;
- unsafe host action reaching a backend despite validation expectations;
- denial of service through unbounded parsing, planning, or model lifecycle behaviour;
- credential leakage in integration examples or tooling.

## non-goals

`muesli-bt` is a task-level runtime. It is not a hard real-time safety controller, robot driver, or safety-rated emergency-stop system. Host systems remain responsible for safety-critical IO and low-level control.

## response

The project is small and response times are best-effort. We will aim to acknowledge reports within 7 days and coordinate a fix or disclosure plan when the issue is confirmed.

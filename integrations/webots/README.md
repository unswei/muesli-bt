# webots integration

This module provides the installable Webots integration surface for downstream C++ consumers.

Public API:

- `webots/extension.hpp`
  - `muslisp::integrations::webots::make_extension(...)`
  - `bt::integrations::webots::install_callbacks(...)`

When built with `-DMUESLI_BT_BUILD_INTEGRATION_WEBOTS=ON` and a valid Webots SDK, the package exports:

- `muesli_bt::integration_webots`

If Webots SDK is not found, this target is omitted cleanly and core/runtime packaging remains available.

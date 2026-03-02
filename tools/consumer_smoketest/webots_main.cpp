#include "bt/runtime_host.hpp"
#include "webots/extension.hpp"

int main() {
    bt::runtime_host host;
    bt::integrations::webots::install_callbacks(host);

    auto maker = &muslisp::integrations::webots::make_extension;
    (void)maker;
    return 0;
}

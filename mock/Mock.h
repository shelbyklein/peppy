#pragma once

// The mock document behind the real catalog (see MockDocument.cpp).

#include "../shared/Catalog.h"

namespace peppy {

// post: how to run work on the "main thread" (a worker thread, or the Cocoa main queue).
Host* MakeMockHost(std::function<void(std::function<void()>)> post);
std::map<std::string, Runner> MockRunners();

} // namespace peppy

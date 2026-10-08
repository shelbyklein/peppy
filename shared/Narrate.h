#pragma once

// Plain-English lines for the panel's feed: what a call did ("Moved “Headline”
// 12 pt right"), or what it tried and why it didn't work. JSON in, text out.

#include "Core.h"

#include <string>

namespace peppy {

// result: null on failure; error: "" on success.
std::string Narrate(const std::string& method, const Json& params, const Json& result, const std::string& error);

} // namespace peppy

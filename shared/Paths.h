#pragma once

// Where a file an agent names should go. Agents often run somewhere else (a
// sandbox, a server behind a relay) and pass paths that don't exist here, so:
//   "proof.pdf"            -> ~/Desktop/proof.pdf
//   "~/Documents/x.pdf"    -> /Users/<you>/Documents/x.pdf
//   "/abs/path/x.pdf"      -> as is, if its folder exists on this Mac
// Anything else throws InvalidParams saying what's accepted.

#include "Core.h"

#include <string>

namespace peppy {

extern const char* kOutputPathHelp;
std::string OutputPath(const std::string& given);

} // namespace peppy

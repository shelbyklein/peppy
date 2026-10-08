#pragma once

// Every command Peppy offers for InDesign, with its parameter docs, in one
// place. The InDesign plug-in (and the mock host) attach a runner to each by
// name; a command with no runner yet answers "not built yet" rather than
// pretending.
//
// Conventions agents rely on:
// - Units are points. Coordinates are page coordinates: origin at the page's
//   top-left corner, y growing down (like InDesign's own rulers).
// - `page` is the page's number as the Pages panel shows it ("1", "iv",
//   "A-Parent" for a parent page) or its 1-based position as a number.
// - Item ids are InDesign's own ids (integers), stable across saves.
// - bounds are {x, y, width, height} in page coordinates.
// - Commands that take ids act on the selection when none are given.

#include "Core.h"

namespace peppy {

using Runner = std::function<Json(const Json& params)>;

// Registers the whole catalog, using runners[method] for each command.
void RegisterCatalog(const std::map<std::string, Runner>& runners);

// The everyday commands that become MCP tools of their own.
const std::vector<std::string>& Everyday();

// MCP instructions describing Peppy to agents.
std::string Instructions();

} // namespace peppy

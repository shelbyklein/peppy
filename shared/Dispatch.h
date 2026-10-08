#pragma once

// Running calls: one JSON-RPC request, or an array of them (a batch), from a
// server thread to the app's main thread and back.
//
// A batch is cut into runs. Each run is one trip to the main thread and, if
// it edits, one undo step. A call that writes a file (save, export) runs on its
// own, so a write never sits inside an edit step. A call that opens, closes,
// creates or switches documents ends its run: the app settles that before the
// next call. Later steps can use earlier results: "$0.id", "$2[0].id",
// "$1.bounds.left".

#include "Core.h"

#include <string>

namespace peppy {

// request: one call or an array. Returns the JSON-RPC response (or array of them).
// agent: who's calling, for the feed ("Claude", "Codex"... or "").
Json Dispatch(const Json& request, const std::string& agent, double timeoutSeconds = 60);

// Brief form of a result: ids, names, bounds, status. For quiet: true.
Json Quiet(const Json& result);

// Told about every call after it runs (main thread). The panel animates from it.
struct CallEvent {
	std::string method;
	bool ok;
	bool edit;
	double milliseconds;
	std::string line;    // plain English (Narrate)
	std::string error;   // full message when !ok
	std::string agent;
};
void SetCallObserver(std::function<void(const CallEvent&)> observer);

// Paused: every call is refused (the panel's Pause agents).
void SetPaused(bool paused);
bool Paused();

} // namespace peppy

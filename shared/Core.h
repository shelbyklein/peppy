#pragma once

// Peppy's core, free of Adobe's SDK: the command table, the errors commands
// throw, and the Host the app glue provides (main thread, undo steps).
// The InDesign plug-in and the mock host both build on this.

// Windows' min/max macros (from windows.h, which the InDesign SDK pulls in)
// break json.hpp; keep them out of its way.
#ifdef _WIN32
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#endif
#include "third_party/json.hpp"
#ifdef _WIN32
#pragma pop_macro("max")
#pragma pop_macro("min")
#endif

#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace peppy {

using Json = nlohmann::ordered_json;

// JSON-RPC error codes. Host errors carry the app's own code in data.
enum ErrorCode {
	kErrParse = -32700,
	kErrInvalidRequest = -32600,
	kErrMethodNotFound = -32601,
	kErrInvalidParams = -32602,
	kErrInternal = -32603,
	kErrHost = -32000,         // InDesign refused (data.idError has its code)
	kErrTimeout = -32001,
	kErrUnavailable = -32002,  // no document, busy in a modal dialog, shutting down
	kErrNotFound = -32004,     // an id that doesn't resolve
};

// What a command throws: one shape for every failure, with a hint saying what
// to try next.
struct Error : std::runtime_error {
	int code;
	std::string hint;
	Json data;
	Error(int code, const std::string& message, std::string hint = "", Json data = nullptr)
		: std::runtime_error(message), code(code), hint(std::move(hint)), data(std::move(data)) {}
};

inline Error InvalidParams(const std::string& m, const std::string& hint = "") { return Error(kErrInvalidParams, m, hint); }
inline Error NotFound(const std::string& m, const std::string& hint = "") { return Error(kErrNotFound, m, hint); }
inline Error Unavailable(const std::string& m, const std::string& hint = "") { return Error(kErrUnavailable, m, hint); }

// One parameter's doc: "type - meaning". The leading type becomes JSON Schema
// when it's a plain one (string, number, integer, boolean, string[], number[], object).
struct Param {
	std::string name;
	std::string doc;
	bool required = false;
};

enum class Kind {
	Read,       // looks, changes nothing
	Edit,       // changes the document: one undo step
	Write,      // writes a file (save, export): runs alone, never inside an edit step
	Switch,     // opens, closes, creates or activates a document: ends the run
	Undo,       // undo / redo: changes the document, but must run alone and outside any undo step
};

struct Command {
	std::string method;        // "item.transform"
	std::string summary;       // one line, what it does
	std::vector<Param> params;
	Kind kind = Kind::Read;
	std::function<Json(const Json& params)> run;
};

// The table. Commands register at startup (main thread), before the server starts.
void Register(Command c);
const Command* Find(const std::string& method);
const std::vector<Command>& All();
// Commands whose name, summary or params mention every word in `words`, best first.
std::vector<const Command*> Search(const std::string& words);
Json Describe(const Command& c);   // {method, description, params:{name: doc}, edits}

// What the app glue provides.
struct Host {
	virtual ~Host() = default;
	// Runs fn on the app's main thread, soon. Called from server threads.
	virtual void Post(std::function<void()> fn) = 0;
	// Undo grouping: everything between Begin and End is one step on Edit > Undo
	// named `name`. End(false) rolls the step back.
	virtual void BeginStep(const std::string& name) = 0;
	virtual void EndStep(bool commit) = 0;
	// False while the app can't take calls (a modal dialog is up, quitting...).
	virtual bool Ready(std::string& why) { (void) why; return true; }
};
void SetHost(Host* host);
Host* GetHost();

// Param helpers commands use.
std::string Str(const Json& p, const char* key, const std::string& fallback = "");
double Num(const Json& p, const char* key, double fallback);
bool Bool(const Json& p, const char* key, bool fallback);
const Json& Need(const Json& p, const char* key);   // throws InvalidParams when missing

} // namespace peppy

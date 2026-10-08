#include "Dispatch.h"
#include "Narrate.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace peppy {

namespace {

std::function<void(const CallEvent&)> gObserver;
std::atomic<bool> gPaused{false};

Json RpcError(const Json& id, int code, const std::string& message, const std::string& hint = "", const Json& data = nullptr)
{
	Json r;
	r["jsonrpc"] = "2.0";
	r["id"] = id;
	r["error"]["code"] = code;
	r["error"]["message"] = message;
	if (!hint.empty() || !data.is_null()) {
		Json d = data.is_object() ? data : Json::object();
		if (!hint.empty()) d["hint"] = hint;
		r["error"]["data"] = d;
	}
	return r;
}

Json RpcResult(const Json& id, Json result)
{
	Json r;
	r["jsonrpc"] = "2.0";
	r["id"] = id;
	r["result"] = std::move(result);
	return r;
}

// ---- batch references: "$2[0].bounds.left" -> a value from step 2's result

struct RefError : std::runtime_error { using std::runtime_error::runtime_error; };

Json ResolveRef(const std::string& s, const std::vector<Json>& done)
{
	size_t pos = 1, index = 0;
	while (pos < s.size() && std::isdigit((unsigned char) s[pos])) {
		index = index * 10 + (size_t) (s[pos++] - '0');
		if (index > 100000) throw RefError("batch reference '" + s + "' is out of range");
	}
	if (index >= done.size()) throw RefError("batch reference '" + s + "' must point to an earlier step");
	const Json& response = done[index];
	if (response.contains("error")) throw RefError("batch reference '" + s + "' points to a step that failed");
	Json found = response.contains("_full") ? response["_full"] : response["result"];
	while (pos < s.size()) {
		if (s[pos] == '.') {
			size_t begin = ++pos;
			while (pos < s.size() && (std::isalnum((unsigned char) s[pos]) || s[pos] == '_')) pos++;
			std::string key = s.substr(begin, pos - begin);
			if (key.empty()) throw RefError("invalid batch reference '" + s + "'");
			// Commands that act on a list return arrays even for one item: let "$0.id" reach into it.
			if (found.is_array() && found.size() == 1) found = Json(found[0]);
			if (!found.is_object() || !found.contains(key))
				throw RefError("batch reference '" + s + "': no field '" + key + "'");
			found = Json(found[key]);
		}
		else if (s[pos] == '[') {
			size_t begin = ++pos, n = 0;
			while (pos < s.size() && std::isdigit((unsigned char) s[pos])) n = n * 10 + (size_t) (s[pos++] - '0');
			if (begin == pos || pos >= s.size() || s[pos++] != ']' || !found.is_array() || n >= found.size())
				throw RefError("invalid batch array reference '" + s + "'");
			found = Json(found[n]);
		}
		else throw RefError("invalid batch reference '" + s + "'");
	}
	return found;
}

Json ResolveRefs(const Json& v, const std::vector<Json>& done)
{
	if (v.is_array()) {
		Json out = Json::array();
		for (const Json& x : v) out.push_back(ResolveRefs(x, done));
		return out;
	}
	if (v.is_object()) {
		Json out = Json::object();
		for (auto it = v.begin(); it != v.end(); ++it) out[it.key()] = ResolveRefs(it.value(), done);
		return out;
	}
	if (v.is_string()) {
		const std::string& s = v.get_ref<const std::string&>();
		if (s.size() >= 2 && s[0] == '$' && std::isdigit((unsigned char) s[1])) return ResolveRef(s, done);
	}
	return v;
}

// ---- one call, on the main thread

Kind KindOf(const Json& call)
{
	if (!call.is_object() || !call.contains("method") || !call["method"].is_string()) return Kind::Read;
	const Command* c = Find(call["method"].get<std::string>());
	return c ? c->kind : Kind::Read;
}

Json RunOne(const Json& call, const std::vector<Json>& done, const std::string& agent)
{
	Json id = call.is_object() && call.contains("id") ? call["id"] : Json(nullptr);
	if (!call.is_object() || !call.contains("method") || !call["method"].is_string())
		return RpcError(id, kErrInvalidRequest, "each call needs a \"method\"");
	std::string method = call["method"].get<std::string>();
	Json params = call.contains("params") && !call["params"].is_null() ? call["params"] : Json::object();

	auto start = std::chrono::steady_clock::now();
	const Command* c = Find(method);
	Json response, result;
	std::string error;
	try {
		if (!c) throw Error(kErrMethodNotFound, "no command '" + method + "'", "search for one with peppy_find (or the commands.search method)");
		if (!params.is_object()) throw InvalidParams("params should be an object");
		params = ResolveRefs(params, done);
		bool quiet = params.value("quiet", false);
		params.erase("quiet");
		result = c->run(params);
		response = RpcResult(id, quiet ? Quiet(result) : result);
		// Batch references see the full result even when the reply is quiet.
		if (quiet) response["_full"] = result;
	}
	catch (const RefError& e) {
		error = e.what();
		response = RpcError(id, kErrInvalidParams, error);
	}
	catch (const Error& e) {
		error = e.what();
		response = RpcError(id, e.code, error, e.hint, e.data);
	}
	catch (const nlohmann::json::exception& e) {
		error = std::string("bad parameter: ") + e.what();
		response = RpcError(id, kErrInvalidParams, error);
	}
	catch (const std::exception& e) {
		error = e.what();
		response = RpcError(id, kErrInternal, error);
	}

	if (gObserver) {
		CallEvent ev;
		ev.method = method;
		ev.ok = error.empty();
		ev.edit = c && c->kind == Kind::Edit;
		ev.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		ev.line = Narrate(method, params, error.empty() ? result : Json(nullptr), error);
		ev.error = error;
		ev.agent = agent;
		gObserver(ev);
	}
	return response;
}

// Runs calls[from, to) on the main thread as one undo step if any of them edits.
// Returns false when a call failed and the batch should stop.
bool RunSpan(const Json& calls, size_t from, size_t to, std::vector<Json>& done, const std::string& agent)
{
	Host* host = GetHost();
	bool edits = false;
	for (size_t i = from; i < to; i++) edits |= KindOf(calls[i]) == Kind::Edit;
	std::string stepName;
	if (edits) {
		size_t n = 0;
		std::string first;
		for (size_t i = from; i < to; i++)
			if (KindOf(calls[i]) == Kind::Edit) { if (!n++) first = calls[i]["method"].get<std::string>(); }
		stepName = n == 1 ? "Peppy: " + first : "Peppy: " + std::to_string(n) + " changes";
		host->BeginStep(stepName);
	}
	bool ok = true;
	for (size_t i = from; i < to; i++) {
		Json r = RunOne(calls[i], done, agent);
		bool failed = r.contains("error");
		done.push_back(std::move(r));
		bool stopOnError = !(calls[i].is_object() && calls[i].value("stopOnError", true) == false);
		if (failed && stopOnError) { ok = false; break; }
	}
	// A failed call threw before changing anything, so the calls before it stand.
	if (edits) host->EndStep(true);
	return ok;
}

// Waits for fn to run on the main thread, up to timeoutSeconds.
bool OnMain(std::function<void()> fn, double timeoutSeconds)
{
	struct Wait {
		std::mutex m;
		std::condition_variable cv;
		bool finished = false;
	};
	auto w = std::make_shared<Wait>();
	GetHost()->Post([w, fn]() {
		fn();
		std::lock_guard<std::mutex> lock(w->m);
		w->finished = true;
		w->cv.notify_all();
	});
	std::unique_lock<std::mutex> lock(w->m);
	return w->cv.wait_for(lock, std::chrono::duration<double>(timeoutSeconds), [&] { return w->finished; });
}

} // namespace

void SetCallObserver(std::function<void(const CallEvent&)> observer) { gObserver = std::move(observer); }
void SetPaused(bool paused) { gPaused = paused; }
bool Paused() { return gPaused; }

Json Dispatch(const Json& request, const std::string& agent, double timeoutSeconds)
{
	bool isBatch = request.is_array();
	Json calls = isBatch ? request : Json::array({request});
	Json firstId = !isBatch && request.is_object() && request.contains("id") ? request["id"] : Json(nullptr);

	auto refuse = [&](int code, const std::string& message, const std::string& hint) {
		if (!isBatch) return RpcError(firstId, code, message, hint);
		Json out = Json::array();
		for (const Json& c : calls) out.push_back(RpcError(c.is_object() && c.contains("id") ? c["id"] : Json(nullptr), code, message, hint));
		return out;
	};
	if (calls.empty()) return RpcError(nullptr, kErrInvalidRequest, "empty batch");
	if (gPaused) return refuse(kErrUnavailable, "agents are paused", "the person at InDesign paused agents in Peppy's panel; ask them to resume");
	Host* host = GetHost();
	if (!host) return refuse(kErrUnavailable, "Peppy isn't attached to InDesign", "");

	// Cut into runs: a write stands alone, a document switch ends its run.
	std::vector<std::pair<size_t, size_t>> runs;
	size_t start = 0;
	for (size_t i = 0; i < calls.size(); i++) {
		Kind k = KindOf(calls[i]);
		if (k == Kind::Write) {
			if (i > start) runs.push_back({start, i});
			runs.push_back({i, i + 1});
			start = i + 1;
		}
		else if (k == Kind::Switch) {
			runs.push_back({start, i + 1});
			start = i + 1;
		}
	}
	if (start < calls.size()) runs.push_back({start, calls.size()});

	auto done = std::make_shared<std::vector<Json>>();
	auto stopped = std::make_shared<bool>(false);
	auto notReady = std::make_shared<std::string>();
	for (auto run : runs) {
		size_t from = run.first, to = run.second;
		bool finished = OnMain([=]() {
			std::string why;
			if (!GetHost()->Ready(why)) { *notReady = why; *stopped = true; return; }
			if (!RunSpan(calls, from, to, *done, agent)) *stopped = true;
		}, timeoutSeconds);
		if (!finished) {
			// The run may still finish later; say so plainly rather than guess.
			return refuse(kErrTimeout, "InDesign didn't answer within " + std::to_string((int) timeoutSeconds) + " s",
				"it may be busy or showing a dialog; the call may still complete - check with document_info before retrying");
		}
		if (!notReady->empty()) return refuse(kErrUnavailable, "InDesign can't take calls right now: " + *notReady, "close any open dialog in InDesign, then try again");
		if (*stopped) break;
	}

	// Steps after a failure didn't run.
	for (size_t i = done->size(); i < calls.size(); i++) {
		Json id = calls[i].is_object() && calls[i].contains("id") ? calls[i]["id"] : Json(nullptr);
		done->push_back(RpcError(id, kErrInvalidRequest, "skipped: an earlier step failed"));
	}
	for (Json& r : *done) r.erase("_full");
	if (!isBatch) return (*done)[0];
	Json out = Json::array();
	for (Json& r : *done) out.push_back(std::move(r));
	return out;
}

Json Quiet(const Json& result)
{
	static const char* keep[] = {"id", "ids", "name", "type", "bounds", "page", "count", "ok", "path", "status", "changed", "document", "label"};
	if (result.is_array()) {
		Json out = Json::array();
		for (const Json& x : result) out.push_back(Quiet(x));
		return out;
	}
	if (!result.is_object()) return result;
	Json out = Json::object();
	for (const char* k : keep)
		if (result.contains(k)) out[k] = result[k];
	if (out.empty()) out["ok"] = true;
	return out;
}

} // namespace peppy

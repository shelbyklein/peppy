#include "Mcp.h"
#include "Dispatch.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>

namespace peppy {

namespace {

const char* kProtocols[] = {"2025-06-18", "2025-03-26", "2024-11-05"};

McpInfo gInfo;
std::mutex gClientsMutex;
std::map<std::string, std::string> gClients;   // User-Agent -> clientInfo.name

Json Reply(const Json& id, Json result)
{
	return {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}

Json Fail(const Json& id, int code, const std::string& message)
{
	return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}

std::string ToolName(std::string method)
{
	std::replace(method.begin(), method.end(), '.', '_');
	return method;
}

Json SchemaFor(const Command& c)
{
	Json s = {{"type", "object"}, {"properties", Json::object()}};
	Json required = Json::array();
	for (const Param& p : c.params) {
		std::string head = p.doc.substr(0, p.doc.find(' '));
		Json prop = {{"description", p.doc}};
		if (head == "string" || head == "number" || head == "integer" || head == "boolean" || head == "object") prop["type"] = head;
		else if (head == "string[]" || head == "number[]" || head == "integer[]") {
			prop["type"] = "array";
			prop["items"]["type"] = head.substr(0, head.size() - 2);
		}
		s["properties"][p.name] = prop;
		if (p.required) required.push_back(p.name);
	}
	s["properties"]["quiet"] = {{"type", "boolean"}, {"description", "Return only ids, names, bounds and status (default false)"}};
	if (!required.empty()) s["required"] = required;
	return s;
}

Json ToolFor(const Command& c)
{
	Json t = {{"name", ToolName(c.method)}, {"description", c.summary}, {"inputSchema", SchemaFor(c)}};
	t["annotations"]["readOnlyHint"] = c.kind == Kind::Read;
	t["annotations"]["destructiveHint"] = c.method.find("delete") != std::string::npos || c.method == "document.close";
	return t;
}

Json ToolsList(bool allTools)
{
	Json tools = Json::parse(R"([
	{"name": "peppy_find",
	 "description": "Search Peppy's commands by words (\"style\", \"table\", \"parent\", \"export pdf\", \"thread\"): each match's method, what it does and its parameters. Run one with peppy_call. No words: every command, briefly.",
	 "inputSchema": {"type": "object", "properties": {"search": {"type": "string", "description": "words to look for"}}},
	 "annotations": {"readOnlyHint": true}},
	{"name": "peppy_call",
	 "description": "Run any Peppy command by its method name from peppy_find (e.g. \"style.apply\", \"table.create\"), with its params.",
	 "inputSchema": {"type": "object", "required": ["method"], "properties": {
		"method": {"type": "string", "description": "dotted method name"},
		"params": {"type": "object", "description": "the command's parameters, as peppy_find lists them"},
		"quiet": {"type": "boolean", "description": "Return only ids, names, bounds and status"}}}},
	{"name": "peppy_batch",
	 "description": "Run several Peppy commands back to back as ONE undo step; stops at the first error. Each call is {method, params}. A later step can use an earlier result: \"$0.id\", \"$1[0].id\", \"$2.bounds.y\". Saves and exports run on their own after the edits before them; opening, closing or switching documents ends a run and the rest follows once InDesign settles.",
	 "inputSchema": {"type": "object", "required": ["calls"], "properties": {
		"calls": {"type": "array", "items": {"type": "object", "required": ["method"],
			"properties": {"method": {"type": "string"}, "params": {"type": "object"}}}},
		"quiet": {"type": "boolean", "description": "Return only ids, bounds and status per step"}}}},
	{"name": "peppy_status",
	 "description": "Check that InDesign and Peppy are reachable: versions, open documents.",
	 "inputSchema": {"type": "object", "properties": {}},
	 "annotations": {"readOnlyHint": true}}
	])");

	for (const Command& c : All()) {
		bool everyday = std::find(gInfo.everyday.begin(), gInfo.everyday.end(), c.method) != gInfo.everyday.end();
		if (allTools || everyday) tools.push_back(ToolFor(c));
	}
	return tools;
}

// A command's picture (view.screenshot, document.export without a path) as an
// MCP image block, taken out of the result so the text stays short; null if none.
Json TakeImage(Json& result)
{
	if (!result.is_object() || !result.contains("image") || !result["image"].is_object() || !result["image"].contains("data")) return nullptr;
	Json block = {{"type", "image"}, {"data", result["image"]["data"]}, {"mimeType", result["image"].value("mimeType", "image/jpeg")}};
	result.erase("image");
	return block;
}

// A JSON-RPC response from Dispatch, as an MCP tool result.
Json AsToolResult(const Json& response)
{
	Json r;
	if (response.is_array()) {
		bool anyError = false;
		Json steps = Json::array(), images = Json::array();
		for (const Json& x : response) {
			if (x.contains("error")) { anyError = true; steps.push_back({{"error", x["error"]}}); continue; }
			Json result = x["result"];
			Json image = TakeImage(result);
			if (!image.is_null()) images.push_back(image);
			steps.push_back(result);
		}
		r["content"] = Json::array({{{"type", "text"}, {"text", steps.dump()}}});
		for (Json& image : images) r["content"].push_back(image);
		if (anyError) r["isError"] = true;
		return r;
	}
	if (response.contains("error")) {
		const Json& e = response["error"];
		std::string text = e.value("message", "error");
		if (e.contains("data") && e["data"].contains("hint")) text += "\nHint: " + e["data"]["hint"].get<std::string>();
		r["content"] = Json::array({{{"type", "text"}, {"text", text}}});
		r["isError"] = true;
		return r;
	}
	Json result = response["result"];
	Json image = TakeImage(result);
	r["content"] = Json::array({{{"type", "text"}, {"text", result.dump()}}});
	if (!image.is_null()) r["content"].push_back(image);
	return r;
}

Json CallTool(const std::string& name, const Json& args, const std::string& agent)
{
	if (name == "peppy_find") {
		std::string words = args.value("search", "");
		Json out = Json::array();
		for (const Command* c : Search(words)) {
			if (words.empty()) out.push_back({{"method", c->method}, {"description", c->summary}});
			else out.push_back(Describe(*c));
			if (!words.empty() && out.size() >= 25) break;
		}
		return {{"content", Json::array({{{"type", "text"}, {"text", out.empty() ? "No command matches \"" + words + "\"; try fewer or other words." : out.dump()}}})}};
	}
	if (name == "peppy_call") {
		Json params = args.contains("params") && args["params"].is_object() ? args["params"] : Json::object();
		if (args.value("quiet", false)) params["quiet"] = true;
		return AsToolResult(Dispatch({{"jsonrpc", "2.0"}, {"id", 1}, {"method", args.value("method", "")}, {"params", params}}, agent));
	}
	if (name == "peppy_batch") {
		if (!args.contains("calls") || !args["calls"].is_array() || args["calls"].empty())
			return {{"content", Json::array({{{"type", "text"}, {"text", "peppy_batch needs a non-empty 'calls' array"}}})}, {"isError", true}};
		Json calls = Json::array();
		int i = 0;
		bool quiet = args.value("quiet", false);
		for (Json c : args["calls"]) {
			if (!c.is_object()) c = Json::object();
			c["jsonrpc"] = "2.0";
			c["id"] = i++;
			if (quiet) c["params"]["quiet"] = true;
			calls.push_back(c);
		}
		return AsToolResult(Dispatch(calls, agent));
	}
	if (name == "peppy_status") {
		Json status = {{"peppy", gInfo.version}};
		if (Find("app.info")) {
			Json r = Dispatch({{"jsonrpc", "2.0"}, {"id", 1}, {"method", "app.info"}}, agent, 10);
			if (r.contains("result")) status["app"] = r["result"];
			else status["error"] = r["error"];
		}
		return {{"content", Json::array({{{"type", "text"}, {"text", status.dump()}}})}};
	}
	std::string method = name;
	std::replace(method.begin(), method.end(), '_', '.');
	// Methods with underscores of their own: match against the table.
	for (const Command& c : All())
		if (ToolName(c.method) == name) { method = c.method; break; }
	return AsToolResult(Dispatch({{"jsonrpc", "2.0"}, {"id", 1}, {"method", method}, {"params", args.is_object() ? args : Json::object()}}, agent));
}

std::string Friendly(const std::string& s)
{
	std::string l = s;
	for (char& c : l) c = (char) std::tolower((unsigned char) c);
	static const std::pair<const char*, const char*> names[] = {
		{"claude", "Claude"}, {"codex", "Codex"}, {"cursor", "Cursor"}, {"gemini", "Gemini"}, {"windsurf", "Windsurf"},
		{"copilot", "Copilot"}, {"vscode", "VS Code"}, {"visual studio code", "VS Code"}, {"cline", "Cline"}, {"roo", "Roo Code"},
		{"goose", "Goose"}, {"opencode", "OpenCode"}, {"chatgpt", "ChatGPT"}, {"openai", "ChatGPT"}, {"grok", "Grok"}, {"qwen", "Qwen"},
		{"kimi", "Kimi"}, {"chatterbox", "Claude"}};
	for (auto& [k, v] : names)
		if (l.find(k) != std::string::npos) return v;
	return "";
}

} // namespace

void SetMcpInfo(McpInfo info) { gInfo = std::move(info); }

std::string AgentFor(const std::string& userAgent)
{
	std::string introduced;
	{
		std::lock_guard<std::mutex> lock(gClientsMutex);
		auto it = gClients.find(userAgent);
		if (it != gClients.end()) introduced = it->second;
	}
	std::string f = Friendly(introduced);
	if (f.empty()) f = Friendly(userAgent);
	if (f.empty()) f = introduced;
	return f;
}

Json HandleMcp(const Json& message, bool allTools, const std::string& userAgent)
{
	if (!message.is_object()) return Fail(nullptr, kErrInvalidRequest, "expected a JSON-RPC message");
	if (!message.contains("id")) return nullptr;   // a notification
	Json id = message["id"];
	std::string method = message.value("method", "");
	Json params = message.contains("params") ? message["params"] : Json::object();

	if (method == "initialize") {
		std::string asked = params.value("protocolVersion", kProtocols[0]);
		std::string version = kProtocols[0];
		for (const char* p : kProtocols) if (asked == p) version = p;
		if (params.contains("clientInfo") && params["clientInfo"].is_object()) {
			std::lock_guard<std::mutex> lock(gClientsMutex);
			gClients[userAgent] = params["clientInfo"].value("name", "");
		}
		return Reply(id, {{"protocolVersion", version},
			{"capabilities", {{"tools", {{"listChanged", false}}}}},
			{"serverInfo", {{"name", "peppy-indesign"}, {"title", gInfo.title}, {"version", gInfo.version}}},
			{"instructions", gInfo.instructions}});
	}
	if (method == "ping") return Reply(id, Json::object());
	if (method == "tools/list") return Reply(id, {{"tools", ToolsList(allTools)}});
	if (method == "tools/call") {
		std::string name = params.value("name", "");
		Json args = params.contains("arguments") && params["arguments"].is_object() ? params["arguments"] : Json::object();
		return Reply(id, CallTool(name, args, AgentFor(userAgent)));
	}
	if (method == "resources/list") return Reply(id, {{"resources", Json::array()}});
	if (method == "prompts/list") return Reply(id, {{"prompts", Json::array()}});
	return Fail(id, kErrMethodNotFound, "unknown MCP method '" + method + "'");
}

} // namespace peppy

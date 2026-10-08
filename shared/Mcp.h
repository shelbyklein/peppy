#pragma once

// MCP over Peppy's HTTP server (streamable HTTP, answered as plain JSON).
// Tools: the everyday commands as their own tools (item.transform ->
// item_transform), peppy_find / peppy_call for the rest, peppy_batch (several
// calls, one undo step) and peppy_status. With ?tools=all every command is a tool.

#include "Core.h"

#include <string>
#include <vector>

namespace peppy {

struct McpInfo {
	std::string title = "Peppy for Adobe InDesign";
	std::string version = "0.0.0";
	std::string instructions;
	std::vector<std::string> everyday;
};
void SetMcpInfo(McpInfo info);

// One MCP message in; its response out, or null for a notification.
Json HandleMcp(const Json& message, bool allTools, const std::string& userAgent);

// A friendly agent name ("Claude", "Codex", "Cursor"...) for a caller, from its
// MCP introduction or User-Agent; "" when there's nothing to go on.
std::string AgentFor(const std::string& userAgent);

} // namespace peppy

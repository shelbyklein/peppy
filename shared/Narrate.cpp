#include "Narrate.h"

#include <cmath>
#include <sstream>

namespace peppy {

namespace {

std::string Quote(const std::string& s) { return "“" + s + "”"; }

std::string Num(double v)
{
	std::ostringstream o;
	if (std::fabs(v - std::round(v)) < 0.005) o << (long long) std::llround(v);
	else { o.precision(2); o << std::fixed << v; }
	return o.str();
}

// What the call was about: a name if it had one, else "the selection" / "an item".
std::string Subject(const Json& params, const Json& result)
{
	auto nameOf = [](const Json& j) -> std::string {
		if (j.is_array() && j.size() == 1) return j[0].is_object() ? j[0].value("name", "") : "";
		if (j.is_object()) return j.value("name", "");
		return "";
	};
	std::string n = nameOf(result);
	if (!n.empty()) return Quote(n);
	if (result.is_array() && result.size() > 1) return std::to_string(result.size()) + " items";
	if (params.contains("ids") && params["ids"].is_array()) {
		size_t k = params["ids"].size();
		return k == 1 ? "item " + params["ids"][0].dump() : std::to_string(k) + " items";
	}
	if (params.contains("id")) return "item " + (params["id"].is_string() ? params["id"].get<std::string>() : params["id"].dump());
	return "the selection";
}

std::string Short(const std::string& error)
{
	if (error.find("no document") != std::string::npos || error.find("No document") != std::string::npos) return "No document is open";
	if (error.find("not found") != std::string::npos || error.find("no item") != std::string::npos) return "Couldn't find that";
	if (error.find("no command") != std::string::npos) return "There's no such command";
	if (error.find("missing '") != std::string::npos) return "Something it needed was left out";
	return "That didn't work";
}

} // namespace

std::string Narrate(const std::string& method, const Json& params, const Json& result, const std::string& error)
{
	if (!error.empty()) return Short(error) + "\n" + error;

	std::string group = method.substr(0, method.find('.'));
	std::string verb = method.substr(method.find('.') + 1);
	std::string what = Subject(params, result);

	if (method == "item.transform") {
		std::vector<std::string> parts;
		double dx = params.value("dx", 0.0), dy = params.value("dy", 0.0);
		if (params.contains("translate") && params["translate"].is_array() && params["translate"].size() == 2) {
			dx = params["translate"][0].get<double>();
			dy = params["translate"][1].get<double>();
		}
		if (dx || dy) parts.push_back("moved " + Num(std::hypot(dx, dy)) + " pt");
		if (params.contains("rotate")) parts.push_back("rotated " + Num(params["rotate"].get<double>()) + "°");
		if (params.contains("scale")) parts.push_back("scaled");
		std::string s = parts.empty() ? "Transformed" : parts[0];
		for (size_t i = 1; i < parts.size(); i++) s += (i + 1 == parts.size() ? " and " : ", ") + parts[i];
		s[0] = (char) std::toupper((unsigned char) s[0]);
		return s + " " + what;
	}
	if (method == "item.delete") return "Deleted " + what;
	if (method == "item.set") return "Changed " + what;
	if (method == "item.select") return "Selected " + what;
	if (method == "item.duplicate") return "Duplicated " + what;
	if (method == "item.group") return "Grouped " + what;
	if (method == "text.set") return "Rewrote the text in " + what;
	if (method == "text.format") return "Formatted the text in " + what;
	if (method == "text.find") return "Searched the text";
	if (method == "text.replace") return "Replaced text";
	if (method == "document.info") return "Looked at the document";
	if (method == "document.new") return "Made a new document";
	if (method == "document.open") return "Opened " + Quote(params.value("path", "a file"));
	if (method == "document.save") return "Saved the document";
	if (method == "document.close") return "Closed the document";
	if (method == "document.export") return "Exported " + Quote(params.value("path", "a copy"));
	if (method == "history.undo") return "Undid the last change";
	if (method == "history.redo") return "Redid a change";
	if (verb == "tree" || verb == "list") return "Listed " + group + (group.back() == 's' ? "" : "s");
	if (verb == "get" || verb == "info") return "Looked at " + what;
	if (verb == "create" || group == "frame") return "Made " + (what == "the selection" ? "a " + group : what);
	return method;
}

} // namespace peppy

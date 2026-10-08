#include "Core.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace peppy {

namespace {

std::vector<Command>& Table()
{
	static std::vector<Command> table;
	return table;
}

Host* gHost = nullptr;

std::string Lower(std::string s)
{
	for (char& c : s) c = (char) std::tolower((unsigned char) c);
	return s;
}

} // namespace

void Register(Command c)
{
	auto& t = Table();
	auto it = std::find_if(t.begin(), t.end(), [&](const Command& x) { return x.method == c.method; });
	if (it != t.end()) *it = std::move(c);
	else t.push_back(std::move(c));
}

const Command* Find(const std::string& method)
{
	for (const Command& c : Table())
		if (c.method == method) return &c;
	return nullptr;
}

const std::vector<Command>& All() { return Table(); }

std::vector<const Command*> Search(const std::string& words)
{
	std::vector<std::string> terms;
	std::istringstream in(Lower(words));
	for (std::string w; in >> w;) terms.push_back(w);

	std::vector<std::pair<int, const Command*>> scored;
	for (const Command& c : Table()) {
		std::string name = Lower(c.method), text = Lower(c.summary);
		for (const Param& p : c.params) text += " " + Lower(p.name) + " " + Lower(p.doc);
		int score = 0;
		bool all = true;
		for (const std::string& t : terms) {
			if (name.find(t) != std::string::npos) score += 3;
			else if (text.find(t) != std::string::npos) score += 1;
			else { all = false; break; }
		}
		if (all) scored.push_back({score, &c});
	}
	std::stable_sort(scored.begin(), scored.end(), [](auto& a, auto& b) { return a.first > b.first; });
	std::vector<const Command*> out;
	for (auto& s : scored) out.push_back(s.second);
	return out;
}

Json Describe(const Command& c)
{
	Json d;
	d["method"] = c.method;
	d["description"] = c.summary;
	d["params"] = Json::object();
	for (const Param& p : c.params) d["params"][p.name] = (p.required ? "(required) " : "") + p.doc;
	d["changesDocument"] = c.kind == Kind::Edit;
	return d;
}

void SetHost(Host* host) { gHost = host; }
Host* GetHost() { return gHost; }

std::string Str(const Json& p, const char* key, const std::string& fallback)
{
	if (!p.is_object() || !p.contains(key) || p[key].is_null()) return fallback;
	if (p[key].is_string()) return p[key].get<std::string>();
	if (p[key].is_number_integer()) return std::to_string(p[key].get<long long>());
	throw InvalidParams(std::string("'") + key + "' should be a string");
}

double Num(const Json& p, const char* key, double fallback)
{
	if (!p.is_object() || !p.contains(key) || p[key].is_null()) return fallback;
	if (p[key].is_number()) return p[key].get<double>();
	if (p[key].is_string()) {
		try { return std::stod(p[key].get<std::string>()); } catch (...) {}
	}
	throw InvalidParams(std::string("'") + key + "' should be a number");
}

bool Bool(const Json& p, const char* key, bool fallback)
{
	if (!p.is_object() || !p.contains(key) || p[key].is_null()) return fallback;
	if (p[key].is_boolean()) return p[key].get<bool>();
	throw InvalidParams(std::string("'") + key + "' should be true or false");
}

const Json& Need(const Json& p, const char* key)
{
	if (!p.is_object() || !p.contains(key) || p[key].is_null())
		throw InvalidParams(std::string("missing '") + key + "'");
	return p[key];
}

} // namespace peppy

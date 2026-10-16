// The mock document: a small in-memory "InDesign document" behind the real
// catalog, for testing agents, the protocol and the panel without InDesign.

#include "Mock.h"
#include "../shared/Dispatch.h"
#include "../shared/Mcp.h"
#include "../shared/Server.h"

#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>

using namespace peppy;

namespace {

// ---- the host: undo steps are snapshots of the document; "main thread" is whatever post does

class MockHost : public Host {
public:
	explicit MockHost(std::function<void(std::function<void()>)> post) : fPost(std::move(post)) {}

	void Post(std::function<void()> fn) override { fPost(std::move(fn)); }
	void BeginStep(const std::string& name) override { fStepName = name; fBefore = Doc(); }
	void EndStep(bool commit) override
	{
		if (fStepName.find("history.") != std::string::npos) return;   // undo/redo aren't steps of their own
		if (!commit) { Doc() = fBefore; return; }
		if (Doc() != fBefore) { fUndo.push_back({fStepName, fBefore}); fRedo.clear(); }
	}

	static Json& Doc()
	{
		static Json doc;
		return doc;
	}
	std::vector<std::pair<std::string, Json>> fUndo, fRedo;

private:
	std::function<void(std::function<void()>)> fPost;
	std::string fStepName;
	Json fBefore;
};

MockHost* gHost;

// ---- the document model: pages with items; items are objects with an "id"

Json& D() { return MockHost::Doc(); }

void NewDocument()
{
	D() = {{"name", "Untitled-1"}, {"nextId", 200}, {"width", 612}, {"height", 792}, {"selection", Json::array()},
		{"layers", Json::array({{{"id", 100}, {"name", "Layer 1"}, {"visible", true}, {"locked", false}}})},
		{"pages", Json::array({{{"name", "1"}, {"parent", "A-Parent"}, {"items", Json::array()}}})}};
}

Json& PageFor(const Json& p)
{
	std::string want = Str(p, "page", "1");
	Json& pages = D()["pages"];
	for (Json& pg : pages) if (pg["name"] == want) return pg;
	try {
		size_t i = std::stoul(want);
		if (i >= 1 && i <= pages.size()) return pages[i - 1];
	} catch (...) {}
	throw NotFound("no page '" + want + "'", "page.list shows the pages");
}

Json* FindItem(long long id, Json** pageOut = nullptr)
{
	for (Json& pg : D()["pages"])
		for (Json& it : pg["items"])
			if (it["id"] == id) { if (pageOut) *pageOut = &pg; return &it; }
	return nullptr;
}

long long IdOf(const Json& v)
{
	if (v.is_number_integer()) return v.get<long long>();
	if (v.is_string()) try { return std::stoll(v.get<std::string>()); } catch (...) {}
	throw InvalidParams("ids are integers");
}

std::vector<Json*> Targets(const Json& p)
{
	Json ids = p.contains("ids") ? p["ids"] : p.contains("id") ? Json::array({p["id"]}) : D()["selection"];
	if (!ids.is_array()) ids = Json::array({ids});
	if (ids.empty()) throw InvalidParams("no ids given and nothing is selected", "pass ids, or select items with item.select");
	std::vector<Json*> out;
	for (const Json& v : ids) {
		Json* it = FindItem(IdOf(v));
		if (!it) throw NotFound("no item " + v.dump(), "item.tree lists the items on a page");
		out.push_back(it);
	}
	return out;
}

Json Brief(const Json& it)
{
	Json b = {{"id", it["id"]}, {"type", it["type"]}, {"name", it["name"]}, {"bounds", it["bounds"]}};
	if (it.contains("contents")) b["text"] = it["contents"].get<std::string>().substr(0, 80);
	return b;
}

Json MakeFrame(const Json& p, const std::string& type)
{
	if (D().is_null()) throw Unavailable("no document is open", "open one with document.open or make one with document.new");
	for (const char* k : {"x", "y", "width", "height"}) Need(p, k);
	Json& page = PageFor(p);
	long long id = D()["nextId"].get<long long>();
	D()["nextId"] = id + 1;
	Json it = {{"id", id}, {"type", type}, {"name", Str(p, "name", "")}, {"layer", Str(p, "layer", "Layer 1")},
		{"bounds", {{"x", Num(p, "x", 0)}, {"y", Num(p, "y", 0)}, {"width", Num(p, "width", 0)}, {"height", Num(p, "height", 0)}}},
		{"rotation", 0}, {"fill", Str(p, "fill", "None")}, {"stroke", Str(p, "stroke", "None")}, {"strokeWeight", Num(p, "strokeWeight", 0)}};
	if (type == "TextFrame") it["contents"] = Str(p, "contents", "");
	page["items"].push_back(it);
	return Brief(it);
}

} // namespace

namespace peppy {

std::map<std::string, Runner> MockRunners()
{
	std::map<std::string, Runner> r;
	r["app.info"] = [](const Json&) {
		return Json{{"app", "InDesign (mock)"}, {"version", "21.6"}, {"peppy", PEPPY_VERSION}, {"documents", D().is_null() ? 0 : 1}, {"ready", true}};
	};
	r["document.new"] = [](const Json& p) {
		NewDocument();
		D()["width"] = Num(p, "width", 612);
		D()["height"] = Num(p, "height", 792);
		int pages = (int) Num(p, "pages", 1);
		for (int i = 2; i <= pages; i++) D()["pages"].push_back({{"name", std::to_string(i)}, {"parent", "A-Parent"}, {"items", Json::array()}});
		return Json{{"document", 0}, {"name", D()["name"]}};
	};
	r["document.info"] = [](const Json&) {
		if (D().is_null()) throw Unavailable("no document is open", "open one with document.open or make one with document.new");
		Json pages = Json::array();
		for (Json& pg : D()["pages"]) pages.push_back({{"name", pg["name"]}, {"parent", pg["parent"]}, {"items", pg["items"].size()}});
		return Json{{"name", D()["name"]}, {"pageSize", {{"width", D()["width"]}, {"height", D()["height"]}}}, {"pages", pages}, {"layers", D()["layers"]}};
	};
	r["page.list"] = [](const Json&) {
		Json out = Json::array();
		int i = 1;
		for (Json& pg : D()["pages"])
			out.push_back({{"name", pg["name"]}, {"position", i++}, {"parent", pg["parent"]},
				{"bounds", {{"x", 0}, {"y", 0}, {"width", D()["width"]}, {"height", D()["height"]}}}, {"items", pg["items"].size()}});
		return out;
	};
	r["page.add"] = [](const Json& p) {
		int count = (int) Num(p, "count", 1);
		Json added = Json::array();
		for (int i = 0; i < count; i++) {
			std::string name = std::to_string(D()["pages"].size() + 1);
			D()["pages"].push_back({{"name", name}, {"parent", Str(p, "parent", "A-Parent")}, {"items", Json::array()}});
			added.push_back(name);
		}
		return Json{{"added", added}};
	};
	r["layer.list"] = [](const Json&) { return D()["layers"]; };
	r["item.tree"] = [](const Json& p) {
		Json out = Json::array();
		Json& pg = PageFor(p);
		for (auto it = pg["items"].rbegin(); it != pg["items"].rend(); ++it) out.push_back(Brief(*it));   // front to back
		return out;
	};
	r["item.get"] = [](const Json& p) {
		Json* it = FindItem(IdOf(Need(p, "id")));
		if (!it) throw NotFound("no item " + p["id"].dump());
		return *it;
	};
	r["item.select"] = [](const Json& p) {
		if (!Bool(p, "add", false)) D()["selection"] = Json::array();
		for (const Json& v : p.value("ids", Json::array())) {
			if (!FindItem(IdOf(v))) throw NotFound("no item " + v.dump());
			D()["selection"].push_back(IdOf(v));
		}
		return Json{{"ids", D()["selection"]}};
	};
	r["item.selection"] = [](const Json&) {
		Json out = Json::array();
		for (const Json& v : D()["selection"]) if (Json* it = FindItem(v.get<long long>())) out.push_back(Brief(*it));
		return out;
	};
	r["item.set"] = [](const Json& p) {
		Json out = Json::array();
		for (Json* it : Targets(p)) {
			for (const char* k : {"name", "fill", "stroke", "strokeWeight", "opacity", "cornerRadius", "locked", "visible"})
				if (p.contains(k)) (*it)[k] = p[k];
			out.push_back(Brief(*it));
		}
		return out;
	};
	r["item.bounds"] = [](const Json& p) {
		Json* it = FindItem(IdOf(Need(p, "id")));
		if (!it) throw NotFound("no item " + p["id"].dump());
		for (const char* k : {"x", "y", "width", "height"})
			if (p.contains(k)) (*it)["bounds"][k] = Num(p, k, 0);
		return Brief(*it);
	};
	r["item.transform"] = [](const Json& p) {
		Json out = Json::array();
		double dx = 0, dy = 0;
		if (p.contains("translate")) { dx = p["translate"].at(0).get<double>(); dy = p["translate"].at(1).get<double>(); }
		for (Json* it : Targets(p)) {
			Json& b = (*it)["bounds"];
			if (p.contains("scale")) {
				double sx = p["scale"].is_array() ? p["scale"][0].get<double>() : p["scale"].get<double>();
				double sy = p["scale"].is_array() ? p["scale"][1].get<double>() : sx;
				double cx = b["x"].get<double>() + b["width"].get<double>() / 2, cy = b["y"].get<double>() + b["height"].get<double>() / 2;
				b["width"] = b["width"].get<double>() * sx;
				b["height"] = b["height"].get<double>() * sy;
				b["x"] = cx - b["width"].get<double>() / 2;
				b["y"] = cy - b["height"].get<double>() / 2;
			}
			b["x"] = b["x"].get<double>() + dx;
			b["y"] = b["y"].get<double>() + dy;
			if (p.contains("rotate")) (*it)["rotation"] = std::fmod((*it)["rotation"].get<double>() + p["rotate"].get<double>(), 360.0);
			out.push_back(Brief(*it));
		}
		return out;
	};
	r["item.delete"] = [](const Json& p) {
		std::vector<long long> ids;
		for (Json* it : Targets(p)) ids.push_back((*it)["id"].get<long long>());
		for (Json& pg : D()["pages"]) {
			Json kept = Json::array();
			for (Json& it : pg["items"])
				if (std::find(ids.begin(), ids.end(), it["id"].get<long long>()) == ids.end()) kept.push_back(it);
			pg["items"] = kept;
		}
		D()["selection"] = Json::array();
		return Json{{"deleted", ids}};
	};
	r["item.duplicate"] = [](const Json& p) {
		Json out = Json::array();
		for (Json* it : Targets(p)) {
			Json* page = nullptr;
			FindItem((*it)["id"].get<long long>(), &page);
			Json copy = *it;
			copy["id"] = D()["nextId"];
			D()["nextId"] = D()["nextId"].get<long long>() + 1;
			if (p.contains("translate")) {
				copy["bounds"]["x"] = copy["bounds"]["x"].get<double>() + p["translate"][0].get<double>();
				copy["bounds"]["y"] = copy["bounds"]["y"].get<double>() + p["translate"][1].get<double>();
			}
			(*page)["items"].push_back(copy);
			out.push_back(Brief(copy));
		}
		return out;
	};
	r["frame.text"] = [](const Json& p) { return MakeFrame(p, "TextFrame"); };
	r["frame.rect"] = [](const Json& p) { return MakeFrame(p, "Rectangle"); };
	r["frame.ellipse"] = [](const Json& p) { return MakeFrame(p, "Oval"); };
	r["text.get"] = [](const Json& p) {
		Json* it = FindItem(IdOf(Need(p, "id")));
		if (!it || !it->contains("contents")) throw NotFound("no text frame " + p["id"].dump());
		return Json{{"id", (*it)["id"]}, {"contents", (*it)["contents"]}, {"overset", false}};
	};
	r["text.set"] = [](const Json& p) {
		Json* it = FindItem(IdOf(Need(p, "id")));
		if (!it || !it->contains("contents")) throw NotFound("no text frame " + p["id"].dump());
		(*it)["contents"] = Str(p, "contents");
		return Brief(*it);
	};
	// A 1x1 white JPEG: the mock can't render, but the image path can be tested.
	r["view.screenshot"] = [](const Json& p) -> Json {
		return {{"page", Str(p, "page", "1")}, {"mock", true}, {"image", {{"mimeType", "image/jpeg"},
			{"data", "/9j/4AAQSkZJRgABAQEASABIAAD/2wBDAP//////////////////////////////////////////////////////////////////////////////////////wgALCAABAAEBAREA/8QAFBABAAAAAAAAAAAAAAAAAAAAAP/aAAgBAQABPxA="}}}};
	};
	r["history.undo"] = [](const Json& p) {
		int steps = (int) Num(p, "steps", 1), done = 0;
		Json names = Json::array();
		// history.undo is itself an Edit (one step); the dispatcher snapshots around it, so restore directly.
		for (; done < steps && !gHost->fUndo.empty(); done++) {
			auto last = gHost->fUndo.back();
			gHost->fUndo.pop_back();
			gHost->fRedo.push_back({last.first, D()});
			D() = last.second;
			names.push_back(last.first);
		}
		return Json{{"undone", names}};
	};
	return r;
}


Host* MakeMockHost(std::function<void(std::function<void()>)> post)
{
	gHost = new MockHost(std::move(post));
	return gHost;
}

} // namespace peppy

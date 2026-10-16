// Peppy's command runners for InDesign. A few are native; the rest call
// peppy.jsx, Peppy's library in InDesign's own scripting DOM, loaded once into
// a persistent engine ("peppy") and called in-process on the main thread.

#include "VCPlugInHeaders.h"

#include "Utils.h"

#include "PpyHost.h"
#include "PpyID.h"
#include "PpyImage.h"

#include "../shared/Paths.h"

#include <cstdio>
#include <fstream>
#include <iterator>

namespace {

using peppy::Json;

// The library's source, made from peppy.jsx at build time.
const char* kLibrary =
#include "peppy.jsx.inc"
;

bool gLibraryLoaded = false;

std::string Escape(const std::string& s)
{
	// A JavaScript string literal (JSON's string form is one).
	return Json(s).dump();
}

// Calls __peppy.run(method, params) in the peppy engine; loads the library first if needed.
Json CallLibrary(const std::string& method, const Json& params)
{
	std::string call = "__peppy.run(" + Escape(method) + ", " + Escape(params.dump()) + ")";
	std::string out;
	for (int attempt = 0; attempt < 2; attempt++) {
		std::string code = "#targetengine \"peppy\"\n";
		if (!gLibraryLoaded) code += std::string(kLibrary) + "\n";
		code += call;
		try {
			out = PpyRunScript(code);
			gLibraryLoaded = true;
			break;
		}
		catch (const peppy::Error& e) {
			// The engine was reset (or never had the library): load it and try once more.
			if (gLibraryLoaded && std::string(e.what()).find("__peppy") != std::string::npos) { gLibraryLoaded = false; continue; }
			throw;
		}
	}
	Json reply;
	try { reply = Json::parse(out); }
	catch (...) { throw peppy::Error(peppy::kErrInternal, "peppy.jsx returned something that isn't JSON: " + out.substr(0, 200)); }
	if (reply.is_object() && reply.contains("error")) {
		const Json& e = reply["error"];
		throw peppy::Error(e.value("code", (int) peppy::kErrHost), e.value("message", "InDesign refused"), e.value("hint", ""));
	}
	return reply.contains("result") ? reply["result"] : Json(nullptr);
}

} // namespace

std::map<std::string, peppy::Runner> PpyRunners()
{
	std::map<std::string, peppy::Runner> r;
	r["script.run"] = [](const Json& p) -> Json {
		std::string code = peppy::Str(p, "code");
		if (code.empty()) throw peppy::InvalidParams("missing 'code'");
		// The script's last value, as JSON when it is JSON, else as text.
		std::string out = PpyRunScript(code);
		try { return Json::parse(out); } catch (...) { return out; }
	};
	// The library exports the page as a PNG; it goes back to the agent as a picture
	// (JPEG, at most 1024 px). With a path, the full-size PNG stays there too.
	r["view.screenshot"] = [](const Json& p) {
		Json q = p;
		if (q.contains("path")) q["path"] = peppy::OutputPath(peppy::Str(p, "path"));
		Json shot = CallLibrary("view.screenshot", q);
		std::string path = shot.value("path", "");
		bool temporary = shot.value("temporary", false);
		Json image;
		try { image = PpyInlineImage(path); }
		catch (...) { if (temporary) std::remove(path.c_str()); throw; }
		if (temporary) { std::remove(path.c_str()); shot.erase("path"); }
		shot.erase("temporary");
		shot["image"] = image;
		return shot;
	};
	// Without a path, export shows the agent a page instead of writing a file.
	r["document.export"] = [](const Json& p) -> Json {
		if (peppy::Str(p, "path").empty()) {
			Json q = {{"resolution", 144}};
			if (p.contains("document")) q["document"] = p["document"];
			std::string page = peppy::Str(p, "page");
			if (page.empty()) {   // the first page of 'pages' ("3-5, 8" -> "3")
				std::string pages = peppy::Str(p, "pages");
				page = pages.substr(0, pages.find_first_of(",-"));
				page.erase(0, page.find_first_not_of(' '));
				page.erase(page.find_last_not_of(' ') + 1);
				if (page == "all") page.clear();
			}
			if (!page.empty()) q["page"] = page;
			return peppy::Find("view.screenshot")->run(q);
		}
		Json q = p;
		q["path"] = peppy::OutputPath(peppy::Str(p, "path"));
		return CallLibrary("document.export", q);
	};
	r["document.save"] = [](const Json& p) {
		Json q = p;
		if (!peppy::Str(p, "path").empty()) q["path"] = peppy::OutputPath(peppy::Str(p, "path"));
		return CallLibrary("document.save", q);
	};
	r["app.info"] = [](const Json& p) {
		Json info = CallLibrary("app.info", p);
		info["peppy"] = kPpyVersion;
		std::string why;
		info["ready"] = peppy::GetHost()->Ready(why);
		return info;
	};
	return r;
}

// Everything else: the library.
peppy::Runner PpyLibraryRunner(const std::string& method)
{
	return [method](const Json& p) { return CallLibrary(method, p); };
}

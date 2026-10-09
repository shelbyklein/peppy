// Peppy's command runners for InDesign. A few are native; the rest call
// peppy.jsx, Peppy's library in InDesign's own scripting DOM, loaded once into
// a persistent engine ("peppy") and called in-process on the main thread.

#include "VCPlugInHeaders.h"

#include "Utils.h"

#include "PpyHost.h"
#include "PpyID.h"

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

std::string Base64(const std::string& in)
{
	static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve((in.size() + 2) / 3 * 4);
	size_t i = 0;
	for (; i + 2 < in.size(); i += 3) {
		unsigned v = (unsigned char) in[i] << 16 | (unsigned char) in[i + 1] << 8 | (unsigned char) in[i + 2];
		out += t[v >> 18]; out += t[(v >> 12) & 63]; out += t[(v >> 6) & 63]; out += t[v & 63];
	}
	if (i < in.size()) {
		unsigned v = (unsigned char) in[i] << 16 | (i + 1 < in.size() ? (unsigned char) in[i + 1] << 8 : 0);
		out += t[v >> 18]; out += t[(v >> 12) & 63];
		out += i + 1 < in.size() ? t[(v >> 6) & 63] : '=';
		out += '=';
	}
	return out;
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
	// The library exports the page; the image goes back to the agent to look at.
	r["view.screenshot"] = [](const Json& p) {
		Json shot = CallLibrary("view.screenshot", p);
		std::string path = shot.value("path", "");
		std::ifstream in(path, std::ios::binary);
		if (!in) throw peppy::Error(peppy::kErrHost, "InDesign didn't write the screenshot");
		std::string png((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		in.close();
		if (shot.value("temporary", false)) { std::remove(path.c_str()); shot.erase("path"); }
		shot.erase("temporary");
		shot["pngBase64"] = Base64(png);
		return shot;
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

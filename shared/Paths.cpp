#include "Paths.h"

#include <cstdlib>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#endif

namespace peppy {

#ifdef _WIN32
const char* kOutputPathHelp = "give a full path to a folder that exists on this PC (C:\\Users\\..., ~\\...), or just a file name to save it on the Desktop";
#else
const char* kOutputPathHelp = "give a full path to a folder that exists on this Mac (/Users/..., ~/...), or just a file name to save it on the Desktop";
#endif

namespace {

#ifdef _WIN32
const char kSep = '\\';
bool IsFolder(const std::string& path)
{
	struct _stat64 s;
	std::string p = path.size() == 2 && path[1] == ':' ? path + "\\" : path;   // "C:" alone is the current folder on C
	return !p.empty() && _stat64(p.c_str(), &s) == 0 && (s.st_mode & _S_IFDIR);
}
std::string Home()
{
	const char* h = std::getenv("USERPROFILE");
	return h ? h : "";
}
// The real Desktop: often not %USERPROFILE%\Desktop (OneDrive moves it).
std::string Desktop()
{
	PWSTR wide = nullptr;
	std::string out;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &wide))) {
		int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
		if (n > 1) { out.resize(n - 1); WideCharToMultiByte(CP_UTF8, 0, wide, -1, &out[0], n, nullptr, nullptr); }
	}
	CoTaskMemFree(wide);
	return out.empty() ? Home() + "\\Desktop" : out;
}
bool IsAbsolute(const std::string& p) { return (p.size() > 2 && p[1] == ':' && (p[2] == '\\' || p[2] == '/')) || p.rfind("\\\\", 0) == 0; }
#else
const char kSep = '/';
bool IsFolder(const std::string& path)
{
	struct stat s;
	return !path.empty() && stat(path.c_str(), &s) == 0 && S_ISDIR(s.st_mode);
}
std::string Home()
{
	const char* h = std::getenv("HOME");
	return h ? h : "";
}
std::string Desktop() { return Home() + "/Desktop"; }
bool IsAbsolute(const std::string& p) { return !p.empty() && p[0] == '/'; }
#endif

} // namespace

std::string OutputPath(const std::string& given)
{
	if (given.empty()) throw InvalidParams("no path given", kOutputPathHelp);
	std::string path = given;
#ifdef _WIN32
	for (char& c : path) if (c == '/') c = '\\';
#endif
	if (path.find(kSep) == std::string::npos && !(path.size() > 1 && path[1] == ':')) path = Desktop() + kSep + path;
	else if (path.size() > 1 && path[0] == '~' && path[1] == kSep) path = Home() + path.substr(1);
	if (!IsAbsolute(path)) throw InvalidParams("'" + given + "' isn't a full path", kOutputPathHelp);
	if (path.back() == kSep) throw InvalidParams("'" + given + "' names a folder, not a file", kOutputPathHelp);
	std::string folder = path.substr(0, path.find_last_of(kSep));
	if (folder.empty()) folder = std::string(1, kSep);
	if (!IsFolder(folder)) throw InvalidParams("there's no folder '" + folder + "' on this computer", kOutputPathHelp);
	return path;
}

} // namespace peppy

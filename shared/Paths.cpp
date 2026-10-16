#include "Paths.h"

#include <cstdlib>
#include <sys/stat.h>

namespace peppy {

const char* kOutputPathHelp = "give a full path to a folder that exists on this Mac (/Users/..., ~/...), or just a file name to save it on the Desktop";

namespace {

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

} // namespace

std::string OutputPath(const std::string& given)
{
	if (given.empty()) throw InvalidParams("no path given", kOutputPathHelp);
	std::string path = given;
	if (path.find('/') == std::string::npos) path = Home() + "/Desktop/" + path;
	else if (path.rfind("~/", 0) == 0) path = Home() + path.substr(1);
	if (path[0] != '/') throw InvalidParams("'" + given + "' isn't a full path", kOutputPathHelp);
	std::string folder = path.substr(0, path.find_last_of('/'));
	if (folder.empty()) folder = "/";
	if (!IsFolder(folder)) throw InvalidParams("there's no folder '" + folder + "' on this Mac", kOutputPathHelp);
	if (path.back() == '/') throw InvalidParams("'" + given + "' names a folder, not a file", kOutputPathHelp);
	return path;
}

} // namespace peppy

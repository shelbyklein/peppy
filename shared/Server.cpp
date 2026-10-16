#include "Server.h"
#include "Dispatch.h"
#include "Mcp.h"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <direct.h>
#include <process.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <map>
#include <random>
#include <sstream>

namespace peppy {

namespace {

// Sockets: a SOCKET on Windows, an int elsewhere; both fit in intptr_t.
#ifdef _WIN32
using Socket = SOCKET;
const Socket kNoSocket = INVALID_SOCKET;
void CloseSocket(intptr_t fd) { closesocket((Socket) fd); }
void MakeFolder(const std::string& path) { _mkdir(path.c_str()); }
void MakePrivate(const std::string&) {}   // %APPDATA% is already the user's own
int ProcessId() { return _getpid(); }
#else
using Socket = int;
const Socket kNoSocket = -1;
void CloseSocket(intptr_t fd) { close((int) fd); }
void MakeFolder(const std::string& path) { mkdir(path.c_str(), 0700); }
void MakePrivate(const std::string& path) { chmod(path.c_str(), 0600); }
int ProcessId() { return (int) getpid(); }
#endif

const size_t kMaxBody = 64 * 1024 * 1024;

std::string Lower(std::string s)
{
	for (char& c : s) c = (char) std::tolower((unsigned char) c);
	return s;
}

std::string Trim(const std::string& s)
{
	size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
	return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::string MakeToken()
{
	std::random_device rd;
	std::ostringstream o;
	for (int i = 0; i < 24; i++) o << std::hex << ((rd() >> 4) & 0xf) << (rd() & 0xf);
	return o.str();
}

bool SendAll(intptr_t fd, const std::string& data)
{
	size_t sent = 0;
	while (sent < data.size()) {
		long long n = send((Socket) fd, data.data() + sent, (int) std::min<size_t>(data.size() - sent, 1 << 30), 0);
		if (n <= 0) return false;
		sent += (size_t) n;
	}
	return true;
}

void Respond(intptr_t fd, int status, const std::string& body, const std::string& type = "application/json")
{
	const char* reason = status == 200 ? "OK" : status == 202 ? "Accepted" : status == 400 ? "Bad Request" : status == 401 ? "Unauthorized"
		: status == 403 ? "Forbidden" : status == 404 ? "Not Found" : status == 405 ? "Method Not Allowed" : status == 413 ? "Payload Too Large" : "Error";
	std::ostringstream o;
	o << "HTTP/1.1 " << status << " " << reason << "\r\n"
	  << "Content-Type: " << type << "\r\n"
	  << "Content-Length: " << body.size() << "\r\n"
	  << "Cache-Control: no-store\r\n"
	  << "Connection: close\r\n\r\n"
	  << body;
	SendAll(fd, o.str());
}

std::string Unauthorized()
{
	return Json{{"jsonrpc", "2.0"}, {"id", nullptr}, {"error", {{"code", -32001},
		{"message", "Peppy needs its token. Ask the person at InDesign for Peppy's URL: it's on the clipboard after they click "
		            "Copy connection in Peppy's panel (Window > Peppy), and it has the token in it."}}}}.dump();
}

} // namespace

std::string Server::Folder()
{
#ifdef _WIN32
	const char* appData = std::getenv("APPDATA");
	std::string base = std::string(appData ? appData : "C:") + "\\Peppy";   // %APPDATA%\Peppy
#else
	const char* home = std::getenv("HOME");
	std::string base = std::string(home ? home : "/tmp") + "/Library/Application Support/Peppy";
#endif
	MakeFolder(base);
	const char* custom = std::getenv("PEPPY_HOME");   // the mock host keeps its own
	std::string dir = custom && *custom ? custom : base;
	MakeFolder(dir);
	return dir;
}

std::string Server::McpUrl() const
{
	return "http://127.0.0.1:" + std::to_string(fPort) + "/mcp/" + fToken;
}

bool Server::Start(int firstPort, const std::string& version, std::string& error)
{
	fVersion = version;
	std::string tokenPath = Folder() + "/token";
	{
		std::ifstream in(tokenPath);
		std::getline(in, fToken);
		fToken = Trim(fToken);
	}
	if (fToken.size() < 32) {
		fToken = MakeToken();
		std::ofstream out(tokenPath, std::ios::trunc);
		out << fToken << "\n";
		out.close();
		MakePrivate(tokenPath);
	}

#ifdef _WIN32
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { error = "Winsock didn't start"; return false; }
#endif
	for (int port = firstPort; port < firstPort + 10; port++) {
		Socket fd = socket(AF_INET, SOCK_STREAM, 0);
		if (fd == kNoSocket) { error = "socket() failed"; return false; }
		int yes = 1;
#ifdef _WIN32
		// Not SO_REUSEADDR: on Windows that lets another program take the port too.
		setsockopt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*) &yes, sizeof yes);
		SetHandleInformation((HANDLE) fd, HANDLE_FLAG_INHERIT, 0);
#else
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
		fcntl(fd, F_SETFD, FD_CLOEXEC);
#endif
		sockaddr_in a{};
		a.sin_family = AF_INET;
		a.sin_port = htons((uint16_t) port);
		a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		if (bind(fd, (sockaddr*) &a, sizeof a) == 0 && listen(fd, 16) == 0) {
			fListen = (intptr_t) fd;
			fPort = port;
			break;
		}
		CloseSocket((intptr_t) fd);
	}
	if (fListen < 0) { error = "no free port from " + std::to_string(firstPort); return false; }
	WriteSession();
	fAccept = std::thread([this] { AcceptLoop(); });
	return true;
}

void Server::Stop()
{
	fStopping = true;
#ifdef _WIN32
	if (fListen >= 0) { shutdown((Socket) fListen, SD_BOTH); CloseSocket(fListen); fListen = -1; }
#else
	if (fListen >= 0) { shutdown((Socket) fListen, SHUT_RDWR); CloseSocket(fListen); fListen = -1; }
#endif
	if (fAccept.joinable()) fAccept.join();
	// Give in-flight requests a moment; they hold no InDesign state of their own.
	for (int i = 0; i < 50 && fActive > 0; i++) std::this_thread::sleep_for(std::chrono::milliseconds(20));
	std::remove((Folder() + "/session.json").c_str());
}

void Server::WriteSession()
{
	Json s = {{"app", "InDesign"}, {"version", fVersion}, {"port", fPort}, {"pid", ProcessId()},
		{"mcp", McpUrl()}, {"rpc", "http://127.0.0.1:" + std::to_string(fPort) + "/rpc/" + fToken}, {"token", fToken}};
	std::string path = Folder() + "/session.json";
	std::ofstream out(path, std::ios::trunc);
	out << s.dump(2) << "\n";
	out.close();
	MakePrivate(path);
}

void Server::AcceptLoop()
{
	while (!fStopping) {
		Socket client = accept((Socket) fListen, nullptr, nullptr);
		if (client == kNoSocket) {
			if (fStopping) break;
			continue;
		}
		intptr_t fd = (intptr_t) client;
		int one = 1;
		setsockopt(client, IPPROTO_TCP, TCP_NODELAY, (const char*) &one, sizeof one);
#ifdef _WIN32
		DWORD timeout = 30000;   // ms
		setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char*) &timeout, sizeof timeout);
#else
		setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
		timeval tv{30, 0};
		setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#endif
		fActive++;
		std::thread([this, fd] {
			Serve(fd);
			CloseSocket(fd);
			fActive--;
		}).detach();
	}
}

void Server::Serve(intptr_t fd)
{
	// Read the head.
	std::string buf;
	char chunk[16384];
	size_t headEnd;
	while ((headEnd = buf.find("\r\n\r\n")) == std::string::npos) {
		int n = (int) recv((Socket) fd, chunk, (int) sizeof chunk, 0);
		if (n <= 0 || buf.size() > 64 * 1024) return;
		buf.append(chunk, (size_t) n);
	}
	std::istringstream head(buf.substr(0, headEnd));
	std::string requestLine, verb, target;
	std::getline(head, requestLine);
	std::istringstream(requestLine) >> verb >> target;
	std::map<std::string, std::string> headers;
	for (std::string line; std::getline(head, line);) {
		size_t colon = line.find(':');
		if (colon != std::string::npos) headers[Lower(Trim(line.substr(0, colon)))] = Trim(line.substr(colon + 1));
	}
	size_t length = headers.count("content-length") ? std::strtoull(headers["content-length"].c_str(), nullptr, 10) : 0;
	if (length > kMaxBody) { Respond(fd, 413, "{}"); return; }
	std::string body = buf.substr(headEnd + 4);
	while (body.size() < length) {
		int n = (int) recv((Socket) fd, chunk, (int) sizeof chunk, 0);
		if (n <= 0) return;
		body.append(chunk, (size_t) n);
	}
	body.resize(length);

	std::string path = target, query;
	if (size_t q = path.find('?'); q != std::string::npos) { query = path.substr(q + 1); path = path.substr(0, q); }

	if (headers.count("origin")) { Respond(fd, 403, R"({"error":"browsers can't call Peppy"})"); return; }
	if (path == "/health") {
		Respond(fd, 200, Json{{"ok", true}, {"app", "Peppy for InDesign"}, {"version", fVersion}}.dump());
		return;
	}

	// /mcp/<token> or /rpc/<token>, or the token in a header.
	std::string route = path, token;
	for (const char* r : {"/mcp/", "/rpc/"}) {
		if (path.rfind(r, 0) == 0) { route = std::string(r, 4); token = path.substr(5); }
	}
	if (token.empty()) {
		std::string auth = headers["authorization"];
		if (Lower(auth).rfind("bearer ", 0) == 0) token = Trim(auth.substr(7));
		else token = headers["x-peppy-token"];
	}
	if (route != "/mcp" && route != "/rpc") { Respond(fd, 404, R"({"error":"not found"})"); return; }
	if (token != fToken) { Respond(fd, 401, Unauthorized()); return; }
	if (verb == "GET" || verb == "DELETE") { Respond(fd, 405, "{}"); return; }   // no SSE stream, no sessions
	if (verb != "POST") { Respond(fd, 405, "{}"); return; }

	Json request;
	try { request = Json::parse(body); }
	catch (...) {
		Respond(fd, 400, Json{{"jsonrpc", "2.0"}, {"id", nullptr}, {"error", {{"code", kErrParse}, {"message", "body isn't JSON"}}}}.dump());
		return;
	}
	std::string userAgent = headers["user-agent"];

	if (route == "/rpc") {
		double timeout = 60;
		if (request.is_object() && request.contains("timeout") && request["timeout"].is_number()) timeout = request["timeout"].get<double>();
		Respond(fd, 200, Dispatch(request, AgentFor(userAgent), timeout).dump());
		return;
	}

	bool allTools = query.find("tools=all") != std::string::npos;
	if (request.is_array()) {   // a JSON-RPC batch of MCP messages
		Json out = Json::array();
		for (const Json& m : request) {
			Json r = HandleMcp(m, allTools, userAgent);
			if (!r.is_null()) out.push_back(r);
		}
		if (out.empty()) Respond(fd, 202, "");
		else Respond(fd, 200, out.dump());
		return;
	}
	Json r = HandleMcp(request, allTools, userAgent);
	if (r.is_null()) Respond(fd, 202, "");
	else Respond(fd, 200, r.dump());
}

} // namespace peppy

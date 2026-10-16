#pragma once

// Loopback HTTP server for agents. Lives on its own threads and never touches
// InDesign: every request goes through Dispatch, which hands the work to the
// main thread.
//
//   GET  /health        {"ok":true,...}            no token needed, reveals nothing
//   POST /rpc/<token>   JSON-RPC: one call or an array (a batch)
//   POST /mcp/<token>   MCP (streamable HTTP, plain JSON responses)
// The token can also come as "Authorization: Bearer <token>" or "X-Peppy-Token".
// Requests with an Origin header (browsers) are refused, so a web page can't
// drive InDesign.
//
// The token is made once and kept in ~/Library/Application Support/Peppy/token
// (mode 0600; %APPDATA%\Peppy\token on Windows), so saved agent configs keep working; session.json next to it
// has the current URLs.

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace peppy {

class Server {
public:
	// Binds 127.0.0.1 on the first free port from firstPort (10 tries).
	bool Start(int firstPort, const std::string& version, std::string& error);
	void Stop();

	int Port() const { return fPort; }
	const std::string& Token() const { return fToken; }
	std::string McpUrl() const;

	static std::string Folder();   // ~/Library/Application Support/Peppy

private:
	intptr_t fListen = -1;   // a socket (SOCKET on Windows)
	int fPort = 0;
	std::string fToken, fVersion;
	std::thread fAccept;
	std::atomic<bool> fStopping{false};
	std::atomic<int> fActive{0};

	void AcceptLoop();
	void Serve(intptr_t fd);
	void WriteSession();
};

} // namespace peppy

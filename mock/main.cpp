// peppy-mock: the real server, MCP, dispatcher and catalog on the mock document,
// in a terminal. For testing agents and the protocol without the SDK or the app.
//
//   make mock && build/peppy-mock            # serves on 127.0.0.1:7343 by default (PEPPY_PORT)

#include "Mock.h"
#include "../shared/Dispatch.h"
#include "../shared/Mcp.h"
#include "../shared/Server.h"

#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <thread>

using namespace peppy;

namespace {

// A worker thread standing in for InDesign's main thread.
class Worker {
public:
	Worker() : fThread([this] { Loop(); }) {}
	void Post(std::function<void()> fn)
	{
		std::lock_guard<std::mutex> lock(fMutex);
		fQueue.push_back(std::move(fn));
		fCv.notify_one();
	}

private:
	std::mutex fMutex;
	std::condition_variable fCv;
	std::deque<std::function<void()>> fQueue;
	std::thread fThread;

	void Loop()
	{
		for (;;) {
			std::function<void()> fn;
			{
				std::unique_lock<std::mutex> lock(fMutex);
				fCv.wait(lock, [this] { return !fQueue.empty(); });
				fn = std::move(fQueue.front());
				fQueue.pop_front();
			}
			fn();
		}
	}
};

Worker& MainThread()
{
	static Worker w;
	return w;
}

} // namespace

int main()
{
	std::signal(SIGPIPE, SIG_IGN);
	// Its own token and session file, apart from the real plug-in's.
	setenv("PEPPY_HOME", (std::string(std::getenv("HOME")) + "/Library/Application Support/Peppy/mock").c_str(), 0);
	SetHost(MakeMockHost([](std::function<void()> fn) { MainThread().Post(std::move(fn)); }));
	RegisterCatalog(MockRunners());
	SetMcpInfo({"Peppy for Adobe InDesign (mock)", PEPPY_VERSION, Instructions(), Everyday()});
	SetCallObserver([](const CallEvent& e) {
		std::printf("%s %-18s %6.1f ms  %s\n", e.ok ? (e.edit ? "✎" : "·") : "✗", e.method.c_str(), e.milliseconds,
			e.line.substr(0, e.line.find('\n')).c_str());
		std::fflush(stdout);
	});

	const char* portEnv = std::getenv("PEPPY_PORT");
	Server server;
	std::string error;
	if (!server.Start(portEnv ? std::atoi(portEnv) : 7343, PEPPY_VERSION "-mock", error)) {
		std::fprintf(stderr, "peppy-mock: %s\n", error.c_str());
		return 1;
	}
	std::printf("Peppy mock listening: %s\n", server.McpUrl().c_str());
	std::fflush(stdout);
	for (;;) std::this_thread::sleep_for(std::chrono::hours(1));
}

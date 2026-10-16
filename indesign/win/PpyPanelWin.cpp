// Windows, for now: no docked panel yet. Window > Peppy copies Peppy's
// connection URL to the clipboard and says so; calls are counted for that note.

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../PpyPanel.h"
#include "../../shared/Dispatch.h"

#include <atomic>
#include <string>

#pragma comment(lib, "user32.lib")

namespace {

std::string gUrl, gStatus = "Peppy is starting";
std::atomic<int> gCalls{0}, gErrors{0};

std::wstring Wide(const std::string& s)
{
	int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
	std::wstring w(n > 0 ? n - 1 : 0, L'\0');
	if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
	return w;
}

bool CopyToClipboard(const std::wstring& text)
{
	if (!OpenClipboard(nullptr)) return false;
	EmptyClipboard();
	size_t bytes = (text.size() + 1) * sizeof(wchar_t);
	HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
	bool ok = false;
	if (memory) {
		memcpy(GlobalLock(memory), text.c_str(), bytes);
		GlobalUnlock(memory);
		ok = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
		if (!ok) GlobalFree(memory);
	}
	CloseClipboard();
	return ok;
}

} // namespace

void PpyPanelAttach()
{
	peppy::SetCallObserver([](const peppy::CallEvent& e) {
		gCalls++;
		if (!e.ok) gErrors++;
	});
}

void PpyPanelSetServer(bool listening, int port, const std::string& url, const std::string& error)
{
	gUrl = url;
	gStatus = listening ? "Listening on 127.0.0.1:" + std::to_string(port) : "Not listening: " + error;
}

void PpyPanelToggle()
{
	std::string message = gStatus + "\n\n";
	if (!gUrl.empty() && CopyToClipboard(Wide(gUrl)))
		message += "Peppy's connection is on the clipboard. Give it to your agent as an MCP server (streamable HTTP):\n\n" + gUrl;
	else message += gUrl;
	message += "\n\n" + std::to_string(gCalls.load()) + " calls, " + std::to_string(gErrors.load()) + " errors so far.";
	MessageBoxW(GetActiveWindow(), Wide(message).c_str(), L"Peppy", MB_OK | MB_ICONINFORMATION);
}

void PpyPanelShutdown()
{
	peppy::SetCallObserver(nullptr);
}

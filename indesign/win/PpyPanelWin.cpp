// Peppy's panel in InDesign on Windows: the GDI+ view (PeppyPanelWin.cpp) in
// a floating tool window that stays above InDesign. Window > Peppy shows and
// hides it; it remembers where it was and whether it was open. Right-click it
// for Pause agents. Main thread only. (Adapted from Slippy's Photoshop host.)

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../PpyPanel.h"
#include "PeppyPanelWin.h"
#include "PpyVersion.h"
#include "../../shared/Dispatch.h"
#include "../../shared/Server.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")

namespace {

const wchar_t* kFrameClass = L"PeppyInDesignPanel";
const UINT kPauseItem = 1;

HWND gFrame = nullptr;
bool gRegistered = false;
std::string gUrl, gStatus = "Starting...";
bool gListening = false, gPaused = false;

HINSTANCE ThisModule()
{
	HMODULE module = nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR) &ThisModule, &module);
	return module;
}

// InDesign's main window: this process's largest visible top-level window.
HWND InDesignWindow()
{
	struct Find { HWND best = nullptr; LONG area = 0; } find;
	EnumWindows([](HWND w, LPARAM lp) -> BOOL {
		DWORD pid = 0;
		GetWindowThreadProcessId(w, &pid);
		if (pid != GetCurrentProcessId() || !IsWindowVisible(w) || GetWindow(w, GW_OWNER) || w == gFrame) return TRUE;
		RECT r;
		GetWindowRect(w, &r);
		Find* f = (Find*) lp;
		LONG area = (r.right - r.left) * (r.bottom - r.top);
		if (area > f->area) { f->area = area; f->best = w; }
		return TRUE;
	}, (LPARAM) &find);
	return find.best;
}

// The window's place and whether it's open, between launches.
std::string StatePath() { return peppy::Server::Folder() + "\\panel-window.txt"; }

void SaveState()
{
	if (!gFrame) return;
	RECT r;
	GetWindowRect(gFrame, &r);
	std::ofstream out(StatePath(), std::ios::trunc);
	out << r.left << " " << r.top << " " << (r.right - r.left) << " " << (r.bottom - r.top) << " " << (IsWindowVisible(gFrame) ? 1 : 0);
}

bool LoadState(RECT& r, bool& open)
{
	std::ifstream in(StatePath());
	int x, y, w, h, o;
	if (!(in >> x >> y >> w >> h >> o)) return false;
	r = {x, y, x + std::max(240, w), y + std::max(300, h)};
	open = o != 0;
	return true;
}

LRESULT CALLBACK FrameProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg) {
	case WM_SIZE:
		peppy::winpanel::Fit(hwnd);
		return 0;
	case WM_GETMINMAXINFO: {
		MINMAXINFO* m = (MINMAXINFO*) lp;
		m->ptMinTrackSize = {240, 300};
		return 0;
	}
	case WM_CLOSE:   // hide, keeping the panel; Window > Peppy brings it back
		ShowWindow(hwnd, SW_HIDE);
		SaveState();
		return 0;
	case WM_EXITSIZEMOVE:
		SaveState();
		return 0;
	case WM_CONTEXTMENU: {
		HMENU menu = CreatePopupMenu();
		AppendMenuW(menu, MF_STRING | (gPaused ? MF_CHECKED : 0), kPauseItem, L"Pause agents");
		POINT pt = {(short) LOWORD(lp), (short) HIWORD(lp)};
		if (pt.x == -1 && pt.y == -1) GetCursorPos(&pt);
		UINT chose = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
		DestroyMenu(menu);
		if (chose == kPauseItem) {
			gPaused = !gPaused;
			peppy::SetPaused(gPaused);
			peppy::winpanel::SetPaused(gPaused);
			if (!gPaused) peppy::winpanel::SetStatus(gStatus, gListening);
		}
		return 0;
	}
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

void Build()
{
	if (gFrame) return;
	if (!gRegistered) {
		WNDCLASSEXW wc = {sizeof wc};
		wc.lpfnWndProc = FrameProc;
		wc.hInstance = ThisModule();
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.hbrBackground = CreateSolidBrush(RGB(0x32, 0x32, 0x32));
		wc.lpszClassName = kFrameClass;
		gRegistered = RegisterClassExW(&wc) != 0;
	}
	RECT r;
	bool open = true;
	if (!LoadState(r, open)) {   // the first time: inside InDesign's window, near its top-right corner
		RECT host = {0, 0, 1280, 800};
		if (HWND id = InDesignWindow()) GetWindowRect(id, &host);
		r = {host.right - 290 - 340, host.top + 140, host.right - 340, host.top + 140 + 480};
	}
	gFrame = CreateWindowExW(WS_EX_TOOLWINDOW, kFrameClass, L"Peppy", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN,
		r.left, r.top, r.right - r.left, r.bottom - r.top, InDesignWindow(), nullptr, ThisModule(), nullptr);
	if (!gFrame) return;
	peppy::winpanel::SetGroups({"document", "page", "layer", "item", "frame", "text", "style", "image", "history", "app"});
	peppy::winpanel::Options o;
	o.connectionInfo = [] { return gUrl; };
	o.version = kPpyVersion;
	peppy::winpanel::Create(gFrame, o);
	peppy::winpanel::SetStatus(gStatus, gListening);
	peppy::winpanel::SetPaused(gPaused);
}

void Show()
{
	Build();
	if (!gFrame) return;
	ShowWindow(gFrame, SW_SHOWNOACTIVATE);
	SaveState();
}

} // namespace

void PpyPanelAttach()
{
	peppy::SetCallObserver([](const peppy::CallEvent& e) {   // main thread
		peppy::winpanel::Call(e.method, e.ok, e.edit, e.milliseconds, e.line, e.agent);
	});
	// Open on the first launch, then as the person left it.
	RECT r;
	bool open = true;
	if (!LoadState(r, open) || open) Show();
}

void PpyPanelSetServer(bool listening, int port, const std::string& url, const std::string& error)
{
	gUrl = url;
	gListening = listening;
	gStatus = listening ? "Listening on 127.0.0.1:" + std::to_string(port) : "Not listening: " + error;
	peppy::winpanel::SetStatus(gStatus, gListening);
}

void PpyPanelToggle()
{
	if (gFrame && IsWindowVisible(gFrame)) { ShowWindow(gFrame, SW_HIDE); SaveState(); }
	else Show();
}

void PpyPanelShutdown()
{
	peppy::SetCallObserver(nullptr);
	SaveState();
	peppy::winpanel::Destroy();
	if (gFrame) { DestroyWindow(gFrame); gFrame = nullptr; }
	if (gRegistered) { UnregisterClassW(kFrameClass, ThisModule()); gRegistered = false; }
}

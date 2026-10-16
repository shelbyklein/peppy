#ifndef __PEPPY_PANEL_WIN_H__
#define __PEPPY_PANEL_WIN_H__

// Peppy's panel on Windows (GDI+), free of any Adobe SDK: one view, made as
// a child of the window that hosts it (PpyPanelWin.cpp's floating window).
// Main thread only. Adapted from Slippy's.

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

namespace peppy {
namespace winpanel {

struct Options {
	std::function<std::string()> connectionInfo;                    // what "Copy connection" copies
	std::function<void(bool open, double extraHeight)> onDrawer;    // grow / shrink the host for the drawer
	// The host's panel background (0-1 each) and whether its theme is dark;
	// false (or none) keeps the default dark gray.
	std::function<bool(double rgb[3], bool& dark)> theme;
	std::string version;   // "1.2.0", shown as "v.1.2"
};

bool Create(HWND parent, Options options);   // the view, filling 'parent' (once; later calls just Fit)
bool Exists();
void Fit(HWND parent);   // fill 'parent' again (after it's resized)
void Destroy();
// The ten command groups the bars stand for, before Create (a method's group
// is its first word: "layer.set" -> "layer"; the last group takes the rest).
void SetGroups(const std::vector<std::string>& groups);
void SetStatus(const std::string& text, bool listening);
void SetPaused(bool paused);
void Call(const std::string& method, bool ok, bool changesDocument, double milliseconds, const std::string& line, const std::string& agent);

} // namespace winpanel
} // namespace peppy

#endif // __PEPPY_PANEL_WIN_H__

#pragma once

// The panel window (Cocoa, PpyPanel.mm), called from the plug-in. Main thread.

#include <string>

void PpyPanelAttach();     // makes the panel (hidden until shown, unless it was open last time) and feeds it calls
void PpyPanelSetServer(bool listening, int port, const std::string& url, const std::string& error);
void PpyPanelToggle();
void PpyPanelShutdown();

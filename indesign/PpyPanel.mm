// Peppy's panel inside InDesign: a floating utility window with Peppy, the
// bars, the feed and Copy connection. Shown with Window > Peppy; it remembers
// whether it was open and where, and comes back that way on the next launch.

#import "PeppyPanelView.h"
#import "Terminal.h"

#include "PpyPanel.h"
#include "../shared/Dispatch.h"
#include "../shared/Server.h"

namespace {

NSPanel* gWindow;
PeppyPanelView* gPanel;
NSString* gUrl = @"";
NSString* const kOpenKey = @"PeppyPanelOpen";
NSString* const kFrameName = @"PeppyPanel";

void MakeWindow()
{
	if (gWindow) return;
	gWindow = [[NSPanel alloc] initWithContentRect:NSMakeRect(80, 400, 290, 480)
		styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable | NSWindowStyleMaskUtilityWindow
		backing:NSBackingStoreBuffered defer:YES];
	gWindow.title = @"Peppy";
	gWindow.floatingPanel = YES;
	gWindow.hidesOnDeactivate = YES;
	gWindow.releasedWhenClosed = NO;
	gWindow.becomesKeyOnlyIfNeeded = YES;
	gWindow.minSize = NSMakeSize(240, 300);
	[gWindow setFrameUsingName:kFrameName];
	gWindow.frameAutosaveName = kFrameName;

	NSString* resources = [[[NSBundle bundleWithIdentifier:@"com.peppy.InDesign.Peppy"] resourcePath] stringByAppendingPathComponent:@"terminal"];
	if (resources) PeppyTerminalView.resourceDirectory = resources;
	PeppyTerminal.shared.stateDirectory = [[NSString stringWithUTF8String:peppy::Server::Folder().c_str()] stringByAppendingPathComponent:@"terminal"];

	[PeppyPanelView setGroups:@[@"document", @"page", @"layer", @"item", @"frame", @"text", @"style", @"image", @"history", @"app"]];
	gPanel = [[PeppyPanelView alloc] initWithFrame:gWindow.contentView.bounds];
	gPanel.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
	gPanel.version = @PEPPY_VERSION;
	gPanel.connectionInfo = ^NSString* { return gUrl; };
	gPanel.onDrawer = ^(BOOL open, CGFloat extra) {
		NSRect f = gWindow.frame;
		f.size.height += open ? extra : -extra;
		f.origin.y -= open ? extra : -extra;
		[gWindow setFrame:f display:YES animate:YES];
	};
	[gWindow.contentView addSubview:gPanel];
	[NSNotificationCenter.defaultCenter addObserverForName:NSWindowWillCloseNotification object:gWindow queue:nil
		usingBlock:^(NSNotification*) { [NSUserDefaults.standardUserDefaults setBool:NO forKey:kOpenKey]; }];
}

void Show()
{
	MakeWindow();
	[gWindow orderFront:nil];
	[NSUserDefaults.standardUserDefaults setBool:YES forKey:kOpenKey];
}

} // namespace

void PpyPanelAttach()
{
	MakeWindow();
	peppy::SetCallObserver([](const peppy::CallEvent& e) {   // main thread
		NSString* method = [NSString stringWithUTF8String:e.method.c_str()];
		NSString* line = [NSString stringWithUTF8String:e.line.c_str()] ?: method;
		[gPanel call:method line:line ok:e.ok edit:e.edit ms:e.milliseconds agent:[NSString stringWithUTF8String:e.agent.c_str()] ?: @""];
	});
	// Open on the first launch, then as the person left it.
	NSUserDefaults* defaults = NSUserDefaults.standardUserDefaults;
	if ([defaults objectForKey:kOpenKey] == nil || [defaults boolForKey:kOpenKey]) Show();
}

void PpyPanelSetServer(bool listening, int port, const std::string& url, const std::string& error)
{
	gUrl = [NSString stringWithUTF8String:url.c_str()];
	PeppyTerminal.shared.environment = @{@"PEPPY_URL": gUrl};
	[gPanel setStatus:listening ? [NSString stringWithFormat:@"Listening on 127.0.0.1:%d", port]
	                            : [NSString stringWithFormat:@"Not listening: %s", error.c_str()] listening:listening];
}

void PpyPanelToggle()
{
	MakeWindow();
	if (gWindow.visible) [gWindow close];
	else Show();
}

void PpyPanelShutdown()
{
	peppy::SetCallObserver(nullptr);
	[PeppyTerminal.shared shutdown];
}

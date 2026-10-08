// `make preview`: Peppy's panel in a plain window, running the real server, MCP
// and dispatcher on the mock document - with the Cocoa main thread standing in
// for InDesign's. Agents can connect to it and Peppy reacts to their calls.
//   build/PeppyPreview                  live: connect an agent (URL is printed, and Copy connection)
//   build/PeppyPreview --demo           plus made-up bursts of calls, with quiet spells (he dozes off)
//   build/PeppyPreview --snapshot DIR   still PNGs of each face (for checking the drawing)

#import "PeppyPanelView.h"
#import "Terminal.h"
#include "../mock/Mock.h"
#include "../shared/Dispatch.h"
#include "../shared/Mcp.h"
#include "../shared/Server.h"

#include <cstdlib>

using namespace peppy;

// A made-up agent session, run through the real dispatcher.
static const char* kSession[] = {
	R"({"method":"document.new","params":{"pages":4}})",
	R"({"method":"document.info"})",
	R"({"method":"frame.text","params":{"x":36,"y":36,"width":540,"height":80,"contents":"Spring Catalog","name":"Headline"}})",
	R"({"method":"frame.rect","params":{"x":36,"y":140,"width":540,"height":320,"name":"Hero photo"}})",
	R"({"method":"item.tree","params":{"page":"1"}})",
	R"({"method":"item.transform","params":{"ids":[200],"translate":[0,12]}})",
	R"({"method":"item.set","params":{"ids":[201],"fill":"Paper","stroke":"Black","strokeWeight":0.5}})",
	R"({"method":"text.set","params":{"id":200,"contents":"Spring Catalog 2027"}})",
	R"({"method":"page.list"})",
	R"({"method":"item.get","params":{"id":999}})",
	R"({"method":"page.add","params":{"count":2}})",
	R"({"method":"item.duplicate","params":{"ids":[201],"translate":[0,340]}})",
	R"({"method":"history.undo"})",
};
static const int kSessionCount = sizeof kSession / sizeof kSession[0];
static const char* kAgents[] = {"Claude", "Claude", "Codex", "Gemini"};

// Off the main thread, like a server thread: Dispatch waits for the main thread.
static void Play(int i)
{
	Json call = Json::parse(kSession[i % kSessionCount]);
	call["jsonrpc"] = "2.0";
	call["id"] = i;
	std::string agent = kAgents[i / 5 % 4];
	dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{ Dispatch(call, agent); });
}

@interface Driver : NSObject
@property (nonatomic) BOOL once;
- (void)burst;
@end

@implementation Driver {
	int _next;
}
- (void)burst
{
	int n = 3 + (int) arc4random_uniform(5);
	for (int i = 0; i < n; i++) {
		int k = _next++;
		dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t) (i * (0.25 + arc4random_uniform(40) / 100.0) * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ Play(k); });
	}
	if (self.once) return;
	double quiet = n * 0.5 + 2 + arc4random_uniform(8);
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t) (quiet * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ [self burst]; });
}
@end

static void Snapshot(NSView* view, NSString* path)
{
	[view layoutSubtreeIfNeeded];
	[view displayIfNeeded];
	[[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
	// Renders the layer tree (model values: what each face settles to), at 2x.
	NSSize size = view.bounds.size;
	NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr pixelsWide:(NSInteger) size.width * 2
		pixelsHigh:(NSInteger) size.height * 2 bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace
		bytesPerRow:0 bitsPerPixel:0];
	NSGraphicsContext* g = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
	CGContextRef ctx = g.CGContext;
	CGContextTranslateCTM(ctx, 0, size.height * 2);
	CGContextScaleCTM(ctx, 2, -2);   // the layers draw y-down here
	[view.window.backgroundColor setFill];
	[NSGraphicsContext saveGraphicsState];
	NSGraphicsContext.currentContext = g;
	NSRectFill(view.bounds);
	[view.effectiveAppearance performAsCurrentDrawingAppearance:^{ [view.layer renderInContext:ctx]; }];
	[NSGraphicsContext restoreGraphicsState];
	[[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:path atomically:YES];
}

int main(int argc, const char* argv[])
{
	@autoreleasepool {
		NSApplication* app = NSApplication.sharedApplication;
		app.activationPolicy = NSApplicationActivationPolicyRegular;

		// The mock document, with the main queue as "InDesign's main thread", and its own token folder.
		setenv("PEPPY_HOME", (std::string(getenv("HOME")) + "/Library/Application Support/Peppy/mock").c_str(), 0);
		SetHost(MakeMockHost([](std::function<void()> fn) {
			auto* f = new std::function<void()>(std::move(fn));
			dispatch_async(dispatch_get_main_queue(), ^{ (*f)(); delete f; });
		}));
		RegisterCatalog(MockRunners());
		SetMcpInfo({"Peppy for Adobe InDesign (preview)", PEPPY_VERSION, Instructions(), Everyday()});
		static Server server;
		std::string error;
		const char* portEnv = getenv("PEPPY_PORT");
		bool listening = server.Start(portEnv ? atoi(portEnv) : 7343, PEPPY_VERSION "-preview", error);

		NSWindow* win = [[NSWindow alloc] initWithContentRect:NSMakeRect(200, 200, 290, 480)
			styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];
		win.title = @"Peppy preview";
		win.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];   // like InDesign's dark UI
		win.backgroundColor = [NSColor colorWithWhite:0.2 alpha:1];
		NSString* url = [NSString stringWithUTF8String:server.McpUrl().c_str()];
		PeppyTerminal.shared.stateDirectory = [NSTemporaryDirectory() stringByAppendingPathComponent:@"PeppyPreview"];
		PeppyTerminal.shared.environment = @{@"PEPPY_URL": url};
		[NSNotificationCenter.defaultCenter addObserverForName:NSApplicationWillTerminateNotification object:nil queue:nil
			usingBlock:^(NSNotification*) { [PeppyTerminal.shared shutdown]; server.Stop(); }];
		[PeppyPanelView setGroups:@[@"document", @"page", @"layer", @"item", @"frame", @"text", @"style", @"image", @"history", @"app"]];
		PeppyPanelView* panel = [[PeppyPanelView alloc] initWithFrame:win.contentView.bounds];
		panel.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
		panel.version = @PEPPY_VERSION;
		[win.contentView addSubview:panel];
		[panel setStatus:listening ? [NSString stringWithFormat:@"Listening on 127.0.0.1:%d", server.Port()]
		                           : [NSString stringWithFormat:@"Not listening: %s", error.c_str()] listening:listening];
		panel.connectionInfo = ^NSString* { return url; };
		panel.onDrawer = ^(BOOL open, CGFloat extra) {
			NSRect f = win.frame;
			f.size.height += open ? extra : -extra;
			f.origin.y -= open ? extra : -extra;
			[win setFrame:f display:YES animate:YES];
		};
		__weak PeppyPanelView* weakPanel = panel;
		SetCallObserver([weakPanel](const CallEvent& e) {   // main thread
			NSString* method = [NSString stringWithUTF8String:e.method.c_str()];
			NSString* line = [NSString stringWithUTF8String:e.line.c_str()] ?: method;
			[weakPanel call:method line:line ok:e.ok edit:e.edit ms:e.milliseconds agent:[NSString stringWithUTF8String:e.agent.c_str()]];
		});
		printf("Peppy preview: %s\n", server.McpUrl().c_str());
		fflush(stdout);

		if (argc == 3 && !strcmp(argv[1], "--snapshot")) {
			NSString* dir = [NSString stringWithUTF8String:argv[2]];
			[NSTimer scheduledTimerWithTimeInterval:0 repeats:NO block:^(NSTimer*) {
				[win makeKeyAndOrderFront:nil];
				[app activateIgnoringOtherApps:YES];
				auto wait = [](double s) { [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:s]]; };
				wait(0.5);
				Snapshot(panel, [dir stringByAppendingPathComponent:@"1-asleep.png"]);
				for (int i = 0; i < 9; i++) Play(i);
				wait(0.35);
				Snapshot(panel, [dir stringByAppendingPathComponent:@"2-waking.png"]);
				wait(1.4);
				Snapshot(panel, [dir stringByAppendingPathComponent:@"3-working.png"]);
				Play(9);   // an error
				wait(0.35);
				Snapshot(panel, [dir stringByAppendingPathComponent:@"4-ouch.png"]);
				server.Stop();
				exit(0);
			}];
			[app run];
			return 0;
		}

		[win makeKeyAndOrderFront:nil];
		[app activateIgnoringOtherApps:YES];
		if (argc == 2 && !strcmp(argv[1], "--demo")) {
			static Driver* driver = [Driver new];
			dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{ [driver burst]; });
		}
		[app run];
	}
	return 0;
}

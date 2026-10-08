// Peppy inside InDesign: the startup service that brings up the server and the
// panel, the idle task that runs agents' calls on the main thread, the undo
// steps, and Window > Peppy.

#include "VCPlugInHeaders.h"

#include "CActionComponent.h"
#include "CIdleTask.h"
#include "CPMUnknown.h"
#include "CmdUtils.h"
#include "ErrorUtils.h"
#include "IApplication.h"
#include "ICommandSequence.h"
#include "IDialogMgr.h"
#include "IIdleTaskMgr.h"
#include "ISession.h"
#include "IStartupShutdownService.h"

#include "PpyID.h"
#include "PpyHost.h"
#include "PpyPanel.h"

#include "../shared/Catalog.h"
#include "../shared/Dispatch.h"
#include "../shared/Mcp.h"
#include "../shared/Server.h"

#include <deque>
#include <mutex>

namespace {

// ---- the main-thread queue, drained by the idle task

std::mutex gQueueMutex;
std::deque<std::function<void()>> gQueue;

bool QueueEmpty()
{
	std::lock_guard<std::mutex> lock(gQueueMutex);
	return gQueue.empty();
}

void Drain()
{
	for (;;) {
		std::function<void()> fn;
		{
			std::lock_guard<std::mutex> lock(gQueueMutex);
			if (gQueue.empty()) return;
			fn = std::move(gQueue.front());
			gQueue.pop_front();
		}
		fn();
	}
}

class InDesignHost : public peppy::Host {
public:
	void Post(std::function<void()> fn) override
	{
		std::lock_guard<std::mutex> lock(gQueueMutex);
		gQueue.push_back(std::move(fn));
	}

	void BeginStep(const std::string& name) override
	{
		fSequence = CmdUtils::BeginCommandSequence();
		if (fSequence) {
			PMString n;
			n.SetUTF8String(name);
			n.SetTranslatable(kFalse);
			fSequence->SetName(n);
		}
	}

	void EndStep(bool commit) override
	{
		if (!fSequence) return;
		// A sequence ends rolled back when the global error code isn't kSuccess.
		if (!commit) ErrorUtils::PMSetGlobalErrorCode(kFailure);
		CmdUtils::EndCommandSequence(fSequence);
		fSequence = nullptr;
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	}

	bool Ready(std::string& why) override
	{
		InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
		InterfacePtr<IDialogMgr> dialogs(app, UseDefaultIID());
		if (dialogs && dialogs->IsModalDialogOpen()) { why = "a dialog is open"; return false; }
		return true;
	}

private:
	ICommandSequence* fSequence = nullptr;
};

InDesignHost gHost;
peppy::Server gServer;

} // namespace

// ---- the idle task

class PpyIdleTask : public CIdleTask {
public:
	PpyIdleTask(IPMUnknown* boss) : CIdleTask(boss) {}
	uint32 RunTask(uint32 appFlags, IdleTimer* timeCheck) override
	{
		// While a dialog is up, Ready() refuses each call with a clear error rather than queueing it.
		Drain();
		return QueueEmpty() ? 20 : 0;   // ms until the next look; 0: more work just arrived
	}
	const char* TaskName() override { return "Peppy"; }
};
CREATE_PMINTERFACE(PpyIdleTask, kPpyIdleTaskImpl)

// ---- startup / shutdown

class PpyStartupShutdown : public CPMUnknown<IStartupShutdownService> {
public:
	PpyStartupShutdown(IPMUnknown* boss) : CPMUnknown<IStartupShutdownService>(boss) {}

	void Startup() override
	{
		peppy::SetHost(&gHost);
		peppy::RegisterCatalog(PpyRunners(), PpyLibraryRunner);
		peppy::SetMcpInfo({"Peppy for Adobe InDesign", kPpyVersion, peppy::Instructions(), peppy::Everyday()});
		PpyPanelAttach();

		const char* portEnv = std::getenv("PEPPY_PORT");
		std::string error;
		bool ok = gServer.Start(portEnv ? std::atoi(portEnv) : 7333, kPpyVersion, error);
		PpyPanelSetServer(ok, gServer.Port(), gServer.McpUrl(), error);

		InterfacePtr<IIdleTask> task(GetExecutionContextSession(), IID_IPPYIDLETASK);
		if (task) task->InstallTask(0);
	}

	void Shutdown() override
	{
		gServer.Stop();
		InterfacePtr<IIdleTask> task(GetExecutionContextSession(), IID_IPPYIDLETASK);
		if (task) task->UninstallTask();
		PpyPanelShutdown();
	}
};
CREATE_PMINTERFACE(PpyStartupShutdown, kPpyStartupShutdownImpl)

// ---- Window > Peppy

class PpyActionComponent : public CActionComponent {
public:
	PpyActionComponent(IPMUnknown* boss) : CActionComponent(boss) {}
	void DoAction(IActiveContext* ac, ActionID actionID, GSysPoint mousePoint, IPMUnknown* widget) override
	{
		if (actionID.Get() == kPpyShowPanelActionID) PpyPanelToggle();
	}
};
CREATE_PMINTERFACE(PpyActionComponent, kPpyActionComponentImpl)

// ExtendScript from C++: runs in InDesign's own JavaScript engine, in-process,
// on the main thread, inside whatever undo step is open.

#include "VCPlugInHeaders.h"

#include "IScriptEngine.h"
#include "IScriptManager.h"
#include "IScriptRunner.h"
#include "IScriptUtils.h"
#include "JavaScriptID.h"
#include "ScriptData.h"
#include "Utils.h"

#include "PpyHost.h"

std::string PpyRunScript(const std::string& code)
{
	InterfacePtr<IScriptManager> manager(Utils<IScriptUtils>()->QueryScriptManager(kJavaScriptMgrBoss));
	if (!manager) throw peppy::Unavailable("InDesign's JavaScript engine isn't available");
	InterfacePtr<IScriptEngine> engine(manager->QueryDefaultEngine());
	InterfacePtr<IScriptRunner> runner(engine, UseDefaultIID());
	if (!runner) throw peppy::Unavailable("InDesign's JavaScript engine isn't available");

	PMString text;
	text.SetUTF8String(code);
	text.SetTranslatable(kFalse);
	ScriptData script(text), result;
	ScriptRecordData args;
	PMString error;
	ErrorCode status = Utils<IScriptUtils>()->DispatchScriptRunner(runner, script, args, result, error, kFalse /*no alert*/);
	if (status != kSuccess || !error.empty()) {
		std::string message = error.GetUTF8String();
		if (message.empty()) message = "the script failed (InDesign error " + std::to_string((long) status) + ")";
		throw peppy::Error(peppy::kErrHost, message, "", {{"idError", (long) status}});
	}
	PMString out;
	if (result.GetType() != ScriptData::s_empty && result.GetPMString(out) == kSuccess) return out.GetUTF8String();
	return "";
}

#pragma once

// Peppy's object-model IDs. The prefix should come from Adobe Developer Support
// for a shipping plug-in; this one is unassigned in the SDK and in InDesign 2026.

#include "SDKDef.h"

#define kPpyCompanyKey		"Peppy"
#define kPpyCompanyValue	"Peppy"
#define kPpyPluginName		"Peppy"
#define kPpyPrefixNumber	0x1C7E00
#define kPpyVersion			"0.1.0"
#define kPpyAuthor			"Shelby Klein"
#define kPpyPrefix			RezLong(kPpyPrefixNumber)
#define kPpyStringPrefix	SDK_DEF_STRINGIZE(kPpyPrefixNumber)

DECLARE_PMID(kPlugInIDSpace, kPpyPluginID, kPpyPrefix + 0)

DECLARE_PMID(kClassIDSpace, kPpyStartupShutdownBoss, kPpyPrefix + 0)
DECLARE_PMID(kClassIDSpace, kPpyActionComponentBoss, kPpyPrefix + 1)

DECLARE_PMID(kInterfaceIDSpace, IID_IPPYIDLETASK, kPpyPrefix + 0)

DECLARE_PMID(kImplementationIDSpace, kPpyStartupShutdownImpl, kPpyPrefix + 0)
DECLARE_PMID(kImplementationIDSpace, kPpyIdleTaskImpl, kPpyPrefix + 1)
DECLARE_PMID(kImplementationIDSpace, kPpyActionComponentImpl, kPpyPrefix + 2)

DECLARE_PMID(kActionIDSpace, kPpyShowPanelActionID, kPpyPrefix + 0)

#define kPpyShowPanelMenuKey	kPpyStringPrefix "kPpyShowPanelMenuKey"
#define kPpyMenuPath			"Main:&Window"

#define kPpyCurrentMajorFormatNumber kSDKDef_30_PersistMajorVersionNumber
#define kPpyCurrentMinorFormatNumber kSDKDef_30_PersistMinorVersionNumber

#include "VCPlugInHeaders.h"
#include "InterfaceFactory.h"

extern bool16 gFalse;

// References every implementation so the linker doesn't strip them.
void DontDeadStrip();
void DontDeadStrip()
{
	if (gFalse) {
#include "PpyFactoryList.h"
	}
}

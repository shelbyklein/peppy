#pragma once

// The command runners for InDesign (PpyCommands.cpp) and the script engine they
// use (PpyScript.cpp).

#include "../shared/Catalog.h"

std::map<std::string, peppy::Runner> PpyRunners();
peppy::Runner PpyLibraryRunner(const std::string& method);   // the rest: peppy.jsx

// Runs ExtendScript in Peppy's persistent engine ("peppy") and returns its last
// value as text. Throws peppy::Error with the script's message on failure.
std::string PpyRunScript(const std::string& code);

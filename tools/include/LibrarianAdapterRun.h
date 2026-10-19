#pragma once

#include <string>

// librarian-test --adapters: plans the perk adapter keyword writes for a scan
// dump and its catalog with the plugin's own planner (LibrarianAdapters.cpp)
// and prints the per-line numbers. "requires" is not read: every plugin and
// DLL a file asks for is taken as there, every config gate as on and every
// keyword as defined, so all files run. With `verbose`, every planned effect write is listed with the
// spells that use that effect.
int RunAdapters(const std::string& dumpPath, const std::string& catalogPath,
    const std::string& adaptersDir, const std::string& reportPath, bool verbose);

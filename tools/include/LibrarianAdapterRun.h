#pragma once

#include <string>

// librarian-test --adapters: plans the perk adapter keyword writes for a scan
// dump and its catalog with the plugin's own planner (LibrarianAdapters.cpp)
// and prints the per-line numbers. Every plugin in the files' "requires" is
// taken as loaded and every keyword as defined, so all files run.
int RunAdapters(const std::string& dumpPath, const std::string& catalogPath,
    const std::string& adaptersDir, const std::string& reportPath);

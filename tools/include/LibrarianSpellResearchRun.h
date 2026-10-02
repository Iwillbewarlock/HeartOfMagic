#pragma once

#include <string>

// librarian-test --spellresearch: the Spell Research registrations for a scan
// dump and its catalog, made with the plugin's own code (LibrarianAdapters.cpp
// and LibrarianSpellResearch.cpp). Prints how many spells go into each SR list
// and, with an answer set, scores the SR elements and techniques against it.
// Every spell counts as not yet registered, as on a load order no SR patch covers.
int RunSpellResearch(const std::string& dumpPath, const std::string& catalogPath,
    const std::string& rulesFile, const std::string& answersPath, const std::string& reportPath, bool verbose);

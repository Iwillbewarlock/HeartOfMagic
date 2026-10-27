#pragma once

// librarian-test --self-test. Classifies small made-up spells with small
// made-up rules and checks the conflict stage, the per-element evidence tiers
// and the axis source after removals; then plans adapter lines on made-up
// catalogs (tierMin, requires.config, damage lines), and checks the explosion
// twin rules (docs/ENVIRONMENT.md) and the scan record's pure rules
// (ScanRecordRules.h). Needs no dump. Returns a
// process exit code (1 when any check fails).
int RunLibrarianSelfTest();

// The adapter part (librarian-adapters-selftest.cpp), adding to the counts.
void RunAdapterSelfTest(int& passed, int& failed);

// The explosion twin rules (environment-selftest.cpp: Frostwalker's matcher,
// its pattern files, which effects take a twin), adding to the counts.
void RunEnvironmentSelfTest(int& passed, int& failed);

// The scan record's pure rules (scanrecord-selftest.cpp, ScanRecordRules.h: the scan
// fingerprint's inputs, what makes a scan incomplete, when a record is kept), adding to the counts.
void RunScanRecordSelfTest(int& passed, int& failed);

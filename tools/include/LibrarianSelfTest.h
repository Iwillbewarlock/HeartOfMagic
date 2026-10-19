#pragma once

// librarian-test --self-test. Classifies small made-up spells with small
// made-up rules and checks the conflict stage, the per-element evidence tiers
// and the axis source after removals; then plans adapter lines on made-up
// catalogs (tierMin, requires.config, damage lines). Needs no dump. Returns a
// process exit code (1 when any check fails).
int RunLibrarianSelfTest();

// The adapter part (librarian-adapters-selftest.cpp), adding to the counts.
void RunAdapterSelfTest(int& passed, int& failed);

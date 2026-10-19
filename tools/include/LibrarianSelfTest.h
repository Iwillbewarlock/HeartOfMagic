#pragma once

// librarian-test --self-test. Classifies small made-up spells with small
// made-up rules and checks the conflict stage, the per-element evidence tiers
// and the axis source after removals. Needs no dump. Returns a process exit
// code (1 when any check fails).
int RunLibrarianSelfTest();

# Visual copy closure regression

Run `tools/blackflame-visual-closure-check/run.ps1` from a Visual Studio Developer PowerShell. It copies the actual production `FireClosure.cpp` and `FireClosure.h` without rewriting them, verifies both SHA256 hashes, and compiles their traversal against a small fake RE form graph. Intermediate files stay under `build/blackflame-visual-closure-check`.

The 13 checks cover supplemental non-fire MGEFs and equip-ability hit art, associated/hazard child spells, cycles, unrelated non-fire roots, unchanged source forms/gameplay fire sets, exclusion rules, costliest provenance and the existing bound-arrow path. The fixture models the Fire Storm supplemental-effect bug without hard-coded Skyrim IDs.

On 2026-10-23 the pre-change production source failed five checks, including the missing supplemental equip art; the revised source passed all 13. The other baseline failures depend on that same unreachable supplemental/associated path. This verifies actual closure control flow, not native engine ABI, runtime ownership attribution, shader pixels or game attachment lifetime. VisualSet membership and runtime swap isolation need their own checks.

The separate builder experiment used the actual IllusionMassRedCastBodyFX NIF and Glorious Gradients palette. The existing builder produced one private ARTO/model with no failures, visual-risk PASS, 17 neutral RGB keys and a derived 65,536-texel palette with zero chromatic pixels and unchanged alpha. No builder transform change was needed.

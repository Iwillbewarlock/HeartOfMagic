# Visual scope regression check

Run from a Visual Studio Developer PowerShell with C++20 tools:

```powershell
& .\tools\blackflame-visual-scope-check\run.ps1
```

The script finds the repository relative to itself, copies the actual production `VisualSet.cpp` and `VisualSet.h` unchanged into `build/blackflame-visual-scope-check`, verifies both SHA256 hashes, generates a `Common.h` dependency shim, and compiles the production CPP as a separate translation unit against `FakeEngine.h`. No production selection logic is duplicated in this test. Generated files stay in the build directory. Optional `-RepositoryRoot` and `-BuildRoot` parameters support an external scratch build without modifying production sources.

The 22 checks exercise the public APIs with an in-memory spell graph: catalog roots, supplemental non-fire effects, effect membership, a nonvisual spell sharing a selected MGEF, exclusive and shared Cloak descendants, a catalog root reused by a noncatalog root, cyclic edges, separate copy candidates and runtime admission, equip abilities with both visual and nonvisual owners, casting-art lookup, null inputs, empty catalog, and rebuild reset with no data handler.

This is a native test of the actual selection implementation with fake engine records. It does not validate real engine layout, catalog parsing, caster attribution, script-tag propagation, equip-selection concurrency, SKSE integration, asset generation, or rendered colour. BuildRelease and the other regression checks remain separate; visual results require an in-game check.

# Script visual provenance regression

Run `tools/blackflame-script-visual-check/run.ps1` from Visual Studio Developer PowerShell with Node.js available. The runner writes only `build/blackflame-script-visual-check`.

This is an extracted production-method seam, not a reimplementation or a compilation of the full Papyrus wrapper. `extract.js` copies the source bytes and prints their SHA256, then extracts unchanged method bodies from `PapyrusWrap.cpp` (`After`, `Decide`, `Tag`, `StorePending`, caller and handle helpers) and `ScriptTags.cpp` (tag storage, `Add`, `TagInfoOf`, pending judgement and public metadata helpers). Those exact bodies compile against a small fake handle/VM environment. Production `ScriptTags.h` is also copied unchanged.

The 21 checks cover confirmed and pending source-only callers on game/VM threads; genuine placed parents; direct active-effect callers; rejected pending callers; Cast-source attribution and subsequent child placement; stale-world/expired markers; completed placed markers; actors and latent natives. Source-only attribution must not become a visual-instance tag.

The runner also compiles an explicit mutant with only the two `info.visualInstance` guards removed from the generated seam. This mutant must fail. It is not claimed to be a historical pre-patch source file. On 2026-10-23 the current production seam passed 21/21, while the mutant exposed the source-to-placement inheritance bug.

This verifies actual method control flow and tag metadata. It does not verify the native Papyrus ABI, real VM scheduling, object lifetimes, game rendering or the unextracted native registration/marshalling implementation. The extractor intentionally fails when a named seam disappears or becomes ambiguous; update it alongside corresponding production method changes.

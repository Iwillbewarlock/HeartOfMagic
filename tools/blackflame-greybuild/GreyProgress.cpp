// =============================================================================
// GreyProgress - progress.jsonl: one line per finished object (CopyContractProgress.h) and the compaction at start
// =============================================================================
// P0 stub (single-launch design v3.2/v3.3): P1 fills it in; its file name and CMake entry are fixed.
// Contract (CopyContractProgress.h): before appending, drop a cut tail (ProgressValidPrefix); the compaction at start
// writes the kept lines to a temporary file, flushes it and renames it over progress.jsonl, never rewriting in place.

#include "../../plugins/BlackFlame/src/CopyContractProgress.h"

namespace GreyBuild
{
}

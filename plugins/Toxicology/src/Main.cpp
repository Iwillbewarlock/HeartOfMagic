// =============================================================================
// Heart of Magic Librarian - Toxicology
// =============================================================================
// When a spell the librarian tags "poison" hits, applies the player's top
// favorited poison to the target. This scaffold only sets up the log; the
// listeners come later.
// =============================================================================

#include "Common.h"

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse, false);
    SetupLog("Heart of Magic Librarian - Toxicology " TOXICOLOGY_VERSION ": loaded");
    return true;
}

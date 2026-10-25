#pragma once

#include "Common.h"

// Equip ability visuals follow buff edges and selected-parent/world changes.
// VisualEquipContext checks the live selected roots and rejects ambiguous shared
// ownership. On AE 1.6.1170 and 1.7.104 (1.7.104: code read, not run in game), both
// listed and unlisted abilities keep their gameplay ActiveEffect: retain visual refs,
// Stop/Detach, then restart through engine 34094.
// A current eligible owner or an existing copy/our shader is required; the latter
// permits restoring originals after ownership changes. No RemoveSpell/AddSpell,
// dispel or recast. Requests recheck queue, thread, generation and buff state.
// Requires copyHitArt, hitArt or burnShader; equip art itself requires a copy.
// Engine evidence and limitations: docs/diagnostics/equip-ability-refresh.md.

namespace BlackFlame
{
    /// Post the refresh to the main-thread queue (any thread).
    void RequestEquipAbilityRefresh(bool a_stoked);
}

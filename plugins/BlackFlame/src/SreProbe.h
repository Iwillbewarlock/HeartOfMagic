#pragma once

#include "Common.h"

// =============================================================================
// SRE PROBES (2026-10-20, run 8, log only) - two open questions of design v2
// =============================================================================
// H8: other plugins hook ShaderReferenceEffect's vtable (findings C4). One line per entry 0x27
//     Detach, 0x28 Update, 0x2D SaveGame, 0x2E LoadGame, 0x36 Init, 0x37 Suspend, 0x38 Resume,
//     0x3B UpdatePosition, 0x3E DetachImpl: where it points (module+offset), whether that is the
//     engine's own function where the id is known (34913, 34920, 34921; 34912 presumed for Init)
//     and where a jump at its first instruction goes. At kDataLoaded (inside BlackFlame's handler:
//     plugins whose handlers run later are not seen yet) and again at the first main menu.
// H6: at the first main menu (MenuWatch's listener, the menu event's thread, Main's in game),
//     BSShaderManager::GetTexture(path, demand true, out, false) for a palette that exists (with
//     and without the textures\ prefix) and two paths that do not exist: the pointer, the
//     NiTexture name, NiSourceTexture or not, rendererTexture null or not (demand loaded at once
//     or not), whether the name is the one asked for and whether the two missing paths came back
//     as one shared texture (a fallback). The textures are released at the end of the probe.
// AE 1.6.1170 and 1.7.104 only: armed by HitPath::Install after its runtime and id gate; otherwise nothing runs.

namespace BlackFlame::HitPath
{
    namespace Detail
    {
        /// HitPath::Install after the id check: the probes may run; the kDataLoaded vtable lines are written
        void ArmSreProbes();
    }

    /// The first main menu (MenuWatch::OnMainMenuOpen): the vtable lines again and the GetTexture probe, when armed; a fault or
    /// a std::exception skips a probe (logged) and never reaches the caller
    void RunMainMenuProbes();
}

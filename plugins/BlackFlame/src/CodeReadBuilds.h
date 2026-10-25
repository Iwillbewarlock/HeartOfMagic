#pragma once

// =============================================================================
// CODE READ BUILDS - the version rule of Patch::IsCodeReadBuild (Patch.h)
// =============================================================================
// Kept apart so tools\blackflame-equip-refresh-check compiles this rule itself instead of restating it.
// REL::Version comes from the includer: the plugin's PCH (CommonLibSSE-NG) or the check's FakeEngine.h.

namespace BlackFlame::Patch
{
    /// 1.6.1170 or 1.7.104: the two builds whose code was read
    [[nodiscard]] inline bool IsCodeReadVersion(const REL::Version& a_version) noexcept
    {
        return a_version == REL::Version(1, 6, 1170, 0) || a_version == REL::Version(1, 7, 104, 0);
    }
}

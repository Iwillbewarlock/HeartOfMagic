#pragma once

#include "SrBlockRules.h"

#include <string>

// =============================================================================
// LANG - every player-visible string of the block, in the language
// Settings::ResolveLanguage chose. Two sources (SrBlockText.h "Strings"):
// SR Prisma's own tables (Data/PrismaUI/views/SpellResearch/lang/<code>.json)
// for the archetype names and the group labels, the block's own files
// (Data/SKSE/Plugins/SL_MagicMenu_SpellResearch/lang/<code>.json) for the rest
// and as the fallback. A text no table has shows as its key (logged once).
// =============================================================================

namespace Lang
{
    /// Reads the four tables (call once, at kDataLoaded, before any Get). Logs what was read
    void Load();

    /// The block's own text of a_key (own <lang>, own en, the key). Thread-safe after Load
    std::string Get(const std::string& a_key);

    /// Archetype a_archetype's name as the block shows it: archetype.<i>.short, falling back to .name, from SR Prisma's tables
    /// first (SrBlock::ArchetypeName)
    std::string ArchetypeName(int a_archetype);

    /// A group's label (SR Prisma's page.group.* first, then the block's group.*)
    std::string GroupLabel(SrBlock::Group a_group);

    /// Self-test (bSelfTest): the language and how many keys each source has, and FAIL when an archetype the block can show
    /// has no name in any table. Writes "[selftest-sr] lang ..." lines
    void SelfTest();
}

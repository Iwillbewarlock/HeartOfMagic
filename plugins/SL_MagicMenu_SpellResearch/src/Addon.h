#pragma once

// =============================================================================
// ADDON - what the plugin's parts share (Main.cpp holds it): whether SR Prisma
// 0.2.1+ and Heart of Magic are loaded, and the block's redraw request.
// =============================================================================

namespace Addon
{
    inline constexpr const char* kBlockId = "SpellResearch.archetypes";

    /// SpellResearchPrismaUI.dll 0.2.1 or later is loaded (checked at kPostPostLoad): it answers 'SRPO', so the links show
    bool SrPrismaUsable();

    /// Heart of Magic (SpellLearning.dll) is loaded: its Librarian adds spells to SR's lists (self-test)
    bool HomInstalled();

    /// Asks SL_MagicMenu to draw the block again on its next UI frame ('SLMD'; ignored by it while the menu is closed). Any
    /// thread; nothing before the block was accepted
    void RequestRedraw();

    /// Asks SL_MagicMenu for the block's height this menu ('SLMH', stage units, <= SrBlock::kBlockHeight - the registered one);
    /// an SL_MagicMenu without 'SLMH' ignores it. Any thread; nothing before the block was accepted
    void RequestHeight(double a_height);
}

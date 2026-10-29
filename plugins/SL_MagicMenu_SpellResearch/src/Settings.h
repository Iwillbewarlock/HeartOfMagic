#pragma once

#include "SrBlockRules.h"

#include <string>

// =============================================================================
// SETTINGS - the add-on's own ini (Data/SKSE/Plugins/SL_MagicMenu_SpellResearch.ini,
// read once at plugin load), the block layout SR Prisma's settings choose
// (ResolveLayout, read once per magic menu open) and the block's language
// (ResolveLanguage).
// =============================================================================

namespace Settings
{
    /// Reads SL_MagicMenu_SpellResearch.ini (call once, at plugin load). Logs the values used
    void Load();

    /// [General] bSelfTest: the "[selftest-sr]" log lines (spec "Self-test")
    bool SelfTest();

    /// The layout from SpellResearchPrismaUI.ini [MagicMenuBlock] sLayout=list|line (the key SR Prisma's SKSE Menu Framework
    /// tab is to write; proposed by the add-on design, to be fixed by the SR Prisma session). Missing file, key or an unknown
    /// value: list. Read as text and parsed by SrBlock::ParseLayout (what tools/magic-menu-sr-block-check tests). Call once
    /// per magic menu open; CurrentLayout() returns it from then on
    SrBlock::LayoutKind ResolveLayout();

    /// The layout ResolveLayout() read last (list before the first menu open)
    SrBlock::LayoutKind CurrentLayout();

    /// The language code of the block's strings (lang/<code>.json). "en" until SR Prisma's own language setting exists (its
    /// session has not decided where it lives or how another plugin reads it): this is the one place to connect it
    std::string ResolveLanguage();
}

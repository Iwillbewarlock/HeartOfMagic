#pragma once

#include "SrBlockRules.h"

#include <string>

// =============================================================================
// SETTINGS - the add-on's own ini (Data/SKSE/Plugins/MagicMenuRewrite_SpellResearch.ini,
// read once at plugin load), the block layout SR Prisma's settings choose
// (ResolveLayout, read once per magic menu open) and the block's language
// (ResolveLanguage, from SR Prisma's [Display] sLanguage or the game's).
// =============================================================================

namespace Settings
{
    /// Reads MagicMenuRewrite_SpellResearch.ini (call once, at plugin load). Logs the values used
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

    /// The language code of the block's strings (lang/<code>.json; a code without its file falls back to en.json):
    /// SpellResearchPrismaUI.ini [Display] sLanguage = en | ko (SR Prisma's settings design draft, not final); auto, missing or
    /// another value -> the game's Skyrim.ini sLanguage (KOREAN -> ko, else en). SrBlock::ParseLanguage. Called at kDataLoaded
    /// and again at each magic menu open (SR Prisma's settings tab can change the key in game); a_log false: no log lines
    /// (the caller logs a change)
    std::string ResolveLanguage(bool a_log = true);
}

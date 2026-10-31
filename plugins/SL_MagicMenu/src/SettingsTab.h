#pragma once

#include <string>

// =============================================================================
// SETTINGS TAB - the magic menu's page in SKSE Menu Framework (2026-10-31, user:
// "SKSE메뉴에서 언어설정이랑 활성화할 블록창을 고를 수 있게 하자"). An optional
// dependency: with SKSE Menu Framework installed, loaded and new enough there is a
// section "Magic Menu Note" with one item, Settings - the text language
// ([Display] sLanguage) and a checkbox per block ([Blocks] sHidden); without it no
// tab and one log line, and both ini keys work as before. The page writes
// SL_MagicMenu.ini (only when a value changes) and nothing else: both DLLs read the
// keys again at the next magic menu open. Its draws run on the framework's render
// thread and touch no game state.
// =============================================================================

namespace SettingsTab
{
    /// At kDataLoaded, after the texts are loaded (the item's name is taken then and fixed): adds the tab, or logs why not.
    /// a_autoCode: the code [Display] sLanguage auto resolves to on this game (LangGame::AutoCode, read on the game thread)
    void Register(const std::string& a_autoCode);
}

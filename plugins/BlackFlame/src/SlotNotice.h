#pragma once

// =============================================================================
// SLOT NOTICE - one HUD line about the single-launch copies (English, as the prototype's other HUD lines)
// =============================================================================
// Through the main-thread queue (RE::SendHUDMessage::ShowHUDMessage, as CopyFreshness' notices); without the queue the log
// says it and nothing is shown (no AddTask fallback for a HUD line). Logged either way. Whether a HUD line shows at the main
// menu (no HUD menu open there) is not checked in game.

#include "Common.h"

#include "MainThreadQueue.h"

#include <string>

namespace BlackFlame::SlotNotice
{
    inline void Show(std::string a_text)
    {
        if (!MainQueue::Installed()) {
            logger::info("BlackFlame: slots: notice (no main-thread queue: not shown): \"{}\"", a_text);
            return;
        }
        logger::info("BlackFlame: slots: telling the player \"{}\"", a_text);
        (void)MainQueue::Post("slot notice", [text = std::move(a_text)]() { RE::SendHUDMessage::ShowHUDMessage(text.c_str()); });
    }
}

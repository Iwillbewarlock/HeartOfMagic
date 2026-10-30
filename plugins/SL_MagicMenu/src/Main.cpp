// =============================================================================
// SL_MagicMenu - the magic menu
// =============================================================================
// Draws a 3D preview box and a spell card into SkyUI's magic menu, on a board
// with the layout mode's gear and the key-hint strip, and hosts other plugins'
// blocks (BlockApi.h). Since HoM add-on plan Task 5 it knows no Heart of Magic:
// the spell tree, the learning progress and the node card are the HoM add-on's
// blocks (SL_MagicMenu_HoM). Spec: docs/superpowers/specs/2026-10-24-magic-menu-proto.md,
// docs/superpowers/specs/2026-10-30-magic-menu-hom-addon-design.md
// =============================================================================

#include "Common.h"

#include "BlockApi.h"
#include "GameThread.h"
#include "LangGame.h"
#include "Settings.h"
#include "Widget.h"
#include "WidgetStrings.h"

namespace
{
    // Blocks plan Task 6 (D9): add-on block registrations from any plugin (BlockApi.h), answered to the sender with a
    // BlockApi::Result; API v2: an add-on's redraw ('SLMD') and height ('SLMH') requests, v3: its show ('SLMS') and visible ('SLMV')
    // requests, not answered. This listener hears every plugin's messages and SKSE's: any other type passes silently
    void OnAnyMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        if (!a_msg) return;
        if (a_msg->type == BlockApi::kMessageHeight) {
            Widget::RequestAddonHeight(a_msg->sender ? a_msg->sender : "", a_msg->data, a_msg->dataLen);
            return;
        }
        if (a_msg->type == BlockApi::kMessageRedraw) {
            Widget::RequestAddonRedraw(a_msg->sender ? a_msg->sender : "", a_msg->data, a_msg->dataLen);
            return;
        }
        if (a_msg->type == BlockApi::kMessageShowSpell) {
            Widget::RequestAddonShowSpell(a_msg->sender ? a_msg->sender : "", a_msg->data, a_msg->dataLen);
            return;
        }
        if (a_msg->type == BlockApi::kMessageVisible) {
            Widget::RequestAddonVisible(a_msg->sender ? a_msg->sender : "", a_msg->data, a_msg->dataLen);
            return;
        }
        if (a_msg->type != BlockApi::kMessageRegister) return;
        if (!a_msg->sender || !*a_msg->sender) {
            logger::warn("SL_MagicMenu: an add-on block registration with no sender name - ignored (not answered)");
            return;
        }
        const auto outcome = Widget::RegisterBlock(a_msg->sender, a_msg->data, a_msg->dataLen);
        BlockApi::Result result;
        result.apiVersion = outcome.apiVersion;  // a v1 add-on is answered as v1
        result.status = outcome.accepted ? BlockApi::Status::kAccepted : BlockApi::Status::kRefused;
        result.id = outcome.id.c_str();
        result.reason = outcome.reason.c_str();
        if (std::string_view(a_msg->sender) == BlockApi::kPluginName) return;  // our own filler registers by a call, not a message
        if (auto* messaging = SKSE::GetMessagingInterface()) {
            messaging->Dispatch(BlockApi::kMessageResult, &result, sizeof(result), a_msg->sender);  // false: the sender does not listen
        }
    }

    void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        if (!a_msg) return;
        if (a_msg->type == SKSE::MessagingInterface::kPostLoad) {
            // every plugin is loaded now, so the any-sender listener (nullptr) covers them all; add-ons send from kPostPostLoad on
            auto* messaging = SKSE::GetMessagingInterface();
            const bool listening = messaging && messaging->RegisterListener(nullptr, OnAnyMessage);
            logger::info("SL_MagicMenu: add-on block registrations {}", listening ? "open (message type 'SLMB', until the first magic menu open)" :
                                                                                   "NOT possible - the any-sender listener was refused");
            return;
        }
        if (a_msg->type != SKSE::MessagingInterface::kDataLoaded) return;
        // the on-screen texts in [Display] sLanguage's language (the game's INI settings are read by now), once per session
        LangGame::Load(WidgetText::g_catalog, WidgetText::kLangDir);
        if (Settings::Get().debugFillerBlock) Widget::RegisterDebugFiller();  // blocks plan Task 6: through the add-on path
        logger::info("SL_MagicMenu: kDataLoaded on thread {}, main thread {}", REX::W32::GetCurrentThreadId(), GameThread::Id());
        Widget::Install();
        Input::Install();
        FlushLog();
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse, SKSE::InitInfo{ .log = false });
    SetupLog("Magic menu");
    // The main thread: the game's preview data are used on it only. kDataLoaded ran on another thread in the game
    // run of 2026-10-25, so it is taken here, where SKSE loads plugins.
    GameThread::Set();
    logger::info("SL_MagicMenu: main thread {}", REX::W32::GetCurrentThreadId());

    Settings::Load();
    if (!Settings::Get().enabled) {
        logger::info("SL_MagicMenu: bEnabled=0 - nothing installed");
        return true;
    }
    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        logger::error("SL_MagicMenu: no SKSE messaging interface - nothing installed");
        return true;
    }
    messaging->RegisterListener(OnSKSEMessage);
    logger::info("SL_MagicMenu: listening for SKSE messages (add-on blocks from kPostLoad on)");
    return true;
}

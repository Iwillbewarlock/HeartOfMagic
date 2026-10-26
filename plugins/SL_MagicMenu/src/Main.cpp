// =============================================================================
// SL_MagicMenu - magic menu prototype (ACF M1-0)
// =============================================================================
// Draws a preview box, a small spell-tree window, a progress card and a node
// card into SkyUI's magic menu, reading Heart of Magic through its public API
// v2 (SpellLearningAPI.h) and the saved spell_tree.json. PROTOTYPE: both data
// paths are replaced by the I1 contract.
// Spec: docs/superpowers/specs/2026-10-24-magic-menu-proto.md
// =============================================================================

#include "Common.h"

#include "Hom.h"
#include "Settings.h"
#include "SpellLearningAPI.h"
#include "Widget.h"

namespace
{
    void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        if (!a_msg || a_msg->type != SKSE::MessagingInterface::kDataLoaded) return;
        logger::info("SL_MagicMenu: kDataLoaded on thread {}, main thread {} (API {})", REX::W32::GetCurrentThreadId(), Hom::GameThreadId(),
            Hom::Ready() ? "received" : "NOT received - is Heart of Magic installed?");
        Widget::LoadTree();
        Widget::Install();
        Input::Install();
        FlushLog();
    }

    // HoM broadcasts its API at kPostPostLoad
    void OnSpellLearningMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        if (!a_msg) return;
        if (a_msg->type != SpellLearning::kMessageType_APIReady || !a_msg->data) {
            logger::info("SL_MagicMenu: SpellLearning message type 0x{:X} ignored", a_msg->type);
            return;
        }
        auto* api = static_cast<SpellLearning::ISpellLearningAPI*>(a_msg->data);
        Hom::SetApi(api);
        logger::info("SL_MagicMenu: Heart of Magic API v{} received", api->GetAPIVersion());
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse, SKSE::InitInfo{ .log = false });
    SetupLog("Heart of Magic - magic menu prototype (ACF M1-0)");
    // The main thread: HoM's API and the game's preview data are used on it only. kDataLoaded ran on another
    // thread in the game run of 2026-10-25, so it is taken here, where SKSE loads plugins.
    Hom::SetGameThread();
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
    messaging->RegisterListener("SpellLearning", OnSpellLearningMessage);
    logger::info("SL_MagicMenu: listening for SKSE and SpellLearning messages");
    return true;
}

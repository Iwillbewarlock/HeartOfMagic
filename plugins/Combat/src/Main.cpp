// =============================================================================
// SL_Combat - Heart of Magic combat addon
// =============================================================================
// Milestone M1: the spell counter. A spell missile that reaches the player
// right after a press of the counter key (the block key unless set) meets a
// ward raised at once, with slow motion, a free next cast and learning XP
// (docs/COMBAT.md).
//
// Talks to Heart of Magic only through the public API (SpellLearningAPI.h);
// runs without it too (no XP then).
// =============================================================================

#include "Common.h"

#include "CombatConfig.h"
#include "CombatMenu.h"
#include "CounterInput.h"
#include "CounterResponse.h"
#include "CounterPrompt.h"
#include "IncomingWarning.h"
#include "ProjectileCounter.h"
#include "WardSupport.h"
#include "SpellLearningAPI.h"

namespace
{
    void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        if (!a_msg) {
            return;
        }
        switch (a_msg->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            {
                auto& config = SLCombat::CombatConfig::Get();
                config.Load();
                if (!config.enableCounter) {
                    logger::info("SL_Combat: counter disabled in config");
                    SLCombat::CombatMenu::Register(false);  // so it can be turned back on
                    break;
                }
                // SkyPrompt is required: it shows the counter key and takes its press
                if (!SLCombat::CounterPrompt::Initialize()) {
                    logger::error("SL_Combat: SkyPrompt is required and was not found - the counter is off");
                    SLCombat::CombatMenu::Register(false);
                    FlushLog();
                    break;
                }
                SLCombat::DetectWardReflectMod();
                SLCombat::WardSupport::Detect();
                SLCombat::ProjectileCounter::Install();
                SLCombat::IncomingWarning::Install();
                SLCombat::CounterInput::Register();
                SLCombat::CounterResponse::Register();
                SLCombat::CounterResponse::RegisterXPSource();
                SLCombat::CombatMenu::Register(true);
                FlushLog();
                break;
            }
        case SKSE::MessagingInterface::kPreLoadGame:
        case SKSE::MessagingInterface::kPostLoadGame:
        case SKSE::MessagingInterface::kNewGame:
            SLCombat::CounterResponse::Reset();
            SLCombat::CounterInput::GetSingleton()->Consume();
            FlushLog();
            break;
        default:
            break;
        }
    }

    // Heart of Magic broadcasts its API at kPostPostLoad, before kDataLoaded
    void OnSpellLearningMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        if (!a_msg || a_msg->type != SpellLearning::kMessageType_APIReady || !a_msg->data) {
            return;
        }
        auto* api = static_cast<SpellLearning::ISpellLearningAPI*>(a_msg->data);
        SLCombat::CounterResponse::SetAPI(api);
        logger::info("SL_Combat: Heart of Magic API v{} received", api->GetAPIVersion());
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::InitInfo info;
    info.log = false;              // SetupLog below makes the log
    info.trampoline = true;        // the Present hook (CounterPrompt)
    info.trampolineSize = 14;
    SKSE::Init(a_skse, info);
    SetupLog("Heart of Magic combat addon (spell counter)");

    auto* messaging = SKSE::GetMessagingInterface();
    messaging->RegisterListener(OnSKSEMessage);
    messaging->RegisterListener("SpellLearning", OnSpellLearningMessage);
    logger::info("SL_Combat: waiting for kDataLoaded");
    return true;
}

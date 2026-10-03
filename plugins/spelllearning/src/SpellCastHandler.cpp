#include "SpellCastHandler.h"
#include "HudNotify.h"
#include "ProgressionManager.h"
#include "SpellEffectivenessHook.h"
#include "ThreadUtils.h"

SpellCastHandler* SpellCastHandler::GetSingleton()
{
    static SpellCastHandler singleton;
    return &singleton;
}

void SpellCastHandler::Register()
{
    if (m_registered) {
        return;
    }

    auto* eventSource = RE::ScriptEventSourceHolder::GetSingleton();
    if (eventSource) {
        eventSource->AddEventSink(this);
        m_registered = true;
        logger::info("SpellCastHandler: Registered for spell cast events");
    } else {
        logger::error("SpellCastHandler: Failed to get event source holder");
    }
}

void SpellCastHandler::Unregister()
{
    if (!m_registered) {
        return;
    }

    auto* eventSource = RE::ScriptEventSourceHolder::GetSingleton();
    if (eventSource) {
        eventSource->RemoveEventSink(this);
        m_registered = false;
        logger::info("SpellCastHandler: Unregistered from spell cast events");
    }
}

RE::BSEventNotifyControl SpellCastHandler::ProcessEvent(
    const RE::TESSpellCastEvent* a_event,
    RE::BSTEventSource<RE::TESSpellCastEvent>*)
{
    if (!a_event) {
        return RE::BSEventNotifyControl::kContinue;
    }

    // PERFORMANCE: Check player first (most common early exit for NPC casts)
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    // PERFORMANCE: Compare object handles directly before dereferencing
    auto casterHandle = a_event->object;
    if (!casterHandle || casterHandle.get() != player) {
        return RE::BSEventNotifyControl::kContinue;
    }

    // The engine sends this event from whichever thread did the casting (seen
    // in game: four different ones), with nothing ordering it against anything
    // else. Everything below grants XP, early-grants spells and renames them -
    // unlocked state that every other writer changes through the SKSE task
    // queue, one task at a time. So the cast joins that queue too. Only the
    // spell id travels; the rest is looked up again when the task runs.
    const RE::FormID spellId = a_event->spell;
    const auto castAt = Clock::now();   // now, not when the task runs: the pairing compares these
    AddTaskToGameThread("SpellCast", [this, spellId, castAt]() { HandlePlayerCast(spellId, false, castAt); });
    return RE::BSEventNotifyControl::kContinue;
}

void SpellCastHandler::OnImmediateCast(RE::FormID spellId)
{
    const auto castAt = Clock::now();
    AddTaskToGameThread("SpellCastImmediate", [this, spellId, castAt]() { HandlePlayerCast(spellId, true, castAt); });
}

// =============================================================================
// ONE CAST, ONE CREDIT (event and immediate-cast hook)
// =============================================================================

bool SpellCastHandler::IsSecondReport(RE::FormID spellId, bool a_immediate, Clock::time_point a_castAt)
{
    auto found = m_pendingCasts.find(spellId);
    if (found == m_pendingCasts.end()) return false;
    auto& pending = found->second;
    const auto expired = [a_castAt](Clock::time_point t) {
        const auto gap = a_castAt > t ? a_castAt - t : t - a_castAt;
        return std::chrono::duration<float>(gap).count() > kSameCastWindowSeconds;
    };
    // Reports run in queue order, not always in time order: drop by distance either way
    for (auto* way : { &pending.byEvent, &pending.byHook }) {
        way->erase(std::remove_if(way->begin(), way->end(), expired), way->end());
    }
    auto& otherWay = a_immediate ? pending.byEvent : pending.byHook;
    bool second = false;
    if (!otherWay.empty()) {
        otherWay.pop_front();
        second = true;
    }
    if (pending.byEvent.empty() && pending.byHook.empty()) m_pendingCasts.erase(found);
    return second;
}

void SpellCastHandler::RememberCredited(RE::FormID spellId, bool a_immediate, Clock::time_point a_castAt)
{
    auto& pending = m_pendingCasts[spellId];
    auto& way = a_immediate ? pending.byHook : pending.byEvent;
    way.push_back(a_castAt);
    while (way.size() > kMaxPendingPerWay) way.pop_front();
}

void SpellCastHandler::HandlePlayerCast(RE::FormID spellId, bool a_immediate, Clock::time_point a_castAt)
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(spellId);
    if (!player || !spell) {
        return;
    }

    // Filter out non-spell items (powers, lesser powers, abilities, etc.)
    auto spellType = spell->GetSpellType();
    if (spellType != RE::MagicSystem::SpellType::kSpell) {
        return;
    }
    // An immediate cast may be a mod's helper or proc spell: only one the player has counts
    if (a_immediate && !player->HasSpell(spell)) {
        return;
    }

    const char* path = a_immediate ? "cast hook (immediate cast: SpellHotbar 2, Spell.Cast)" : "cast event";

    // An immediate re-cast stream: only its first cast counts (every cast moves the mark)
    bool inStream = false;
    if (a_immediate) {
        auto prev = m_lastImmediateAt.find(spellId);
        if (prev != m_lastImmediateAt.end()) {
            const auto gap = a_castAt > prev->second ? a_castAt - prev->second : prev->second - a_castAt;
            inStream = std::chrono::duration<float>(gap).count() < kImmediateStreamGapSeconds;
        }
        m_lastImmediateAt[spellId] = a_castAt;
    }

    // The same cast seen by both the event and the immediate-cast hook counts once
    if (IsSecondReport(spellId, a_immediate, a_castAt)) {
        logger::info("XP: duplicate skipped - {} ({:08X}) from {}: the same cast was already credited the other way",
            spell->GetName(), spellId, path);
        return;
    }
    if (inStream) {
        logger::debug("XP: {} ({:08X}) re-cast by an immediate cast within {:.2f}s - counted at the start of the stream",
            spell->GetName(), spellId, kImmediateStreamGapSeconds);
        return;
    }
    RememberCredited(spellId, a_immediate, a_castAt);

    // Get spell school
    auto* effect = spell->GetCostliestEffectItem();
    if (!effect || !effect->baseEffect) {
        return;
    }

    auto school = effect->baseEffect->GetMagickSkill();
    std::string schoolName;
    switch (school) {
        case RE::ActorValue::kAlteration:  schoolName = "Alteration"; break;
        case RE::ActorValue::kConjuration: schoolName = "Conjuration"; break;
        case RE::ActorValue::kDestruction: schoolName = "Destruction"; break;
        case RE::ActorValue::kIllusion:    schoolName = "Illusion"; break;
        case RE::ActorValue::kRestoration: schoolName = "Restoration"; break;
        default: return;
    }

    // Calculate XP based on magicka cost (higher cost = more XP)
    float magickaCost = spell->CalculateMagickaCost(player);
    float xpGain = (std::max)(1.0f, magickaCost / 10.0f);  // Parentheses to avoid Windows max macro

    // PERFORMANCE: Use trace level for frequent events (only visible with verbose logging)
    logger::trace("SpellCastHandler: Player cast {} ({:08X}) - school: {}, cost: {:.1f}, XP: {:.1f}",
        spell->GetName(), 
        spell->GetFormID(),
        schoolName,
        magickaCost,
        xpGain);

    // Grant XP to learning targets in this school
    ProgressionManager::GetSingleton()->OnSpellCast(schoolName, spell->GetFormID(), xpGain, path);

    // =========================================================================
    // THROTTLED WEAKENED SPELL NOTIFICATION
    // Show "Weakened spell at X% effectiveness" at configurable interval when casting early-learned spells
    // =========================================================================
    if (m_weakenedNotificationsEnabled) {
        auto* effectivenessHook = SpellEffectivenessHook::GetSingleton();
        RE::FormID spellFormId = spell->GetFormID();
        
        if (effectivenessHook && effectivenessHook->IsEarlyLearnedSpell(spellFormId)) {
            // Accumulate XP gained
            m_accumulatedXP += xpGain;
            
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::duration<float>>(now - m_lastWeakenedNotifTime).count();
            
            // Show notification at configured interval (or on first cast of this spell)
            if (elapsed >= m_notificationIntervalSeconds || m_lastNotifiedSpell != spellFormId) {
                float effectiveness = effectivenessHook->CalculateEffectiveness(spellFormId);
                int effectivenessPercent = static_cast<int>(effectiveness * 100);
                
                if (m_accumulatedXP > 0.1f) {
                    HudNotify::Show("hud.castPowerXP", "{{spell}} operating at {{percent}}% power (+{{xp}} XP)",
                        { { "spell", spell->GetName() },
                          { "percent", std::to_string(effectivenessPercent) },
                          { "xp", std::format("{:.1f}", m_accumulatedXP) } });
                } else {
                    HudNotify::Show("hud.castPower", "{{spell}} operating at {{percent}}% power",
                        { { "spell", spell->GetName() }, { "percent", std::to_string(effectivenessPercent) } });
                }
                
                // Reset tracking
                m_lastWeakenedNotifTime = now;
                m_accumulatedXP = 0.0f;
                m_lastNotifiedSpell = spellFormId;
            }
        }
    }
}

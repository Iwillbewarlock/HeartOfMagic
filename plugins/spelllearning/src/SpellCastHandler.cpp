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
    AddTaskToGameThread("SpellCast", [this, spellId]() { HandlePlayerCast(spellId, false); });
    return RE::BSEventNotifyControl::kContinue;
}

void SpellCastHandler::OnImmediateCast(RE::FormID spellId)
{
    AddTaskToGameThread("SpellCastImmediate", [this, spellId]() { HandlePlayerCast(spellId, true); });
}

void SpellCastHandler::HandlePlayerCast(RE::FormID spellId, bool a_immediate)
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

    const auto castAt = std::chrono::steady_clock::now();

    // An immediate re-cast stream: only its first cast counts (every cast moves the mark)
    if (a_immediate) {
        auto prev = m_lastImmediateAt.find(spellId);
        const bool inStream = prev != m_lastImmediateAt.end() &&
            std::chrono::duration<float>(castAt - prev->second).count() < kImmediateStreamGapSeconds;
        m_lastImmediateAt[spellId] = castAt;
        if (inStream) {
            return;
        }
    }

    // The same cast seen by both the event and the immediate-cast hook counts once
    auto last = m_lastCredited.find(spellId);
    if (last != m_lastCredited.end() && last->second.immediate != a_immediate && !last->second.matched &&
        std::chrono::duration<float>(castAt - last->second.at).count() < kSameCastWindowSeconds) {
        last->second.matched = true;
        // Once a session: shows whether vanilla casts reach the hook too
        if (!m_loggedDuplicate) {
            m_loggedDuplicate = true;
            logger::info("SpellCastHandler: {} ({:08X}) seen by both the cast event and the immediate-cast hook; "
                "credited once (logged once a session)", spell->GetName(), spellId);
        }
        return;
    }
    m_lastCredited[spellId] = Credited{ castAt, a_immediate };
    if (a_immediate && !m_loggedImmediate) {
        m_loggedImmediate = true;
        logger::info("SpellCastHandler: {} ({:08X}) credited from an immediate cast (SpellHotbar 2, Spell.Cast; "
            "logged once a session)", spell->GetName(), spellId);
    }

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
    ProgressionManager::GetSingleton()->OnSpellCast(schoolName, spell->GetFormID(), xpGain);

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

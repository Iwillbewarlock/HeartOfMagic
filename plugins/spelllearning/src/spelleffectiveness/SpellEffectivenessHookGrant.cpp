#include "SpellEffectivenessHook.h"
#include "FormIdText.h"
#include "HudNotify.h"
#include "ISLIntegration.h"
#include "ProgressionCoSave.h"
#include "ProgressionManager.h"
#include "uimanager/UIManager.h"
#include "ThreadUtils.h"

// =============================================================================
// SPELL GRANTING
// =============================================================================

bool SpellEffectivenessHook::CanLearnEarly(RE::SpellItem* spell)
{
    // The school the learning target and cast XP use (first effect, else the costliest)
    return spell && spell->GetSpellType() == RE::MagicSystem::SpellType::kSpell &&
        !ProgressionManager::TargetSchoolOf(spell).empty();
}

bool SpellEffectivenessHook::IsHeldForISL(RE::FormID formId) const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_heldForISL.contains(formId);
}

bool SpellEffectivenessHook::ShouldGrantEarly(RE::SpellItem* spell) const
{
    if (!CanLearnEarly(spell)) return false;
    const RE::FormID formId = spell->GetFormID();
    // ISL adds a spell it studies when the study is done, never we (an AddSpell here
    // taught it before the study, and ISL then found it known)
    if (IsHeldForISL(formId)) return false;
    // A copy of it known: the spell is learned already (ProgressionManager::CanUnlock)
    return !ProgressionManager::GetSingleton()->PlayerKnowsCopyOf(formId);
}

bool SpellEffectivenessHook::ReleaseHeldIfOwned(RE::SpellItem* spell)
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!spell || !player || !IsHeldForISL(spell->GetFormID()) || !player->HasSpell(spell)) return false;
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_heldForISL.erase(spell->GetFormID());
    }
    logger::info("SpellEffectivenessHook: {} ({:08X}) was held for ISL and the player has it now - taught",
        spell->GetName(), spell->GetFormID());
    return true;
}

void SpellEffectivenessHook::OnISLTaught(RE::SpellItem* spell)
{
    if (!CanLearnEarly(spell)) return;
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    AddToEarlySet(spell->GetFormID());
    m_heldForISL.erase(spell->GetFormID());
}

void SpellEffectivenessHook::GrantEarlySpell(RE::SpellItem* spell)
{
    if (!CanLearnEarly(spell)) {
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return;
    }

    auto* hook = GetSingleton();
    RE::FormID formId = spell->GetFormID();
    bool alreadyHasSpell = player->HasSpell(spell);
    bool alreadyTracked = hook->IsEarlyLearnedSpell(formId);

    // Add spell to player if they don't have it
    if (!alreadyHasSpell) {
        player->AddSpell(spell);
        logger::info("SpellEffectivenessHook: Added spell {} ({:08X}) to player",
            spell->GetName(), formId);

        // Show notification that spell was granted (weakened)
        int step = hook->GetCurrentPowerStep(formId);
        HudNotify::Show("hud.learnedWeakened", "{{step}} {{spell}} learned (weakened)",
            { { "step", hook->GetPowerStepLabel(step) }, { "spell", spell->GetName() } });
    }

    // Track as early-learned even if player already has it (but not if already tracked)
    if (!alreadyTracked) {
        hook->AddEarlyLearnedSpell(formId);
        hook->UpdateSpellDisplayCache(formId, spell);

        // Apply the modified name and description to the actual spell (works with SkyUI)
        hook->ApplyModifiedSpellName(formId);
        hook->ApplyModifiedDescriptions(formId);

        logger::info("SpellEffectivenessHook: Now tracking {} ({:08X}) as early-learned (had spell: {})",
            spell->GetName(), formId, alreadyHasSpell);

        // Notify UI
        UIManager::GetSingleton()->UpdateSpellState(
            FormIdText::Format(formId), "weakened");
    } else {
        logger::trace("SpellEffectivenessHook: Spell {} ({:08X}) already tracked as early-learned",
            spell->GetName(), formId);
    }
}

void SpellEffectivenessHook::RegisterISLPendingSpell(RE::SpellItem* spell)
{
    if (!CanLearnEarly(spell)) return;

    RE::FormID formId = spell->GetFormID();

    // Add to early-learned tracking so effectiveness hook will nerf when ISL teaches it.
    // Do NOT call player->AddSpell — ISL handles that after study is complete.
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (m_earlyLearnedSpells.find(formId) != m_earlyLearnedSpells.end()) {
            logger::trace("SpellEffectivenessHook: ISL pending spell {} ({:08X}) already tracked",
                spell->GetName(), formId);
            return;
        }
        AddToEarlySet(formId);
        // Not taught yet: only ISL adds it (ShouldGrantEarly)
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->HasSpell(spell)) {
            m_heldForISL.insert(formId);
        }
    }

    logger::info("SpellEffectivenessHook: Registered ISL pending spell {} ({:08X}) for weakness tracking",
        spell->GetName(), formId);

    // Store original name NOW (before any modification) so we have it for later.
    // Do NOT modify spell name/description yet — ISL reads akSpell.GetName() in
    // its study notifications, and we don't want "(Learning - 20%)" showing there.
    // Name/description modifications are applied later in OnStudyComplete.
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (m_originalSpellNames.find(formId) == m_originalSpellNames.end()) {
            m_originalSpellNames[formId] = spell->GetName();
            logger::info("SpellEffectivenessHook: Stored original name for ISL pending spell {:08X}: '{}'",
                formId, spell->GetName());
        }
    }

    // Notify UI of the new tracked state (no name change yet)
    UIManager::GetSingleton()->UpdateSpellState(
        FormIdText::Format(formId), "studying");
}

void SpellEffectivenessHook::MarkMastered(RE::FormID spellFormId)
{
    // Restore original spell name and description BEFORE removing from tracking
    RestoreOriginalSpellName(spellFormId);
    RestoreOriginalDescriptions(spellFormId);

    RemoveEarlyLearnedSpell(spellFormId);

    // Clear display cache since spell is now at full power
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_displayCache.erase(spellFormId);
    }

    logger::info("SpellEffectivenessHook: Spell {:08X} mastered - nerf removed, name restored", spellFormId);

    // Notify UI
    UIManager::GetSingleton()->UpdateSpellState(
        FormIdText::Format(spellFormId), "mastered");
}

// =============================================================================
// SPELL REMOVAL / RE-GRANTING (for learning target changes)
// =============================================================================

void SpellEffectivenessHook::RemoveEarlySpellFromPlayer(RE::FormID spellFormId)
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return;
    }

    // Get the spell
    auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(spellFormId);
    if (!spell) {
        logger::warn("SpellEffectivenessHook: Could not find spell {:08X} to remove", spellFormId);
        return;
    }

    // Only remove if player has it and it's in our early-learned set
    auto* hook = GetSingleton();
    if (!hook->IsEarlyLearnedSpell(spellFormId)) {
        return;  // Not an early-learned spell, don't touch it
    }

    // Its own name and effect descriptions back first, as MarkMastered does (the
    // name restore can fall back to the display cache cleared below). An effect's
    // description is shared by every spell using it: left as "[x% Power] ..." it
    // showed on another spell until the next load. CheckAndRegrantSpell applies
    // them again when the spell is the target once more.
    hook->RestoreOriginalSpellName(spellFormId);
    hook->RestoreOriginalDescriptions(spellFormId);

    if (player->HasSpell(spell)) {
        player->RemoveSpell(spell);
        logger::info("SpellEffectivenessHook: Removed early-learned spell {} ({:08X}) from player",
            spell->GetName(), spellFormId);
    }

    // Remove from tracking but keep progress data
    hook->RemoveEarlyLearnedSpell(spellFormId);

    // Clear display cache
    {
        std::unique_lock<std::shared_mutex> lock(hook->m_mutex);
        hook->m_displayCache.erase(spellFormId);
    }

    // Notify UI
    UIManager::GetSingleton()->UpdateSpellState(
        FormIdText::Format(spellFormId), "available");
}

void SpellEffectivenessHook::CheckAndRegrantSpell(RE::FormID spellFormId)
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return;
    }

    // Get current progress
    auto progress = ProgressionManager::GetSingleton()->GetProgress(spellFormId);
    float progressPercent = progress.progressPercent * 100.0f;

    logger::info("SpellEffectivenessHook: CheckAndRegrantSpell {:08X} - progress {:.1f}%, threshold {:.1f}%",
        spellFormId, progressPercent, m_settings.unlockThreshold);

    // Check if progress is above early learning threshold but below 100% (mastery)
    if (progressPercent >= m_settings.unlockThreshold && progressPercent < 100.0f) {
        // Get the spell
        auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(spellFormId);
        if (!spell) {
            logger::warn("SpellEffectivenessHook: Could not find spell {:08X} to regrant", spellFormId);
            return;
        }

        ReleaseHeldIfOwned(spell);
        bool playerHasSpell = player->HasSpell(spell);
        bool isTracked = IsEarlyLearnedSpell(spellFormId);

        // Added by us only by the one rule (held for ISL, a copy known: not)
        if (!playerHasSpell && !ShouldGrantEarly(spell)) {
            logger::info("SpellEffectivenessHook: {} ({:08X}) not regranted - held for ISL, a copy known or "
                "not a school spell", spell->GetName(), spellFormId);
            return;
        }

        // Add spell if player doesn't have it
        if (!playerHasSpell) {
            player->AddSpell(spell);
            logger::info("SpellEffectivenessHook: Added spell {} ({:08X}) to player",
                spell->GetName(), spellFormId);
        }

        // Always add to early-learned tracking if not already tracked
        // (Needed even if player already has spell from previous session)
        if (!isTracked) {
            AddEarlyLearnedSpell(spellFormId);
            UpdateSpellDisplayCache(spellFormId, spell);
            // RemoveEarlySpellFromPlayer put the name and descriptions back when
            // the spell stopped being the target
            ApplyModifiedSpellName(spellFormId);
            ApplyModifiedDescriptions(spellFormId);

            logger::info("SpellEffectivenessHook: Tracking spell {} ({:08X}) as early-learned - progress {:.1f}%",
                spell->GetName(), spellFormId, progressPercent);

            // Notify UI
            UIManager::GetSingleton()->UpdateSpellState(
                FormIdText::Format(spellFormId), "weakened");
        }
    }
}

// =============================================================================
// SKSE CO-SAVE SERIALIZATION
// =============================================================================

void SpellEffectivenessHook::OnGameSaved(SKSE::SerializationInterface* a_intfc)
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);

    // v2: the early ids, then the held-for-ISL ones (ProgressionCoSave's form id list
    // twice). A build from before v2 checks no version: it reads the early list and
    // ignores the held one after it.
    const std::vector<std::uint32_t> early(m_earlyLearnedSpells.begin(), m_earlyLearnedSpells.end());
    const std::vector<std::uint32_t> held(m_heldForISL.begin(), m_heldForISL.end());
    if (!a_intfc->OpenRecord(kEarlyLearnedRecord, kEarlyLearnedVersion) ||
        !ProgressionCoSave::WriteFormIds(*a_intfc, early) || !ProgressionCoSave::WriteFormIds(*a_intfc, held)) {
        logger::error("SpellEffectivenessHook: Failed to write the early-learned record ({} spells, {} held for ISL)",
            early.size(), held.size());
        return;
    }

    logger::info("SpellEffectivenessHook: Saved {} early-learned spells ({} held for ISL)", early.size(), held.size());
}

void SpellEffectivenessHook::BeginLoad()
{
    // Nothing to clear here: OnRevert runs before every load and empties the
    // set. The record itself clears again below, so a save without one leaves
    // whatever the revert left, which is empty.
}

void SpellEffectivenessHook::EndLoad()
{
    // The effect hook's progress copy, now that ProgressionManager has the save's
    // progress (its records are read in the same loop; CoSave.cpp ends its load first)
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_progressMirror.clear();
        auto* progression = ProgressionManager::GetSingleton();
        for (RE::FormID formId : m_earlyLearnedSpells) {
            m_progressMirror[formId] = progression->GetProgressPercent(formId) * 100.0f;
        }
    }

    // Refresh all spell displays after load (no lock held: RefreshAllSpellDisplays
    // takes its own). Deferred so the game is fully loaded first.
    AddTaskToGameThread("RefreshSpellDisplays", [this]() {
        RefreshAllSpellDisplays();
    });
}

bool SpellEffectivenessHook::ReadRecord(SKSE::SerializationInterface* a_intfc,
                                        uint32_t type, uint32_t version, uint32_t length)
{
    if (type != kEarlyLearnedRecord) return false;
    if (version < 1 || version > kEarlyLearnedVersion) {
        logger::warn("SpellEffectivenessHook: Skipping early-learned record with unsupported version {} "
            "(written by a newer version?)", version);
        return true;
    }

    // Every id read in full and a count the record can hold (ProgressionCoSave::ReadFormIds):
    // a cut-short id read as a whole one named another spell
    std::vector<std::uint32_t> early;
    std::vector<std::uint32_t> held;
    auto result = ProgressionCoSave::ReadFormIds(*a_intfc, length, early);
    const bool earlyOk = result == ProgressionCoSave::ReadResult::Ok;
    if (!earlyOk) {
        logger::error("SpellEffectivenessHook: early-learned list unreadable ({}), {} ids read before it kept",
            ProgressionCoSave::Describe(result), early.size());
    }
    if (version >= 2 && earlyOk) {
        const auto used = static_cast<std::uint32_t>(sizeof(std::uint32_t) * (early.size() + 1));
        result = ProgressionCoSave::ReadFormIds(*a_intfc, length > used ? length - used : 0, held);
        if (result != ProgressionCoSave::ReadResult::Ok) {
            logger::error("SpellEffectivenessHook: held-for-ISL list unreadable ({}), {} ids read before it kept",
                ProgressionCoSave::Describe(result), held.size());
        }
    }

    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);

        m_earlyLearnedSpells.clear();
        m_heldForISL.clear();
        m_earlySpellCount.store(0, std::memory_order_release);
        m_displayCache.clear();  // Clear display cache too
        m_progressMirror.clear();  // EndLoad fills it
        // A v1 list (or a v2 one cut short in its early list) does not say what ISL holds
        m_heldListLoaded = version >= 2 && earlyOk;

        uint32_t notSpells = 0;
        auto resolve = [&](std::uint32_t formId, RE::FormID& resolvedId) {
            // Resolve formId in case load order changed
            if (!a_intfc->ResolveFormID(formId, resolvedId)) {
                logger::warn("SpellEffectivenessHook: Failed to resolve formId {:08X}", formId);
                return false;
            }
            return true;
        };
        for (const std::uint32_t formId : early) {
            RE::FormID resolvedId = 0;
            if (!resolve(formId, resolvedId)) continue;
            // Saves from before 2026-10-07 can hold a power or a schoolless spell a tome taught weakened;
            // dropping it here gives it back its full power and its own name
            // (OnRevert already put the name back, and nothing renames it again)
            if (CanLearnEarly(RE::TESForm::LookupByID<RE::SpellItem>(resolvedId))) {
                m_earlyLearnedSpells.insert(resolvedId);
            } else {
                ++notSpells;
            }
        }
        for (const std::uint32_t formId : held) {
            RE::FormID resolvedId = 0;
            if (resolve(formId, resolvedId) && m_earlyLearnedSpells.contains(resolvedId)) {
                m_heldForISL.insert(resolvedId);
            }
        }

        logger::info("SpellEffectivenessHook: Loaded {} early-learned spells ({} held for ISL, record version {})",
            m_earlyLearnedSpells.size(), m_heldForISL.size(), version);
        if (notSpells > 0) {
            logger::info("SpellEffectivenessHook: Dropped {} powers, abilities or schoolless spells from the early-learned list", notSpells);
        }
        m_earlySpellCount.store(m_earlyLearnedSpells.size(), std::memory_order_release);
    }
    return true;
}

void SpellEffectivenessHook::OnRevert([[maybe_unused]] SKSE::SerializationInterface* a_intfc)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);

    // Names and descriptions live on the forms, and the forms outlive the
    // save: loading another one does not reset them. Forgetting the originals
    // here without putting them back left "(Learning - 20%)" on the spell,
    // and the next grant then stored that as the original - seen in game as
    // "Spell (Learning - 20%) (Learning - 20%)", and after mastery the name
    // stayed wrong until the game was restarted. Put them back first.
    PutBackOriginalTextLocked();

    m_earlyLearnedSpells.clear();
    m_heldForISL.clear();
    m_heldListLoaded = false;
    m_earlySpellCount.store(0, std::memory_order_release);
    m_displayCache.clear();
    m_originalSpellNames.clear();
    m_originalEffectDescriptions.clear();
    m_effectSpellTracking.clear();
    m_progressMirror.clear();
    logger::info("SpellEffectivenessHook: Cleared all early-learned spell data on revert");
}

void SpellEffectivenessHook::PutBackOriginalTextLocked()
{
    for (const auto& [spellId, name] : m_originalSpellNames) {
        if (auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(spellId)) {
            spell->fullName = name;
        }
    }
    for (const auto& [effectId, description] : m_originalEffectDescriptions) {
        if (auto* effect = RE::TESForm::LookupByID<RE::EffectSetting>(effectId)) {
            effect->magicItemDescription = description;
        }
    }
    if (!m_originalSpellNames.empty() || !m_originalEffectDescriptions.empty()) {
        logger::info("SpellEffectivenessHook: Put back {} spell names and {} effect descriptions",
            m_originalSpellNames.size(), m_originalEffectDescriptions.size());
    }
}

// =============================================================================
// LOAD REFRESH
// =============================================================================

// Game thread (queued by EndLoad, once the save is in). A spell the player does
// not have is added back only by the grant rule (ShouldGrantEarly): one ISL holds
// stays out until ISL teaches it, and keeps its own name meanwhile (ISL shows it).
void SpellEffectivenessHook::RefreshAllSpellDisplays()
{
    logger::info("SpellEffectivenessHook: Refreshing all spell displays after load...");

    std::unordered_set<RE::FormID> spellsCopy;
    bool heldListLoaded = false;
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        spellsCopy = m_earlyLearnedSpells;
        heldListLoaded = m_heldListLoaded;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    std::size_t leftOut = 0;
    for (RE::FormID spellId : spellsCopy) {
        auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(spellId);
        if (!spell || !player) continue;

        ReleaseHeldIfOwned(spell);
        if (!player->HasSpell(spell)) {
            // A v1 save does not say what ISL holds: with ISL active, a spell the
            // player lacks is taken as held (a re-grant taught it before the study)
            if (!heldListLoaded && DESTIntegration::IsActive()) {
                std::unique_lock<std::shared_mutex> lock(m_mutex);
                m_heldForISL.insert(spellId);
            }
            if (!ShouldGrantEarly(spell)) {
                ++leftOut;
                logger::info("SpellEffectivenessHook: {} ({:08X}) not re-granted on load - held for ISL or a copy known",
                    spell->GetName(), spellId);
                continue;
            }
            player->AddSpell(spell);
            logger::info("SpellEffectivenessHook: Re-granted spell {} on load", spell->GetName());
        }

        UpdateSpellDisplayCache(spellId, spell);
        // Apply the modified name and description to the actual spell
        ApplyModifiedSpellName(spellId);
        ApplyModifiedDescriptions(spellId);
    }

    logger::info("SpellEffectivenessHook: Refreshed {} spell displays ({} left to ISL or a known copy)",
        spellsCopy.size() - leftOut, leftOut);
}

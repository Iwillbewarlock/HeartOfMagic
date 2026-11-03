// =============================================================================
// ProgressionManagerMastery.cpp - mastery ranks: XP past 100% from casting a mastered spell
// =============================================================================
//
// A mastered tree spell keeps earning XP when the player casts it (MasteryMath.h for the numbers:
// ranks, the XP a rank costs by tier, the skill factor). The XP is kept per tree spell - a copy
// of it (another plugin's record, SpellScanner::DuplicateGroupOf) earns for the tree's record -
// in m_masteryXP, apart from the learning progress, and saved in its own record, SLMX. This base
// has no effect on the spell yet: XP, ranks, the save, the panel and a read API.
//
// Which cast earns is decided before the cast's learning XP (OnSpellCast): the cast that takes a
// spell from 99% to 100% earns learning XP, not mastery XP. Game thread only, as the rest of
// ProgressionManager.

#include "ProgressionManager.h"
#include "HudNotify.h"
#include "ProgressionNotify.h"
#include "SpellEffectivenessHook.h"
#include "SpellScanner.h"
#include "SKSE/SKSE.h"

#include <format>
#include <string>
#include <vector>

namespace
{
    // The school skill the cast's XP factor reads: the player's base value of the spell's school
    // (the levelled skill; a Fortify potion or enchantment does not raise mastery XP). 0 when the
    // spell has no magic school or there is no player.
    float SchoolSkill(RE::SpellItem* spell)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        const auto school = SpellScanner::GetSpellSchool(spell);
        if (!player || !SpellScanner::IsValidMagicSchool(school)) return 0.0f;
        auto* owner = player->AsActorValueOwner();
        return owner ? owner->GetBaseActorValue(school) : 0.0f;
    }
}

// =============================================================================
// SETTINGS AND KEYS
// =============================================================================

void ProgressionManager::SetMasterySettings(const MasteryMath::Settings& settings)
{
    m_mastery = settings;
    logger::info("ProgressionManager: mastery ranks {} - {} ranks, tier XP {:.0f}/{:.0f}/{:.0f}/{:.0f}/{:.0f}, "
                 "{} rank multiplier(s), skill factor {:.2f} -> {:.2f}, XP rate x{:.2f}",
                 m_mastery.enabled ? "on" : "off", m_mastery.ranks, m_mastery.tierXP[0], m_mastery.tierXP[1],
                 m_mastery.tierXP[2], m_mastery.tierXP[3], m_mastery.tierXP[4], m_mastery.rankMultipliers.size(),
                 m_mastery.skillAtMin, m_mastery.skillAt100, m_mastery.xpRate);
}

RE::FormID ProgressionManager::MasteryKeyOf(RE::FormID spellId) const
{
    // The tree's record of the spell's copy group (the one member the tree holds), else the spell
    auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(spellId);
    if (!spell) return spellId;
    auto* tree = SpellScanner::TreeSpellForTome(spell);
    return tree ? tree->GetFormID() : spellId;
}

std::size_t ProgressionManager::MasteryTierOf(RE::FormID key) const
{
    auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(key);
    return spell ? MasteryMath::TierIndex(SpellScanner::DetermineSpellTier(spell)) : 0;
}

RE::FormID ProgressionManager::MasteryKeyForCast(RE::FormID castSpellId) const
{
    if (!m_mastery.enabled) return 0;
    auto* castSpell = RE::TESForm::LookupByID<RE::SpellItem>(castSpellId);
    if (!castSpell) return 0;
    const RE::FormID key = MasteryKeyOf(castSpellId);
    if (!IsTreeSpell(key)) return 0;

    // The player has the spell cast, the tree's record or a copy of it
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* keySpell = RE::TESForm::LookupByID<RE::SpellItem>(key);
    const bool owned = player && (player->HasSpell(castSpell) || (keySpell && player->HasSpell(keySpell)) ||
                                  PlayerKnowsCopyOf(key));
    if (!owned) return 0;

    // Still being learned: weakened (early learned) or waiting for ISL to teach it
    auto* hook = SpellEffectivenessHook::GetSingleton();
    if (hook->IsEarlyLearnedSpell(key) || hook->IsEarlyLearnedSpell(castSpellId) || hook->IsHeldForISL(key)) return 0;

    // Mastered: unlocked here, at 100%, or known from outside the tree (IsSpellMastered, copies too)
    return (IsUnlocked(key) || IsSpellMastered(key)) ? key : 0;
}

// =============================================================================
// EARNING
// =============================================================================

void ProgressionManager::OnMasteredCast(RE::FormID masteryKey, RE::FormID castSpellId, float baseXP)
{
    auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(masteryKey);
    if (!spell || masteryKey == 0) return;
    const auto tier = MasteryTierOf(masteryKey);
    const float skill = SchoolSkill(spell);
    const float gain = MasteryMath::GainFor(baseXP, skill, m_mastery);
    const float total = MasteryMath::TotalXP(m_mastery, tier);
    const auto found = m_masteryXP.find(masteryKey);
    const float before = found != m_masteryXP.end() ? found->second : 0.0f;
    const float after = MasteryMath::Accumulate(before, gain, total);
    if (!(after > before)) {
        logger::debug("Mastery: cast {:08X} for {} ({:08X}) adds nothing ({:.1f} of {:.1f})", castSpellId,
                      spell->GetName(), masteryKey, before, total);
        return;
    }
    m_masteryXP[masteryKey] = after;
    m_dirty = true;

    const int rankBefore = MasteryMath::RankFor(before, m_mastery, tier);
    const int rankAfter = MasteryMath::RankFor(after, m_mastery, tier);
    // Debug: every cast of every mastered spell lands here; the rank-up line is info
    logger::debug("Mastery: +{:.2f} to {} ({:08X}) from cast {:08X} (base {:.2f}, skill {:.0f}): {:.1f} / {:.1f}, "
                  "rank {}/{}",
                  after - before, spell->GetName(), masteryKey, castSpellId, baseXP, skill, after, total, rankAfter,
                  m_mastery.ranks);
    if (rankAfter > rankBefore) {
        logger::info("Mastery: {} ({:08X}) reached rank {} of {} ({:.1f} XP)", spell->GetName(), masteryKey,
                     rankAfter, m_mastery.ranks, after);
        HudNotify::Show("hud.masteryRankUp", "{{spell}}: mastery rank {{rank}} of {{ranks}}",
                        { { "spell", spell->GetName() },
                          { "rank", std::to_string(rankAfter) },
                          { "ranks", std::to_string(m_mastery.ranks) } });
        // SpellLearning_MasteryRankUp: the school the way SpellMastered names it, the rank reached, the spell
        SendModEvent("SpellLearning_MasteryRankUp", EventSchoolOf(masteryKey), static_cast<float>(rankAfter), spell);
    }
    // The panel's card follows (UIManager::NotifyProgressUpdate carries the mastery fields)
    // (the entry's own required XP when it has one: GetRequiredXP walks the known-higher-spell links)
    const auto progress = m_spellProgress.find(masteryKey);
    if (progress != m_spellProgress.end() && progress->second.requiredXP > 0.0f) {
        ProgressionNotify::Progress(masteryKey, progress->second.GetCurrentXP(), progress->second.requiredXP);
    } else {
        ProgressionNotify::Progress(masteryKey, 0.0f, GetRequiredXP(masteryKey));
    }
}

void ProgressionManager::ClearMastery(RE::FormID spellId)
{
    const RE::FormID key = MasteryKeyOf(spellId);
    std::size_t erased = m_masteryXP.erase(key);
    if (key != spellId) erased += m_masteryXP.erase(spellId);
    if (erased == 0) return;
    m_dirty = true;
    logger::info("Mastery: {:08X} mastery XP cleared (progress below 100% or relocked)", key);
}

// =============================================================================
// QUERIES
// =============================================================================

float ProgressionManager::GetMasteryXP(RE::FormID spellId) const
{
    const auto found = m_masteryXP.find(MasteryKeyOf(spellId));
    return found != m_masteryXP.end() ? found->second : 0.0f;
}

MasteryMath::Progress ProgressionManager::GetMasteryProgress(RE::FormID spellId) const
{
    const RE::FormID key = MasteryKeyOf(spellId);
    return MasteryMath::ProgressFor(GetMasteryXP(key), m_mastery, MasteryTierOf(key));
}

int ProgressionManager::GetMasteryRank(RE::FormID spellId) const
{
    return GetMasteryProgress(spellId).rank;
}

void ProgressionManager::SetMasteryXPForTest(RE::FormID spellId, float xp)
{
    const RE::FormID key = MasteryKeyOf(spellId);
    if (MasteryMath::IsValidSavedXP(xp)) {
        m_masteryXP[key] = xp;
    } else {
        m_masteryXP.erase(key);
    }
}

// =============================================================================
// CO-SAVE (SLMX)
// =============================================================================

void ProgressionManager::SaveMasteryRecord(SKSE::SerializationInterface* a_intfc) const
{
    std::vector<ProgressionCoSave::MasteryEntry> entries;
    entries.reserve(m_masteryXP.size());
    for (const auto& [formId, xp] : m_masteryXP) {
        if (MasteryMath::IsValidSavedXP(xp)) entries.push_back({ formId, xp });
    }
    if (!a_intfc->OpenRecord(kMasteryRecord, ProgressionCoSave::kMasteryVersion) ||
        !ProgressionCoSave::WriteMasteryEntries(*a_intfc, entries)) {
        logger::error("ProgressionManager: Failed to write the mastery record ({} spells)", entries.size());
        return;
    }
    logger::info("ProgressionManager: Saved mastery XP for {} spells", entries.size());
}

bool ProgressionManager::ReadMasteryRecord(SKSE::SerializationInterface* a_intfc, uint32_t version, uint32_t length)
{
    if (version < 1 || version > ProgressionCoSave::kMasteryVersion) {
        logger::warn("ProgressionManager: Skipping mastery record version {} (written by a newer version?)", version);
        return true;
    }
    std::vector<ProgressionCoSave::MasteryEntry> saved;
    const auto result = ProgressionCoSave::ReadMasteryEntries(*a_intfc, length, saved);
    if (result != ProgressionCoSave::ReadResult::Ok) {
        logger::error("ProgressionManager: mastery record unreadable ({}), {} entries read before it kept",
                      ProgressionCoSave::Describe(result), saved.size());
    }
    std::size_t dropped = 0;
    for (const auto& e : saved) {
        RE::FormID resolvedId = 0;
        if (!MasteryMath::IsValidSavedXP(e.xp) || !a_intfc->ResolveFormID(e.formId, resolvedId)) {
            ++dropped;
            continue;
        }
        // Two saved ids resolving to one spell (merged plugins): the larger XP
        auto& held = m_masteryXP[resolvedId];
        held = (std::max)(held, e.xp);
    }
    logger::info("ProgressionManager: Loaded mastery XP for {} spells ({} dropped: bad value or plugin gone)",
                 m_masteryXP.size(), dropped);
    return true;
}

void ProgressionManager::EndLoadMastery()
{
    // Only spells: an id that resolved to another kind of form is no spell's mastery
    const auto dropped = std::erase_if(m_masteryXP, [](const auto& entry) {
        return RE::TESForm::LookupByID<RE::SpellItem>(entry.first) == nullptr;
    });
    if (dropped > 0) logger::info("ProgressionManager: {} mastery entries dropped at load end (not a spell)", dropped);
}

// =============================================================================
// SELF-TEST: the learning targets set aside for a scope
// =============================================================================

ProgressionManager::LearningTargetsAside::LearningTargetsAside(ProgressionManager& manager) :
    m_manager(manager), m_targets(std::move(manager.m_learningTargets))
{
    m_manager.m_learningTargets.clear();
}

ProgressionManager::LearningTargetsAside::~LearningTargetsAside()
{
    m_manager.m_learningTargets = std::move(m_targets);
}

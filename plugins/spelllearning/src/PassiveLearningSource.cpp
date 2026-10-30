#include "PassiveLearningSource.h"
#include "ThreadUtils.h"

#include <chrono>

#include "ProgressionManager.h"
#include "ProgressionMath.h"
#include "SpellScanner.h"

namespace SpellLearning {

PassiveLearningSource* PassiveLearningSource::s_singleton = nullptr;

PassiveLearningSource* PassiveLearningSource::GetSingleton() {
    return s_singleton;
}

void PassiveLearningSource::Initialize() {
    s_singleton = this;

    // Register as an internal XP source for per-spell cap tracking (not shown in modded UI)
    ProgressionManager::GetSingleton()->RegisterModdedXPSource(ProgressionManager::kPassiveSource, "Passive Learning", true);

    auto* calendar = RE::Calendar::GetSingleton();
    if (calendar) {
        m_lastGameTime = calendar->GetHoursPassed();  // hours: GetCurrentGameTime is days
        m_initialized = true;
    }

    // Start polling thread
    m_running = true;
    m_pollThread = std::thread(&PassiveLearningSource::PollLoop, this);

    logger::info("PassiveLearningSource: Initialized (enabled: {}, scope: {}, xp/day: {})",
        m_settings.enabled, m_settings.scope, m_settings.xpPerGameDay);
}

void PassiveLearningSource::Shutdown() {
    BaseXPSource::Shutdown();
    m_running = false;
    if (m_pollThread.joinable()) {
        m_pollThread.join();
    }
    logger::info("PassiveLearningSource: Shutdown");
}

void PassiveLearningSource::SetSettings(const Settings& settings) {
    std::lock_guard<std::mutex> lock(m_settingsMutex);
    m_settings = settings;
    m_enabled = settings.enabled;
    logger::info("PassiveLearningSource: Settings updated - enabled: {}, scope: {}, xp/day: {}",
        settings.enabled, settings.scope, settings.xpPerGameDay);
}

void PassiveLearningSource::OnGameLoad() {
    auto* calendar = RE::Calendar::GetSingleton();
    if (calendar) {
        m_lastGameTime = calendar->GetHoursPassed();
        m_initialized = true;
        logger::info("PassiveLearningSource: Game loaded, game time: {:.2f} hours", m_lastGameTime);
    }
}

void PassiveLearningSource::PollLoop() {
    while (m_running) {
        // Sleep 3 real-time seconds between checks
        std::this_thread::sleep_for(std::chrono::seconds(3));

        if (!m_running) break;

        // Quick check without lock
        if (!m_initialized) continue;

        // Must read Calendar on the game thread. Also while off: the last time then follows
        // the clock (PassiveTickFor), or switching it on paid every hour it was off
        AddTaskToGameThread("PassiveLearningTick", [this]() {
            auto* calendar = RE::Calendar::GetSingleton();
            if (!calendar) return;
            bool enabled = false;
            {
                std::lock_guard<std::mutex> lock(m_settingsMutex);
                enabled = m_settings.enabled;
            }

            // Game hours (the 0.1 h floor): GetCurrentGameTime is in days
            const auto tick = ProgressionMath::PassiveTickFor(m_lastGameTime, calendar->GetHoursPassed(), enabled);
            m_lastGameTime = tick.lastHours;
            if (tick.grantHours > 0.0f) {
                GrantPassiveXP(tick.grantHours);
            }
        });
    }
}

void PassiveLearningSource::GrantPassiveXP(float gameHoursElapsed) {
    Settings currentSettings;
    {
        std::lock_guard<std::mutex> lock(m_settingsMutex);
        currentSettings = m_settings;
    }

    if (!currentSettings.enabled || currentSettings.xpPerGameDay <= 0.0f) return;

    auto* pm = ProgressionManager::GetSingleton();
    if (!pm) return;

    float baseXP = ProgressionMath::PassiveXPFor(currentSettings.xpPerGameDay, gameHoursElapsed);

    // Iterate all learning targets and grant passive XP
    // m_learningTargets is school -> formId, access via public API
    const std::string schools[] = {"Alteration", "Conjuration", "Destruction", "Illusion", "Restoration"};

    int granted = 0;
    for (const auto& school : schools) {
        RE::FormID targetId = pm->GetLearningTarget(school);
        if (targetId == 0) continue;

        // Check if spell is eligible for passive learning based on scope
        if (!IsSpellEligible(targetId)) continue;

        auto progress = pm->GetProgress(targetId);

        // Skip if already mastered
        if (progress.unlocked && progress.progressPercent >= 1.0f) continue;

        // Check tier cap for passive source
        float tierCap = GetTierCap(targetId);
        if (tierCap <= 0.0f) continue;

        // Calculate max XP allowed from passive for this spell
        float maxPassiveXP = (tierCap / 100.0f) * progress.requiredXP;

        // Check how much passive XP has already been granted
        float alreadyGranted = 0.0f;
        auto it = progress.xpFromModded.find(ProgressionManager::kPassiveSource);
        if (it != progress.xpFromModded.end()) {
            alreadyGranted = it->second;
        }

        float remaining = ProgressionMath::CapRemaining(maxPassiveXP, alreadyGranted);
        if (remaining <= 0.0f) continue;

        // The tier cap is this source's only cap: AddInternalSourcedXP tracks "passive" (an
        // internal source) but neither caps it nor scales it beyond the overall
        // multiplier, which it applies after this - so ask for the amount that
        // lands inside the cap once multiplied
        const float xpToGrant =
            ProgressionMath::PreMultiplierGrant(baseXP, remaining, pm->GetGainRates(targetId).globalMultiplier);
        if (xpToGrant <= 0.0f) continue;

        float actual = pm->AddInternalSourcedXP(targetId, xpToGrant, ProgressionManager::kPassiveSource);
        if (actual > 0.0f) {
            granted++;
            logger::trace("PassiveLearningSource: Granted {:.1f} XP to {:08X} ({:.1f} hrs elapsed)",
                actual, targetId, gameHoursElapsed);
        }
    }

    if (granted > 0) {
        logger::info("PassiveLearningSource: Granted passive XP to {} spell(s) ({:.1f} game hours, {:.1f} base XP)",
            granted, gameHoursElapsed, baseXP);
    }
}

float PassiveLearningSource::GetTierCap(RE::FormID spellId) const {
    // The spell's own tier, not one read off its required XP: a spell learned
    // downward costs a share of its XP (reverse unlock) and would pass for a lower tier
    Settings currentSettings;
    {
        std::lock_guard<std::mutex> lock(m_settingsMutex);
        currentSettings = m_settings;
    }

    const std::string tier = SpellScanner::DetermineSpellTier(RE::TESForm::LookupByID<RE::SpellItem>(spellId));
    if (tier == "Apprentice") return currentSettings.maxApprentice;
    if (tier == "Adept") return currentSettings.maxAdept;
    if (tier == "Expert") return currentSettings.maxExpert;
    if (tier == "Master") return currentSettings.maxMaster;
    return currentSettings.maxNovice;
}

bool PassiveLearningSource::IsSpellEligible(RE::FormID spellId) const {
    Settings currentSettings;
    {
        std::lock_guard<std::mutex> lock(m_settingsMutex);
        currentSettings = m_settings;
    }

    // "all" - every learning target gets passive XP
    if (currentSettings.scope == "all") return true;

    auto* pm = ProgressionManager::GetSingleton();

    // "root" - only spells with no prerequisites (root/starting spells)
    if (currentSettings.scope == "root") {
        auto reqs = pm->GetPrereqRequirements(spellId);
        return reqs.hardPrereqs.empty() && reqs.softPrereqs.empty();
    }

    // "novice" - only novice-tier spells (by the spell's tier, see GetTierCap)
    if (currentSettings.scope == "novice") {
        return SpellScanner::DetermineSpellTier(RE::TESForm::LookupByID<RE::SpellItem>(spellId)) == "Novice";
    }

    return false;
}

} // namespace SpellLearning

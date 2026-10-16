// =============================================================================
// ProgressionManagerAPI.cpp — Public modder API (sourced XP, raw XP, sources)
// =============================================================================

#include "ProgressionManager.h"
#include "uimanager/UIManager.h"
#include "ProgressionMath.h"

// =============================================================================
// PUBLIC MODDER API
// =============================================================================

bool ProgressionManager::IsReservedSourceName(const std::string& sourceName) const
{
    if (sourceName == kPassiveSource || sourceName == kStudySource) return true;
    const auto it = m_xpSettings.moddedSources.find(sourceName);
    return it != m_xpSettings.moddedSources.end() && it->second.internal;
}

bool ProgressionManager::AcceptExternalSourceName(const std::string& sourceName) const
{
    // Once per session: a mod calling in a loop would fill the log
    static bool s_warned = false;
    const char* reason = nullptr;
    if (IsReservedSourceName(sourceName)) {
        reason = "it is one of Heart of Magic's own sources";
    } else if (!ProgressionCoSave::IsWritableSourceName(sourceName)) {
        reason = "the name is longer than the co-save can hold";
    }
    if (!reason) return true;
    if (!s_warned) {
        s_warned = true;
        logger::warn("ProgressionManager: XP source '{:.64}' ({} bytes) refused: {} (further refusals not logged)",
                     sourceName, sourceName.size(), reason);
    }
    return false;
}

bool ProgressionManager::RegisterModdedXPSource(const std::string& sourceId, const std::string& displayName, bool internal)
{
    // Check if already registered
    if (m_xpSettings.moddedSources.find(sourceId) != m_xpSettings.moddedSources.end()) {
        logger::info("ProgressionManager: Modded source '{}' already registered", sourceId);
        return false;
    }
    if (!internal && !AcceptExternalSourceName(sourceId)) {
        return false;
    }
    if (m_xpSettings.moddedSources.size() >= ProgressionCoSave::kMaxModdedSources) {
        logger::warn("ProgressionManager: XP source '{:.64}' not registered: {} sources already", sourceId,
                     m_xpSettings.moddedSources.size());
        return false;
    }

    // Create default config
    ModdedSourceConfig config;
    config.displayName = displayName.empty() ? sourceId : displayName;
    config.enabled = true;
    config.multiplier = 100.0f;
    config.cap = 25.0f;
    config.internal = internal;
    m_xpSettings.moddedSources[sourceId] = config;

    logger::info("ProgressionManager: Registered {} XP source '{}' (display: '{}')",
        internal ? "internal" : "modded", sourceId, config.displayName);

    // Only notify UI for external (non-internal) sources
    if (!internal) {
        UIManager::GetSingleton()->NotifyModdedSourceRegistered(sourceId, config.displayName, config.multiplier, config.cap);
    }

    // Fire ModEvent
    SendModEvent("SpellLearning_SourceRegistered", sourceId, 0.0f, nullptr);

    return true;
}

float ProgressionManager::AddSourcedXP(RE::FormID targetId, float amount, const std::string& sourceName)
{
    return AddSourcedXPImpl(targetId, amount, sourceName, false);
}

float ProgressionManager::AddInternalSourcedXP(RE::FormID targetId, float amount, const std::string& sourceName)
{
    return AddSourcedXPImpl(targetId, amount, sourceName, true);
}

float ProgressionManager::AddSourcedXPImpl(RE::FormID targetId, float amount, const std::string& sourceName,
                                           bool internalCaller)
{
    if (targetId == 0 || !(amount > 0.0f)) return 0.0f;
    const bool builtIn = sourceName == "any" || sourceName == "school" || sourceName == "direct" || sourceName == "self";
    if (!builtIn && !internalCaller && !AcceptExternalSourceName(sourceName)) return 0.0f;

    auto& progress = EnsureProgress(targetId);

    // Already mastered
    if (progress.unlocked && progress.progressPercent >= 1.0f) return 0.0f;

    // Apply global multiplier (a spell learned downward can have its own rates)
    const XPGainRates rates = GetGainRates(targetId);
    float adjustedAmount = amount * rates.globalMultiplier;

    // Built-in sources
    if (builtIn) {
        float maxFromSource = 0.0f;
        float currentFromSource = 0.0f;

        if (sourceName == "any") {
            adjustedAmount *= rates.multiplierAny;
            maxFromSource = progress.requiredXP * (rates.capAny / 100.0f);
            currentFromSource = progress.xpFromAny;
        } else if (sourceName == "school") {
            adjustedAmount *= rates.multiplierSchool;
            maxFromSource = progress.requiredXP * (rates.capSchool / 100.0f);
            currentFromSource = progress.xpFromSchool;
        } else if (sourceName == "direct") {
            adjustedAmount *= rates.multiplierDirect;
            maxFromSource = progress.requiredXP * (rates.capDirect / 100.0f);
            currentFromSource = progress.xpFromDirect;
        } else {  // "self" - no cap
            // Self-casting uses the direct multiplier — casting the target spell IS direct interaction
            adjustedAmount *= rates.multiplierDirect;
            maxFromSource = progress.requiredXP * (ProgressionMath::kNoCapPercent / 100.0f);
            currentFromSource = progress.xpFromSelf;
        }

        float remaining = maxFromSource - currentFromSource;
        adjustedAmount = (std::min)(adjustedAmount, (std::max)(0.0f, remaining));

        if (adjustedAmount > 0.0f) {
            if (sourceName == "any")         progress.xpFromAny += adjustedAmount;
            else if (sourceName == "school") progress.xpFromSchool += adjustedAmount;
            else if (sourceName == "direct") progress.xpFromDirect += adjustedAmount;
            else                             progress.xpFromSelf += adjustedAmount;
        }
    }
    // Modded sources
    else {
        // Auto-register if unknown (refused when the registry is full)
        if (m_xpSettings.moddedSources.find(sourceName) == m_xpSettings.moddedSources.end() &&
            !RegisterModdedXPSource(sourceName, sourceName, internalCaller)) {
            return 0.0f;
        }
        // A spell keeps at most kMaxModdedSources sources: the co-save reads no more
        if (!progress.xpFromModded.contains(sourceName) &&
            progress.xpFromModded.size() >= ProgressionCoSave::kMaxModdedSources) {
            return 0.0f;
        }

        auto& srcConfig = m_xpSettings.moddedSources[sourceName];
        if (!srcConfig.enabled) return 0.0f;

        // An internal source (passive learning) applies its own cap and rate: the
        // modded defaults (cap 25%) cut passive's per-tier caps short at 25%
        if (!srcConfig.internal) {
            adjustedAmount *= (srcConfig.multiplier / 100.0f);
            const float maxFromSource = progress.requiredXP * (srcConfig.cap / 100.0f);
            adjustedAmount = ProgressionMath::GrantWithinCap(adjustedAmount, maxFromSource,
                                                             progress.xpFromModded[sourceName]);
        }

        if (adjustedAmount > 0.0f) {
            progress.xpFromModded[sourceName] += adjustedAmount;
        }
    }

    if (adjustedAmount > 0.0f) {
        AddXP(targetId, adjustedAmount);
        // BookXP's source reads as "book" in the log; any other mod's by its id
        LogXPGrant(sourceName == "book_reading" ? std::string("book") : "source " + sourceName, targetId, adjustedAmount,
                   std::format("{:.2f} asked", amount));
        SendModEvent("SpellLearning_XPGained", sourceName, adjustedAmount,
                      RE::TESForm::LookupByID(targetId));
    }

    return adjustedAmount;
}

float ProgressionManager::AddRawXP(RE::FormID targetId, float amount)
{
    if (targetId == 0 || amount <= 0.0f) return 0.0f;

    auto& progress = EnsureProgress(targetId);
    if (progress.unlocked && progress.progressPercent >= 1.0f) return 0.0f;

    // Clamp to remaining XP needed
    float currentXP = progress.GetCurrentXP();
    float remaining = progress.requiredXP - currentXP;
    float actualAmount = (std::min)(amount, (std::max)(0.0f, remaining));

    if (actualAmount > 0.0f) {
        AddXP(targetId, actualAmount);
        LogXPGrant("raw", targetId, actualAmount);
        SendModEvent("SpellLearning_XPGained", "raw", actualAmount,
                      RE::TESForm::LookupByID(targetId));
    }

    return actualAmount;
}

float ProgressionManager::GetSourceCap(const std::string& sourceName) const
{
    if (sourceName == "any")    return m_xpSettings.capAny;
    if (sourceName == "school") return m_xpSettings.capSchool;
    if (sourceName == "direct") return m_xpSettings.capDirect;
    if (sourceName == "self")   return ProgressionMath::kNoCapPercent;  // Self has no cap

    // Check modded sources
    auto it = m_xpSettings.moddedSources.find(sourceName);
    if (it != m_xpSettings.moddedSources.end()) {
        // An internal source is not capped here (AddSourcedXP): its own rules apply
        return it->second.internal ? ProgressionMath::kNoCapPercent : it->second.cap;
    }

    return 0.0f;  // Unknown source
}

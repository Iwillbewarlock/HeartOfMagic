// =============================================================================
// ProgressionManagerAPI.cpp — Public modder API (sourced XP, raw XP, sources)
// =============================================================================

#include "ProgressionManager.h"
#include "ProgressionNotify.h"
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
    if (auto known = m_xpSettings.moddedSources.find(sourceId); known != m_xpSettings.moddedSources.end()) {
        if (known->second.registered) {
            logger::info("ProgressionManager: Modded source '{}' already registered", sourceId);
            return false;
        }
        // Known only from config.json (ReadModdedSourcesFromConfig): this is the mod's own
        // registration. The player's saved settings for it (on/off, multiplier, cap) stay
        known->second.registered = true;
        known->second.internal = internal;
        // The mod's name for it, unless it gave none (AddSourcedXP registers with the id itself)
        if (!displayName.empty() && displayName != sourceId) known->second.displayName = displayName;
        logger::info("ProgressionManager: Registered {} XP source '{}' with its saved settings",
            internal ? "internal" : "modded", sourceId);
        if (!internal) {
            ProgressionNotify::SourceRegistered(sourceId, known->second.displayName,
                known->second.multiplier, known->second.cap, known->second.enabled);
        }
        SendModEvent("SpellLearning_SourceRegistered", sourceId, 0.0f, nullptr);
        return true;
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
        ProgressionNotify::SourceRegistered(sourceId, config.displayName, config.multiplier, config.cap);
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

    // Built-in sources: the cap is a share of the required XP, the count the share given so far
    if (builtIn) {
        float capShare = ProgressionMath::kNoCapPercent / 100.0f;
        float trackedShare = 0.0f;

        if (sourceName == "any") {
            adjustedAmount *= rates.multiplierAny;
            capShare = rates.capAny / 100.0f;
            trackedShare = progress.shareAny;
        } else if (sourceName == "school") {
            adjustedAmount *= rates.multiplierSchool;
            capShare = rates.capSchool / 100.0f;
            trackedShare = progress.shareSchool;
        } else if (sourceName == "direct") {
            adjustedAmount *= rates.multiplierDirect;
            capShare = rates.capDirect / 100.0f;
            trackedShare = progress.shareDirect;
        } else {  // "self" - no cap
            // Self-casting uses the direct multiplier — casting the target spell IS direct interaction
            adjustedAmount *= rates.multiplierDirect;
            trackedShare = progress.shareSelf;
        }

        adjustedAmount = (std::min)(adjustedAmount, ProgressionMath::CapRoom(capShare, trackedShare, progress.requiredXP));
    }
    // Modded sources
    else {
        // Auto-register if unknown (refused when the registry is full), or known only from
        // config.json: the mod's first XP is its registration (SourceRegistered, panel row)
        const auto known = m_xpSettings.moddedSources.find(sourceName);
        if ((known == m_xpSettings.moddedSources.end() || !known->second.registered) &&
            !RegisterModdedXPSource(sourceName, sourceName, internalCaller)) {
            return 0.0f;
        }
        // A spell keeps at most kMaxModdedSources sources: the co-save reads no more
        if (!progress.shareModded.contains(sourceName) &&
            progress.shareModded.size() >= ProgressionCoSave::kMaxModdedSources) {
            return 0.0f;
        }

        auto& srcConfig = m_xpSettings.moddedSources[sourceName];
        if (!srcConfig.enabled) return 0.0f;

        // An internal source (passive learning) applies its own cap and rate: the
        // modded defaults (cap 25%) cut passive's per-tier caps short at 25%
        if (!srcConfig.internal) {
            adjustedAmount *= (srcConfig.multiplier / 100.0f);
            const auto tracked = progress.shareModded.find(sourceName);
            const float trackedShare = tracked != progress.shareModded.end() ? tracked->second : 0.0f;
            adjustedAmount = ProgressionMath::GrantWithinCap(
                adjustedAmount, ProgressionMath::CapRoom(srcConfig.cap / 100.0f, trackedShare, progress.requiredXP), 0.0f);
        }
    }

    if (!(adjustedAmount > 0.0f)) return 0.0f;

    // What the grant really added (less at the top, 0 on a spell already at 100% waiting for Unlock):
    // the count against the source's cap, the log, the event and the answer all take it, so BookXP
    // does not mark a book read for a spell that took nothing from it
    const float added = AddXPMeasured(targetId, adjustedAmount);
    if (!(added > 0.0f)) {
        logger::debug("XP: {:.2f} from source {} to {:08X} added nothing (the spell is at 100%)", adjustedAmount,
                      sourceName, targetId);
        return 0.0f;
    }
    const float addedShare = progress.ShareOfXP(added);
    if (sourceName == "any")         progress.shareAny += addedShare;
    else if (sourceName == "school") progress.shareSchool += addedShare;
    else if (sourceName == "direct") progress.shareDirect += addedShare;
    else if (sourceName == "self")   progress.shareSelf += addedShare;
    else                             progress.shareModded[sourceName] += addedShare;
    // BookXP's source reads as "book" in the log; any other mod's by its id
    LogXPGrant(sourceName == "book_reading" ? std::string("book") : "source " + sourceName, targetId, added,
               std::format("{:.2f} asked", amount));
    SendModEvent("SpellLearning_XPGained", sourceName, added, RE::TESForm::LookupByID(targetId));
    return added;
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

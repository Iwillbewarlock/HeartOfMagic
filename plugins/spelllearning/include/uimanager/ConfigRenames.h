#pragma once

#include <nlohmann/json.hpp>

// =============================================================================
// CONFIG RENAMES - settings saved under an older name (UIManager::OnLoadUnifiedConfig)
// =============================================================================
//
// Run on what was read from a file before it is merged over the defaults: the
// defaults carry the new names, so after the merge an old name can no longer win.
// No game types, so progression-check compiles it as it is.
namespace ConfigRenames
{
    // passiveLearning.xpPerGameHour -> xpPerGameDay (1.6): the number always gave
    // that much per game day, so it moves over as it is. A file that has both keeps
    // the new one; the old one is dropped either way.
    inline void Migrate(nlohmann::json& config)
    {
        const auto passive = config.find("passiveLearning");
        if (passive == config.end() || !passive->is_object()) return;
        const auto old = passive->find("xpPerGameHour");
        if (old == passive->end()) return;
        if (!passive->contains("xpPerGameDay") && !old->is_null()) (*passive)["xpPerGameDay"] = *old;
        passive->erase("xpPerGameHour");
    }
}

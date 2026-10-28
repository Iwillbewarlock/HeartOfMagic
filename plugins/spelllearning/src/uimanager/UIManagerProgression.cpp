#include "Common.h"
#include "JsonText.h"
#include "uimanager/UIManager.h"
#include "uimanager/UIManagerInternal.h"
#include "uimanager/StartupState.h"
#include "ProgressionManager.h"
#include "SpellEffectivenessHook.h"
#include "ThreadUtils.h"

// =============================================================================
// PROGRESSION SYSTEM CALLBACKS
// =============================================================================

void UIManager::OnSetLearningTarget(const char* argument)
{
    if (!argument || strlen(argument) == 0) {
        logger::warn("UIManager: SetLearningTarget - no data provided");
        return;
    }

    logger::info("UIManager: SetLearningTarget: {}", argument);

    std::string argStr(argument);

    AddTaskToGameThread("SetLearningTarget", [argStr]() {
        auto* instance = GetSingleton();
        if (!instance || !instance->m_prismaUI) return;

        try {
            json request = json::parse(argStr);
            std::string school = request.value("school", "");
            std::string formIdStr = request.value("formId", "");

            if (school.empty() || formIdStr.empty()) {
                logger::warn("UIManager: SetLearningTarget - missing school or formId");
                return;
            }

            const auto parsedId = ParsePanelFormId(formIdStr, "SetLearningTarget");
            if (!parsedId) return;
            const RE::FormID formId = *parsedId;

            // Parse prerequisites array if provided; one that is not a formId is skipped
            std::vector<RE::FormID> prereqs;
            if (request.contains("prerequisites") && request["prerequisites"].is_array()) {
                for (const auto& prereqJson : request["prerequisites"]) {
                    // Not text: skipped, as in SetTreePrerequisites (get<std::string>() threw
                    // and the whole target was not set)
                    if (!prereqJson.is_string()) {
                        logger::warn("UIManager: SetLearningTarget - a prerequisite that is not text skipped");
                        continue;
                    }
                    const auto prereqId = ParsePanelFormId(prereqJson.get<std::string>(), "SetLearningTarget prerequisite");
                    if (prereqId && *prereqId != 0) {
                        prereqs.push_back(*prereqId);
                    }
                }
                logger::info("UIManager: Received {} direct prerequisites for {:08X}", prereqs.size(), formId);
            }

            auto* pm = ProgressionManager::GetSingleton();
            pm->SetLearningTarget(school, formId, prereqs);

            // Set requiredXP from tree data if provided (syncs JS tree XP to C++)
            if (request.contains("requiredXP") && request["requiredXP"].is_number()) {
                float requiredXP = request["requiredXP"].get<float>();
                if (requiredXP > 0) {
                    pm->SetRequiredXP(formId, requiredXP, RequiredXP::Kind::kCharacter);
                    logger::info("UIManager: Set requiredXP for {:08X} to {:.0f} (from tree)", formId, requiredXP);
                }
            }

            // Notify UI
            json response;
            response["success"] = true;
            response["school"] = school;
            response["formId"] = formIdStr;
            instance->CallView("onLearningTargetSet", JsonText::Dump(response).c_str());

            // Update spell state to "learning" so canvas renderer shows learning visuals
            instance->UpdateSpellState(formIdStr, "learning");

        } catch (const std::exception& e) {
            logger::error("UIManager: SetLearningTarget exception: {}", e.what());
        }
    });
}

void UIManager::OnClearLearningTarget(const char* argument)
{
    if (!argument || strlen(argument) == 0) {
        return;
    }

    logger::info("UIManager: ClearLearningTarget: {}", argument);

    std::string argStr(argument);

    AddTaskToGameThread("ClearLearningTarget", [argStr]() {
        auto* instance = GetSingleton();
        if (!instance || !instance->m_prismaUI) return;

        try {
            json request = json::parse(argStr);
            std::string school = request.value("school", "");
            // The spell the panel means: cleared under whatever key holds it (the
            // panel's school key and the plugin's could differ in a save from before)
            const std::string formIdStr = request.value("formId", "");
            const auto parsedId = formIdStr.empty() ? std::optional<RE::FormID>(0) : ParsePanelFormId(formIdStr, "ClearLearningTarget");
            if (!parsedId) return;
            const RE::FormID spellId = *parsedId;

            if (!school.empty()) {
                // Get the current learning target formId BEFORE clearing
                RE::FormID targetId = ProgressionManager::GetSingleton()->GetLearningTarget(school);

                ProgressionManager::GetSingleton()->ClearLearningTarget(school);

                // Update UI to show spell is no longer in learning state
                if (targetId != 0) {
                    const auto targetText = FormIdText::Format(targetId);
                    instance->UpdateSpellState(targetText, "available");
                    logger::info("UIManager: Cleared learning target {} - set to available", targetText);
                }
            }
            if (spellId != 0) {
                ProgressionManager::GetSingleton()->ClearLearningTargetForSpell(spellId);
            }
        } catch (const std::exception& e) {
            logger::error("UIManager: ClearLearningTarget exception: {}", e.what());
        }
    });
}

void UIManager::OnUnlockSpell(const char* argument)
{
    if (!argument || strlen(argument) == 0) {
        logger::warn("UIManager: UnlockSpell - no formId provided");
        return;
    }

    logger::info("UIManager: UnlockSpell: {}", argument);

    std::string argStr(argument);

    AddTaskToGameThread("UnlockSpell", [argStr]() {
        auto* instance = GetSingleton();
        if (!instance || !instance->m_prismaUI) return;

        try {
            json request = json::parse(argStr);
            std::string formIdStr = request.value("formId", "");

            if (formIdStr.empty()) {
                logger::warn("UIManager: UnlockSpell - no formId");
                return;
            }

            const auto parsedId = ParsePanelFormId(formIdStr, "UnlockSpell");
            if (!parsedId) return;
            const RE::FormID formId = *parsedId;

            bool success = ProgressionManager::GetSingleton()->UnlockSpell(formId);

            instance->NotifySpellUnlocked(formId, success);

            if (success) {
                instance->UpdateSpellState(formIdStr, "unlocked");
            }

        } catch (const std::exception& e) {
            logger::error("UIManager: UnlockSpell exception: {}", e.what());
        }
    });
}

void UIManager::OnGetProgress([[maybe_unused]] const char* argument)
{
    logger::info("UIManager: GetProgress requested");

    AddTaskToGameThread("GetProgress", []() {
        auto* instance = GetSingleton();
        if (!instance || !instance->m_prismaUI) return;

        std::string progressJson = ProgressionManager::GetSingleton()->GetProgressJSON();
        instance->SendProgressData(progressJson);
    });
}

void UIManager::OnGetPlayerKnownSpells([[maybe_unused]] const char* argument)
{
    logger::info("UIManager: GetPlayerKnownSpells requested");

    AddTaskToGameThread("GetPlayerKnownSpells", []() {
        auto* instance = GetSingleton();
        if (!instance || !instance->m_prismaUI) return;

        auto* player = RE::PlayerCharacter::GetSingleton();

        if (!player) {
            logger::error("UIManager: Cannot get player spells - player not found");
            return;
        }

        json result;
        json knownSpells = json::array();
        json weakenedSpells = json::array();  // Track which spells are early-learned/weakened
        std::set<RE::FormID> foundSpells;  // Track to avoid duplicates

        // Get effectiveness hook for checking weakened state
        auto* effectivenessHook = SpellEffectivenessHook::GetSingleton();

        // Helper lambda to check if a spell is a valid combat spell (not ability/passive)
        auto isValidCombatSpell = [](RE::SpellItem* spell) -> bool {
            if (!spell) return false;

            // Filter by spell type - only include actual spells, not abilities/powers/etc
            auto spellType = spell->GetSpellType();
            if (spellType != RE::MagicSystem::SpellType::kSpell) {
                return false;
            }

            // Must have a casting type (not constant effect)
            auto castType = spell->GetCastingType();
            if (castType == RE::MagicSystem::CastingType::kConstantEffect) {
                return false;
            }

            // Must have a magicka cost (filters out free abilities)
            auto* costEffect = spell->GetCostliestEffectItem();
            if (!costEffect || !costEffect->baseEffect) {
                return false;
            }

            // Check it's from a magic school
            auto school = costEffect->baseEffect->GetMagickSkill();
            if (school != RE::ActorValue::kAlteration &&
                school != RE::ActorValue::kConjuration &&
                school != RE::ActorValue::kDestruction &&
                school != RE::ActorValue::kIllusion &&
                school != RE::ActorValue::kRestoration) {
                return false;
            }

            return true;
        };

        // One entry per spell, whichever list it came from. Per-spell lines are
        // trace: this runs on every panel open and a late game knows hundreds.
        auto addKnownSpell = [&](RE::SpellItem* spell, const char* source) {
            if (!spell || foundSpells.contains(spell->GetFormID())) return;
            if (!isValidCombatSpell(spell)) {
                logger::trace("UIManager: Skipping non-combat spell/ability: {} ({:08X})",
                    spell->GetName(), spell->GetFormID());
                return;
            }
            foundSpells.insert(spell->GetFormID());
            std::string formIdStr = FormIdText::Format(spell->GetFormID());

            // Check if this spell is weakened (early-learned)
            const bool weakened = effectivenessHook && effectivenessHook->IsEarlyLearnedSpell(spell->GetFormID());
            logger::trace("UIManager: Player {} spell: {} ({}){}", source, spell->GetName(), formIdStr,
                weakened ? " [WEAKENED]" : "");
            if (weakened) {
                weakenedSpells.push_back(formIdStr);
            }
            knownSpells.push_back(std::move(formIdStr));
        };

        // Get the player's spell list from ActorBase
        auto* actorBase = player->GetActorBase();
        if (actorBase) {
            auto* spellList = actorBase->GetSpellList();
            if (spellList && spellList->spells) {
                for (uint32_t i = 0; i < spellList->numSpells; ++i) {
                    addKnownSpell(spellList->spells[i], "knows");
                }
            }
        }

        // Also check spells added at runtime via AddSpell
        for (auto* spell : player->GetActorRuntimeData().addedSpells) {
            addKnownSpell(spell, "added");
        }

        result["knownSpells"] = knownSpells;
        result["weakenedSpells"] = weakenedSpells;  // Include list of early-learned spells
        result["count"] = knownSpells.size();

        logger::info("UIManager: Found {} valid combat spells", knownSpells.size());
        instance->CallView("onPlayerKnownSpells", JsonText::Dump(result).c_str());
    });
}

void UIManager::OnCheatUnlockSpell(const char* argument)
{
    if (!argument || strlen(argument) == 0) {
        logger::warn("UIManager: CheatUnlockSpell - no formId provided");
        return;
    }

    logger::info("UIManager: CheatUnlockSpell (cheat mode): {}", argument);

    std::string argStr(argument);

    AddTaskToGameThread("CheatUnlockSpell", [argStr]() {
        auto* instance = GetSingleton();
        if (!instance || !instance->m_prismaUI) return;

        try {
            json request = json::parse(argStr);
            std::string formIdStr = request.value("formId", "");

            if (formIdStr.empty()) {
                logger::warn("UIManager: CheatUnlockSpell - no formId");
                return;
            }

            const auto parsedId = ParsePanelFormId(formIdStr, "CheatUnlockSpell");
            if (!parsedId) return;
            const RE::FormID formId = *parsedId;

            // Get player and spell
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formId);

            if (!player || !spell) {
                logger::error("UIManager: CheatUnlockSpell - failed to get player or spell {:08X}", formId);
                return;
            }

            // Add spell to player (cheat - no XP required)
            player->AddSpell(spell);

            logger::info("UIManager: Cheat unlocked spell {} ({:08X})", spell->GetName(), formId);

            instance->NotifySpellUnlocked(formId, true);
            instance->UpdateSpellState(formIdStr, "unlocked");

        } catch (const std::exception& e) {
            logger::error("UIManager: CheatUnlockSpell exception: {}", e.what());
        }
    });
}

void UIManager::OnRelockSpell(const char* argument)
{
    if (!argument || strlen(argument) == 0) {
        logger::warn("UIManager: RelockSpell - no formId provided");
        return;
    }

    logger::info("UIManager: RelockSpell (cheat mode): {}", argument);

    std::string argStr(argument);

    AddTaskToGameThread("RelockSpell", [argStr]() {
        auto* instance = GetSingleton();
        if (!instance || !instance->m_prismaUI) return;

        try {
            json request = json::parse(argStr);
            std::string formIdStr = request.value("formId", "");

            if (formIdStr.empty()) {
                logger::warn("UIManager: RelockSpell - no formId");
                return;
            }

            const auto parsedId = ParsePanelFormId(formIdStr, "RelockSpell");
            if (!parsedId) return;
            const RE::FormID formId = *parsedId;

            // Get player and spell
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formId);

            if (!player || !spell) {
                logger::error("UIManager: RelockSpell - failed to get player or spell {:08X}", formId);
                return;
            }

            // Remove spell from player, and the progress that would unlock it
            // again on the next progress reply or after a reload
            player->RemoveSpell(spell);
            ProgressionManager::GetSingleton()->RelockSpell(formId);

            logger::info("UIManager: Relocked spell {} ({:08X})", spell->GetName(), formId);

            // Notify UI that spell was relocked
            json notify;
            notify["formId"] = FormIdText::Format(formId);
            notify["success"] = true;
            notify["relocked"] = true;

            instance->CallView("onSpellRelocked", JsonText::Dump(notify).c_str());
            instance->UpdateSpellState(formIdStr, "available");

        } catch (const std::exception& e) {
            logger::error("UIManager: RelockSpell exception: {}", e.what());
        }
    });
}

void UIManager::OnSetSpellXP(const char* argument)
{
    if (!argument || strlen(argument) == 0) {
        logger::warn("UIManager: SetSpellXP - no data provided");
        return;
    }

    std::string argStr(argument);
    logger::info("UIManager: SetSpellXP (cheat mode): {}", argStr);

    AddTaskToGameThread("SetSpellXP", [argStr]() {
        try {
            json request = json::parse(argStr);
            std::string formIdStr = request.value("formId", "");
            float xp = request.value("xp", 0.0f);

            if (formIdStr.empty()) {
                logger::warn("UIManager: SetSpellXP - no formId");
                return;
            }

            const auto parsedId = ParsePanelFormId(formIdStr, "SetSpellXP");
            if (!parsedId) return;
            const RE::FormID formId = *parsedId;
            if (formId == 0) {
                logger::warn("UIManager: SetSpellXP - formId resolved to 0 for '{}', ignoring", formIdStr);
                return;
            }

            auto* progressionMgr = ProgressionManager::GetSingleton();
            if (progressionMgr) {
                progressionMgr->SetSpellXP(formId, xp);
                logger::info("UIManager: Set XP for spell {:08X} to {:.0f}", formId, xp);
            }
        } catch (const std::exception& e) {
            logger::error("UIManager: SetSpellXP exception: {}", e.what());
        }
    });
}

// The panel's required XP for a learning target (RequiredXPSync.js): after a load,
// which keeps only the percent, and whenever the number changes mid-session
// (a known higher spell opens the target, a share slider moves).
void UIManager::OnSetRequiredXP(const char* argument)
{
    if (!argument || strlen(argument) == 0) return;
    std::string argStr(argument);
    AddTaskToGameThread("SetRequiredXP", [argStr]() {
        try {
            json request = json::parse(argStr);
            std::string formIdStr = request.value("formId", "");
            float requiredXP = request.value("requiredXP", 0.0f);
            const auto parsedId = formIdStr.empty() ? std::optional<RE::FormID>(0) : ParsePanelFormId(formIdStr, "SetRequiredXP");
            if (!parsedId) return;
            const RE::FormID formId = *parsedId;
            if (formId == 0 || requiredXP <= 0.0f) return;
            ProgressionManager::GetSingleton()->SetRequiredXP(formId, requiredXP, RequiredXP::Kind::kCharacter);
            GetSingleton()->NotifyProgressUpdate(formIdStr);
        } catch (const std::exception& e) {
            logger::error("UIManager: SetRequiredXP exception: {}", e.what());
        }
    });
}

void UIManager::OnSetTreePrerequisites(const char* argument)
{
    if (!argument || strlen(argument) == 0) {
        logger::warn("UIManager: SetTreePrerequisites - no data provided");
        return;
    }

    std::string argStr(argument);
    logger::info("UIManager: SetTreePrerequisites called");

    AddTaskToGameThread("SetTreePrerequisites", [argStr]() {
        bool dataApply = false;
        try {
            json request = json::parse(argStr);

            // Check if this is a clear command
            if (request.contains("clear") && request["clear"].get<bool>()) {
                ProgressionManager::GetSingleton()->ClearAllTreePrerequisites();
                logger::info("UIManager: Cleared all tree prerequisites");
                return;
            }

            // Otherwise, expect an array of spell prerequisites
            // Format: [{ "formId": "0x...", "prereqs": ["0x...", "0x..."] }, ...]
            if (!request.is_array()) {
                logger::error("UIManager: SetTreePrerequisites - expected array");
                return;
            }

            dataApply = true;
            ApplyTreePrerequisiteEntries(request);
            // The tree game start applied against the panel's (once, after the saved tree's load)
            TreeParity::AfterPanelApply(nullptr);
        } catch (const std::exception& e) {
            logger::error("UIManager: SetTreePrerequisites exception: {}", e.what());
            if (dataApply) TreeParity::AfterPanelApply(e.what());
        }
    });
}

#include "SpellTomeHook.h"
#include "ProgressionManager.h"
#include "SpellEffectivenessHook.h"
#include "uimanager/UIManager.h"
#include "ISLIntegration.h"
#include "SpellTomeHookGate.h"
#include "SpellTomeHookSites.h"

// Xbyak for assembly code generation
#include <xbyak/xbyak.h>

#include <mutex>

// =============================================================================
// Offset for TESObjectBOOK::ProcessBook
// =============================================================================
// This is the function that handles reading books, including spell tomes.
// We patch at the point where it would teach the spell and consume the book.
// Based on DEST by Exit-9B

namespace
{
    // Function ID for TESObjectBOOK::Read (aka ProcessBook)
    // SE (1.5.97):    ID 17439
    // AE (1.6.317+):  ID 17842, still the same function on 1.7.x: RVA 0x280290
    //                 in the 1.7.99 address library; the 1.7.104 RVA (also
    //                 0x280290) is inferred by aligning that library's IDs with
    //                 the 1.7.104 .pdata function table (see SpellTomeHookSites.h)
    // Source: CommonLibSSE-NG src/RE/T/TESObjectBOOK.cpp — RELOCATION_ID(17439, 17842)
    constexpr REL::RelocationID ProcessBookID(17439, 17842);

    constexpr std::string_view kVersionSeparator = ".";
}

// =============================================================================
// Singleton
// =============================================================================

SpellTomeHook* SpellTomeHook::GetSingleton()
{
    static SpellTomeHook singleton;
    return &singleton;
}

// =============================================================================
// Hook Callback
// =============================================================================

void SpellTomeHook::OnSpellTomeRead(RE::TESObjectBOOK* a_book, RE::SpellItem* a_spell)
{
    try {
        OnSpellTomeReadImpl(a_book, a_spell);
    } catch (const std::exception& e) {
        logger::error("SpellTomeHook: reading a tome threw: {}", e.what());
    } catch (...) {
        logger::error("SpellTomeHook: reading a tome threw an unknown exception");
    }
}

void SpellTomeHook::OnSpellTomeReadImpl(RE::TESObjectBOOK* a_book, RE::SpellItem* a_spell)
{
    auto* hook = GetSingleton();
    
    if (!a_book || !a_spell) {
        logger::warn("SpellTomeHook: Null book or spell in callback");
        return;
    }
    
    logger::info("SpellTomeHook: Player reading spell tome '{}' for spell '{}'",
                 a_book->GetName(), a_spell->GetName());
    
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        logger::error("SpellTomeHook: Player not available");
        return;
    }

    // =========================================================================
    // ISL INTEGRATION — Immersive Spell Learning compatibility
    // =========================================================================
    // When ISL is active we:
    //   1. Check prerequisites + skill level (block if not met)
    //   2. Set spell as learning target
    //   3. Register in early-learned tracking (so effectiveness hook nerfs it)
    //   4. Delegate to ISL for study UX (menus, animations, time)
    //   5. Do NOT grant XP — player earns XP by casting the weakened spell
    //
    // When ISL finishes studying and calls AddSpell, our
    // SpellEffectivenessHook applies power scaling. Player then gains
    // mastery through the normal spell-casting XP system.
    // =========================================================================
    if (DESTIntegration::IsActive()) {
        logger::info("SpellTomeHook: ISL/DEST active — checking requirements before delegation");

        RE::FormID spellFormId = a_spell->GetFormID();
        char formIdStr[32];
        snprintf(formIdStr, sizeof(formIdStr), "0x%08X", spellFormId);

        auto* pm = ProgressionManager::GetSingleton();

        // --- Check prerequisites + skill level (same checks as non-ISL path) ---
        if (!CheckLearningRequirements(a_spell, spellFormId)) {
            return;  // Blocked — notification already shown, tome kept
        }

        // --- Set as learning target ---
        if (hook->m_settings.autoSetLearningTarget && pm) {
            pm->SetLearningTargetFromTome(formIdStr, a_spell);
            logger::info("SpellTomeHook: [ISL] Set '{}' as learning target", a_spell->GetName());
        }

        // --- Register for weakness tracking (NO XP grant, NO AddSpell) ---
        // When ISL finishes study and calls AddSpell, our effectiveness hook
        // will recognize this spell and apply weakened power scaling.
        auto* effectHook = SpellEffectivenessHook::GetSingleton();
        if (effectHook) {
            effectHook->RegisterISLPendingSpell(a_spell);
        }

        // --- Dispatch to ISL for study UX ---
        DESTIntegration::DispatchSpellTomeRead(a_book, a_spell, player->AsReference());

        if (hook->m_settings.showNotifications) {
            char msg[256];
            snprintf(msg, sizeof(msg), "You begin to study %s...", a_spell->GetName());
            RE::SendHUDMessage::ShowHUDMessage(msg);
        }

        // Notify UI so the tree updates
        UIManager::GetSingleton()->NotifyProgressUpdate(formIdStr);
        return;
    }

    // =========================================================================
    // NON-ISL PATH — our built-in spell tome handling
    // =========================================================================

    // =========================================================================
    // VANILLA MODE - Instant learn, consume book (like normal Skyrim)
    // =========================================================================
    if (!hook->m_settings.enabled || !hook->m_settings.useProgressionSystem) {
        // Still check if player already knows the spell
        if (player->HasSpell(a_spell)) {
            if (hook->m_settings.showNotifications) {
                RE::SendHUDMessage::ShowHUDMessage("You already know this spell.");
            }
            return;
        }

        logger::info("SpellTomeHook: Using VANILLA mode - teaching spell instantly");

        player->AddSpell(a_spell);

        auto* container = GetBookContainer();
        RE::TESObjectREFR* removeFrom = container ? container : player->AsReference();
        if (removeFrom) {
            removeFrom->RemoveItem(a_book, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
        }

        if (hook->m_settings.showNotifications) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Learned %s", a_spell->GetName());
            RE::SendHUDMessage::ShowHUDMessage(msg);
        }

        logger::info("SpellTomeHook: Vanilla mode - taught '{}', consumed tome", a_spell->GetName());
        return;
    }

    // =========================================================================
    // PROGRESSION MODE - XP gain, weakened spell system
    // =========================================================================
    logger::info("SpellTomeHook: Using PROGRESSION mode for spell '{}'", a_spell->GetName());

    char formIdStr[32];
    snprintf(formIdStr, sizeof(formIdStr), "0x%08X", a_spell->GetFormID());

    auto* pm = ProgressionManager::GetSingleton();
    if (!pm) {
        logger::error("SpellTomeHook: ProgressionManager not available");
        return;
    }

    RE::FormID spellFormId = a_spell->GetFormID();

    // --- Shared prereq + skill checks ---
    if (!CheckLearningRequirements(a_spell, spellFormId)) {
        return;  // Blocked — notification already shown
    }

    // =========================================================================
    // CHECK IF TOME XP ALREADY GRANTED - Prevent exploit
    // =========================================================================
    bool alreadyGrantedXP = hook->HasGrantedTomeXP(spellFormId);
    
    // Calculate XP to grant (percentage of required XP)
    float requiredXP = pm->GetRequiredXP(formIdStr);
    if (requiredXP <= 0) {
        requiredXP = 100.0f;  // Default fallback
    }
    
    float xpToGrant = requiredXP * (hook->m_settings.xpPercentToGrant / 100.0f);
    
    // Auto-set as learning target FIRST (initializes progress entry)
    // This is allowed even if XP was already granted (player might have changed target)
    if (hook->m_settings.autoSetLearningTarget) {
        pm->SetLearningTargetFromTome(formIdStr, a_spell);
    }
    
    // Grant XP ONLY if not already granted for this spell
    if (hook->m_settings.grantXPOnRead && !alreadyGrantedXP) {
        pm->AddXP(formIdStr, xpToGrant);
        hook->MarkTomeXPGranted(spellFormId);
        
        logger::info("SpellTomeHook: Granted {:.1f} XP ({:.0f}% of {:.1f} required) for '{}'",
                     xpToGrant, hook->m_settings.xpPercentToGrant, requiredXP, a_spell->GetName());
        
        // Show notification
        if (hook->m_settings.showNotifications) {
            char msg[256];
            snprintf(msg, sizeof(msg), "You begin to study %s...", a_spell->GetName());
            RE::SendHUDMessage::ShowHUDMessage(msg);
        }
    } else if (alreadyGrantedXP) {
        logger::info("SpellTomeHook: XP already granted for '{}' - no additional XP", a_spell->GetName());
        
        // Show different notification
        if (hook->m_settings.showNotifications) {
            char msg[256];
            snprintf(msg, sizeof(msg), "You review %s... (no additional insight)", a_spell->GetName());
            RE::SendHUDMessage::ShowHUDMessage(msg);
        }
    }
    
    // Notify UI
    UIManager::GetSingleton()->NotifyProgressUpdate(formIdStr);
    
    // Book is NOT consumed, NOT removed from inventory
    logger::info("SpellTomeHook: Tome '{}' kept in inventory", a_book->GetName());
}

// =============================================================================
// Check Learning Requirements (prereqs + skill level)
// =============================================================================

bool SpellTomeHook::CheckLearningRequirements(RE::SpellItem* a_spell, RE::FormID spellFormId)
{
    auto* hook = GetSingleton();
    auto* pm = ProgressionManager::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!pm || !player) return true;  // Can't check, allow

    // Already knows this spell
    if (player->HasSpell(a_spell)) {
        if (hook->m_settings.showNotifications) {
            RE::SendHUDMessage::ShowHUDMessage("You already know this spell.");
        }
        logger::info("SpellTomeHook: Player already knows '{}', keeping tome", a_spell->GetName());
        return false;
    }

    // =========================================================================
    // TREE PREREQUISITE CHECK
    // =========================================================================
    logger::info("SpellTomeHook: Checking prerequisites for spell {:08X} '{}' - requirePrereqs={}",
        spellFormId, a_spell->GetName(), hook->m_settings.requirePrereqs);

    if (hook->m_settings.requirePrereqs) {
        auto reqs = pm->GetPrereqRequirements(spellFormId);
        bool hasAnyPrereqs = !reqs.hardPrereqs.empty() || !reqs.softPrereqs.empty();

        logger::info("SpellTomeHook: Prereqs for {:08X}: {} hard, {} soft (need {})",
            spellFormId, reqs.hardPrereqs.size(), reqs.softPrereqs.size(), reqs.softNeeded);

        // A known higher spell opens this one whatever its own prerequisites
        if (hasAnyPrereqs && !pm->IsUnlockedByKnownChild(spellFormId)) {
            // Check hard prerequisites - ALL must be mastered
            std::vector<RE::FormID> unmetHard;
            for (RE::FormID prereqId : reqs.hardPrereqs) {
                bool mastered = pm->IsSpellMastered(prereqId);
                auto* prereqSpell = RE::TESForm::LookupByID<RE::SpellItem>(prereqId);
                logger::info("SpellTomeHook:   - HARD {:08X} '{}' mastered={}",
                    prereqId, prereqSpell ? prereqSpell->GetName() : "UNKNOWN", mastered);
                if (!mastered) {
                    unmetHard.push_back(prereqId);
                }
            }

            // Check soft prerequisites - need at least softNeeded mastered
            int softMastered = 0;
            std::vector<RE::FormID> unmetSoft;
            for (RE::FormID prereqId : reqs.softPrereqs) {
                bool mastered = pm->IsSpellMastered(prereqId);
                auto* prereqSpell = RE::TESForm::LookupByID<RE::SpellItem>(prereqId);
                logger::info("SpellTomeHook:   - SOFT {:08X} '{}' mastered={}",
                    prereqId, prereqSpell ? prereqSpell->GetName() : "UNKNOWN", mastered);
                if (mastered) {
                    softMastered++;
                } else {
                    unmetSoft.push_back(prereqId);
                }
            }

            int softNeeded = reqs.softNeeded;
            bool hardMet = unmetHard.empty();
            bool softMet = (softNeeded <= 0) || (softMastered >= softNeeded);

            logger::info("SpellTomeHook: hardMet={}, softMet={} ({}/{})",
                hardMet, softMet, softMastered, softNeeded);

            if (!hardMet || !softMet) {
                if (hook->m_settings.showNotifications) {
                    std::string msg;

                    if (!hardMet) {
                        std::vector<std::string> spellNames;
                        for (RE::FormID prereqId : unmetHard) {
                            auto* prereqSpell = RE::TESForm::LookupByID<RE::SpellItem>(prereqId);
                            if (prereqSpell) {
                                spellNames.push_back(prereqSpell->GetName());
                            }
                        }

                        msg = "You must first master ";
                        for (size_t i = 0; i < spellNames.size(); i++) {
                            if (i == 0) msg += spellNames[i];
                            else if (i == spellNames.size() - 1) msg += " and " + spellNames[i];
                            else msg += ", " + spellNames[i];
                        }
                    } else if (!softMet) {
                        int stillNeeded = softNeeded - softMastered;
                        msg = "You need to master " + std::to_string(stillNeeded) + " more related spell";
                        if (stillNeeded > 1) msg += "s";
                    }

                    msg += " to grasp this tome";
                    RE::SendHUDMessage::ShowHUDMessage(msg.c_str());
                }

                logger::info("SpellTomeHook: Player missing prerequisites for '{}' (hardMet={}, softMet={})",
                    a_spell->GetName(), hardMet, softMet);
                return false;  // Blocked
            }
        }
    }

    // =========================================================================
    // SKILL LEVEL CHECK
    // =========================================================================
    if (hook->m_settings.requireSkillLevel) {
        auto* spellEffect = a_spell->GetCostliestEffectItem();
        if (spellEffect && spellEffect->baseEffect) {
            int minimumSkill = static_cast<int>(spellEffect->baseEffect->data.minimumSkill);
            auto school = spellEffect->baseEffect->GetMagickSkill();

            if (minimumSkill > 0) {
                float playerSkill = player->AsActorValueOwner()->GetActorValue(school);

                if (playerSkill < minimumSkill) {
                    if (hook->m_settings.showNotifications) {
                        char msg[256];
                        const char* schoolName = "";
                        switch (school) {
                            case RE::ActorValue::kAlteration:  schoolName = "Alteration"; break;
                            case RE::ActorValue::kConjuration: schoolName = "Conjuration"; break;
                            case RE::ActorValue::kDestruction: schoolName = "Destruction"; break;
                            case RE::ActorValue::kIllusion:    schoolName = "Illusion"; break;
                            case RE::ActorValue::kRestoration: schoolName = "Restoration"; break;
                            default: schoolName = "magic"; break;
                        }
                        snprintf(msg, sizeof(msg),
                            "You lack the %s skill to learn this spell. (%s: %.0f/%d)",
                            schoolName, schoolName, playerSkill, minimumSkill);
                        RE::SendHUDMessage::ShowHUDMessage(msg);
                    }
                    logger::info("SpellTomeHook: Player lacks skill for '{}' (needs {}, has {:.0f})",
                        a_spell->GetName(), minimumSkill, player->AsActorValueOwner()->GetActorValue(school));
                    return false;  // Blocked
                }
            }
        }
    }

    return true;  // All checks passed
}

// =============================================================================
// Get Container (for books read from containers)
// =============================================================================

RE::TESObjectREFR* SpellTomeHook::GetBookContainer()
{
    const auto ui = RE::UI::GetSingleton();
    if (!ui) return nullptr;
    
    const auto menu = ui->GetMenu<RE::ContainerMenu>();
    if (!menu) return nullptr;
    
    const auto movie = menu->uiMovie;
    if (!movie) return nullptr;
    
    // Check if viewing a container
    RE::GFxValue isViewingContainer;
    movie->Invoke("Menu_mc.isViewingContainer", &isViewingContainer, nullptr, 0);
    
    if (!isViewingContainer.GetBool()) {
        return nullptr;
    }
    
    // Get the container reference
    auto refHandle = menu->GetTargetRefHandle();
    RE::TESObjectREFRPtr refr;
    RE::LookupReferenceByHandle(refHandle, refr);
    
    return refr.get();
}

// =============================================================================
// Install Hook
// =============================================================================

bool SpellTomeHook::Install()
{
    logger::info("SpellTomeHook: Installing spell tome read hook...");
    logger::info("SpellTomeHook: Runtime = {} ({})",
        REL::Module::get().version().string(kVersionSeparator),
        REL::Module::IsAE() ? "AE" : "SE");

    // Get the base address of TESObjectBOOK::Read
    const std::uintptr_t funcBase = ProcessBookID.address();
    logger::info("SpellTomeHook: Function base at {:X}", funcBase);

    const bool isAE = REL::Module::IsAE();
    // The gate (SpellTomeHookGate.cpp) logs why when the hook stays out
    if (!SpellTomeGate::SiteIsKnownLayout(funcBase, isAE)) {
        return false;
    }
    const auto& layout = isAE ? SpellTomeSites::kAELayout : SpellTomeSites::kSELayout;

    const auto patchOffset = layout.siteOffset;
    const std::uintptr_t hookAddr = funcBase + patchOffset;
    logger::info("SpellTomeHook: Patch site found at offset +{:X} (addr {:X})", patchOffset, hookAddr);

    // Resume point after the replaced block, measured from the patch site
    const auto jumpOffset = layout.returnOffset;
    const auto patchSize = SpellTomeSites::kPatchSize;
    logger::info("SpellTomeHook: Jump offset = +{:X}, patch size = {:X}", jumpOffset, patchSize);

    // Create the patch using Xbyak
    // Register usage differs between SE and AE:
    //   SE:  rdi = TESObjectBOOK*   (source: DEST v1.2.0 SE, commit 18b81b1)
    //   AE:  r15 = TESObjectBOOK*
    // rdx = RE::SpellItem* in both versions

    struct Patch : Xbyak::CodeGenerator
    {
        Patch(std::uintptr_t a_callbackAddr, std::uintptr_t a_returnAddr, bool a_isAE)
        {
            // Move book pointer to rcx (first param for our callback)
            if (a_isAE) {
                mov(rcx, r15);  // AE: book is in r15
            } else {
                mov(rcx, rdi);  // SE: book is in rdi
            }
            // rdx already has spell pointer (second param)
            
            // Load our callback address and call it
            mov(rax, a_callbackAddr);
            call(rax);
            
            // Set rsi = 0 to prevent book consumption: esi is Read's return
            // value, and its callers remove the book when it is set (shown
            // on 1.7.104, see SpellTomeHookSites.h)
            xor_(rsi, rsi);
            
            // Jump to return address (past the patched region)
            mov(rax, a_returnAddr);
            jmp(rax);
        }
    };
    
    std::uintptr_t callbackAddr = reinterpret_cast<std::uintptr_t>(&OnSpellTomeRead);
    std::uintptr_t returnAddr = hookAddr + jumpOffset;  // Where to jump after our code
    Patch patch(callbackAddr, returnAddr, isAE);
    patch.ready();
    
    // Verify patch size
    if (patch.getSize() > patchSize) {
        logger::error("SpellTomeHook: Patch too large ({} bytes, max {})", patch.getSize(), patchSize);
        return false;
    }
    
    logger::info("SpellTomeHook: Patch size: {} bytes (max {})", patch.getSize(), patchSize);
    
    // Write the patch
    // First, NOP out the entire region we're replacing
    REL::safe_fill(hookAddr, REL::NOP, patchSize);
    
    // Then write our patch code
    REL::safe_write(hookAddr, patch.getCode(), patch.getSize());
    
    GetSingleton()->m_installed = true;
    logger::info("SpellTomeHook: Hook installed successfully!");
    
    return true;
}

// =============================================================================
// Tome XP Tracking - Prevent exploit of reading same tome multiple times
// =============================================================================

bool SpellTomeHook::HasGrantedTomeXP(RE::FormID spellFormId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tomeXPGranted.find(spellFormId) != m_tomeXPGranted.end();
}

void SpellTomeHook::MarkTomeXPGranted(RE::FormID spellFormId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tomeXPGranted.insert(spellFormId);
    logger::info("SpellTomeHook: Marked spell {:08X} as having received tome XP", spellFormId);
}

void SpellTomeHook::ClearTomeXPTracking()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tomeXPGranted.clear();
    logger::info("SpellTomeHook: Cleared tome XP tracking");
}

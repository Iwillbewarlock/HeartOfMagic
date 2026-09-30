#include "Common.h"
#include "EncodingUtils.h"
#include "SpellScanner.h"

namespace SpellScanner
{
    // =============================================================================
    // NON-PLAYER COPIES
    // =============================================================================
    //
    // The game keeps copies of many spells for NPCs, traps and scripts: the trap
    // Fireball, the hazard Guardian Circle, Miraak's Lightning Storm, a cloak's
    // damage spell. They carry the spell's name and school but no half-cost perk,
    // so their tier falls back to the effect's minimum skill, often 0 - a Master
    // spell's copy lands at the root of the tree as Novice. The editor id filter
    // in ScanSpellsToJson cannot catch them because the engine keeps no editor ids
    // for spells, so this goes by what the records themselves say.

    namespace
    {
        // How many dropped spells each log line names; the count covers the rest.
        constexpr std::size_t kLoggedCopies = 5;

        // Below this a spell costs nothing to cast: a player spell always costs
        // something, a creature's attack, a follower's call or a test spell not.
        constexpr float kFreeSpellCost = 0.5f;

        // Same spell to a player: same name (ASCII case ignored) in the same school.
        std::string CopyKey(RE::SpellItem* spell)
        {
            std::string key = spell->GetFullName();
            std::transform(key.begin(), key.end(), key.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            key += '|';
            key += std::to_string(static_cast<int>(GetSpellSchool(spell)));
            return key;
        }

        // A spell a tome teaches is one the player can learn, whatever its record says.
        std::unordered_set<RE::FormID> SpellsTaughtByTomes()
        {
            std::unordered_set<RE::FormID> taught;
            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            if (!dataHandler) return taught;

            for (auto* book : dataHandler->GetFormArray<RE::TESObjectBOOK>()) {
                if (!book || !book->TeachesSpell()) continue;
                if (auto* spell = book->GetSpell()) taught.insert(spell->GetFormID());
            }
            return taught;
        }
    }

    std::unordered_set<RE::FormID> FindNonPlayerCopies(const std::vector<RE::SpellItem*>& spells)
    {
        std::unordered_set<std::string> keysWithPerk;
        for (auto* spell : spells) {
            if (spell->data.castingPerk) keysWithPerk.insert(CopyKey(spell));
        }

        const auto taught = SpellsTaughtByTomes();
        std::unordered_set<RE::FormID> dropped;
        std::size_t copies = 0, free = 0;
        std::string loggedCopies, loggedFree;
        const auto note = [](std::string& logged, std::size_t count, RE::SpellItem* spell) {
            if (count > kLoggedCopies) return;
            if (!logged.empty()) logged += ", ";
            logged += std::format("'{}' (0x{:08X})",
                EncodingUtils::SanitizeToUTF8(spell->GetFullName()), spell->GetFormID());
        };
        for (auto* spell : spells) {
            // A spell a tome teaches stays, whatever else its record says
            if (taught.contains(spell->GetFormID())) continue;

            if (!spell->data.castingPerk && keysWithPerk.contains(CopyKey(spell))) {
                dropped.insert(spell->GetFormID());
                note(loggedCopies, ++copies, spell);
            } else if (spell->CalculateMagickaCost(nullptr) < kFreeSpellCost) {
                // Free and no tome: a creature's attack, a follower's call (Inigo,
                // Val Serano), a pet's whistle, a test or utility spell of a mod
                dropped.insert(spell->GetFormID());
                note(loggedFree, ++free, spell);
            }
        }

        if (copies) {
            logger::info("SpellScanner: dropped {} non-player copies (same name and school as a spell with a "
                         "half-cost perk, no perk of their own, no tome): {}{}",
                copies, loggedCopies, copies > kLoggedCopies ? ", ..." : "");
        }
        if (free) {
            logger::info("SpellScanner: dropped {} free spells no tome teaches (NPC attacks, follower calls, "
                         "test and utility spells): {}{}",
                free, loggedFree, free > kLoggedCopies ? ", ..." : "");
        }
        return dropped;
    }
}

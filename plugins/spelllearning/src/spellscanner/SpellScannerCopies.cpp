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
        // How many dropped copies the log names; the count covers the rest.
        constexpr std::size_t kLoggedCopies = 5;

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
        std::unordered_set<RE::FormID> copies;
        std::string logged;
        for (auto* spell : spells) {
            if (spell->data.castingPerk) continue;
            if (!keysWithPerk.contains(CopyKey(spell))) continue;
            if (taught.contains(spell->GetFormID())) continue;

            copies.insert(spell->GetFormID());
            if (copies.size() <= kLoggedCopies) {
                if (!logged.empty()) logged += ", ";
                logged += std::format("'{}' (0x{:08X})",
                    EncodingUtils::SanitizeToUTF8(spell->GetFullName()), spell->GetFormID());
            }
        }

        if (!copies.empty()) {
            logger::info("SpellScanner: dropped {} non-player copies (same name and school as a spell with a "
                         "half-cost perk, no perk of their own, no tome): {}{}",
                copies.size(), logged, copies.size() > kLoggedCopies ? ", ..." : "");
        }
        return copies;
    }
}

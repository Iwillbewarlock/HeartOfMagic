#include "Common.h"
#include "EncodingUtils.h"
#include "SpellScanner.h"

// windows.h renames GetObject to GetObjectW, which hides BGSDefaultObjectManager::GetObject
#ifdef GetObject
#    undef GetObject
#endif

namespace SpellScanner
{
    // =============================================================================
    // SPELLS THE TREE LEAVES OUT
    // =============================================================================
    //
    // The scan keeps every spell, so the librarian's catalog and the perk adapters
    // see them all, and marks two facts from the record: taughtByTome (some tome
    // teaches it) and voiceSlot (it is equipped in the voice slot). The tree takes
    // only spells a tome teaches that are not voice slot spells
    // (isTaughtByTome, proceduralTreeBuilder.js): a player casts spells from the
    // hands, and a voice slot spell (a mod's script or animation spell, Smooth
    // Animation's ChargeEffect) would take the shout's place when learned.
    //
    // What that leaves out, counted in the log:
    // - Non-player copies. The game keeps copies of many spells for NPCs, traps
    //   and scripts: the trap Fireball, the hazard Guardian Circle, Miraak's
    //   Lightning Storm, a cloak's damage spell. They carry the spell's name and
    //   school but no half-cost perk, so their tier falls back to the effect's
    //   minimum skill, often 0 - a Master spell's copy would land at the root as
    //   Novice. The editor id filter in ScanSpellsToJson cannot catch them because
    //   the engine keeps no editor ids for spells.
    // - Free spells: a creature's attack, a follower's call, a pet's whistle.
    // - Voice slot spells, whether a tome teaches them or not.
    // - The other spells no tome teaches: quest, perk and script spells.

    namespace
    {
        // How many spells each log line names; the count covers the rest.
        constexpr std::size_t kLoggedSpells = 5;

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

        // One kind of spell the tree leaves out: how many, and the first few by name
        struct OutOfTree
        {
            const char* what;
            std::size_t count = 0;
            std::string named;

            void Add(RE::SpellItem* spell)
            {
                if (++count > kLoggedSpells) return;
                if (!named.empty()) named += ", ";
                named += std::format("'{}' (0x{:08X})",
                    EncodingUtils::SanitizeToUTF8(spell->GetFullName()), spell->GetFormID());
            }

            void Log() const
            {
                if (!count) return;
                logger::info("SpellScanner: {} {} stay in the scan, out of the tree: {}{}",
                    count, what, named, count > kLoggedSpells ? ", ..." : "");
            }
        };
    }

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

    bool IsEquippedInVoiceSlot(RE::SpellItem* spell)
    {
        // Asked each time: before the data loads the default object is not set yet
        const auto* defaults = RE::BGSDefaultObjectManager::GetSingleton();
        const auto* voiceSlot = defaults ?
            defaults->GetObject<RE::BGSEquipSlot>(RE::DEFAULT_OBJECTS::kVoiceEquip) : nullptr;
        return spell && voiceSlot && spell->GetEquipSlot() == voiceSlot;
    }

    void LogSpellsOutOfTree(const std::vector<RE::SpellItem*>& spells,
        const std::unordered_set<RE::FormID>& taught)
    {
        std::unordered_set<std::string> keysWithPerk;
        for (auto* spell : spells) {
            if (spell->data.castingPerk) keysWithPerk.insert(CopyKey(spell));
        }

        OutOfTree voice{ "spells equipped in the voice slot (script and animation spells)" };
        OutOfTree copies{ "non-player copies (same name and school as a spell with a half-cost perk, "
                          "no perk of their own, no tome)" };
        OutOfTree freeSpells{ "free spells no tome teaches (NPC attacks, follower calls, test and utility spells)" };
        OutOfTree other{ "other spells no tome teaches (quest, perk and script spells)" };
        for (auto* spell : spells) {
            if (IsEquippedInVoiceSlot(spell)) {
                voice.Add(spell);
            } else if (taught.contains(spell->GetFormID())) {
                continue;
            } else if (!spell->data.castingPerk && keysWithPerk.contains(CopyKey(spell))) {
                copies.Add(spell);
            } else if (spell->CalculateMagickaCost(nullptr) < kFreeSpellCost) {
                freeSpells.Add(spell);
            } else {
                other.Add(spell);
            }
        }
        voice.Log();
        copies.Log();
        freeSpells.Log();
        other.Log();
    }
}

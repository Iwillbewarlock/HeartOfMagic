// spell-tomes-check - offline check of the tome-spell pairs for tomes that teach nothing
// (plugins/spelllearning/include/SpellTomePairs.h, used by src/SpellTomes.cpp).
//
//   spell-tomes-check --self-test     made-up SynthesizedPatch.json texts and SR lists, no game needed
//
// The JSON reader: good entries, a null tomeId, an id that is not __formData, a
// local id over 24 bits, a file that is not JSON, ill-formed UTF-8 (the whole file
// refused), no "newSpells". The pair rule: JSON pairs, a plugin the load order does
// not have, a book whose winning override is in another plugin (Synthesis.esp: still used, counted by plugin),
// a book that teaches a spell, equal live parts paired and unequal ones not, record
// and script parts kept apart, a re-run patch's shifted but equal-length lists
// against the JSON (conflicts, not used), live pairs outside a usable JSON, books whose
// TeachesSpell flag was cleared at runtime (alone, before the JSON and the live lists), and
// SamePairs (the generation only moves on a real change).
//
// Exit 0 when every case passes, 1 on a failure, 2 on a bad argument.

#include "SpellTomePairs.h"

#include <cstdio>
#include <map>
#include <string>
#include <string_view>

namespace
{
    using namespace SpellTomePairs;

    constexpr int kExitOk = 0;
    constexpr int kExitFailed = 1;
    constexpr int kExitUsage = 2;

    int g_failures = 0;
    int g_checks = 0;

    void Check(bool ok, const char* what)
    {
        ++g_checks;
        if (!ok) {
            ++g_failures;
            std::printf("FAIL %s\n", what);
        }
    }

    // ------------------------------------------------------------------------
    // A made-up load order
    // ------------------------------------------------------------------------

    constexpr FormId kSkyrimIndex = 0x00000000;
    constexpr FormId kModIndex = 0x05000000;
    constexpr FormId kLightBase = 0xFE001000;    // light.esl, light slot 1
    constexpr FormId kLight2Base = 0xFE002000;   // light2.esl, light slot 2

    struct FakePlugin
    {
        FormId base = 0;
        bool light = false;
    };

    struct FakeBook
    {
        FormId taught = 0;
        std::string winner = "SpellResearchSynthesizer.esp";
    };

    struct FakeGame
    {
        std::map<std::string, FakePlugin> plugins{ { "skyrim.esm", { kSkyrimIndex } }, { "spellmod.esp", { kModIndex } },
            { "light.esl", { kLightBase, true } }, { "light2.esl", { kLight2Base, true } } };
        std::map<FormId, FakeBook> books;
        std::map<FormId, bool> spells;

        Facts AsFacts() const
        {
            Facts facts;
            facts.resolve = [this](const FormRef& ref) -> FormId {
                // As the game side: the id must fit its plugin, then CommonLib's LookupFormID
                // adds it to the plugin's index bits (no mask)
                const auto it = plugins.find(ref.plugin);
                if (it == plugins.end() || !LocalIdFits(ref.localId, it->second.light)) return 0;
                const FormId id = it->second.base + ref.localId;
                return (books.contains(id) || spells.contains(id)) ? id : 0;
            };
            facts.book = [this](FormId id) {
                BookFacts out;
                const auto it = books.find(id);
                if (it == books.end()) return out;
                out.isBook = true;
                out.taughtSpell = it->second.taught;
                out.winner = it->second.winner;
                return out;
            };
            facts.isSpell = [this](FormId id) { return spells.contains(id); };
            return facts;
        }
    };

    // Three tomes that teach nothing (0x...800-802) for three spells (0x...900-902)
    FakeGame ThreeTomes()
    {
        FakeGame game;
        for (FormId i = 0; i < 3; ++i) {
            game.books[kModIndex | (0x800 + i)] = {};
            game.spells[kModIndex | (0x900 + i)] = true;
        }
        return game;
    }

    constexpr const char* kThreePairs = R"({
        "newSpells": [
            { "name": "A", "spellId": "__formData|spellmod.esp|0x000900", "tomeId": "__formData|spellmod.esp|0x000800" },
            { "name": "B", "spellId": "__formData|spellmod.esp|0x000901", "tomeId": "__formData|spellmod.esp|0x000801" },
            { "name": "C", "spellId": "__formData|spellmod.esp|0x000902", "tomeId": "__formData|spellmod.esp|0x000802" }
        ],
        "removedSpells": []
    })";

    FormId Mod(FormId local) { return kModIndex | local; }

    FormId SpellOf(const Result& result, FormId book)
    {
        const auto it = result.pairs.find(book);
        return it == result.pairs.end() ? 0 : it->second.spell;
    }

    // ------------------------------------------------------------------------
    // The reader
    // ------------------------------------------------------------------------

    void FormDataCases()
    {
        const auto good = ParseFormData("__formData|spellmod.esp|0x000800");
        Check(good && good->plugin == "spellmod.esp" && good->localId == 0x800, "formData: plugin and local id read");
        Check(ParseFormData("__formData|light.esl|0x000801").has_value(), "formData: a light plugin's id reads the same");
        Check(!ParseFormData("spellmod.esp|0x000800"), "formData: no tag refused");
        Check(!ParseFormData("__formData||0x000800"), "formData: no plugin refused");
        Check(!ParseFormData("__formData|spellmod.esp|0xZZ"), "formData: a non-hex id refused");
        Check(!ParseFormData("__formData|spellmod.esp|0x01000800"), "formData: an id over 24 bits refused");
        Check(!ParseFormData("__formData|spellmod.esp"), "formData: no id refused");
    }

    void ReaderCases()
    {
        const auto good = ReadPatch(kThreePairs);
        Check(good.usable && good.entries.size() == 3 && good.newSpells == 3 && good.malformed == 0,
            "reader: three good entries");

        const auto mixed = ReadPatch(R"({ "newSpells": [
            { "name": "No tome", "spellId": "__formData|spellmod.esp|0x000903", "tomeId": null },
            { "name": "Bad id", "spellId": "spellmod.esp:0x904", "tomeId": "__formData|spellmod.esp|0x000804" },
            7,
            { "name": "Good", "spellId": "__formData|spellmod.esp|0x000900", "tomeId": "__formData|spellmod.esp|0x000800" }
        ] })");
        Check(mixed.usable && mixed.entries.size() == 1 && mixed.noTome == 1 && mixed.malformed == 2,
            "reader: null tomeId counted, malformed ids and a non-object skipped, the good one kept");
        Check(mixed.problems.size() == 2, "reader: each malformed entry leaves a problem line");

        const auto notJson = ReadPatch("{ \"newSpells\": [ ");
        Check(!notJson.usable && notJson.problems.size() == 1, "reader: a cut-off file is not usable, with a reason");

        const std::string badUtf8 = std::string(R"({ "newSpells": [ { "name": ")") + "\xC3\x28" +
            R"(", "spellId": "__formData|spellmod.esp|0x000900", "tomeId": "__formData|spellmod.esp|0x000800" } ] })";
        const auto bad = ReadPatch(badUtf8);
        Check(!bad.usable && bad.entries.empty() && !bad.problems.empty(), "reader: ill-formed UTF-8 refuses the whole file");

        const auto noList = ReadPatch(R"({ "removedSpells": [] })");
        Check(!noList.usable && noList.problems.size() == 1, "reader: no newSpells array is not usable");
        Check(!ReadPatch("[]").usable, "reader: a top-level array is not usable");
        const auto empty = ReadPatch(R"({ "newSpells": [] })");
        Check(empty.usable && empty.entries.empty(), "reader: an empty newSpells is usable and pairs nothing");
    }

    // ------------------------------------------------------------------------
    // The pairs
    // ------------------------------------------------------------------------

    LivePart Part(std::vector<FormId> books, std::vector<FormId> spells, bool script)
    {
        return LivePart{ std::move(books), std::move(spells), script };
    }

    void PatchCases()
    {
        auto game = ThreeTomes();
        const auto patch = ReadPatch(kThreePairs);
        const auto result = Build({ &patch, {} }, game.AsFacts());
        Check(result.counts.patch == 3 && SpellOf(result, Mod(0x801)) == Mod(0x901), "patch: three pairs from the JSON");

        // A plugin the load order does not have
        auto missing = ReadPatch(R"({ "newSpells": [
            { "name": "Gone", "spellId": "__formData|gone.esp|0x000900", "tomeId": "__formData|gone.esp|0x000800" } ] })");
        const auto gone = Build({ &missing, {} }, game.AsFacts());
        Check(gone.pairs.empty() && gone.counts.unresolved == 1, "patch: an id from a plugin not loaded is unresolved");

        // Light plugins: a 12-bit id resolves; a longer one would add into the next light plugin's records
        auto light = ThreeTomes();
        light.books[kLightBase + 0x800] = {};
        light.spells[kLightBase + 0x900] = true;
        light.books[kLight2Base + 0x800] = {};
        light.spells[kLight2Base + 0x900] = true;
        const auto lightPatch = ReadPatch(R"({ "newSpells": [
            { "name": "L", "spellId": "__formData|light.esl|0x000900", "tomeId": "__formData|light.esl|0x000800" },
            { "name": "Over", "spellId": "__formData|light.esl|0x001900", "tomeId": "__formData|light.esl|0x001800" } ] })");
        const auto lit = Build({ &lightPatch, {} }, light.AsFacts());
        Check(SpellOf(lit, kLightBase + 0x800) == kLightBase + 0x900, "patch: a light plugin's 12-bit id resolves");
        Check(lit.counts.unresolved == 1 && !lit.pairs.contains(kLight2Base + 0x800),
            "patch: a light plugin's id over 12 bits is unresolved, not the next light plugin's record");
        Check(LocalIdFits(0xFFF, true) && !LocalIdFits(0x1000, true) && LocalIdFits(0x1000, false),
            "LocalIdFits: 12 bits for a light plugin, 24 for a full one");

        // Run inside the Synthesis app: the override is in Synthesis.esp - the JSON pair still counts
        game.books[Mod(0x801)].winner = "Synthesis.esp";
        const auto group = Build({ &patch, { Part({ Mod(0x801) }, { Mod(0x902) }, false) } }, game.AsFacts());
        Check(group.counts.patch == 3 && group.pairs.at(Mod(0x801)).source == Source::kPatch &&
                  SpellOf(group, Mod(0x801)) == Mod(0x901),
            "patch: a book whose winning override is in Synthesis.esp keeps its JSON pair");
        Check(group.patchByWinner.size() == 2 && group.patchByWinner.at("Synthesis.esp") == 1 &&
                  group.patchByWinner.at("SpellResearchSynthesizer.esp") == 2,
            "patch: JSON pairs are counted by the book's winning plugin");
        Check(group.counts.liveConflicts == 1 && group.counts.liveRecord == 0,
            "patch: a live pair disagreeing with that JSON pair is dropped and counted");

        // A later plugin makes the book teach again: the record wins, whatever the JSON says
        game.books[Mod(0x802)].taught = Mod(0x902);
        const auto restored = Build({ &patch, {} }, game.AsFacts());
        Check(!restored.pairs.contains(Mod(0x802)) && restored.counts.teaches == 1 && restored.counts.teachesOther == 0,
            "patch: a book that teaches its spell again needs no pair");

        // The book teaches a spell itself: the record wins
        auto teaching = ThreeTomes();
        teaching.books[Mod(0x800)].taught = Mod(0x902);
        const auto taught = Build({ &patch, {} }, teaching.AsFacts());
        Check(!taught.pairs.contains(Mod(0x800)) && taught.counts.teaches == 1 && taught.counts.teachesOther == 1,
            "patch: a book that teaches another spell keeps its own");
    }

    void LiveCases()
    {
        const auto game = ThreeTomes();
        const auto facts = game.AsFacts();

        // No JSON: equal parts pair, record and script parts each on their own
        const auto live = Build({ nullptr, { Part({ Mod(0x800), Mod(0x801) }, { Mod(0x900), Mod(0x901) }, false),
                                             Part({ Mod(0x802) }, { Mod(0x902) }, true) } }, facts);
        Check(live.counts.liveRecord == 2 && live.counts.liveScript == 1 &&
                  live.pairs.at(Mod(0x802)).source == Source::kLiveScript,
            "live: record part and script part pair on their own");

        // An unequal part is not paired; the other part still is
        const auto unequal = Build({ nullptr, { Part({ Mod(0x800), Mod(0x801) }, { Mod(0x900) }, false),
                                                Part({ Mod(0x802) }, { Mod(0x902) }, true) } }, facts);
        Check(unequal.counts.unequalParts == 1 && unequal.pairs.size() == 1 && SpellOf(unequal, Mod(0x802)) == Mod(0x902),
            "live: an unequal part is skipped, the equal one used");

        // A re-run patch on an old save: both lists shifted by one, equal length, wrong pairs
        const auto patch = ReadPatch(kThreePairs);
        const auto shifted = Build({ &patch, { Part({ Mod(0x800), Mod(0x801), Mod(0x802) },
                                                     { Mod(0x901), Mod(0x902), Mod(0x900) }, false) } }, facts);
        Check(shifted.counts.patch == 3 && shifted.counts.liveConflicts == 3 && shifted.counts.liveRecord == 0 &&
                  SpellOf(shifted, Mod(0x800)) == Mod(0x900),
            "live: shifted equal-length lists disagree with the JSON - conflicts, the JSON's pairs kept");

        // Live pairs that agree with the JSON add nothing; one for a book the JSON does not name is not used
        auto more = ThreeTomes();
        more.books[Mod(0x803)] = {};
        more.spells[Mod(0x903)] = true;
        const auto outside = Build({ &patch, { Part({ Mod(0x800), Mod(0x803) }, { Mod(0x900), Mod(0x903) }, true) } },
            more.AsFacts());
        Check(outside.counts.patch == 3 && outside.counts.liveScript == 0 && outside.counts.liveOutsidePatch == 1 &&
                  !outside.pairs.contains(Mod(0x803)),
            "live: with a usable JSON, a live pair the JSON does not name is not used");

        // An unreadable JSON leaves the fallback: the live pairs are used
        const auto broken = ReadPatch("not json");
        const auto fallback = Build({ &broken, { Part({ Mod(0x800) }, { Mod(0x900) }, false) } }, facts);
        Check(fallback.counts.liveRecord == 1, "live: an unreadable JSON falls back to the live lists");

        // A live pair whose spell is no spell, or whose book is no book
        const auto junk = Build({ nullptr, { Part({ Mod(0x800), Mod(0x999) }, { Mod(0x777), Mod(0x900) }, false) } }, facts);
        Check(junk.pairs.empty() && junk.counts.notBookOrSpell == 2, "live: a pair that is not a BOOK and a SPEL is skipped");
    }

    // Books whose TeachesSpell flag a mod cleared at runtime (SpellScribe through SkyPatcher)
    void ClearedCases()
    {
        const auto game = ThreeTomes();
        const auto facts = game.AsFacts();

        const auto alone = Build({ nullptr, {}, { { Mod(0x800), Mod(0x900) }, { Mod(0x801), Mod(0x901) } } }, facts);
        Check(alone.counts.cleared == 2 && alone.pairs.at(Mod(0x800)).source == Source::kClearedFlag &&
                  SpellOf(alone, Mod(0x801)) == Mod(0x901),
            "cleared: a book whose flag was cleared pairs with the spell it still points at");

        const auto patch = ReadPatch(kThreePairs);
        const auto agree = Build({ &patch, {}, { { Mod(0x800), Mod(0x900) } } }, facts);
        Check(agree.counts.patch == 2 && agree.counts.cleared == 1 && agree.counts.clearedInPatch == 1 &&
                  agree.counts.clearedConflicts == 0 && agree.counts.Paired() == agree.pairs.size() &&
                  agree.pairs.at(Mod(0x800)).source == Source::kClearedFlag,
            "cleared: the same pair in the JSON is the book's, counted once");

        const auto disagree = Build({ &patch, {}, { { Mod(0x800), Mod(0x901) } } }, facts);
        Check(disagree.counts.clearedConflicts == 1 && SpellOf(disagree, Mod(0x800)) == Mod(0x901) &&
                  disagree.notes.size() == 1 && disagree.counts.patch == 2,
            "cleared: the book's own spell wins over a JSON pair naming another, the conflict counted and noted");

        const auto beforeLive = Build({ nullptr, { Part({ Mod(0x800) }, { Mod(0x902) }, false) },
                                        { { Mod(0x800), Mod(0x900) } } }, facts);
        Check(beforeLive.counts.cleared == 1 && beforeLive.counts.liveConflicts == 1 &&
                  SpellOf(beforeLive, Mod(0x800)) == Mod(0x900),
            "cleared: the book's own spell wins over SR's lists");

        auto teaching = ThreeTomes();
        teaching.books[Mod(0x800)].taught = Mod(0x900);
        const auto still = Build({ nullptr, {}, { { Mod(0x800), Mod(0x900) } } }, teaching.AsFacts());
        Check(still.pairs.empty() && still.counts.teaches == 1, "cleared: a book that teaches again needs no pair");

        const auto junk = Build({ nullptr, {}, { { Mod(0x800), Mod(0x777) }, { 0, Mod(0x900) } } }, facts);
        Check(junk.pairs.empty() && junk.counts.notBookOrSpell == 1, "cleared: no spell or no book is not paired");
    }

    void SamePairsCases()
    {
        const auto game = ThreeTomes();
        const auto patch = ReadPatch(kThreePairs);
        const auto a = Build({ &patch, {} }, game.AsFacts());
        const auto b = Build({ nullptr, { Part({ Mod(0x800), Mod(0x801), Mod(0x802) },
                                               { Mod(0x900), Mod(0x901), Mod(0x902) }, true) } }, game.AsFacts());
        Check(SamePairs(a.pairs, b.pairs), "same pairs: the same books to the same spells, whatever the source");
        const auto c = Build({ nullptr, { Part({ Mod(0x800) }, { Mod(0x900) }, true) } }, game.AsFacts());
        Check(!SamePairs(a.pairs, c.pairs), "same pairs: fewer pairs is a change");
        Check(SamePairs({}, {}), "same pairs: nothing and nothing");
    }

    int SelfTest()
    {
        FormDataCases();
        ReaderCases();
        PatchCases();
        LiveCases();
        ClearedCases();
        SamePairsCases();
        std::printf("spell-tomes-check: %d checks, %d failed\n", g_checks, g_failures);
        return g_failures == 0 ? kExitOk : kExitFailed;
    }
}

int main(int argc, char** argv)
{
    if (argc != 2 || std::string_view(argv[1]) != "--self-test") {
        std::fprintf(stderr, "usage: spell-tomes-check --self-test\n");
        return kExitUsage;
    }
    return SelfTest();
}

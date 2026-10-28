#pragma once

#include "FormIdText.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

// =============================================================================
// SPELL TOME PAIRS - which spell a tome that teaches nothing stands for (no game types)
// =============================================================================
//
// Spell Research Synthesizer overrides spell tomes (SpellResearchSynthesizer.esp, or
// the Synthesis group's plugin) so they teach nothing, and puts each tome-spell pair in two places: its
// SynthesizedPatch.json ("newSpells": "tomeId" / "spellId" as
// "__formData|<plugin>|0x<local id>") and Spell Research's per-tier lists
// _SR_ListTomes<tier> / _SR_ListTomeSpells<tier>, same index = one pair (an FLM ini
// fills the record part, SR's Import the script-added part).
//
// The JSON is the source trusted first: it pairs each tome with its spell
// explicitly, from the same run that wrote the plugin. An entry counts for a book
// whose winning override teaches nothing, in whatever plugin that override is: run
// standalone the patcher writes SpellResearchSynthesizer.esp, inside the Synthesis
// app the group's plugin (Synthesis.esp by default). The pairs are counted by that
// plugin for the log. The live lists prove less - FLM adds to each list on its own
// and runs again at every load, so a re-run patch on an old save can shift both lists
// into wrong pairs of equal length - so they are a fallback, "unverified": used only
// when there is no usable JSON. With a usable JSON a live pair that
// disagrees with it is a conflict (counted, not used), and one for a book the JSON
// does not name is not used either. A list part pairs only when the book list's and
// the spell list's part have the same length. A book that teaches a spell keeps it
// (the record wins; a pair naming another spell is a conflict).
//
// Experimental: a mod can also clear a tome's TeachesSpell flag at runtime (SpellScribe does it
// through SkyPatcher, `removeFlags=teachesspell`). The record was loaded with the flag, so the book
// still points at its spell; the game side passes those as `cleared` (a pointer that is one of the
// game's spells). They come first: the record's own spell wins, as it does for a book that still
// teaches; a JSON or live pair naming another spell for that book is a conflict, not used.
//
// The game side (src/SpellTomes.cpp) and tools/spell-tomes-check make the same
// pairs from the same inputs. docs/superpowers/specs/2026-10-27-sr-paired-tomes-design.md
// has the why.
namespace SpellTomePairs
{
    using FormId = std::uint32_t;

    inline constexpr std::string_view kFormDataTag = "__formData";
    /// A plugin-local id fills at most the low 24 bits (the high byte is the load order index)
    inline constexpr std::uint32_t kMaxLocalId = 0x00FFFFFF;
    /// A light plugin's records use 12 bits of local id
    inline constexpr std::uint32_t kMaxLightLocalId = 0x00000FFF;
    /// Problems kept as text for the log; the counts cover the rest
    inline constexpr std::size_t kKeptProblems = 5;

    /// A form named by its plugin and its id inside that plugin
    struct FormRef
    {
        std::string plugin;
        std::uint32_t localId = 0;
    };

    /// "__formData|<plugin>|0x<local id>"; nullopt for anything else
    [[nodiscard]] inline std::optional<FormRef> ParseFormData(std::string_view text)
    {
        const auto first = text.find('|');
        if (first == std::string_view::npos || text.substr(0, first) != kFormDataTag) return std::nullopt;
        const auto second = text.find('|', first + 1);
        if (second == std::string_view::npos) return std::nullopt;
        const auto plugin = text.substr(first + 1, second - first - 1);
        const auto local = FormIdText::Parse(text.substr(second + 1));
        if (plugin.empty() || !local || *local > kMaxLocalId) return std::nullopt;
        return FormRef{ std::string(plugin), *local };
    }

    /// Whether a local id can name a record of its plugin. CommonLib's LookupFormID adds
    /// the id to the plugin's index bits without masking, so a light plugin's id over
    /// 12 bits would name a record of the next light plugin.
    [[nodiscard]] constexpr bool LocalIdFits(std::uint32_t localId, bool light) noexcept
    {
        return localId <= (light ? kMaxLightLocalId : kMaxLocalId);
    }

    // =========================================================================
    // SynthesizedPatch.json
    // =========================================================================

    struct PatchEntry
    {
        std::string name;
        FormRef spell;
        FormRef tome;
    };

    struct PatchRead
    {
        bool usable = false;               ///< parsed, with a "newSpells" array
        std::vector<PatchEntry> entries;   ///< every entry with a tome and a spell
        std::size_t newSpells = 0;         ///< entries in "newSpells"
        std::size_t noTome = 0;            ///< "tomeId" null or absent: a spell with no tome
        std::size_t malformed = 0;         ///< not an object, or an id that is not __formData
        std::vector<std::string> problems; ///< the first kKeptProblems, for the log
    };

    namespace Detail
    {
        inline void Problem(PatchRead& read, std::string text)
        {
            if (read.problems.size() < kKeptProblems) read.problems.push_back(std::move(text));
        }

        inline std::string Text(const nlohmann::json& entry, const char* key)
        {
            const auto found = entry.find(key);
            return (found != entry.end() && found->is_string()) ? found->get<std::string>() : std::string();
        }

        /// One "newSpells" entry into `read`
        inline void ReadEntry(const nlohmann::json& entry, std::size_t index, PatchRead& read)
        {
            if (!entry.is_object()) {
                ++read.malformed;
                Problem(read, std::format("newSpells[{}] is not an object - skipped", index));
                return;
            }
            const auto tomeField = entry.find("tomeId");
            if (tomeField == entry.end() || tomeField->is_null()) {
                ++read.noTome;
                return;
            }
            const std::string spellText = Text(entry, "spellId");
            const std::string tomeText = tomeField->is_string() ? tomeField->get<std::string>() : std::string();
            const auto spell = ParseFormData(spellText);
            const auto tome = ParseFormData(tomeText);
            if (!spell || !tome) {
                ++read.malformed;
                Problem(read, std::format("newSpells[{}] '{}': {} '{}' is not \"__formData|<plugin>|0x<id>\" - skipped",
                    index, Text(entry, "name"), spell ? "tomeId" : "spellId", spell ? tomeText : spellText));
                return;
            }
            read.entries.push_back({ Text(entry, "name"), *spell, *tome });
        }
    }

    /// The file's bytes. Not JSON (ill-formed UTF-8 included: the whole file is
    /// refused), or no "newSpells" array: not usable, the reason in `problems`.
    [[nodiscard]] inline PatchRead ReadPatch(std::string_view bytes)
    {
        PatchRead read;
        nlohmann::json document;
        try {
            document = nlohmann::json::parse(bytes);
        } catch (const nlohmann::json::parse_error& e) {
            Detail::Problem(read, std::format("not valid JSON ({})", e.what()));
            return read;
        }
        const auto list = document.is_object() ? document.find("newSpells") : document.end();
        if (!document.is_object() || list == document.end() || !list->is_array()) {
            Detail::Problem(read, "no \"newSpells\" array at the top level");
            return read;
        }
        read.usable = true;
        read.newSpells = list->size();
        for (std::size_t i = 0; i < list->size(); ++i) Detail::ReadEntry((*list)[i], i, read);
        return read;
    }

    // =========================================================================
    // The pairs
    // =========================================================================

    enum class Source : std::uint8_t
    {
        kPatch,        ///< SynthesizedPatch.json
        kLiveRecord,   ///< SR's lists, the plugin part (FLM)
        kLiveScript,   ///< SR's lists, the script-added part (SR's Import, in the save)
        kClearedFlag   ///< the book's own spell, its TeachesSpell flag cleared at runtime
    };

    /// A book whose TeachesSpell flag was cleared after load, and the spell it still points at
    struct ClearedTome
    {
        FormId book = 0;
        FormId spell = 0;
    };

    /// One tier's part of SR's book list and spell list (record or script-added)
    struct LivePart
    {
        std::vector<FormId> books;
        std::vector<FormId> spells;
        bool script = false;
    };

    /// What the game says about a book
    struct BookFacts
    {
        bool isBook = false;
        FormId taughtSpell = 0;          ///< 0: the record teaches nothing
        std::string winner;              ///< the winning override's file, for the log
    };

    struct Facts
    {
        std::function<FormId(const FormRef&)> resolve;   ///< 0: plugin not loaded or no such form
        std::function<BookFacts(FormId)> book;
        std::function<bool(FormId)> isSpell;
    };

    struct Inputs
    {
        const PatchRead* patch = nullptr;    ///< nullptr or not usable: no JSON
        std::vector<LivePart> live;
        std::vector<ClearedTome> cleared;
    };

    struct Entry
    {
        FormId spell = 0;
        Source source = Source::kPatch;
    };

    struct Counts
    {
        std::size_t patch = 0;
        std::size_t liveRecord = 0;
        std::size_t liveScript = 0;
        std::size_t unresolved = 0;       ///< JSON ids the load order does not have
        std::size_t notBookOrSpell = 0;   ///< resolves, but not a BOOK and a SPEL
        std::size_t teaches = 0;          ///< the book teaches a spell itself
        std::size_t teachesOther = 0;     ///< ... and the pair names another one (record wins)
        std::size_t unequalParts = 0;     ///< live list parts of unequal length, not paired
        std::size_t liveConflicts = 0;    ///< live pair disagreeing with an earlier pair (cleared book, JSON or list)
        std::size_t liveOutsidePatch = 0; ///< live pair for a book a usable JSON does not name
        std::size_t patchConflicts = 0;   ///< the same book twice in the JSON with another spell
        std::size_t cleared = 0;          ///< books whose TeachesSpell flag was cleared at runtime
        std::size_t clearedInPatch = 0;   ///< JSON pairs for such a book with the same spell (not counted twice)
        std::size_t clearedConflicts = 0; ///< JSON pairs for such a book naming another spell (the book's kept)

        [[nodiscard]] std::size_t Paired() const { return patch + liveRecord + liveScript + cleared; }
    };

    struct Result
    {
        std::unordered_map<FormId, Entry> pairs;   ///< book -> its spell
        Counts counts;
        std::unordered_map<std::string, std::size_t> patchByWinner;   ///< JSON pairs used, by the book's winning plugin
        std::vector<std::string> notes;            ///< the first kKeptProblems conflicts
    };

    namespace Detail
    {
        inline void Note(Result& result, std::string text)
        {
            if (result.notes.size() < kKeptProblems) result.notes.push_back(std::move(text));
        }

        /// The checks every pair passes; false (and counted) when it is not used
        inline bool Usable(FormId book, FormId spell, const BookFacts& facts, const Facts& game, Result& result)
        {
            if (!facts.isBook || !game.isSpell(spell)) {
                ++result.counts.notBookOrSpell;
                return false;
            }
            if (facts.taughtSpell != 0) {
                ++result.counts.teaches;
                if (facts.taughtSpell != spell) {
                    ++result.counts.teachesOther;
                    Note(result, std::format("book 0x{:08X} teaches 0x{:08X}, a pair names 0x{:08X} - the book's own kept",
                        book, facts.taughtSpell, spell));
                }
                return false;
            }
            return true;
        }

        /// The JSON's pairs
        inline void AddPatch(const PatchRead& patch, const Facts& game, Result& result)
        {
            for (const auto& entry : patch.entries) {
                const FormId book = game.resolve(entry.tome);
                const FormId spell = game.resolve(entry.spell);
                if (!book || !spell) {
                    ++result.counts.unresolved;
                    continue;
                }
                const BookFacts facts = game.book(book);
                if (!Usable(book, spell, facts, game, result)) continue;
                const auto [it, added] = result.pairs.try_emplace(book, Entry{ spell, Source::kPatch });
                if (added) {
                    ++result.counts.patch;
                    ++result.patchByWinner[facts.winner];
                } else if (it->second.source == Source::kClearedFlag) {
                    // The book's own record was here first: it wins, as it does for a book that teaches
                    if (it->second.spell == spell) {
                        ++result.counts.clearedInPatch;
                    } else {
                        ++result.counts.clearedConflicts;
                        Note(result, std::format("book 0x{:08X} points at 0x{:08X} with its flag cleared, the JSON says "
                            "0x{:08X} - the book's own kept", book, it->second.spell, spell));
                    }
                } else if (it->second.spell != spell) {
                    ++result.counts.patchConflicts;
                    Note(result, std::format("book 0x{:08X} is in the JSON twice (0x{:08X}, 0x{:08X}) - the first kept",
                        book, it->second.spell, spell));
                }
            }
        }

        inline void AddLivePair(FormId book, FormId spell, bool script, bool patchUsable, const Facts& game, Result& result)
        {
            if (!book || !spell || !Usable(book, spell, game.book(book), game, result)) return;
            const auto found = result.pairs.find(book);
            if (found != result.pairs.end()) {
                if (found->second.spell != spell) {
                    ++result.counts.liveConflicts;
                    const auto source = found->second.source;
                    Note(result, std::format("SR's lists pair book 0x{:08X} with 0x{:08X}, {} says 0x{:08X} - not used",
                        book, spell,
                        source == Source::kPatch ? "the JSON" : source == Source::kClearedFlag ? "the book" : "another list entry",
                        found->second.spell));
                }
                return;
            }
            if (patchUsable) {
                ++result.counts.liveOutsidePatch;
                return;
            }
            result.pairs.emplace(book, Entry{ spell, script ? Source::kLiveScript : Source::kLiveRecord });
            ++(script ? result.counts.liveScript : result.counts.liveRecord);
        }

        inline void AddCleared(const ClearedTome& tome, const Facts& game, Result& result)
        {
            if (!tome.book || !tome.spell || !Usable(tome.book, tome.spell, game.book(tome.book), game, result)) return;
            // The game hands each book once; a repeat keeps the first
            if (result.pairs.try_emplace(tome.book, Entry{ tome.spell, Source::kClearedFlag }).second) ++result.counts.cleared;
        }
    }

    /// Every pair: the books whose flag was cleared first (the record wins), then the JSON, then the live lists
    [[nodiscard]] inline Result Build(const Inputs& inputs, const Facts& game)
    {
        Result result;
        const bool patchUsable = inputs.patch && inputs.patch->usable;
        for (const auto& tome : inputs.cleared) Detail::AddCleared(tome, game, result);
        if (patchUsable) Detail::AddPatch(*inputs.patch, game, result);
        for (const auto& part : inputs.live) {
            if (part.books.size() != part.spells.size()) {
                ++result.counts.unequalParts;
                continue;
            }
            for (std::size_t i = 0; i < part.books.size(); ++i) {
                Detail::AddLivePair(part.books[i], part.spells[i], part.script, patchUsable, game, result);
            }
        }
        return result;
    }

    /// Same books to the same spells (a new answer only when this is false)
    [[nodiscard]] inline bool SamePairs(const std::unordered_map<FormId, Entry>& a, const std::unordered_map<FormId, Entry>& b)
    {
        if (a.size() != b.size()) return false;
        for (const auto& [book, entry] : a) {
            const auto it = b.find(book);
            if (it == b.end() || it->second.spell != entry.spell) return false;
        }
        return true;
    }
}

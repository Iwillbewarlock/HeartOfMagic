#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

// =============================================================================
// EnvironmentMatch - the pure rules behind the explosion twins
//
// No game types: the plugin (EnvironmentTwins.cpp, EnvironmentSelfTest*.cpp)
// and the offline harness (librarian-test --self-test) compile the same code.
// See docs/ENVIRONMENT.md.
//
// Two things live here:
//   - Frostwalker's explosion matcher (Frostwalker 2.2, src/Utils.cpp
//     MatchesAnyPattern / GetExplosionType; pattern files as Settings.cpp
//     LoadPatternsFromFile). tools/frostwalker-twin-check.js is the same
//     matcher in JavaScript; the two must agree.
//   - Whether a magic effect may take a twin instead of its explosion.
// =============================================================================

namespace Environment
{
    // =========================================================================
    // Frostwalker's matcher
    // =========================================================================

    enum class FrostwalkerClass
    {
        kUnknown,
        kNeutral,
        kCold,
        kFire,
        kWater
    };

    [[nodiscard]] inline const char* FrostwalkerClassName(FrostwalkerClass value)
    {
        switch (value) {
            case FrostwalkerClass::kNeutral: return "neutral";
            case FrostwalkerClass::kCold: return "cold";
            case FrostwalkerClass::kFire: return "fire";
            case FrostwalkerClass::kWater: return "water";
            default: return "unknown";
        }
    }

    // The four lists, lower-cased, in Frostwalker's testing order.
    struct FrostwalkerPatterns
    {
        std::vector<std::string> neutral;
        std::vector<std::string> cold;
        std::vector<std::string> fire;
        std::vector<std::string> water;
    };

    // The folder each list is read from (SKSE/Plugins/Frostwalker/<folder>/*.ini)
    inline constexpr std::array<std::string_view, 4> kFrostwalkerFolders = {
        "NeutralSources", "FrostSources", "FireSources", "WaterSources"
    };

    // Frostwalker 2.2's Default.ini lists, for when its folder is not installed
    [[nodiscard]] inline FrostwalkerPatterns FrostwalkerDefaultPatterns()
    {
        return { { "practice" },
            { "cold", "frost", "ice", "blizzard", "freeze", "freezing" },
            { "fire", "flame", "heat" },
            { "water", "steam", "river", "ocaen", "sea" } };
    }

    [[nodiscard]] inline std::string LowerAscii(std::string_view text)
    {
        std::string lowered(text);
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return lowered;
    }

    // One pattern file's lines as Frostwalker keeps them: trimmed (spaces,
    // tabs, CR, LF), blank lines and lines starting with '#' dropped, lower-cased.
    inline void AppendPatternLines(std::string_view fileText, std::vector<std::string>& out)
    {
        constexpr std::string_view kSpace = " \t\r\n";
        std::size_t start = 0;
        while (start <= fileText.size()) {
            std::size_t end = fileText.find('\n', start);
            if (end == std::string_view::npos) end = fileText.size();
            std::string_view line = fileText.substr(start, end - start);
            const std::size_t first = line.find_first_not_of(kSpace);
            if (first != std::string_view::npos) {
                const std::size_t last = line.find_last_not_of(kSpace);
                line = line.substr(first, last - first + 1);
                if (line.front() != '#') out.push_back(LowerAscii(line));
            }
            start = end + 1;
        }
    }

    // "ice" does not count right after "vo" (voice: shouts are named Voice...)
    [[nodiscard]] inline bool MatchesAnyPattern(const std::string& lower, const std::vector<std::string>& patterns)
    {
        for (const auto& pattern : patterns) {
            if (pattern.empty()) continue;
            if (pattern == "ice") {
                for (std::size_t pos = lower.find("ice"); pos != std::string::npos; pos = lower.find("ice", pos + 1)) {
                    if (!(pos >= 2 && lower[pos - 2] == 'v' && lower[pos - 1] == 'o')) return true;
                }
            } else if (lower.find(pattern) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    // The class Frostwalker gives one string (an EditorID, a name or a model path).
    [[nodiscard]] inline FrostwalkerClass ClassifyText(std::string_view text, const FrostwalkerPatterns& patterns)
    {
        if (text.empty()) return FrostwalkerClass::kUnknown;
        const std::string lower = LowerAscii(text);
        if (MatchesAnyPattern(lower, patterns.neutral)) return FrostwalkerClass::kNeutral;
        if (MatchesAnyPattern(lower, patterns.cold)) return FrostwalkerClass::kCold;
        if (MatchesAnyPattern(lower, patterns.fire)) return FrostwalkerClass::kFire;
        if (MatchesAnyPattern(lower, patterns.water)) return FrostwalkerClass::kWater;
        return FrostwalkerClass::kUnknown;
    }

    // An explosion: its EditorID, then its name, then its model path; the
    // first string that classifies decides.
    [[nodiscard]] inline FrostwalkerClass ClassifyExplosion(std::string_view editorId, std::string_view name,
        std::string_view model, const FrostwalkerPatterns& patterns)
    {
        for (const std::string_view text : { editorId, name, model }) {
            const FrostwalkerClass found = ClassifyText(text, patterns);
            if (found != FrostwalkerClass::kUnknown) return found;
        }
        return FrostwalkerClass::kUnknown;
    }

    // What reading a Frostwalker folder found
    struct PatternDirRead
    {
        bool rootFound = false;                     // the folder itself (no folder: Frostwalker is not installed)
        std::vector<std::string> missingFolders;    // *Sources folders not there: empty lists, as Frostwalker reads them
        std::size_t files = 0;
    };

    // The four lists as Frostwalker 2.2 reads them (Settings.cpp LoadAllPatterns):
    // every regular file in each *Sources folder whatever its extension; a
    // missing folder is an empty list. No root: four empty lists and
    // rootFound false - Frostwalker uses no built-in defaults either.
    [[nodiscard]] inline FrostwalkerPatterns ReadFrostwalkerPatternDir(const std::filesystem::path& root,
        PatternDirRead& read)
    {
        read = {};
        FrostwalkerPatterns patterns;
        std::error_code error;
        if (!std::filesystem::is_directory(root, error)) return patterns;
        read.rootFound = true;
        std::vector<std::string>* lists[] = { &patterns.neutral, &patterns.cold, &patterns.fire, &patterns.water };
        for (std::size_t i = 0; i < kFrostwalkerFolders.size(); ++i) {
            const auto folder = root / kFrostwalkerFolders[i];
            if (!std::filesystem::is_directory(folder, error)) {
                read.missingFolders.emplace_back(kFrostwalkerFolders[i]);
                continue;
            }
            std::vector<std::filesystem::path> files;
            for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
                if (entry.is_regular_file(error)) files.push_back(entry.path());
            }
            std::sort(files.begin(), files.end());   // order does not change a substring match; fixed for the log
            for (const auto& file : files) {
                std::ifstream stream(file, std::ios::binary);
                const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
                AppendPatternLines(text, *lists[i]);
                ++read.files;
            }
        }
        return patterns;
    }

    // The in-game frostwalker check's verdict on the user's own lists. The
    // twin must read as no element; the original reading cold is what makes
    // the twin needed, not a condition of the fix (a user who put "blizzard"
    // in NeutralSources has no bug to fix).
    enum class TwinVerdict
    {
        kPass,        // original cold, twin unknown
        kNotNeeded,   // twin unknown, but the original is not cold with these lists
        kFail         // the twin reads as an element
    };

    [[nodiscard]] inline TwinVerdict JudgeTwin(FrostwalkerClass original, FrostwalkerClass twin)
    {
        if (twin != FrostwalkerClass::kUnknown) return TwinVerdict::kFail;
        return original == FrostwalkerClass::kCold ? TwinVerdict::kPass : TwinVerdict::kNotNeeded;
    }

    // =========================================================================
    // The Mysticism patch (HeartOfMagic_Mysticism.esp)
    // =========================================================================
    //
    // MAG_ScreenShakeEffect's script (magicPlaceActivatorScript) places its
    // ExplosionRef property, not the effect's Explosion field; the patch points
    // that property at the twin. It works only as the last file to override
    // the effect: a later override brings the Blizzard explosion back.

    inline constexpr std::string_view kMysticismPlugin = "MysticismMagic.esp";
    inline constexpr std::string_view kMysticismPatchPlugin = "HeartOfMagic_Mysticism.esp";

    enum class PatchVerdict
    {
        kNoMysticism,   // SKIP: nothing to patch
        kMissing,       // FAIL: Mysticism loaded, the patch not
        kNotLast,       // FAIL: another file overrides the effect after the patch (or no file is known)
        kPass           // the patch is the effect's last override
    };

    [[nodiscard]] inline bool SameFileName(std::string_view a, std::string_view b)
    {
        return a.size() == b.size() && LowerAscii(a) == LowerAscii(b);
    }

    // `lastFile`: the last entry of the effect's source-file list (TESForm::GetFile(-1)), empty if it has none
    [[nodiscard]] inline PatchVerdict JudgeMysticismPatch(bool mysticismLoaded, bool patchLoaded, std::string_view lastFile)
    {
        if (!mysticismLoaded) return PatchVerdict::kNoMysticism;
        if (!patchLoaded) return PatchVerdict::kMissing;
        return SameFileName(lastFile, kMysticismPatchPlugin) ? PatchVerdict::kPass : PatchVerdict::kNotLast;
    }

    // =========================================================================
    // Which effects take the twin
    // =========================================================================

    // What the plugin reads off one magic effect record and every item that
    // carries it. Keywords are not read on purpose: Mysticism's screen shake
    // carries MagicDamageFrost and ResistFrost though it does no damage.
    struct EffectFacts
    {
        bool explosionIsOriginal = false;   // data.explosion is the twin's original
        bool archetypeScript = false;       // EffectArchetype::kScript
        bool primaryAvNone = false;         // data.primaryAV == ActorValue::kNone
        bool hideInUI = false;
        bool hostile = false;
        bool detrimental = false;
        std::vector<float> carrierMagnitudes;   // one per entry in a spell, scroll, enchantment, potion or ingredient
    };

    // Empty when the effect takes the twin, else the first reason it does not.
    // An effect no item carries passes the carrier test (nothing carries it
    // with a magnitude); what Frostwalker reads is a cast, which needs a carrier,
    // so such an effect is harmless either way and is reported with its count.
    [[nodiscard]] inline std::string TwinRefusal(const EffectFacts& facts)
    {
        if (!facts.explosionIsOriginal) return "explosion is not the original";
        if (!facts.archetypeScript) return "archetype is not Script";
        if (!facts.primaryAvNone) return "has a primary actor value";
        if (!facts.hideInUI) return "not hidden";
        if (facts.hostile) return "hostile";
        if (facts.detrimental) return "detrimental";
        const bool anyMagnitude = std::any_of(facts.carrierMagnitudes.begin(), facts.carrierMagnitudes.end(),
            [](float magnitude) { return magnitude != 0.0f; });
        if (anyMagnitude) return "an item gives it a magnitude";
        return {};
    }

    // =========================================================================
    // Switching one recorded effect
    // =========================================================================
    //
    // Pointers are only compared, never dereferenced, so the harness can pass
    // made-up addresses. `lastWritten` is what this plugin itself last put on
    // the effect (null: never). The effect is touched only while it still holds
    // that value - or the original, before the first write - so a value another
    // mod set (a third explosion, none, or the original put back while the twin
    // was on) is never overwritten.

    enum class SwitchStep
    {
        kAlready,   // holds what the switch wants: nothing written
        kWrite,     // write the wanted value and remember it
        kForeign    // another mod changed it since: left alone, warn
    };

    template <class T>
    [[nodiscard]] SwitchStep DecideSwitch(const T* current, const T* original, const T* twin, const T* lastWritten,
        bool on)
    {
        const T* want = on ? twin : original;
        if (current == want) return SwitchStep::kAlready;
        const T* ours = lastWritten ? lastWritten : original;
        return current == ours ? SwitchStep::kWrite : SwitchStep::kForeign;
    }

    // What a scan records for a recorded effect holding `current`: the original
    // in place of the twin, anything else as it is.
    template <class T>
    [[nodiscard]] const T* ScannedOf(const T* current, const T* original, const T* twin)
    {
        return current == twin ? original : current;
    }
}

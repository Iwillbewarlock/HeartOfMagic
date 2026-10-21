// =============================================================================
// librarian-test --self-test, environment part  -  the explosion twin rules
// =============================================================================
//
// The pure half of EnvironmentTwins (include/environment/EnvironmentMatch.h):
// Frostwalker's explosion matcher and pattern file reading, which magic
// effects may take a twin, and the in-game mysticism-patch verdict. The same cases as tools/frostwalker-twin-check.js
// for the matcher, so the C++ and the JavaScript copies cannot drift apart
// unnoticed. Also AsciiText::ToLower, which the pattern lines go through.
// Needs no dump and no game.

#include "Common.h"
#include "LibrarianSelfTest.h"

#include "AsciiText.h"
#include "environment/EnvironmentMatch.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    using Environment::FrostwalkerClass;

    int* g_passed = nullptr;
    int* g_failed = nullptr;

    void Check(bool ok, const char* what)
    {
        (ok ? *g_passed : *g_failed)++;
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    }

    void Matcher()
    {
        const auto patterns = Environment::FrostwalkerDefaultPatterns();
        const auto classify = [&](const char* text) { return Environment::ClassifyText(text, patterns); };

        Check(classify("MAG_BlizzardForceExplosion") == FrostwalkerClass::kCold &&
                  classify("BlizzardForceExplosion") == FrostwalkerClass::kCold,
            "matcher: the original's EditorIDs (Mysticism, Skyrim.esm) read cold");
        Check(Environment::ClassifyExplosion("HoM_ShakeBlastTwin", "", "Effects\\FXEmptyObject.nif", patterns) ==
                  FrostwalkerClass::kUnknown,
            "matcher: the twin (EditorID, no name, model) reads unknown");

        Check(classify("VoiceUnrelentingForce") == FrostwalkerClass::kUnknown &&
                  classify("VoiceDragonFrost01") == FrostwalkerClass::kCold && classify("voiceice") == FrostwalkerClass::kCold,
            "matcher edge: 'ice' right after 'vo' does not count, a later 'ice' and 'frost' in a shout do");
        Check(classify("PracticeIceBlast") == FrostwalkerClass::kNeutral && classify("FIREBALL") == FrostwalkerClass::kFire,
            "matcher edge: neutral is tested before cold; case is ignored");
        Check(Environment::ClassifyExplosion("", "Frost Blast", "Effects\\FireNova.nif", patterns) == FrostwalkerClass::kCold &&
                  Environment::ClassifyExplosion("HoM_Twin", "", "Effects\\FireNova.nif", patterns) == FrostwalkerClass::kFire,
            "matcher edge: no EditorID falls to the name, an unknown EditorID and name fall to the model");

        Environment::FrostwalkerPatterns empty;
        Check(Environment::ClassifyText("MAG_BlizzardForceExplosion", empty) == FrostwalkerClass::kUnknown &&
                  Environment::ClassifyText("", patterns) == FrostwalkerClass::kUnknown,
            "matcher fail: no patterns or no text classify nothing");
    }

    void PatternFiles()
    {
        std::vector<std::string> lines;
        Environment::AppendPatternLines("#practICE\npractice\n\n#voICE\n  Cold \r\n\tFrost\r\n", lines);
        Check(lines == std::vector<std::string>{ "practice", "cold", "frost" },
            "pattern file: '#' and blank lines dropped, lines trimmed (CR, tabs) and lower-cased");

        lines.clear();
        Environment::AppendPatternLines("heat", lines);
        Environment::AppendPatternLines("", lines);
        Check(lines == std::vector<std::string>{ "heat" }, "pattern file edge: last line without a newline kept, an empty file adds nothing");

        lines.clear();
        Environment::AppendPatternLines("   \r\n# only comments\n\t\n", lines);
        Check(lines.empty(), "pattern file fail: a file of comments and blanks adds no pattern (no empty pattern matching everything)");
    }

    // AsciiText::ToLower, the one lower-casing of text (pattern lines, plugin keys, editor ids)
    void AsciiFold()
    {
        Check(AsciiText::ToLower("MAG_BlizzardForce01.ESP") == "mag_blizzardforce01.esp",
            "ascii fold: A-Z lowered, digits and punctuation kept");
        // \xED\x95\x9C is UTF-8 for a Korean syllable, \xC4 A-umlaut in Windows-1252, \xD0\x9F Cyrillic Pe in UTF-8
        Check(AsciiText::ToLower("\xED\x95\x9C-ABC-\xC4-\xD0\x9F") == "\xED\x95\x9C-abc-\xC4-\xD0\x9F",
            "ascii fold edge: UTF-8 and code-page bytes stay as they are (no locale)");
        Check(AsciiText::ToLower("").empty() && AsciiText::ToLower(std::string_view()).empty(),
            "ascii fold fail: empty text gives empty text");
    }

    Environment::EffectFacts ShakeEffect()
    {
        Environment::EffectFacts facts;
        facts.explosionIsOriginal = true;
        facts.archetypeScript = true;
        facts.primaryAvNone = true;
        facts.hideInUI = true;
        facts.carrierMagnitudes = std::vector<float>(23, 0.0f);   // the 23 carriers in TAKEALOOK
        return facts;
    }

    void Eligibility()
    {
        Check(Environment::TwinRefusal(ShakeEffect()).empty(), "twin: a hidden harmless Script effect, every carrier magnitude 0, takes it");

        auto unused = ShakeEffect();
        unused.carrierMagnitudes.clear();
        auto negativeZero = ShakeEffect();
        negativeZero.carrierMagnitudes.push_back(-0.0f);
        Check(Environment::TwinRefusal(unused).empty() && Environment::TwinRefusal(negativeZero).empty(),
            "twin edge: an effect no item carries, and a -0 magnitude, still take it");

        auto hazard = ShakeEffect();
        hazard.archetypeScript = false;   // MAG_FrostDamageFFHazardArea: SpawnHazard, not hidden
        hazard.hideInUI = false;
        auto visible = ShakeEffect();
        visible.hideInUI = false;
        auto hostile = ShakeEffect();
        hostile.hostile = true;
        auto detrimental = ShakeEffect();
        detrimental.detrimental = true;
        auto withAv = ShakeEffect();
        withAv.primaryAvNone = false;
        Check(Environment::TwinRefusal(hazard) == "archetype is not Script" && !Environment::TwinRefusal(visible).empty() &&
                  !Environment::TwinRefusal(hostile).empty() && !Environment::TwinRefusal(detrimental).empty() &&
                  !Environment::TwinRefusal(withAv).empty(),
            "twin fail: the Blizzard hazard, a visible, hostile, detrimental or actor-value effect keeps the original");

        auto oneHit = ShakeEffect();
        oneHit.carrierMagnitudes.back() = 5.0f;
        auto other = ShakeEffect();
        other.explosionIsOriginal = false;
        Check(Environment::TwinRefusal(oneHit) == "an item gives it a magnitude" && !Environment::TwinRefusal(other).empty(),
            "twin fail: one carrier with a magnitude among 22 zeros, or another explosion, keeps the original");
    }
}

namespace
{
    using Environment::SwitchStep;

    // One recorded effect driven the way EnvironmentTwins::ApplySwitch drives it
    struct FakeEffect
    {
        const int* current = nullptr;
        const int* lastWritten = nullptr;
    };

    SwitchStep Apply(FakeEffect& effect, const int* original, const int* twin, bool on)
    {
        const SwitchStep step = Environment::DecideSwitch<int>(effect.current, original, twin, effect.lastWritten, on);
        if (step == SwitchStep::kWrite) {
            effect.current = on ? twin : original;
            effect.lastWritten = effect.current;
        }
        return step;
    }

    void Switching()
    {
        const int forms[3] = {};   // three distinct addresses: original, twin, a third mod's explosion
        const int* original = &forms[0];
        const int* twin = &forms[1];
        const int* third = &forms[2];
        const auto scanned = [&](const FakeEffect& e) { return Environment::ScannedOf<int>(e.current, original, twin); };

        FakeEffect effect{ original, nullptr };
        const bool firstOn = Apply(effect, original, twin, true) == SwitchStep::kWrite && effect.current == twin;
        const bool scannedOn = scanned(effect) == original;
        const bool off = Apply(effect, original, twin, false) == SwitchStep::kWrite && effect.current == original;
        const bool scannedOff = scanned(effect) == original;
        const bool onAgain = Apply(effect, original, twin, true) == SwitchStep::kWrite && effect.current == twin;
        Check(firstOn && scannedOn && off && scannedOff && onAgain,
            "switch: on, off, on again each write; the scan records the original in both states");

        const bool sameOn = Apply(effect, original, twin, true) == SwitchStep::kAlready && effect.current == twin;
        FakeEffect fresh{ original, nullptr };
        const bool offFirst = Apply(fresh, original, twin, false) == SwitchStep::kAlready && fresh.current == original &&
                              fresh.lastWritten == nullptr;
        Check(sameOn && offFirst, "switch edge: the same setting again, or off before any write, writes nothing");

        FakeEffect foreign{ third, nullptr };
        const bool thirdKept = Apply(foreign, original, twin, true) == SwitchStep::kForeign && foreign.current == third &&
                               scanned(foreign) == third;
        FakeEffect none{ nullptr, nullptr };
        const bool noneKept = Apply(none, original, twin, true) == SwitchStep::kForeign && none.current == nullptr &&
                              scanned(none) == nullptr;
        Check(thirdKept && noneKept, "switch fail: a third explosion or none (another mod's) is kept and scanned as it is");

        FakeEffect putBack{ original, nullptr };
        Apply(putBack, original, twin, true);
        putBack.current = original;   // another mod puts the original back while the twin is on
        const bool offKeeps = Apply(putBack, original, twin, false) == SwitchStep::kAlready;
        const bool onRefuses = Apply(putBack, original, twin, true) == SwitchStep::kForeign && putBack.current == original;
        FakeEffect moved{ original, nullptr };
        Apply(moved, original, twin, true);
        moved.current = third;        // another mod sets its own while the twin is on
        const bool offRefuses = Apply(moved, original, twin, false) == SwitchStep::kForeign && moved.current == third;
        Check(offKeeps && onRefuses && offRefuses,
            "switch fail: the original another mod put back while the twin was on is not overwritten by switching on "
            "again; another mod's explosion is not reverted");
    }

    void WriteText(const std::filesystem::path& file, const char* text)
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << text;
    }

    // ReadFrostwalkerPatternDir on a made-up folder in the temp directory
    void PatternFolders()
    {
        namespace fs = std::filesystem;
        std::error_code error;
        const fs::path root = fs::temp_directory_path(error) / "hom-environment-selftest-frostwalker";
        fs::remove_all(root, error);

        Environment::PatternDirRead read;
        const auto none = Environment::ReadFrostwalkerPatternDir(root, read);
        Check(!read.rootFound && none.neutral.empty() && none.cold.empty() && none.fire.empty() && none.water.empty(),
            "pattern folder fail: no Frostwalker folder - four empty lists, no built-in defaults (as Frostwalker)");

        fs::create_directories(root / "FrostSources", error);
        fs::create_directories(root / "NeutralSources", error);
        WriteText(root / "FrostSources" / "Default.ini", "cold\r\nfrost\r\n");
        WriteText(root / "FrostSources" / "extra.txt", "Blizzard\n");
        WriteText(root / "NeutralSources" / "Mine", "# a comment\nblizzard\n");
        const auto mine = Environment::ReadFrostwalkerPatternDir(root, read);
        Check(read.rootFound && read.files == 3 && mine.cold == std::vector<std::string>{ "cold", "frost", "blizzard" } &&
                  mine.neutral == std::vector<std::string>{ "blizzard" },
            "pattern folder: every regular file read whatever its extension (.ini, .txt, none), in name order");
        Check(read.missingFolders == std::vector<std::string>{ "FireSources", "WaterSources" } && mine.fire.empty() &&
                  mine.water.empty(),
            "pattern folder edge: a missing *Sources folder is an empty list and is named");

        // The in-game check on those lists: "blizzard" in NeutralSources makes the original neutral
        const auto original = Environment::ClassifyExplosion("MAG_BlizzardForceExplosion", "", "Effects\\FXEmptyObject.nif", mine);
        const auto twin = Environment::ClassifyExplosion("HoM_ShakeBlastTwin", "", "Effects\\FXEmptyObject.nif", mine);
        Check(original == FrostwalkerClass::kNeutral && Environment::JudgeTwin(original, twin) == Environment::TwinVerdict::kNotNeeded,
            "twin verdict edge: the original not cold with the user's lists - the twin is not needed (SKIP, not FAIL)");
        fs::remove_all(root, error);
    }

    void TwinVerdicts()
    {
        using Environment::TwinVerdict;
        Check(Environment::JudgeTwin(FrostwalkerClass::kCold, FrostwalkerClass::kUnknown) == TwinVerdict::kPass,
            "twin verdict: original cold, twin unknown - PASS");
        Check(Environment::JudgeTwin(FrostwalkerClass::kCold, FrostwalkerClass::kCold) == TwinVerdict::kFail &&
                  Environment::JudgeTwin(FrostwalkerClass::kUnknown, FrostwalkerClass::kFire) == TwinVerdict::kFail,
            "twin verdict fail: a twin that reads as an element fails, whatever the original");
    }

    // The in-game mysticism-patch check's verdict (EnvironmentSelfTestChecks.cpp CheckMysticismPatch)
    void MysticismPatchVerdicts()
    {
        using Environment::PatchVerdict;
        const auto judge = [](bool mysticism, bool patch, const char* last) {
            return Environment::JudgeMysticismPatch(mysticism, patch, last);
        };
        Check(judge(false, false, "") == PatchVerdict::kNoMysticism,
            "mysticism patch: no MysticismMagic.esp - SKIP, nothing to patch");
        Check(judge(true, true, "HeartOfMagic_Mysticism.esp") == PatchVerdict::kPass &&
                  judge(true, true, "heartofmagic_mysticism.ESP") == PatchVerdict::kPass,
            "mysticism patch: the patch is the effect's last file - PASS (file name case ignored)");
        Check(judge(true, false, "Synthesis - Sound and Combat.esp") == PatchVerdict::kMissing,
            "mysticism patch fail: Mysticism without the patch - FAIL, install it");
        Check(judge(true, true, "Synthesis - Sound and Combat.esp") == PatchVerdict::kNotLast &&
                  judge(true, true, "MysticismMagic.esp") == PatchVerdict::kNotLast &&
                  judge(true, true, "") == PatchVerdict::kNotLast &&
                  judge(true, true, "HeartOfMagic_Mysticism.esp.bak") == PatchVerdict::kNotLast,
            "mysticism patch fail: another file after the patch, no file, or a longer name - FAIL, load it last");
    }
}

void RunEnvironmentSelfTest(int& passed, int& failed)
{
    g_passed = &passed;
    g_failed = &failed;
    Matcher();
    PatternFiles();
    AsciiFold();
    Eligibility();
    Switching();
    PatternFolders();
    TwinVerdicts();
    MysticismPatchVerdicts();
}

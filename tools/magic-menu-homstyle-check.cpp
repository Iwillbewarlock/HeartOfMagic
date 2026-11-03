// magic-menu-homstyle-check - offline check of the Heart of Magic block's style reader
// (plugins/SL_MagicMenu/src/HomStyle.cpp, spec v11 T, R1, R4, R7, R10).
//
//   magic-menu-homstyle-check [config.json [presetDir]]   reads HoM's real files as the plugin does and prints what it got
//                                                         (default: the TAKEALOOK overwrite config.json and the repo's
//                                                         SKSE/Plugins/SpellLearning/presets/design)
//   magic-menu-homstyle-check --selftest                  made-up input: normal, boundary and failure cases
//
// Exit 0 on success, 1 when a self-test fails.

#include "HomStyle.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr int kExitOk = 0;
    constexpr int kExitFail = 1;

    constexpr const char* kDefaultConfig = "D:/TAKEALOOK/overwrite/SKSE/Plugins/SpellLearning/config.json";
    constexpr const char* kDefaultPresets = "D:/DEV/HeartOfMagic/SKSE/Plugins/SpellLearning/presets/design";

    int g_failures = 0;

    void Expect(bool a_ok, const std::string& a_what)
    {
        std::printf("  %s  %s\n", a_ok ? "PASS" : "FAIL", a_what.c_str());
        if (!a_ok) ++g_failures;
    }

    bool Near(double a_lhs, double a_rhs, double a_tolerance = 1e-9) { return std::abs(a_lhs - a_rhs) < a_tolerance; }

    bool HasNote(const HomStyle::Style& a_style, std::string_view a_part)
    {
        for (const auto& note : a_style.notes) {
            if (note.find(a_part) != std::string::npos) return true;
        }
        return false;
    }

    double Drawn(const std::vector<HomStyle::Segment>& a_segments)
    {
        double length = 0.0;
        for (const auto& s : a_segments) length += std::hypot(s.b.x - s.a.x, s.b.y - s.a.y);
        return length;
    }

    // Skald.json's "tree" block as shipped (SKSE/Plugins/SpellLearning/presets/design/Skald.json)
    constexpr std::string_view kSkald = R"json({
        "id": "skald", "name": "Skald",
        "tree": {
            "pageColor": "#0b0b0c", "pageGrain": 0.35, "pageGrainColor": "#4a4740", "pageGlow": "#1e1d1a", "pageGlowAlpha": 0.7,
            "pageGlowRadius": 0.65, "pageEdge": "#000000", "pageEdgeAlpha": 0.8, "schoolInk": 0.4, "schoolInkTone": "#cbc5b6",
            "labelFont": "var(--skald-font)", "labelMaxChars": 18, "labelHalo": "rgba(5, 5, 6, 0.9)", "labelHaloWidth": 3,
            "labelUnlocked": "#f2eee2", "labelAvailable": "rgba(232, 228, 216, 0.85)", "labelHidden": "rgba(203, 197, 182, 0.4)",
            "nodeFill": "#0e0e0f", "lockedStroke": "#6f6b62", "mysteryFill": "rgba(14, 14, 15, 0.9)", "focusStroke": "#fffbef",
            "ringTrack": "#e8e4d8", "availableAlpha": 1, "availableRing": true, "learningGlow": 0.5, "hubFill": "#000000",
            "hubRing": "#cfc9b8", "hubText": "#e8e4d8", "globeColor": "#cfc9b8", "dimEdgeColor": "#1c1b19", "unlockedEdgeAlpha": 0.6,
            "unlockedEdgeWidth": 1.6, "lockedEdgeColor": "#6f6b62", "lockedEdgeAlpha": 0.22, "frontierEdgeAlpha": 0.45,
            "selectedPathColor": "#fffbef", "selectedPathAlpha": 0.9, "selectedPathWidth": 2.2, "hoverPathAlpha": 0.8,
            "accent": "#cfc9b8", "chapterTitles": true, "dividerOrnament": true, "pageOrnament": "themes/skald/corner.png"
        }
    })json";

    // The user's config.json, the keys the reader uses (D:/TAKEALOOK/overwrite/SKSE/Plugins/SpellLearning/config.json, 2026-10-26)
    constexpr std::string_view kConfig = R"json({
        "designPreset": "skald", "discoveryMode": true, "showSelectionPath": true, "showNodeNames": true,
        "showBaseConnections": true, "revealName": 10, "cheatMode": false, "focusDimOthers": true,
        "learningPathColor": "#00ffff", "heartRingColor": "#b8a878", "learningColor": "#7890A8",
        "schoolColors": {"Alteration": "#22c55e", "Conjuration": "#a855f7", "Destruction": "#ef4444", "Illusion": "#38bdf8",
                         "Restoration": "#facc15", "Unknown": "#f97316"},
        "schoolVisibility": {}
    })json";

    void TestColours()
    {
        using HomStyle::Color;
        using HomStyle::ParseColor;
        std::printf("colours: parse, ink, dim, inner accent\n");
        Expect(ParseColor("#ef4444") == Color{ 0xEF4444, 1.0 }, "\"#ef4444\" -> 0xEF4444, alpha 1");
        Expect(ParseColor(" #FFFBEF ") == Color{ 0xFFFBEF, 1.0 }, "upper case and spaces around it");
        Expect(ParseColor("#abc") == Color{ 0xAABBCC, 1.0 }, "\"#abc\" -> 0xAABBCC");
        const auto rgba = ParseColor("rgba(232, 228, 216, 0.85)");
        Expect(rgba && rgba->rgb == 0xE8E4D8 && Near(rgba->alpha, 0.85), "\"rgba(232, 228, 216, 0.85)\" keeps its alpha 0.85");
        Expect(ParseColor("rgb(1,2,3)") == Color{ 0x010203, 1.0 }, "\"rgb(1,2,3)\" without spaces");
        Expect(!ParseColor("red") && !ParseColor("#12345") && !ParseColor("#ggg") && !ParseColor(""), "a colour name, 5 hex digits, not hex, empty: none");
        Expect(!ParseColor("rgba(1, 2, 3)") && !ParseColor("rgb(1, 2, 3, 0.5)") && !ParseColor("rgba(300, 0, 0, 1)") &&
                   !ParseColor("rgba(1, 2, 3, 1.5)") && !ParseColor("rgb(1, 2, 3"),
            "rgba with 3 parts, rgb with 4, a channel over 255, alpha over 1, no ')': none");
        Expect(!ParseColor("var(--skald-font)"), "a CSS variable is no colour");

        const Color tone{ 0xCBC5B6, 1.0 };
        const auto inked = HomStyle::Ink({ 0xEF4444, 1.0 }, 0.4, tone);
        Expect(inked.rgb == 0xE17872, std::format("Destruction #ef4444 inked 0.4 toward #cbc5b6 -> #e17872 (got {})", HomStyle::Text(inked)));
        Expect(HomStyle::Ink({ 0x22C55E, 1.0 }, 0.4, tone).rgb == 0x66C581, "Alteration #22c55e -> #66c581 (0x22 + (0xcb - 0x22) x 0.4 = 101.6 -> 102)");
        Expect(HomStyle::Ink({ 0xEF4444, 0.5 }, 0.0, tone) == Color{ 0xEF4444, 0.5 }, "ink 0: the colour unchanged, alpha too");
        Expect(HomStyle::Ink({ 0x000000, 1.0 }, 1.0, tone).rgb == 0xCBC5B6, "ink 1: the tone");
        // review v11 #3: TreeStyle.ink returns '#rrggbb' - a mixed see-through colour comes back opaque (GPT ran treeStyle.js)
        const auto inkedHalf = HomStyle::Ink({ 0xEF4444, 0.5 }, 0.4, tone);
        Expect(inkedHalf == Color{ 0xE17872, 1.0 }, std::format("rgba(239,68,68,0.5) with Skald's ink -> opaque #e17872 (got {})", HomStyle::Text(inkedHalf)));
        Expect(HomStyle::Dim(inked, HomStyle::kMysteryDim).rgb == 0x5A302E, "the mystery outline: #e17872 x 0.4 -> #5a302e");
        Expect(HomStyle::InnerAccent(inked).rgb == 0x4F242E, "the unlocked core: #e17872 -> r x 0.35, g x 0.3, b x 0.4 = #4f242e");
    }

    void TestFullPreset()
    {
        std::printf("normal: the user's config with the full Skald preset\n");
        const auto style = HomStyle::Build(std::string(kConfig), { { "Skald", std::string(kSkald) }, { "Other", R"({"id":"other","tree":{}})" } });
        const auto& t = style.tree;
        Expect(style.configFound && style.presetId == "skald" && style.presetFile == "presets/design/Skald.json", "preset skald found in Skald.json");
        Expect(style.notes.empty(), std::format("no notes ({})", style.notes.empty() ? "" : style.notes.front()));
        Expect(t.labelMaxChars == 18 && t.labelUnlocked.rgb == 0xF2EEE2 && Near(t.labelAvailable.alpha, 0.85) && Near(t.labelHidden.alpha, 0.4),
            "labels: 18 characters, #f2eee2, available alpha 0.85, hidden alpha 0.4");
        Expect(t.nodeFill.rgb == 0x0E0E0F && t.lockedStroke && t.lockedStroke->rgb == 0x6F6B62 && t.mysteryFill.rgb == 0x0E0E0F &&
                   Near(t.mysteryFill.alpha, 0.9) && t.focusStroke.rgb == 0xFFFBEF,
            "nodes: fill #0e0e0f, locked stroke #6f6b62, mystery fill alpha 0.9, focus #fffbef");
        Expect(!t.unlockedFill && !t.unlockedRim && !t.unlockedCore, "Skald leaves unlocked fill / rim / core to the school colour");
        Expect(Near(t.availableAlpha, 1.0) && t.availableRing && Near(t.schoolInk, 0.4) && t.schoolInkTone.rgb == 0xCBC5B6,
            "available alpha 1 (a JSON 1 is a number like the default 0.8), ring on, ink 0.4 toward #cbc5b6");
        Expect(t.dimEdgeColor.rgb == 0x1C1B19 && Near(t.unlockedEdgeAlpha, 0.6) && Near(t.unlockedEdgeWidth, 1.6) && t.lockedEdgeColor.rgb == 0x6F6B62 &&
                   Near(t.lockedEdgeAlpha, 0.22) && Near(t.frontierEdgeAlpha, 0.45),
            "edges: dim #1c1b19, unlocked 0.6 x 1.6, locked #6f6b62 at 0.22, frontier 0.45");
        Expect(t.selectedPathColor.rgb == 0xFFFBEF && Near(t.selectedPathAlpha, 0.9) && Near(t.selectedPathWidth, 2.2), "selected path #fffbef 0.9 x 2.2");
        Expect(style.hubRing.rgb == 0xCFC9B8, "hub ring: Skald's #cfc9b8 (the player's #b8a878 is the shipped default, R4)");
        Expect(style.learning.rgb == 0x00FFFF, "learning colour: the player's learningPathColor #00ffff (Skald has no learningColor; config's "
                                               "top-level learningColor is not the renderer's)");
        Expect(style.discoveryMode && style.Mystery() && style.showSelectionPath && style.showNodeNames && style.showBaseConnections &&
                   !style.cheatMode && style.focusDimOthers && Near(style.revealName, 10.0),
            "switches: discovery (mystery look), selection path, names, base connections on; cheat off; revealName 10");
        Expect(style.SchoolColor("Destruction").rgb == 0xE17872 && style.SchoolColor("Unknown").rgb != 0x888888,
            "school colours from config, inked (Destruction #e17872; config's 'Unknown' entry used)");
        Expect(style.SchoolColor("Destruction", "#000000").rgb == 0x514F49, "a node's themeColor wins, inked too (#000000 -> #514f49)");
        Expect(style.SchoolShown("Destruction") && Near(style.DimFactor(), 0.6), "no school hidden; dim factor 0.6");
        Expect(style.Summary().find("design preset 'skald' from presets/design/Skald.json") != std::string::npos, "the log line names the file");
    }

    void TestMissingKeys()
    {
        std::printf("boundary: missing keys, wrong types, other files\n");
        // a preset with three keys, one of the wrong type; a config without most keys
        const auto style = HomStyle::Build(std::string(R"({"designPreset": "Thin", "schoolVisibility": {"Illusion": false, "Alteration": 0},
                                                            "cheatMode": 1, "focusDimOthers": false})"),
            { { "thin", R"({"tree": {"labelMaxChars": 10, "availableRing": 1, "nodeFill": "#010101"}})" } });
        const auto& t = style.tree;
        Expect(style.presetFile == "presets/design/thin.json", "a file without an id is found by its name (lowercased: Thin = thin)");
        Expect(t.labelMaxChars == 10 && t.nodeFill.rgb == 0x010101, "the keys it has: labelMaxChars 10, nodeFill #010101");
        Expect(!t.availableRing && HasNote(style, "tree.availableRing = 1 is not true or false"), "availableRing 1 (not a bool): default false, noted");
        Expect(Near(t.availableAlpha, 0.8) && t.labelUnlocked.rgb == 0xFFFFFF && t.dimEdgeColor.rgb == 0x222222 && Near(t.schoolInk, 0.0),
            "the keys it lacks: treeStyle.js DEFAULTS (available alpha 0.8, labels white, dim edges #222222, no ink)");
        Expect(style.SchoolColor("Destruction").rgb == 0xEF4444 && style.SchoolColor("Mystery").rgb == 0x888888,
            "no schoolColors: HoM's defaults (Destruction #ef4444), an unknown school #888888; no ink");
        Expect(!style.SchoolShown("Illusion") && style.SchoolShown("Alteration") && style.SchoolShown("Destruction"),
            "schoolVisibility: Illusion false hidden; Alteration 0 is not false (HoM tests === false); a school not listed shown");
        Expect(style.cheatMode && !style.Mystery() && style.discoveryMode, "cheatMode 1 is truthy: no mystery look although discovery is on");
        Expect(Near(style.DimFactor(), 1.0) && Near(style.revealName, 0.0), "focusDimOthers false: no dimming; no revealName: 0");
        Expect(style.learning.rgb == 0x00FFFF && style.hubRing.rgb == 0xB8A878, "no colours in config or preset: #00ffff and #b8a878");

        // the player's own colours win over a design's tokens; a design's render block over the player's setting
        const auto own = HomStyle::Build(std::string(R"({"designPreset":"p","heartRingColor":"#FF0000","learningPathColor":"#00FFFF"})"),
            { { "p", R"({"id":"P","tree":{"hubRing":"#cfc9b8","learningColor":"#123456"}})" } });
        Expect(own.hubRing.rgb == 0xFF0000, "heartRingColor changed by the player (#FF0000): kept over the design's hubRing");
        Expect(own.learning.rgb == 0x123456, "learningPathColor at its shipped default (any case): the design's learningColor #123456");
        const auto render = HomStyle::Build(std::string(R"({"designPreset":"p","learningPathColor":"#ff00ff"})"),
            { { "p", R"({"render":{"learningPathColor":"#00ff00"},"tree":{}})" } });
        Expect(render.learning.rgb == 0x00FF00, "a design's render block sets learningPathColor before the player's setting");
    }

    void TestFailures()
    {
        std::printf("failure: broken and missing files, an id that matches nothing, built-in designs\n");
        const auto broken = HomStyle::Build(std::string("{ \"designPreset\": "), { { "Skald", std::string(kSkald) } });
        Expect(broken.configFound && HasNote(broken, "config.json is not a JSON object"), "a cut-off config: noted");
        Expect(broken.presetId == "arcane" && broken.presetFile.empty() && HasNote(broken, "built-in"),
            "and with no designPreset HoM's default design 'arcane' (built in): DEFAULTS");
        Expect(broken.showNodeNames && broken.discoveryMode && !broken.cheatMode && broken.learning.rgb == 0x00FFFF,
            "every setting at HoM's default");

        const auto missing = HomStyle::Build(std::nullopt, {});
        Expect(!missing.configFound && HasNote(missing, "config.json not found"), "no config file: noted, defaults");

        const auto noMatch = HomStyle::Build(std::string(R"({"designPreset":"skald"})"),
            { { "Skald", R"({"id":"skald-old","tree":{"nodeFill":"#010101"}})" }, { "Bad", "{not json" } });
        Expect(noMatch.presetFile.empty() && noMatch.tree.nodeFill.rgb == 0x1A1A2E, "a file whose id is not designPreset is not used (file name ignored)");
        Expect(HasNote(noMatch, "preset skald not found, HoM's built-in presets are not read by this block"), "R7's note: preset skald not found");
        Expect(HasNote(noMatch, "Bad.json is not a JSON object"), "a broken preset file: skipped and noted");

        const auto builtIn = HomStyle::Build(std::string(R"({"designPreset":"Arcane"})"), { { "arcane", R"({"tree":{"nodeFill":"#010101"}})" } });
        Expect(builtIn.presetFile.empty() && builtIn.tree.nodeFill.rgb == 0x1A1A2E,
            "a built-in id (arcane) is HoM's own: a file cannot replace it (designPresets.js:337), DEFAULTS");

        const auto badValues = HomStyle::Build(std::string(R"({"designPreset":"p","revealName":"10","heartRingColor":"gold",
                                                              "schoolColors":{"Destruction":5}})"),
            { { "p", R"({"tree":{"labelMaxChars":0,"focusStroke":"bright","schoolInk":2}})" } });
        Expect(Near(badValues.revealName, 0.0) && HasNote(badValues, "revealName = \"10\" is not a number"), "revealName as text: 0, noted");
        Expect(badValues.hubRing.rgb == 0xB8A878 && HasNote(badValues, "heartRingColor \"gold\" is not a colour"), "a colour name: HoM's default, noted");
        Expect(badValues.SchoolColor("Destruction").rgb == 0xEF4444 && HasNote(badValues, "schoolColors.Destruction = 5"), "a school colour 5: the default");
        Expect(badValues.tree.labelMaxChars == 12 && badValues.tree.focusStroke.rgb == 0xFFFFFF && Near(badValues.tree.schoolInk, 0.0),
            "labelMaxChars 0, focusStroke 'bright', schoolInk 2: each keeps its default");

        // Read on real files: a missing config, a preset folder with a broken file and Skald
        std::error_code ec;
        const auto dir = std::filesystem::temp_directory_path(ec) / "magic-menu-homstyle-check";
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
        {
            std::ofstream(dir / "Skald.json", std::ios::binary) << kSkald;
            std::ofstream(dir / "Broken.JSON", std::ios::binary) << "{";
            std::ofstream(dir / "notes.txt", std::ios::binary) << "not a preset";
            std::ofstream(dir / "config.json", std::ios::binary) << kConfig;
        }
        const auto read = HomStyle::Read((dir / "config.json").string(), dir.string());
        Expect(read.configFound && read.presetFile == "presets/design/Skald.json" && read.tree.availableRing,
            "Read: config and Skald.json from a folder (the .txt left out)");
        Expect(HasNote(read, "Broken.json is not a JSON object"), "Read: Broken.JSON (upper-case extension) read and skipped");
        const auto none = HomStyle::Read((dir / "no-config.json").string(), (dir / "no-folder").string());
        Expect(!none.configFound && HasNote(none, "could not be listed"), "Read: a missing config and preset folder: defaults, both noted");
        std::filesystem::remove_all(dir, ec);
    }

    void TestStates()
    {
        using HomStyle::Behaviour;
        using HomStyle::Display;
        using HomStyle::DisplayOf;
        std::printf("states: R1 mapping and the reveal rule (T15, T16)\n");
        Expect(DisplayOf(Behaviour::kMastered, false) == Display::kUnlocked && DisplayOf(Behaviour::kMastered, true) == Display::kUnlocked,
            "mastered -> unlocked (the target too, once mastered)");
        Expect(DisplayOf(Behaviour::kTarget, true) == Display::kLearning, "the target -> learning");
        Expect(DisplayOf(Behaviour::kReady, true) == Display::kLearning, "a 100 % target not learned yet (LookOf says kReady) -> learning");
        Expect(DisplayOf(Behaviour::kReady, false) == Display::kAvailable, "ready, not the target -> available");
        Expect(DisplayOf(Behaviour::kLearning, false) == Display::kAvailable, "learned early, not the target -> available (HoM keeps it available)");
        Expect(DisplayOf(Behaviour::kAvailable, false) == Display::kAvailable && DisplayOf(Behaviour::kLocked, false) == Display::kLocked,
            "available -> available, locked -> locked");
        Expect(DisplayOf(Behaviour::kUnknown, true) == Display::kLocked && DisplayOf(Behaviour::kUnknown, false) == Display::kLocked,
            "no state yet -> locked, even for the target");

        using HomStyle::Name;
        using HomStyle::Reveal;
        Expect(Reveal(Display::kLocked, 100.0, 10.0, false) == Name::kNone, "locked: no name (the label is none)");
        Expect(Reveal(Display::kLocked, 0.0, 10.0, true) == Name::kShown,
            "review v11 #9: locked under cheatMode - the name (tooltip, card; canvasRendererInput.js:29,34); the label stays none in Widget");
        Expect(Reveal(Display::kAvailable, 9.9, 10.0, false) == Name::kHidden, "available at 9.9 % under revealName 10: ???");
        Expect(Reveal(Display::kAvailable, 10.0, 10.0, false) == Name::kShown, "available at 10 % = revealName: the name (pct < threshold hides)");
        Expect(Reveal(Display::kAvailable, 0.0, 10.0, true) == Name::kShown, "cheatMode shows it");
        Expect(Reveal(Display::kAvailable, 0.0, 0.0, false) == Name::kShown, "revealName 0: always the name");
        Expect(Reveal(Display::kLearning, 0.0, 10.0, false) == Name::kShown && Reveal(Display::kUnlocked, 0.0, 10.0, false) == Name::kShown,
            "learning and unlocked: the name at any XP");

        Expect(HomStyle::ClipLabel("Eighteen chars abc", 18) == "Eighteen chars abc", "18 characters at 18: as it is");
        Expect(HomStyle::ClipLabel("Nineteen chars abcd", 18) == "Nineteen chars ab...", "19 at 18: 17 kept and the clip mark");
        Expect(HomStyle::ClipLabel("\xED\x99\x94\xEC\x97\xBC\xEA\xB5\xAC", 2) == "\xED\x99\x94...", "Korean counts characters, not bytes (3 at 2 -> 1 kept)");
        Expect(HomStyle::ClipLabel("abc", 0) == "abc" && HomStyle::ClipLabel("", 5).empty(), "a limit under 1 and an empty name: unchanged");

        Expect(Near(HomStyle::MysterySize("Master", 0), 11.0) && Near(HomStyle::MysterySize("novice", 5), 7.0), "mystery size by level: master 11, novice 7");
        Expect(Near(HomStyle::MysterySize("", 3), 9.0) && Near(HomStyle::MysterySize("Legend", 9), 11.0) && Near(HomStyle::MysterySize("", 0), 7.0),
            "else by tier: 3 -> 9, 9 -> 11 (at most master's), 0 -> 7");
    }

    void TestShapes()
    {
        std::printf("shapes: HoM's school shapes, the turn toward the origin, the dashed outline (R10)\n");
        // Destruction at world (10, 0): turn = atan2(0, 10) + pi/4; its first point (0, -1) turns to (sin, -cos) x size
        const auto diamond = HomStyle::ShapePoints("Destruction", 10.0, 0.0, 0.0, 0.0, 2.0);
        const double q = std::sqrt(2.0);
        Expect(diamond.size() == 4 && Near(diamond[0].x, q) && Near(diamond[0].y, -q), "Destruction at (10, 0): a diamond turned 45 degrees, first point (1.41, -1.41)");
        const auto turned = HomStyle::ShapePoints("Destruction", 0.0, 10.0, 5.0, 5.0, 2.0);
        Expect(Near(turned[0].x, 5.0 + q) && Near(turned[0].y, 5.0 + q), "at (0, 10) it turns a further 90 degrees, moved to (5, 5)");
        const auto circle = HomStyle::ShapePoints("Restoration", 3.0, 4.0, 0.0, 0.0, 9.0);
        Expect(circle.size() == static_cast<std::size_t>(HomStyle::kCircleSegments) && Near(circle[0].x, 9.0) && Near(circle[0].y, 0.0),
            "Restoration: a circle, not turned, starting at angle 0");
        Expect(HomStyle::ShapePoints("Mysticism", 1, 1, 0, 0, 1).size() == static_cast<std::size_t>(HomStyle::kCircleSegments),
            "a school HoM has no shape for: the circle");
        Expect(HomStyle::ShapePoints("Alteration", 1, 1, 0, 0, 1).size() == 6 && HomStyle::ShapePoints("Conjuration", 1, 1, 0, 0, 1).size() == 5 &&
                   HomStyle::ShapePoints("Illusion", 1, 1, 0, 0, 1).size() == 3,
            "hexagon, pentagon, triangle");
        Expect(Near(HomStyle::ShapeReach("Illusion"), std::hypot(0.85, 0.6)) && Near(HomStyle::ShapeReach("Alteration"), std::hypot(0.9, 0.5)) &&
                   Near(HomStyle::ShapeReach("Destruction"), 1.0) && Near(HomStyle::ShapeReach("Restoration"), 1.0),
            "reach: triangle 1.04, hexagon 1.03, diamond and circle 1");

        using HomStyle::Display;
        const auto available = HomStyle::SizeOf(Display::kAvailable, true, "", 0, false, true);
        Expect(Near(available.size, 9.0) && available.ring && Near(HomStyle::Reach(available, "Destruction", 0.75), 13.0 * 0.75 + 0.6),
            "available: radius 9 with the ring at 13 -> reach 13 x 0.75 + its half line 0.6 = 10.35");
        const auto known = HomStyle::SizeOf(Display::kLearning, true, "", 0, true, true);
        Expect(Near(known.size, 13.5) && !known.ring && Near(known.stroke, 1.5), "learning, selected: 12 + 1.5, focus line 1.5, no ring");
        Expect(Near(HomStyle::SizeOf(Display::kLocked, true, "Master", 0, false, true).size, 11.0) &&
                   Near(HomStyle::SizeOf(Display::kLocked, false, "Master", 0, false, true).size, 7.0),
            "locked: the mystery look by tier (master 11), else 7");
        Expect(Near(HomStyle::MaxReach(0.75, true), 14.5 * 0.75 + 0.6), "largest reach with availableRing: a selected learnable's ring (9 + 1.5 + 4) "
                                                                         "x 0.75 + 0.6 = 11.475");
        Expect(Near(HomStyle::MaxReach(0.75, false), 13.5 * std::hypot(0.85, 0.6) * 0.75 + 0.75),
            "without the ring: a selected learned triangle 13.5 x 1.04 x 0.75 + 0.75 = 11.28");
        Expect(HomStyle::MaxReach(0.75, true) <= 37.9 * 0.75 / 2, "R5: 11.475 <= half the tree's closest pair at 0.75 (14.21)");
        // review v11 #5: line widths in world units - the half lines x fTreeScale as well
        Expect(Near(HomStyle::Reach(available, "Destruction", 0.75, 0.75), 13.0 * 0.75 + 0.6 * 0.75),
            "available at line scale 0.75: 9.75 + its half ring line 0.45 = 10.2");
        Expect(Near(HomStyle::MaxReach(0.75, true, 0.75), 14.5 * 0.75 + 0.45), "largest reach, lines x 0.75: 10.875 + 0.45 = 11.325");
        Expect(Near(HomStyle::MaxReach(0.75, false, 0.75), 13.5 * std::hypot(0.85, 0.6) * 0.75 + 0.75 * 0.75),
            "without the ring, lines x 0.75: 13.5 x 1.04 x 0.75 + 0.5625 = 11.09");
        Expect(HomStyle::MaxReach(0.75, true, 0.75) <= 37.9 * 0.75 / 2, "R5 with world-unit lines: 11.325 <= 14.21");

        // a 7 x 7 square, dash 3 gap 2: one running phase draws dashes at 0, 5, .. 25 of the 28 = 18 (a pattern restarted at
        // each corner would draw 3 + 2 per side = 20)
        const std::vector<HomStyle::Point> square{ { 0, 0 }, { 7, 0 }, { 7, 7 }, { 0, 7 } };
        const auto dashes = HomStyle::DashOutline(square, 3.0, 2.0);
        Expect(Near(Drawn(dashes), 18.0, 1e-6), std::format("square 7, dash 3 gap 2: {:.3f} drawn (18 with one phase round the outline)", Drawn(dashes)));
        Expect(!dashes.empty() && Near(dashes.front().a.x, 0.0) && Near(dashes.front().a.y, 0.0), "the first dash starts at the first point");
        Expect(Near(Drawn(HomStyle::DashOutline(square, 3.0, 0.0)), 28.0, 1e-6), "gap 0: the whole outline");
        Expect(HomStyle::DashOutline(square, 0.0, 2.0).empty() && HomStyle::DashOutline({ { 0, 0 } }, 3.0, 2.0).empty(),
            "dash 0 or a single point: nothing");
    }

    int RunSelfTest()
    {
        TestColours();
        TestFullPreset();
        TestMissingKeys();
        TestFailures();
        TestStates();
        TestShapes();
        std::printf(g_failures == 0 ? "selftest: all PASS\n" : "selftest: %d FAIL\n", g_failures);
        return g_failures == 0 ? kExitOk : kExitFail;
    }
}

int main(int a_argc, char** a_argv)
{
    const std::string_view arg = a_argc > 1 ? a_argv[1] : "";
    if (arg == "--selftest") return RunSelfTest();
    const std::string config = arg.empty() ? kDefaultConfig : std::string(arg);
    const std::string presets = a_argc > 2 ? a_argv[2] : kDefaultPresets;
    const auto style = HomStyle::Read(config, presets);
    std::printf("config: %s\npresets: %s\n%s\n", config.c_str(), presets.c_str(), style.Summary().c_str());
    for (const auto& note : style.notes) std::printf("  note: %s\n", note.c_str());
    for (const auto* school : { "Alteration", "Conjuration", "Destruction", "Illusion", "Restoration" }) {
        std::printf("  %-12s %s (inked)%s\n", school, HomStyle::Text(style.SchoolColor(school)).c_str(), style.SchoolShown(school) ? "" : " - hidden");
    }
    return kExitOk;
}

#pragma once

// =============================================================================
// HOM STYLE - Heart of Magic's spell-tree look and tree settings, read for the
// magic menu's tree window (spec v11 T, R1-R12). PROTOTYPE ONLY. Read at menu
// open from HoM's own files: Data/SKSE/Plugins/SpellLearning/config.json and the
// design preset presets/design/*.json whose "id" is config's designPreset (its
// "tree" block); a key the preset lacks keeps treeStyle.js DEFAULTS. Pure data:
// no GFx, no game calls, no logger (reasons come back in Style::notes), so the
// offline check tools link it too.
//
// Numbers below are HoM's (PrismaUI/views/SpellLearning/SpellLearningPanel/
// modules/): world units, every size a RADIUS - the unit shapes are drawn
// under ctx.scale(size, size) (canvasRendererNodes.js:261,
// canvasRendererSpell.js:258; NodeBatch.unitPath's circle has radius 1).
// =============================================================================

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace HomStyle
{
    // ---- colours ----

    /// A colour as HoM writes it: "#rrggbb" (or "#rgb"), "rgb(r, g, b)" or "rgba(r, g, b, a)"; alpha 0..1
    struct Color
    {
        std::uint32_t rgb{};
        double alpha = 1.0;

        friend bool operator==(const Color&, const Color&) = default;
    };

    std::optional<Color> ParseColor(std::string_view a_text);

    /// TreeStyle.ink (treeStyle.js:213): each channel mixed toward a_tone by a_amount and rounded (Math.round);
    /// a_amount <= 0 leaves the colour as it is (its alpha too); a mixed colour is opaque, as ink's '#rrggbb' (review v11 #3).
    Color Ink(Color a_color, double a_amount, Color a_tone);

    /// CanvasRenderer.dimColor (canvasRendererColors.js:72): each channel times a_factor, rounded
    Color Dim(Color a_color, double a_factor);

    /// CanvasRenderer.getInnerAccentColor (canvasRendererColors.js:97): r x 0.35, g x 0.3, b x 0.4, rounded
    Color InnerAccent(Color a_color);

    /// "#rrggbb", with "@0.85" when the alpha is under 1 (for the log)
    std::string Text(Color a_color);

    // ---- HoM's sizes and fixed looks (canvasRendererV2.js:84-110, canvasRendererSpell.js, canvasRendererEdges.js) ----

    inline constexpr double kKnownSize = 12.0;      // KNOWN_SIZE: unlocked and the one being learned
    inline constexpr double kLearnableSize = 9.0;   // LEARNABLE_SIZE
    inline constexpr double kLockedSize = 7.0;      // LOCKED_SIZE; the mystery look adds the tier (MysterySize)
    inline constexpr double kFocusGrow = 1.5;       // FOCUS_GROW: the selected spell (R6: the hovered one keeps its size)
    inline constexpr double kRingGap = 4.0;         // RING_GAP: the learnable ring this far out
    inline constexpr double kRingAlpha = 0.6, kRingWidth = 1.2;  // RING_ALPHA, RING_WIDTH
    inline constexpr double kDimSameSchool = 0.6;   // DIM_SAME_SCHOOL (spec v11 T11)
    inline constexpr double kKnownStroke = 1.5, kOtherStroke = 1.0, kFocusStroke = 1.5;
    inline constexpr double kLockedAlpha = 0.4;     // a locked spell outside the mystery look
    inline constexpr double kMysteryAlpha = 0.6, kMysteryMarkAlpha = 0.8, kMysteryDim = 0.4;  // renderMysteryNode
    inline constexpr double kMysteryStroke = 1.0;
    inline constexpr double kCoreShare = 0.5, kLearningDotShare = 0.4;  // the unlocked core, the learning node's white dot
    inline constexpr std::uint32_t kLearningDot = 0xFFFFFF;
    inline constexpr double kDashShare = 0.5, kDashGapShare = 0.4;      // NodeBatch.DASH, in shape units (x size)
    inline constexpr double kMarkFontSize = 10.0;   // NodeBatch.MARK_FONT, the mystery "?"
    inline constexpr double kDimEdgeAlpha = 0.08, kDimEdgeWidth = 1.0;  // the dim batch (canvasRendererEdges.js:239-242)
    inline constexpr double kLockedEdgeWidth = 1.0, kFrontierEdgeWidth = 1.25;
    inline constexpr double kLearningEdgeAlpha = 0.7, kLearningEdgeWidth = 2.0, kLearningEdgeIntoTarget = 3.0;  // :343-346
    // The chain of a selected spell's hard prerequisite (canvasRendererEdges.js:380-418): links 5 x 3.2, 1.15 x 5 apart,
    // fill rgba(130,130,140,0.6), outline rgba(80,80,90,0.9) 1.8 wide, the whole at alpha 0.8
    inline constexpr double kChainLinkW = 5.0, kChainLinkH = 3.2, kChainSpacing = 1.15, kChainAlpha = 0.8, kChainStroke = 1.8;
    inline constexpr Color kChainFill{ 0x82828C, 0.6 }, kChainEdge{ 0x50505A, 0.9 };

    enum class Display;

    /// _mysterySize (canvasRendererNodes.js:66-75): LOCKED_SIZE + the level's index (novice 0 .. master 4), else tier - 1
    /// (at most 4) when the tier is above 0
    double MysterySize(std::string_view a_skillLevel, int a_tier);

    /// A node's body as renderNode sizes it (canvasRendererSpell.js:99-142): unlocked and learning KNOWN_SIZE, available
    /// LEARNABLE_SIZE (+ the learnable ring when the design has availableRing), locked LOCKED_SIZE or the mystery tier size;
    /// the selected spell FOCUS_GROW larger with the focus outline. size in world units, stroke in stage units.
    struct MarkSize
    {
        double size{};
        double stroke{};
        bool ring{};
    };

    MarkSize SizeOf(Display a_display, bool a_mystery, std::string_view a_skillLevel, int a_tier, bool a_focus, bool a_availableRing);

    /// How far a node's drawing reaches from its centre, stage units: the shape's farthest point (ShapeReach x size x
    /// a_scale) and the ring ((size + RING_GAP) x a_scale), each plus half its line. Line widths are HoM world units x
    /// a_lineScale (review v11 #5: the tree's fTreeScale, HoM strokes under its zoom; 1 for the fixed-size legend and card
    /// icons); the hover outline (kFocusStroke) can be on any node, so a body counts its half.
    double Reach(const MarkSize& a_mark, std::string_view a_school, double a_scale, double a_lineScale = 1.0);

    /// The largest Reach any node can have at a_scale (every display, mystery tier, focus, school shape): the tree window's
    /// margin and the spacing test (R5)
    double MaxReach(double a_scale, bool a_availableRing, double a_lineScale = 1.0);

    // ---- shapes (nodeBatch.js SHAPES / ROTATION) ----

    struct Point
    {
        double x{}, y{};
    };

    /// A school's unit shape turned toward the tree origin as HoM turns it (NodeBatch.turnAt: atan2(y, x) of the node's
    /// own world position + the school's ROTATION; circles are not turned), scaled to a_size and moved to (a_cx, a_cy).
    /// Destruction diamond, Alteration hexagon, Conjuration pentagon, Illusion triangle; any other school a circle of
    /// kCircleSegments points starting at angle 0 (unitPath's arc). First point first (the dash starts there).
    inline constexpr int kCircleSegments = 32;
    std::vector<Point> ShapePoints(std::string_view a_school, double a_worldX, double a_worldY, double a_cx, double a_cy, double a_size);

    /// How far the school's unit shape reaches from its centre (its farthest point: the triangle 1.04, the hexagon 1.03)
    double ShapeReach(std::string_view a_school);

    struct Segment
    {
        Point a, b;
    };

    /// A closed outline cut into dashes a_dash long and a_gap apart, one running phase round the whole outline (R10: the
    /// pattern does not restart at each corner). Empty for fewer than two points or a dash <= 0.
    std::vector<Segment> DashOutline(const std::vector<Point>& a_points, double a_dash, double a_gap);

    // ---- states (R1) and names (T15, T16) ----

    /// The plugin's behaviour states (Spells::State, mirrored here so the tools need no game types)
    enum class Behaviour
    {
        kUnknown,
        kLocked,
        kAvailable,
        kReady,
        kTarget,
        kLearning,
        kMastered,
    };

    /// HoM's four looks
    enum class Display
    {
        kUnlocked,
        kLearning,
        kAvailable,
        kLocked,
    };

    /// R1: mastered -> unlocked; no state yet -> locked; the learning target (any progress, a 100 % one not learned yet
    /// too) -> learning; ready or learned early but not the target -> available (HoM cppCallbacks.js:1033 keeps it
    /// available); locked -> locked.
    Display DisplayOf(Behaviour a_state, bool a_isTarget);

    const char* DisplayName(Display a_display);

    enum class Name
    {
        kNone,    // no label (locked)
        kHidden,  // "???" (available under revealName % without cheatMode)
        kShown,
    };

    /// _labelTextOf (canvasRendererLabels.js:140-153) on the display state: only unlocked, learning and available get a
    /// name; an available one shows "???" until its XP reaches revealName % (cheatMode shows it). A locked one: none, its
    /// name under cheatMode (the tooltip and card, canvasRendererInput.js:29,34; review v11 #9) - a label checks kLocked first.
    Name Reveal(Display a_display, double a_percent, double a_revealName, bool a_cheatMode);

    inline constexpr const char* kHiddenName = "???";
    inline constexpr const char* kClipMark = "...";  // HoM's "…" is not known to be in the game font (Widget.cpp's kEllipsis)

    /// TreeStyle.clipLabel (treeStyle.js:257): a name over a_maxChars characters (code points) keeps a_maxChars - 1 and
    /// ends in kClipMark
    std::string ClipLabel(std::string_view a_name, int a_maxChars);

    // ---- the style ----

    /// TreeStyle tokens the window uses (treeStyle.js DEFAULTS, then the preset's "tree" block; a value of another type
    /// than the default's keeps the default, as TreeStyle.set does). An empty optional is HoM's '' (= the school colour /
    /// the setting's colour).
    struct Tokens
    {
        int labelMaxChars = 12;
        Color labelUnlocked{ 0xFFFFFF, 1.0 };
        Color labelAvailable{ 0xFFFFFF, 0.7 };
        Color labelHidden{ 0xFFFFFF, 0.35 };
        Color nodeFill{ 0x1A1A2E, 1.0 };
        std::optional<Color> unlockedFill, unlockedRim, unlockedCore, lockedStroke;
        Color mysteryFill{ 0x14141E, 0.9 };
        Color focusStroke{ 0xFFFFFF, 1.0 };
        double availableAlpha = 0.8;
        bool availableRing = false;
        std::optional<Color> hubRing, learningColor;
        double schoolInk = 0.0;
        Color schoolInkTone{ 0x000000, 1.0 };
        Color dimEdgeColor{ 0x222222, 1.0 };
        std::optional<Color> unlockedEdgeColor;
        Color lockedEdgeColor{ 0x333333, 1.0 };
        double lockedEdgeAlpha = 0.15, unlockedEdgeAlpha = 0.5, unlockedEdgeWidth = 2.0, frontierEdgeAlpha = 0.0;
        Color selectedPathColor{ 0x555555, 1.0 };
        double selectedPathAlpha = 0.5, selectedPathWidth = 2.0;
    };

    struct Style
    {
        Tokens tree;
        // where it came from
        bool configFound = false;
        std::string presetId;    // config's designPreset, lowercased ("" when it names none)
        std::string presetFile;  // the file whose id matched ("" = treeStyle.js DEFAULTS)
        std::vector<std::string> notes;  // each default taken and why (file missing, key of the wrong type, ...)
        // config.json (settingsPanel.js load rules: line numbers in HomStyle.cpp)
        bool discoveryMode = true, showSelectionPath = true, showNodeNames = true, showBaseConnections = true, cheatMode = false,
             focusDimOthers = true;
        double revealName = 0.0;
        Color learning{ 0x00FFFF, 1.0 };  // _learningColor (canvasRendererColors.js:49)
        Color hubRing{ 0xB8A878, 1.0 };   // _heartRing (:53)
        std::unordered_map<std::string, Color> schoolColors;
        std::unordered_map<std::string, bool> schoolVisibility;

        /// Locked spells drawn as HoM's undiscovered ones (canvasRendererData.js:297: discoveryMode and not cheatMode)
        [[nodiscard]] bool Mystery() const { return discoveryMode && !cheatMode; }
        /// schoolVisibility[school] == false hides a school (T17)
        [[nodiscard]] bool SchoolShown(const std::string& a_school) const;
        /// _getSchoolColor (canvasRendererColors.js:20): config's schoolColors, else HoM's defaults, else #888888; inked. A
        /// node's own themeColor wins, inked too (canvasRendererSpell.js:71, R4).
        [[nodiscard]] Color SchoolColor(const std::string& a_school, std::string_view a_themeColor = {}) const;
        /// The off-path factor (T11): DIM_SAME_SCHOOL, 1 when focusDimOthers is off (canvasRendererNodes.js:46)
        [[nodiscard]] double DimFactor() const { return focusDimOthers ? kDimSameSchool : 1.0; }
        /// One line for the log (T19)
        [[nodiscard]] std::string Summary() const;
    };

    struct PresetFile
    {
        std::string stem;  // the file name without ".json" (HoM's entry key: the id when the file has none)
        std::string text;
    };

    /// The style from config.json's text (nullopt: no file) and the preset files' texts. Never fails: what is missing or
    /// broken keeps its default and says why in notes.
    Style Build(const std::optional<std::string>& a_config, const std::vector<PresetFile>& a_presets);

    /// Reads a_configPath and every *.json in a_presetDir (UTF-8 paths, relative to the game folder in game), then Build.
    Style Read(const std::string& a_configPath, const std::string& a_presetDir);
}

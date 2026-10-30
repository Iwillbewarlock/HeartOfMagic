#pragma once

#include "BlockApi.h"
#include "HomLayoutMath.h"
#include "Spells.h"
#include "Strings.h"
#include "TextFit.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// =============================================================================
// ADDON STATE - what the Heart of Magic block's parts share (HoM add-on plan
// Task 6): its three blocks, the session's centre and school (kept across menu
// opens, as the core's g_session at f83dc6f3), the per-menu state (the HoM part
// of the core's g_menu), the school's numbers, the self-test log, and the
// requests to SL_MagicMenu (Main.cpp sends them). Every callback runs on the UI
// thread from the host's frame handler, one at a time; the menu sink only bumps
// the open generation (EnsureMenu resets the state at the next callback).
// =============================================================================

namespace HomAddon
{
    enum class BlockKind : std::size_t
    {
        kTree,
        kProgress,
        kNodeCard,
    };
    inline constexpr std::size_t kBlockCount = 3;

    inline constexpr std::size_t Index(BlockKind a_kind) { return static_cast<std::size_t>(a_kind); }

    // The blocks' final ids (in SL_MagicMenu_Layout.json under their retired built-in ids: legacyId) - registration order
    inline constexpr std::array<const char*, kBlockCount> kBlockIds{ "SL_MagicMenu_HoM.tree", "SL_MagicMenu_HoM.progress",
        "SL_MagicMenu_HoM.nodecard" };
    inline constexpr std::array<const char*, kBlockCount> kLegacyIds{ "tree", "progress", "nodecard" };
    inline constexpr const char* kLogPrefix = "SL_MagicMenu_HoM";  // TextFit's Q4 lines

    // Panels and cards (the core's W:173-218 at f83dc6f3): the colours, the panel padding, the cards' top and bottom room
    inline constexpr double kEdgeWidth = 1.0, kPad = 8.0;  // Settings::kPanelPad
    inline constexpr std::uint32_t kPanelFill = 0x000000, kPanelEdge = 0x8A7A5A, kTitleColor = 0xC8C0B0;
    inline constexpr std::uint32_t kWhite = 0xF0F0F0, kLabelColor = 0xD8D8D8, kDimText = 0x8A8A8A;
    inline constexpr double kCardTop = 4.0, kCardBottom = 2.0;
    // the cards' content clip inset (Registration::pad): the cards draw into a child clip at (kPad - kCardPad, -kCardPad), so each
    // field sits where the core drew it in the card's box (x from kPad, y from the box's top)
    inline constexpr double kCardPad = kCardTop;
    inline constexpr double kLineFactor = 1.25;  // a text line's height per size unit until the host's Context gives the font's
    inline constexpr int kPollFrames = 15;       // HoM state is asked for again this often
    inline constexpr int kNoDataFrames = 600;    // frames without HoM's state for the school before the card stops saying "loading"

    // The active school's state counts over its non-dup nodes (Q2); available = Spells::Learnable, candidates = those of them
    // with a place in the tree
    struct Tally
    {
        int candidates{}, available{}, target{}, ready{}, learning{}, mastered{}, locked{}, unknown{}, nodes{};
    };

    struct SchoolInfo
    {
        std::string school;
        std::uint64_t epoch{};
        std::vector<int> candidates;  // what the next-learnable key walks, in Spells::Learnable's order
        Tally tally;
        bool valid = false;
    };

    // Kept across menu opens (spec: "keep the previous centre")
    struct Session
    {
        int centre = -1;
        std::string school;  // the list's active school
        // The centre was a fallback (DefaultCentre) picked from a snapshot that did not cover this school yet: the school's root
        // stood in for its learning target. Picked again once a snapshot covers the school; a centre the user chose (list
        // selection, tree click, the next-learnable key) is never provisional.
        std::string provisionalSchool;
    };

    // Reset on every menu open (EnsureMenu)
    struct MenuState
    {
        std::uint64_t generation = 0;  // the open it belongs to
        bool firstFrame = true;        // no frame call seen yet this menu
        int frame = 0;
        bool refresh = true;           // ask Spells for HoM's state at the end of this frame
        bool dirty = true;             // the cards are drawn again ('SLMD')
        std::uint64_t epoch = 0;       // the snapshot drawn last
        // the host's view, from the last frame's Context (BlockApi.h "Context")
        RE::FormID shown = 0;
        std::uint32_t selectionSeq = 0;
        std::optional<std::uint32_t> seqSeen;    // the seq WatchSelection acted on last
        std::optional<std::uint32_t> tabBits;    // the tab WatchTab acted on last
        std::string tabSchool;                   // the school tab's school (empty on other tabs)
        bool layoutMode = false;                 // the last Context's (own handlers ignore events while it is on)
        // what the node card shows: the clicked node (Task 7) or the U key's pick (selectedByKey; bug G1: it counts as chosen)
        int selected = -1;
        bool selectedByKey = false;
        bool notInTree = false;                  // the list's spell has no tree node (the tree keeps its centre)
        std::uint32_t lastRequest = 0;           // the requestId of the add-on's last 'SLMS' (Task 7's tree click; 0 = none)
        SchoolInfo schoolInfo;
        std::set<std::string> availabilityLogged;
        std::string uncoveredSchool;             // the school no snapshot has had since frame uncoveredFrom
        int uncoveredFrom = 0;
        bool noData = false;                     // kNoDataFrames passed without HoM's state for uncoveredSchool
        TextFit::Log textLog{ {}, {}, kLogPrefix };  // the Q4 cut / room lines and the Q2 states logged this menu
        TextFit::Metrics metrics{ 0.0, kLineFactor, kLineFactor };  // the host's (Context), the fallback until the first frame
        std::array<std::optional<TextFit::Metrics>, kBlockCount> drawnWith;  // the metrics each block was last drawn with
        std::array<LayoutMath::HeightSender, kBlockCount> heights;          // 'SLMH' sent this menu
        std::optional<bool> progressShown;       // the 'SLMV' last sent (every menu opens with it shown)
        // the node card's button as last drawn (L2 node button bottom, once per menu)
        bool buttonDrawn = false;
        double buttonBottom = 0.0;     // the button rect's bottom in its card clip
        double nodeContentH = 0.0;     // the content clip's height it was drawn in
        std::uint32_t nodeContentGeneration = 0;  // Context::contentGeneration seen at the node card's frame
        int nodeStableFrames = 0;
        bool buttonChecked = false;
    };

    Session& TheSession();
    MenuState& Menu();

    /// The menu sink (any thread): a new open - EnsureMenu resets the per-menu state at the next callback; HoM's style is read again
    void OnMenuOpen();

    /// Resets the per-menu state when a new menu opened since the last callback. Every callback calls it first
    void EnsureMenu();

    /// The pending button press of the node card (its own AS2 handler; acted on in the frame)
    void PressButton();
    bool TakeButton();

    // ---- the tree's data and the school's numbers (the core's W:1004-1056) ----

    const Spells::Data& Data();  // callers check Spells::Get() first
    std::string CentreSchool();
    bool Centrable(int a_index);
    int DefaultCentre(const std::string& a_school, const Spells::Snapshot& a_snapshot);
    const SchoolInfo& SchoolInfoOf(const Spells::Snapshot& a_snapshot);
    void SetCentre(int a_index);
    void SetDefaultCentre(const std::string& a_school, const Spells::Snapshot& a_snapshot);
    void SettleProvisionalCentre(const Spells::Snapshot& a_snapshot);
    void SetSchool(const std::string& a_school);

    /// The key pick (bug G1): the node the U key picked, -1 when none
    int KeyPick();

    /// The add-on has something to show for a_shown (Selection::Chosen with the key pick)
    bool Chosen(RE::FormID a_shown);

    // ---- text ----

    TextFit::Metrics Metrics();
    double Size(double a_base);
    double FieldH(double a_size, int a_lines = 1);
    double LineH(double a_size);

    /// The tree's level word ("Novice" .. "Master") as the game's own level names; the saved word when it is another one
    std::string LevelName(const std::string& a_level);

    /// A self-test line ("SelfTest: ...") with the host ini's bSelfTest only; PASS / FAIL / WARN lines are written through
    void SelfTest(const std::string& a_line);

    /// "SelfTest: Q2 state <name>" the first time a state is shown in this menu (TextFit::Q2State)
    void Q2State(std::string_view a_name, const std::string& a_intended, const RE::GFxValue* a_field = nullptr);
    void Q2State(HomStrings::Str a_id, const std::string& a_intended, const RE::GFxValue* a_field = nullptr);

    // ---- AS2 helpers ----

    std::optional<double> Number(const RE::GFxValue& a_object, const char* a_member);
    RE::GFxValue Child(RE::GFxValue& a_parent, const char* a_name);
    void SetHandler(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, const char* a_event, RE::GFxFunctionHandler* a_handler);
    /// A point of a_from's space in a_to's (localToGlobal, globalToLocal); nullopt when either call fails
    std::optional<std::pair<double, double>> MapPoint(RE::GFxMovieView* a_movie, RE::GFxValue& a_from, RE::GFxValue& a_to, double a_x, double a_y);

    /// A card's own clip in the lent content clip: at (kPad - kCardPad, -kCardPad), so it draws in the card box's coordinates
    /// from x kPad (the core's "content" child of the card's panel)
    RE::GFxValue CardClip(RE::GFxValue& a_content);

    // ---- the host (Main.cpp) ----

    double RegisteredHeight(BlockKind a_kind);
    void RequestRedraw(BlockKind a_kind);
    void RequestHeight(BlockKind a_kind, double a_height);  // the block's height ('SLMH'), at most its registered one
    void RequestVisible(BlockKind a_kind, bool a_shown);    // 'SLMV'

    /// A card's needed height (from its box's top) sent as 'SLMH' (LayoutMath::CardHeight, once per change)
    void FitCard(BlockKind a_kind, double a_needed);

    /// The menu changed what the cards show: both drawn again on the next UI frame (the tree too, from Task 7)
    void MarkDirty();
}

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Layout
{
    struct State;           // Layout.h (it includes this header, so only declared here)
    enum class FileState;   // Layout.h
}

// =============================================================================
// SETTINGS - Data/SKSE/Plugins/SL_MagicMenu.ini, read once at plugin load.
// Every value has a default here; a missing or bad key keeps the default and
// logs a warning. Rects are in the magic menu movie's stage units (1280x720).
// =============================================================================

namespace Settings
{
    struct Rect
    {
        double left{}, top{}, right{}, bottom{};

        [[nodiscard]] double Width() const { return right - left; }
        [[nodiscard]] double Height() const { return bottom - top; }
        [[nodiscard]] double CenterX() const { return (left + right) / 2; }
        [[nodiscard]] double CenterY() const { return (top + bottom) / 2; }
        [[nodiscard]] bool Contains(double a_x, double a_y) const { return a_x >= left && a_x <= right && a_y >= top && a_y <= bottom; }
    };

    struct Values
    {
        // [General]
        bool enabled = true;
        bool selfTest = true;      // the preview gates' "SelfTest:" log lines
        bool movePreview = true;   // G2: move the 3D preview into the preview box
        bool moveItemCard = true;  // move SkyUI's item card into the spell card block
        bool drawPreviewOver = true;  // draw the 3D preview again after the menu movie (MagicMenu PostDisplay)
        bool previewUnlearned = true;  // P3: an add-on's show request for a spell not in the list loads its 3D (test build: on)
        bool hideGamePass = true;  // Q3 C: hide the game's own 3D pass of the selected spell's model (only our copy in the box shows)
        bool clipPreview = true;   // Q3 A: clip the 3D preview to the box (scissor)
        bool ownCard = true;       // v7 C1: our own info card instead of SkyUI's item card (0: build 6's moved and fitted SkyUI card)
        bool hideBackdrop = true;  // v7 P7.1: hide the dark backing disc ("Cylinder*", normal alpha blend) of a spell's preview art
        bool hideGlow = false;     // v7 P7.1: also hide its additive halo shapes ("Sphere_Halo", "glow01", "lightRays*")

        // [Preview]
        // spec v13 A2: the over-pass puts the box's alpha back after drawing the art (the target is composited later by its
        // alpha over the world: the art raising it was the dark cloud); 0 = build 11's drawing exactly (A4)
        bool keepPanelAlpha = true;
        // spec v13 B1 as a switch (user 2026-10-26: the Shouts and Powers tabs should show the preview too): 1 = no 3D on the
        // Powers tab (build 13's rule), 0 = the Powers tab previews its 3D like the spell tabs (the default since build 15)
        bool hidePowers3D = false;

        // [Keys] DirectInput scan codes. HoM add-on plan Task 5: iNext / iSetTarget (U, G) are read by the Heart of Magic block
        // (SL_MagicMenu_HoM) from this ini. Blocks plan Task 4 (spec "Input boundary", Defaults): the layout mode's toggle. K (37) is in no controlmap line and was
        // free in the build-5 key scan; the gamepad's RS click (SKSE code 273, the game's Item Zoom here: every pad button is
        // taken in this menu, Task 0) - -1 = no gamepad toggle
        std::uint32_t keyLayout = 37;
        int gamepadLayout = 273;

        // [Layout] stage units: layout E2 the user chose from the offline renders (spec v6 L1, 2026-10-25): one two-column
        // grid, gap fPanelGap everywhere - left column 648..969, right 978..1238; rows from 67 down to 623. The item card's height
        // is the spell card block's (width-limited: 321 / 468 = 0.686 of SkyUI's 468 x 300 card background); its bottom ends
        // the columns. HoM add-on plan Task 5: the tree window's, progress card's and node card's rects (fTree*, fProgress*,
        // fNodeCard*), fTreeScale, fTreeMarkScale, iMaxSteps, iMaxNodes and [Debug] fDebugExtraHeight are the Heart of Magic
        // block's (SL_MagicMenu_HoM reads them from this ini); the core reads fTreeRight alone - the right column's right edge
        Rect preview{ 648, 67, 969, 297 };
        Rect itemCard{ 648, 417, 969, 623 };
        double treeRight = 1238.0;  // [Layout] fTreeRight: where the two columns end on the right (Layout::DefaultFrames)
        double panelGap = 9.0;  // between the blocks of a column (306 - 297) and between the columns (978 - 969)

        // [Debug] blocks plan Task 6: the plugin registers its own tall add-on block "SL_MagicMenu.filler" (300 high) through
        // the add-on path, to test the add-on API and the column scroll in game; without a saved place it joins the left column
        // after the spell card, so the preview's column scrolls (Layout::kDebugFillerId)
        bool debugFillerBlock = false;
    };

    // Every panel: a title strip this high and this much padding inside the border (Widget draws by these; the Heart of Magic
    // block's tree window, HomSettings.h TreeContent, too)
    inline constexpr double kPanelTitleH = 22.0, kPanelPad = 8.0;

    /// A titled panel's content area in stage units: below the title strip (drawn at kPanelPad/2 .. +kPanelTitleH),
    /// inside the padding. The 3D preview is placed, fitted and tested in the preview panel's (review R9 2026-10-25:
    /// the whole box let a tall model cover the "PREVIEW" title while the self-test said inside).
    inline Rect PanelContent(const Rect& a_panel)
    {
        return { a_panel.left + kPanelPad, a_panel.top + kPanelTitleH + kPanelPad, a_panel.right - kPanelPad, a_panel.bottom - kPanelPad };
    }

    /// Reads the ini and then the layout file (call once, at plugin load). Logs the values used.
    void Load();

    /// The values read by Load() (defaults before it).
    const Values& Get();

    /// The block layout read by Load() from Data/SKSE/Plugins/SL_MagicMenu_Layout.json (blocks plan Task 2): repaired
    /// (Layout::Repair), Layout::Default(Get()) when the file is missing, broken or from a newer build; since Task 4 the layout
    /// last saved by the layout mode (SaveLayout, or SetSavedLayout when the file may not be written).
    const Layout::State& SavedLayout();

    /// What the layout file was when it was last read or written (plan D10)
    Layout::FileState LayoutFileState();

    enum class SaveResult
    {
        kSaved,    // written, and SavedLayout() is the state from now on
        kRefused,  // the file is a newer build's: not written (SavedLayout() unchanged)
        kFailed,   // a step failed (a_why says which): the file is as it was, SavedLayout() unchanged; a retry is safe
    };

    /// Writes the layout file (plan D10): a .tmp in the same folder (every write, flush and close checked), then ReplaceFileW
    /// keeping SL_MagicMenu_Layout.json.bak (fallback: the old file copied to .bak, then MoveFileExW REPLACE_EXISTING), or a
    /// plain move when there is no file yet. A file that failed to read at load is copied to .bad first; a newer build's file
    /// is never written. Called from the menu's frame handler (the layout mode); not thread-safe.
    SaveResult SaveLayout(const Layout::State& a_state, std::string* a_why);

    /// HoM add-on design G-e: the retired ids v3 blocks took over (legacyId), retired -> final (BlockRegistry::Registry::LegacyAliases).
    /// Call once at the first magic menu open, before AddBlocks: AddBlocks renames the saved layout and the default by it
    /// (Layout::ToFinal), ReadLayoutFile reads through it and SaveLayout writes the retired ids back (Layout::ToLegacy)
    void SetLegacyAliases(const std::map<std::string, std::string>& a_legacyToFinal);

    /// Blocks plan Task 6: the add-on blocks registered this game session (call once, at the first magic menu open, in
    /// registration order). The saved layout is repaired against them: one without a saved place goes to the end of the right
    /// column (in memory; the file is written only by the layout mode). DefaultLayout() has them too from then on
    void AddBlocks(const std::vector<std::string>& a_ids);

    /// Layout::Default(Get()) with the add-on blocks (AddBlocks) at the end of the right column: the layout mode's reset and
    /// the safety-net states' layout
    const Layout::State& DefaultLayout();

    /// The layout used from now on without touching the file (a newer build's file: the edit lives for this game session)
    void SetSavedLayout(const Layout::State& a_state);

    /// The layout file read again as it is (not repaired), for SelfTest L3; nullopt with a reason when it does not read
    std::optional<Layout::State> ReadLayoutFile(std::string* a_why);
}

#pragma once

#include <cstdint>

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
        bool moveItemCard = true;  // move SkyUI's item card under the progress card
        bool drawPreviewOver = true;  // draw the 3D preview again after the menu movie (MagicMenu PostDisplay)
        bool previewUnlearned = true;  // P3: a tree click on a spell not in the list loads its 3D (test build: on)
        bool hideGamePass = true;  // Q3 C: hide the game's own 3D pass of the selected spell's model (only our copy in the box shows)
        bool clipPreview = true;   // Q3 A: clip the 3D preview to the box (scissor)
        bool ownCard = true;       // v7 C1: our own info card instead of SkyUI's item card (0: build 6's moved and fitted SkyUI card)
        bool hideBackdrop = true;  // v7 P7.1: hide the dark backing disc ("Cylinder*", normal alpha blend) of a spell's preview art
        bool hideGlow = false;     // v7 P7.1: also hide its additive halo shapes ("Sphere_Halo", "glow01", "lightRays*")

        // [Preview]
        // spec v13 A2: the over-pass puts the box's alpha back after drawing the art (the target is composited later by its
        // alpha over the world: the art raising it was the dark cloud); 0 = build 11's drawing exactly (A4)
        bool keepPanelAlpha = true;

        // [Keys] DirectInput scan codes. Chosen by the build-5 key scan of the installed mods
        // (docs/superpowers/specs/v5-key-scan.md): R (19) is Spell Organizer's remove-spell key in this menu and
        // N (49) Creature Framework's puppet key with no menu check; U and G are used by nothing live in the magic menu.
        std::uint32_t keyNext = 22;       // U: next learnable spell of the school
        std::uint32_t keySetTarget = 34;  // G: what the node card's button does

        // [Layout] stage units: layout E2 the user chose from the offline renders (spec v6 L1, 2026-10-25): one two-column
        // grid, gap fPanelGap everywhere - left column 648..969, right 978..1238; row 1 67..297, row 2 306..623. The item card
        // slot is the card's fitted size (width-limited: 321 / 468 = 0.686 of its 468 x 300 background)
        Rect preview{ 648, 67, 969, 297 };
        Rect tree{ 978, 67, 1238, 297 };
        Rect progress{ 648, 306, 969, 408 };
        Rect itemCard{ 648, 417, 969, 623 };
        Rect nodeCard{ 978, 306, 1238, 623 };
        double panelGap = 9.0;  // between the preview and the progress card (306 - 297); the item card keeps it on Powers/Shouts
        // stage units per tree (world) unit, TreeData::View::scale. Spec v10 T2: one fixed scale; every school's closest two
        // nodes are >= 37.9 tree units apart (spell_tree.json), 28.4 at 0.75
        double treeScale = 0.75;
        // a multiplier on Heart of Magic's own mark sizes (spec v11 T12: radii in HoM world units x treeScale x this; 1 = HoM's).
        // The largest drawn reach at 1 (a selected learnable spell's ring, 14.5 + its half line) x 0.75 = 11.5 < 14.2, half
        // the closest pair (CheckSpacing, tools/magic-menu-tree-check)
        double treeMarkScale = 1.0;
        int maxSteps = 1;  // link steps that count as linked to the centre: drawn in full and clickable even when locked
        int maxNodes = 60;
    };

    // Every panel: a title strip this high and this much padding inside the border (Widget draws by these;
    // tools/magic-menu-tree-check uses them too, so its window is the one the menu draws)
    inline constexpr double kPanelTitleH = 22.0, kPanelPad = 8.0;
    inline constexpr double kTreeLegendH = 18.0;  // the legend strip at the bottom of the tree window

    struct Area
    {
        double cx{}, cy{};        // centre, in the panel's own coordinates
        double halfW{}, halfH{};  // half size
    };

    /// The tree window's content area: below the title, above the legend, inside the padding.
    inline Area TreeContent(const Rect& a_tree)
    {
        const double bodyH = a_tree.Height() - kPanelTitleH - kTreeLegendH;
        return { a_tree.Width() / 2, kPanelTitleH + bodyH / 2, a_tree.Width() / 2 - kPanelPad, bodyH / 2 - kPanelPad };
    }

    /// A titled panel's content area in stage units: below the title strip (drawn at kPanelPad/2 .. +kPanelTitleH),
    /// inside the padding. The 3D preview is placed, fitted and tested in the preview panel's (review R9 2026-10-25:
    /// the whole box let a tall model cover the "PREVIEW" title while the self-test said inside).
    inline Rect PanelContent(const Rect& a_panel)
    {
        return { a_panel.left + kPanelPad, a_panel.top + kPanelTitleH + kPanelPad, a_panel.right - kPanelPad, a_panel.bottom - kPanelPad };
    }

    /// Reads the ini (call once, at plugin load). Logs the values used.
    void Load();

    /// The values read by Load() (defaults before it).
    const Values& Get();
}

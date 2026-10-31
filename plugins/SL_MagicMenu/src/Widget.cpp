#include "Common.h"

#include "Widget.h"

#include "BlockApi.h"
#include "BlockRegistry.h"
#include "BlockRules.h"
#include "BlockRulesV3.h"
#include "BlockRulesStrip.h"

#include "BlockHiding.h"
#include "Draw.h"
#include "GameThread.h"
#include "HiddenBlocks.h"
#include "KeyHints.h"
#include "LangGame.h"
#include "Layout.h"
#include "LayoutMath.h"
#include "Preview.h"
#include "SelectionRule.h"
#include "Settings.h"
#include "SpellSchool.h"
#include "TextFit.h"
#include "WidgetText.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <format>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// =============================================================================
// WIDGET - see Widget.h. The frame handler runs on several threads (game run
// 2026-10-25); since HoM add-on plan Task 5 it knows no Heart of Magic: the
// spell tree, the learning progress and the node card are an add-on's blocks.
// =============================================================================

namespace Widget
{
    namespace
    {
        // ---------------------------------------------------------------------
        // Constants
        // ---------------------------------------------------------------------

        constexpr const char* kRootName = "mmx_root";

        // SkyUI's item card, then vanilla's (the bookmark branch, game runs 2026-10-08/09); its _parent is the fade holder
        constexpr std::array<const char*, 2> kCardPaths{ "_root.Menu_mc.itemCardFadeHolder.ItemCard_mc",
                                                         "_root.Menu_mc.ItemCardFadeHolder_mc.ItemCard_mc" };
        // Inventory Reforged's panel script on the menu's _root: present only when IR runs its panel in this menu
        constexpr const char* kIrPanelPath = "_root.equipmentPanelScript";
        // SkyUI's category list (SkyUI-Community b7a24a93: MagicMenu.inventoryLists.categoryList, an entry's "flag")
        constexpr const char* kCategoryFlagPath = "_root.Menu_mc.inventoryLists.categoryList.selectedEntry.flag";

        // SkyUI's magic category flags (skyui/defines/Inventory.as FILTERFLAG_MAGIC_*): BlockRules.h, which also maps them to the
        // Block API's tab bits (one list for both)
        using BlockRules::kFlagAlteration, BlockRules::kFlagIllusion, BlockRules::kFlagDestruction, BlockRules::kFlagConjuration,
            BlockRules::kFlagRestoration, BlockRules::kFlagShouts, BlockRules::kFlagPowers, BlockRules::kFlagActiveEffects;
        constexpr int kFlagUnread = 0;  // no category flag read yet (ReadTab's default): a spell tab, as ReadTab treats it

        constexpr int kPollFrames = 15;  // SkyUI's item card is measured this often outside a measure window (FitItemCard)
        constexpr int kMaxFitLogs = 5;   // item card re-fits logged per menu open
        constexpr int kMaxWheelLogs = 5;  // column scrolls logged per menu open (blocks plan Task 3)

        // Item card fit (spec v4 P4): measured every frame for this many frames after the menu opens and after each
        // content change (the card's external background loads late: game run 2026-10-25, LEDGER Confirmed 10)
        constexpr int kCardMeasureFrames = 60;
        constexpr std::array kAreaDumpFrames{ 30, 300 };  // DumpCardArea: after the card's background loaded, and later
        constexpr int kMaxChosenLogs = 12;                 // WatchChosen lines per menu open
        constexpr int kCardSettleFrames = 5;     // a put-back outside a measure window is checked this many frames later
        constexpr int kMaxCardVerifyLogs = 8;    // "SelfTest: P4" lines per menu open
        constexpr int kMaxCardLayoutLogs = 6;    // layout lines of the card's surroundings per menu open (R1)
        constexpr int kCardLayoutEvery = 10;     // the layout is read this often during a measure window (second review I5)

        // Tree click on a spell the list does not have (spec v4 P2, P3)
        constexpr int kP2HoldFrames = 30;  // the card is read back again this many frames after we filled it
        constexpr int kP3Frames = 30;      // frames for the game to put the clicked spell's model up
        constexpr double kCardTypeSpell = 7.0;  // SkyUI skyui/defines/Inventory.as:10 ICT_SPELL (card type, not a form type)
        constexpr const char* kListsPath = "_root.Menu_mc.inventoryLists";  // SkyUI ItemMenu.as:42 inventoryLists
        constexpr const char* kListIndexPath = "_root.Menu_mc.inventoryLists.itemList.selectedIndex";
        constexpr const char* kMenuPath = "_root.Menu_mc";  // SkyUI's ItemMenu (MagicMenu extends it)
        // blocks plan Task 3 / D7: ItemMenu's wheel handler (ItemMenu.as:325-333: the preview zoom over mouseRotationRect), wrapped
        // on Menu_mc itself, and our own Mouse listener, a member of our widget clip
        constexpr const char* kWheelName = "onMouseWheel";
        constexpr const char* kSavedWheel = "mmxSavedMouseWheel";
        constexpr const char* kWheelListener = "mmxWheelListener";
        constexpr double kWheelStep = 40.0;  // stage units a column scrolls per wheel notch
        constexpr const char* kCardFadeFlag = "_bItemCardFadedIn";  // ItemMenu.as:20, private in AS2 but a plain member at run time
        constexpr const char* kMoreHudCardPath = "_root.AHZItemCardContainer";  // moreHUD Inventory Edition's card clip

        // Our own info card (spec v7 C1). SkyUI's card types (skyui/defines/Inventory.as ICT_*) and the fields its itemInfo setter
        // reads for each (ItemCard.as:242-359): spell 7 castLevel/effects/spellCost/castTime (0 = cost per second); power 13
        // effects/spellCost (when > 0); shout 10 word0..2/dragonWord0..2/unlocked0..2, recharge in spellCost; active effect 11
        // effects/timeRemaining (none when <= 0). magicSchoolName is used when the object has it (ItemCard::SetForm's object does).
        constexpr int kTypeSpell = 7, kTypeShout = 10, kTypeEffect = 11, kTypePower = 13;
        constexpr int kShoutWords = 3;
        constexpr double kCardRuleGap = 5.0;      // above and below the card's rules
        constexpr double kCardFootShare = 0.62;   // the footer's left text (cost) takes this share of the width, the cast type the rest
        constexpr int kC1Frames = 30;             // frames a new selection may take before our card shows its item (else FAIL)
        constexpr int kMaxC1Logs = 12;            // "SelfTest: C1 card PASS" lines per menu (FAIL lines always)
        constexpr int kC1NoInfoFrames = 60;       // a selection whose card info is no object this long: SkyUI's card comes back
        constexpr const char* kEllipsis = "...";

        // Panels (stage units)
        constexpr double kEdgeWidth = 1.0, kTitleH = Settings::kPanelTitleH, kPad = Settings::kPanelPad, kTitleSize = 12.0;
        constexpr std::uint32_t kPanelFill = 0x000000, kPanelEdge = 0x8A7A5A, kTitleColor = 0xC8C0B0;
        // the left spell list's darkening, matched (user 2026-10-26): measured 0.70 on screenshots (2026-10-25 23:44, the
        // list's edge, rows 900-1200 px), the skin's fInventoryAlpha = 70 (TAKEALOOK - UI Edit deardiary_dm/config.txt)
        constexpr double kPreviewAlpha = 70.0, kPanelAlpha = 70.0;
        // Reading sizes (spec v5 Q4): at least the left SkyUI list's row size, measured from the movie (MeasureListRow);
        // until it is measured kListRowFallback (UNVERIFIED: build 3 frame f_008 shows the list rows about as large as our 14).
        // A reading outside kListRowMin..kListRowMax is clamped and logged, so one bad read cannot blow the layout.
        constexpr double kListRowMin = 10.0, kListRowMax = 20.0;
        constexpr int kListMeasureFrames = 600;  // frames the list row is looked for after the menu opens (it needs a list selection)
        constexpr double kLineFactor = 1.25;     // a text line's height per font size unit until the font is measured (MeasureTextMetrics)
        constexpr double kProbeSize = 20.0, kMetricMin = 0.8, kMetricMax = 2.5;  // the font probe's size; accepted line shares of a size
        // the item card's description fields by frame (SkyUI ItemCard.as): the first one shown is measured for the log (round 2 #5)
        constexpr auto kCardTextFields = std::to_array<const char*>({ "MagicEffectsLabel", "ShoutEffectsLabel", "BookDescriptionLabel",
            "PotionsLabel", "ApparelEnchantedLabel", "WeaponEnchantedLabel", "EnchantmentLabel", "MessageText" });
        // The text-fit helpers and their constants (TextFit.h)
        using TextFit::kFitStep;
        using TextFit::kGutter;
        using TextFit::kListRowFallback;
        using TextFit::kMinFitSize;
        using TextFit::CodePoints;
        using TextFit::Number;
        using TextFit::SetText;
        using TextFit::StringMember;
        using TextFit::TextH;
        using TextFit::TextW;
        constexpr const char* kItemListPath = "_root.Menu_mc.inventoryLists.itemList";  // SkyUI ItemMenu.as:42, BasicList.as:123 selectedClip

        // Selection (spec v12 S / G1-G4). InventoryLists listens to itemList's selectionChange by the method's name
        // (InventoryLists.as:166) and CLIK's dispatcher looks that name up on the object at every dispatch
        // (EventDispatcher.as:139/148: listenerObject[listenerFunction](event)), so an own member of that name on
        // inventoryLists replaces the prototype's for every later change. The original is kept on inventoryLists itself
        // (it goes with the movie: no GFxValue of it outlives the menu, G4)
        constexpr const char* kSelectionChangeName = "onItemsListSelectionChange";  // InventoryLists.as:498-504
        constexpr const char* kSavedSelectionChange = "mmx_savedSelectionChange";
        constexpr const char* kPressListsMember = "mmx_lists";  // our press listener's reference to inventoryLists
        constexpr int kSelectionInstallFrames = 60;  // frames the wrap is tried for after the menu opens (G3: then build 11's way)
        constexpr int kMaxSelectionLogs = 64;        // "selection <id> by ..." lines per menu
        constexpr int kMaxHoverLogs = 16;            // "hover <id> ignored" lines per menu (each id once)
        // review v12 #S1: InventoryLists also dispatches itemHighlightChange itself (InventoryLists.as:327 showItemsList, :397
        // InvalidateListData), past the wrap above; ItemMenu hears it by name (ItemMenu.as:103 -> :344), so the menu clip's
        // own member of that name is wrapped the same way
        constexpr const char* kHighlightChangeName = "onItemHighlightChange";  // ItemMenu.as:344-367
        constexpr const char* kSavedHighlightChange = "mmx_savedHighlightChange";
        constexpr int kMaxHighlightLogs = 16;        // "highlight <i> not the selection, ignored" lines per menu
        constexpr int kMaxNestedLogs = 8;            // review v12 #S5: nested changes during our own set, per menu
        // review v12 #S4: the sound the original plays for a row change (InventoryLists.as:502-503)
        constexpr const char* kGameDelegatePath = "_global.gfx.io.GameDelegate";
        constexpr const char* kFocusSound = "UIMenuFocus";
        // review v12 #S3: frames a category change waits for the list's deferred re-fill (BasicList.as:224 requestUpdate goes
        // through an interval) before the committed entry is selected in it again
        constexpr int kReselectFrames = 10;
        constexpr int kReselectWaitFrames = 2;  // frames before the first try

        // Text colours and the hint box (the gear's hover hint; the look the Heart of Magic block's tooltip had at f83dc6f3)
        constexpr double kLabelSize = 10.0;
        constexpr std::uint32_t kWhite = 0xF0F0F0, kLabelColor = 0xD8D8D8, kDimText = 0x8A8A8A;
        constexpr double kTooltipW = 150.0, kTooltipPad = 4.0, kTooltipAlpha = 90.0;

        // Our spell card (v7 C1): its top inset, the name's size and lines, the body's sizes; kNotYet colours a harmful spell's name
        constexpr double kCardTop = 4.0, kNameSize = 19.0, kSubSize = 12.0, kRowSize = 13.0;
        constexpr int kNameLines = 2;
        constexpr std::uint32_t kNotYet = 0xE05050;

        using namespace WidgetText;  // the on-screen text table and the card's text helpers (WidgetText.h)

        // ---------------------------------------------------------------------
        // Menu state
        // ---------------------------------------------------------------------

        struct CardFit
        {
            double x{}, y{}, scale{};  // the fade holder's _x, _y (in its parent), and _xscale/_yscale in percent
            std::array<double, 4> local{};  // the card's visible parts' union in the holder (xMin, xMax, yMin, yMax) it was fitted for
        };

        enum class Tab
        {
            kFollow,   // All, Favorites (and anything unknown)
            kSchool,   // one school's tab
            kPowers,   // Powers, Shouts
            kEffects,  // Active Effects (build 16)
        };

        // spec v12 G2: the one committed selection of a menu - what the list last had selected by a click, a key, a tree
        // click or the menu's opening. WatchSelection, WatchChosen, CardSelection, CardSpell and the preview's selection read
        // it, never the list's live highlight (which follows the mouse, BasicList.as:318-324)
        struct Committed
        {
            RE::FormID id = 0;        // the entry's formId (any form: a spell, power, shout); 0 = none or no formId
            int index = -1;           // its entryList index when committed (-1 = nothing committed)
            std::string text;         // the entry's text then
            RE::FormType type = RE::FormType::None;
            SelectionRule::Source source = SelectionRule::Source::kOpen;
        };

        // blocks plan Task 4 (D8): the one drag session of a block - its id, where in the block it was grabbed, the menu it began in
        struct LayoutDrag
        {
            Layout::BlockId id;
            double grabX{}, grabY{};
            std::uint64_t generation{};
            std::optional<Layout::Slot> slot;  // the slot the insertion line was drawn for
            double lineScroll{};               // and its column's scroll then
        };

        // blocks plan Task 4: the layout mode of one menu. active is the widget's side (the frame handler's); g_layoutMode the input's
        struct LayoutEdit
        {
            bool active = false;
            Layout::State entry, working;  // the layout when the mode was entered; the one the mode edits (CurrentLayout)
            std::optional<LayoutDrag> drag;
            std::optional<double> borderGrab;  // a border drag: the pointer's x minus the border when pressed
            std::optional<double> guideAt;     // the border guide line as drawn
            // recorded by the handlers, done by the frame handler (no clip is changed inside its own handler)
            std::optional<std::tuple<Layout::BlockId, double, double>> pressDue;  // a cover pressed: block, pointer x, y
            bool releaseDue = false, borderPressDue = false, borderReleaseDue = false, lockDue = false, resetDue = false;
            const char* lockSource = "the gear button";  // the gear (LockClick)
            double borderPressX = 0.0;
            // counts of this menu (logs and SelfTest L5)
            int entered = 0, drops = 0, borderMoves = 0, resets = 0, shieldPresses = 0;
            std::uint64_t inputLeaks = 0, equipKept = 0;  // SkyUI's handleInput / AttemptEquip called in the mode
            std::set<std::uint32_t> wrapKeysOff;          // keyboard scan codes of handleInput keyDowns with the mode off
            std::uint64_t wrapKeyDownsOff = 0, wrapNoSkseCode = 0;
            bool inputWrapped = false, equipWrapped = false, wrapsDone = false;
            int wrapsLeft = kSelectionInstallFrames;
            bool restLogged = false;     // the safety-net rest is logged once per menu
            double showPanel = 1.0;      // SkyUI's InventoryLists.SHOW_PANEL (InventoryLists.as:34), read at the wraps' install
            // blocks plan Task 5: the gamepad / keyboard focus model's side of the movie (the focus itself: g_layoutFocus)
            std::set<Layout::BlockId> coverSet;              // the visible blocks the covers were made for
            std::optional<std::pair<Layout::Slot, double>> padLine;  // the pick's insertion line as drawn: its slot, its column's scroll
            int padKeys = 0, padIgnored = 0, padFocusMoves = 0, padPicks = 0, padDrops = 0, padInPlace = 0, padPickCancels = 0, padBorders = 0;
            int padLogs = 0, padChecksFailed = 0, stickScrolls = 0;
            bool stickNoFocusLogged = false;
        };

        // Reset on every menu open
        struct MenuState
        {
            bool firstFrame = true;
            // 2026-10-31 (the settings tab): [Blocks] sHidden as this menu applies it (HiddenBlocks::Effective, set at AttachWidget):
            // these blocks are absent this menu, as an add-on that is not installed (BlockHeights leaves them out)
            std::set<std::string> hidden;
            bool powersHideLogged = false;   // spec v13 B1: the Powers tab's line, once per menu
            Committed sel;                   // spec v12 G2
            std::uint64_t selSeq = 0;        // bumped by every change of sel
            std::uint64_t selSeen = 0;       // the selSeq WatchSelection acted on last
            std::optional<SelectionRule::Mark> selMark;  // our own selectedIndex set is running (marked BEFORE the setter, G1)
            bool selInCall = false;          // inside the saved original (re-entry guard, G4)
            bool selWrapped = false;         // the wrap is on this movie; false = build 11's follow-the-highlight (G3)
            bool selInstallDone = false;
            int selInstallLeft = kSelectionInstallFrames;
            int selectionLogs = 0;
            std::set<RE::FormID> hoverLogged;
            int hoverLogs = 0;
            bool highlightOurs = false;      // review v12 #S1: our own itemHighlightChange dispatch is running (RestoreListCard)
            bool highlightWrapped = false;   // review v12 #S1: ItemMenu.onItemHighlightChange is wrapped on this movie
            int highlightLogs = 0;
            int nestedLogs = 0;              // review v12 #S5
            std::uint64_t originalCalls = 0; // saved originals called (SelectInList: did its setter reach one, review v12 #S3)
            bool reselecting = false;        // review v12 #S3: SelectInList puts the committed entry back after a re-fill
            int reselectLeft = 0;            // review v12 #S3: frames left to put it back after a category change (0 = none due)
            std::uint64_t reselectSeq = 0;   // the selSeq it is for (a newer selection cancels it)
            std::string tabSchool;           // the school tab's school (empty on other tabs)
            RE::FormID listSelection = 0;
            RE::FormID selectedByUs = 0;     // an add-on's show request selected this spell in the list: its change is ours
            int frame = 0;
            bool irActive = false;           // Inventory Reforged's panel runs in this menu
            std::optional<CardFit> cardFit;
            int fits = 0;                    // our (re-)applies of the holder transform
            int movedAway = 0;               // times something else (SkyUI) moved the holder off our transform
            int refits = 0;                  // the card's content changed size: the transform was worked out again
            int cardChecks = 0;
            int cardMeasureLeft = kCardMeasureFrames;  // frames left in the current measure window (P4)
            bool cardVerifyDue = false;      // the holder transform was applied since the last P4 check
            int cardVerifies = 0;            // P4 lines logged
            int cardFitLogs = 0;             // fit computations logged
            std::string cardSignature;       // the card's frame and item: a change opens a measure window
            bool levelLabelLogged = false;   // SkillLevelLabel's fix (or its absence) is logged once
            bool cardTextLogged = false;     // the item card's description text size is logged once (round 2 #5)
            RE::FormID cardFromTree = 0;     // the card shows this tree spell (P2), not the list's selection
            std::optional<int> cardFromTreeTab;  // the category flag when it was shown
            int cardHoldLeft = 0;            // frames until the P2 card is read back again
            std::string cardLayout;          // the last logged layout of the card's surroundings (R1)
            int cardLayoutLogs = 0;
            int cardLayoutTick = 0;          // measured frames since the menu opened; the layout is read every kCardLayoutEvery
            std::optional<int> tabFlag;
            Tab tab = Tab::kFollow;
            double rowSize = 0.0;            // the list's row text size in stage units, 0 = not measured yet (Q4)
            int listMeasureLeft = kListMeasureFrames;
            double lineFirst = kLineFactor;  // the game font's first line height per size unit (MeasureTextMetrics)
            double linePitch = kLineFactor;  // and its step per further line
            // our own info card (v7 C1)
            std::string ownCardSig;          // what it shows (tab, type, texts, sizes); "" = nothing shown
            bool ownCardShown = false;
            double ownCardBottom = 0.0;      // its panel's bottom in stage units as last drawn
            // build 16: the tab and the rect (on screen) it was last drawn in - ownCardBottom belongs to them, not to the tab
            // shown now until WatchOwnCard draws it again (a tab change logged C2 with the last tab's bottom: 648,306..969,157)
            int ownCardDrawnTab = -1;
            Settings::Rect ownCardDrawnIn;
            // the block layout (blocks plan Task 2): this tab's arranged rects, the same with nothing hidden (where a hidden
            // block sits), each panel's rect as last placed (a width change redraws it, D11) and SkyUI's card slot as last fitted
            // UNSCROLLED rects (Arrange's; BlockRect adds the scroll). placed: each panel's rect as last placed (BlockRect's)
            std::map<Layout::BlockId, Settings::Rect> blocks, blocksAll, placed;
            // user 2026-10-30: this tab's rects with no block fitted to its content - each block's full size, the most a fit may
            // take (FitBlock), and its size in the layout mode
            std::map<Layout::BlockId, Settings::Rect> blocksMax;
            // the same with nothing hidden: the full height of a block this tab hides (an add-on may still draw it - its rooms must
            // not come from its own fit, or each draw would ask for less)
            std::map<Layout::BlockId, Settings::Rect> blocksMaxAll;
            int resizeLogs = 0;  // Rearrange's "block X is W x H now" lines this menu (a fitted card changes with its content)
            std::optional<Settings::Rect> gearLast;  // where the gear was put last (PlaceStripAndGear)
            Settings::Rect board;                    // the board's rect (stage), fixed for the menu (DrawBoard)
            std::optional<bool> nothingLaid;         // whether nothing was shown when the columns were laid out last (WatchEmpty)
            // user 2026-10-30: the reset button right of the gear in the edit mode - its full width, its width now and its unfold /
            // fold (from, to, start, length; BlockRules::AnimWidth)
            double resetFullW = 0.0, resetShownW = 0.0, resetFrom = 0.0, resetTo = 0.0, resetStartMs = 0.0, resetDurationMs = 0.0;
            std::optional<Settings::Rect> resetPlaced;  // where it was put last
            bool resetDrawn = false;                    // its mask drawn once this menu
            // user 2026-10-30: the key-hint strip (SkyUI's bottomBar.buttonPanel, one clip): its origin last known in place (set by the
            // host, or found right; its parent's space, BlockRules::PlacePanel; none yet at a menu's start), StripContent's cache (what
            // the visible content was read from, and that content on the stage) and its last visible size (the gear's row while no
            // button shows)
            std::optional<BlockRules::Point> stripInPlace;
            std::vector<double> stripSignature;
            std::optional<Settings::Rect> stripContent;
            std::optional<std::pair<double, double>> stripSize;
            int stripMoveLogs = 0, stripApplies = 0, stripChecks = 0;  // review R4: moves logged, moves this menu, read-back lines
            int stripFoundFrame = -1;   // the frame the panel was first found this menu (-1: not yet; a menu without it logs at the close)
            int stripReadFails = 0;     // frames a shown button's bounds did not read (StripContent; logged at the close)
            // Task 8 R1: the look (signature without the origin) the reads failed under, frames in a row, the content partial
            std::vector<double> stripFailLook;
            int stripFailsInRow = 0;
            bool stripPartial = false;
            std::set<std::string> stripUnreadLogged;  // the children logged as unread (once each this menu)
            bool stripLeft = false;                   // Task 8 R3: SkyUI's strip left where it is (it would cover the board)
            // Task 8 T1 (L8 last): the strip's target in effect, the frame of its last move, the last read of where it is (frame thread)
            std::optional<Settings::Rect> stripTarget;
            int stripMoveFrame = -1, stripReadFrame = -1;
            std::optional<BlockRules::Point> stripRead;
            // Task 8 T2 (L9, bSelfTest): read back every kPollFrames frames, logged at the close - checks, failures, the first
            // failure; the reset button's clicks taken / ignored and taken before it was fully out
            struct ChromeCheck
            {
                int checks = 0, fails = 0;
                std::string firstFail;
            };
            ChromeCheck boardCheck, gearCheck, resetCheck;
            int resetTaken = 0, resetIgnored = 0, resetTakenEarly = 0;
            bool gearHintLeft = false;  // the gear's hint is on its left (the edit mode: the reset button is on its right)
            bool gearHover = false;                  // the mouse is over the gear (GearHover)
            std::optional<bool> gearDrawnMode;       // the layout mode the gear's look and hint were made for (UpdateGear)
            // user 2026-10-30: each fitted block's waiting shrink (BlockRules::DecideFit; built-in cards and add-ons alike)
            std::map<Layout::BlockId, BlockRules::HeldShrink> heldShrink;
            // blocks plan Task 6: add-on blocks to draw next frame (DrawAddons), drawn this menu (SelfTest L6), whose callback threw
            std::set<Layout::BlockId> addonDrawDue, addonDrawn, addonFailed;
            // Block API v2: per add-on its draws this menu and the form its drawSelection was last given; the press waiting for its
            // release; a release judged by BlockRules::Judge, waiting for the frame handler (AddonClicks)
            std::map<Layout::BlockId, std::uint64_t> addonDraws;
            std::map<Layout::BlockId, RE::FormID> addonShown;
            std::optional<std::pair<Layout::BlockId, BlockRules::Press>> addonPress;
            struct AddonClickDue
            {
                Layout::BlockId id;
                BlockRules::Verdict verdict{};
                BlockRules::Point press, release;  // content clip coordinates
            };
            std::optional<AddonClickDue> addonClickDue;
            // a block's height this menu (absent: its full height): an add-on's kMessageHeight ('SLMH') or a built-in card's fit to
            // its content (FitBlock, user 2026-10-30; not the 3D preview and not the tree window) - one map, BlockRules::ApplyFits
            std::map<Layout::BlockId, double> fitHeight;
            int addonHeightLogs = 0;                        // height lines logged this menu (kMaxHeightLogs)
            int fitLogs = 0;                                // L7 fit lines logged this menu (FitBlock)
            std::set<Layout::BlockId> addonTooTallLogged;   // blocks whose too-tall request was logged this menu (once each)
            int addonClickLogs = 0;
            // Block API v3: what made the last selection and the last kMessageShowSpell served (Context::source, servedRequest);
            // the blocks a kMessageVisible hid (their places kept); panels made per column (an overlay below a newer one is made
            // again, BlockRules::StepOverlay); log caps and the blocks whose first frame call was checked (SelfTest L9)
            BlockApi::SelectionSource selFrom = BlockApi::SelectionSource::kNone;
            std::uint32_t servedRequest = 0;
            std::set<Layout::BlockId> addonHidden, addonFramed;
            std::array<std::uint64_t, Layout::kColumns> panelsMade{};
            int addonKeyLogs = 0, addonShowLogs = 0, overlayLogs = 0;
            int addonKeysUnused = 0;  // key calls no block used this menu (counted, logged at the close: final review M1)
            std::uint64_t layoutModeChanges = 0;  // the layout mode entered or left this menu (BlockRules::Moment)
            std::array<std::uint64_t, Layout::kColumns> scrollMoves{};  // each column's scroll changes this menu (BlockRules::Moment)
            // blocks plan Task 3 (D4): each column's scroll offset (stage units, 0 = its top block at the column's top), applied in
            // one place only: the column clip's _y (PlaceColumns). Clamped by Layout::ClampScroll at every placing
            std::array<double, Layout::kColumns> scroll{};
            std::array<std::optional<Settings::Rect>, Layout::kColumns> masked;  // each column's area as its mask was last drawn
            int wheelScrolls = 0, wheelZoomsKept = 0;  // wheel notches that scrolled a column / zooms of SkyUI's kept from firing
            bool wheelInstallDone = false;
            int wheelInstallLeft = kSelectionInstallFrames;  // frames InstallWheel waits for Menu_mc's wheel handler
            std::optional<Settings::Rect> cardSlot;
            double previewAlpha = 0.0;       // the preview box's fill alpha (0: see-through, no over-pass)
            bool skyCardHidden = false;      // SkyUI's card holder was hidden by us this menu
            std::optional<double> moreHudAlpha;  // moreHUD's card clip: its alpha before we made it transparent this menu
            bool chosen = false;             // a list row is highlighted or a tree card is up (WatchChosen)
            int chosenLogs = 0;
            std::set<int> cardTypesLogged;   // card types whose fields were logged
            std::string c1Selection;         // the AS2 list selection (index and text) or the tree card's spell, last seen
            std::string c1InfoAtSelection;   // the card's info as it was the frame before that selection came
            std::string c1LastInfo;          // the card's info last frame (the game may fill the card in the frame the selection changes)
            int c1SelectionFrame = 0;
            bool c1Pending = false;          // a new selection whose card has not been checked yet
            int c1Logs = 0;
            std::set<int> c2Logged;          // tabs whose layout was logged (C2)
            bool ownCardFallback = false;    // the card info never came: SkyUI's card is back for this menu (review v7 #C)
            int c1NoInfoSince = -1;          // the frame since which a selection has had no card info object (-1: it has one)
            std::string c1Form;              // the selection's FormID part, last seen (same form again = same card)
            bool c1SameForm = false;         // the new selection is the same form as the one before it
            std::set<int> colourLogged;      // card types whose description colours were checked
            TextFit::Log textLog;            // the Q4 cut / room lines and the Q2 states logged this menu
            LayoutEdit layout;               // blocks plan Task 4
        } g_menu;

        std::atomic<bool> g_closing{ false };  // the close event came: the frame handler does nothing until the next open
        // Block API v3: g_menu.selSeq for the message threads (a queued kMessageShowSpell keeps the sequence it was sent at);
        // set by Commit, 0 at each menu open
        std::atomic<std::uint64_t> g_selSeqShared{ 0 };
        // The over-pass (MagicMenu PostDisplay): the 3D preview drawn again after the menu movie, over our black box
        std::atomic<RE::GFxMovieView*> g_attachedMovie{ nullptr };  // the movie our widget is on
        std::atomic<bool> g_overPass{ false };       // this menu: widget attached, IR not active, bDrawPreviewOver=1
        std::atomic<bool> g_fitPass{ false };        // this menu: widget attached, IR not active, bMovePreview=1 (place/fit in PostDisplay)
        std::atomic<bool> g_previewShown{ true };    // the preview box is visible (something chosen; every tab since build 16)
        // spec v13 B1: the Powers tab (flag 128) shows the box but no 3D (user 2026-10-26: like the Shouts tab); Preview treats it
        // as a tab without the box (every model hidden, no over-pass draw), the layout stays the Powers/Shouts one
        std::atomic<bool> g_hide3D{ false };
        // review v13 round 2 #1: with neither place/fit nor the over-pass, PostDisplay still calls Preview for the Powers hide
        // (this menu: widget attached, IR not active, not VR - where Preview touches nothing) and once more after it
        std::atomic<bool> g_powersHideCalls{ false };
        std::atomic<bool> g_powersHidePending{ false };
        std::atomic<bool> g_overPassLogged{ false };  // the first over-pass of this menu is logged
        bool g_postDisplayHooked = false;           // set once at kDataLoaded

        // P3 (spec v4): a tree click on a spell the list does not have asks the game's preview for that spell's model.
        // Frame handler only (the movie advances one frame at a time); reset at attach, before the handler is set.
        struct UnlearnedPreview
        {
            bool allowed = false;   // this menu: bPreviewUnlearned, not VR, IR not active
            RE::FormID spell = 0;   // loaded and waiting for its model (0 = nothing waits)
            int checks = 0;         // frames checked for it
            bool failed = false;    // a FAIL this menu: no more tries until the next open
        } g_p3;

        // ---------------------------------------------------------------------
        // Small helpers
        // ---------------------------------------------------------------------

        // A self-test line ("SelfTest: P2 PASS ..."), with bSelfTest only; the verdicts are written through (info lines
        // wait in the buffer, Common.h), the details ride along
        void SelfTest(const std::string& a_line)
        {
            if (!Settings::Get().selfTest) return;
            logger::info("SelfTest: {}", a_line);
            if (a_line.find("PASS") != std::string::npos || a_line.find("FAIL") != std::string::npos ||
                a_line.find("WARN") != std::string::npos) {
                FlushLog();
            }
        }

        // A GFx Number holding a FormID (a FormID over 0x7FFFFFFF may come as a negative number)
        RE::FormID ToFormId(const RE::GFxValue& a_value)
        {
            return static_cast<RE::FormID>(static_cast<std::int64_t>(a_value.GetNumber()));
        }

        std::string KeyName(std::uint32_t a_scanCode)
        {
            // DirectInput scan codes of the letter rows
            constexpr std::string_view kRow1 = "QWERTYUIOP", kRow2 = "ASDFGHJKL", kRow3 = "ZXCVBNM";
            constexpr std::uint32_t kRow1Start = 16, kRow2Start = 30, kRow3Start = 44;
            if (a_scanCode >= kRow1Start && a_scanCode < kRow1Start + kRow1.size()) return std::string(1, kRow1[a_scanCode - kRow1Start]);
            if (a_scanCode >= kRow2Start && a_scanCode < kRow2Start + kRow2.size()) return std::string(1, kRow2[a_scanCode - kRow2Start]);
            if (a_scanCode >= kRow3Start && a_scanCode < kRow3Start + kRow3.size()) return std::string(1, kRow3[a_scanCode - kRow3Start]);
            return std::format("#{}", a_scanCode);
        }

        // The key as the hints show it: its name in brackets ("[U]")
        std::string KeyLabel(std::uint32_t a_scanCode)
        {
            return "[" + KeyName(a_scanCode) + "]";
        }

        // The menu's measured text metrics for TextFit
        TextFit::Metrics Metrics()
        {
            return { g_menu.rowSize, g_menu.lineFirst, g_menu.linePitch };
        }

        // ---------------------------------------------------------------------
        // The magic menu's list and category tabs
        // ---------------------------------------------------------------------

        RE::MagicItemList* ItemList()
        {
            auto* ui = RE::UI::GetSingleton();
            auto menu = ui ? ui->GetMenu<RE::MagicMenu>() : nullptr;
            return menu ? menu->GetRuntimeData().itemList : nullptr;
        }

        // ---------------------------------------------------------------------
        // The committed selection (spec v12 S, G1-G4). The native list's GetSelectedItem reads the AS2 list's
        // selectedIndex (CommonLib MagicItemList.cpp), which follows the mouse - so nothing here reads it any more
        // ---------------------------------------------------------------------

        struct ListEntry
        {
            RE::FormID id = 0;
            std::string text;
        };

        // An item list's entryList[a_index] (the index selectedIndex and the events carry): its formId and text
        ListEntry EntryAt(const RE::GFxValue& a_itemList, int a_index)
        {
            ListEntry entry;
            RE::GFxValue entries, value, formId;
            if (a_index < 0 || !a_itemList.IsObject() || !a_itemList.GetMember("entryList", &entries) || !entries.IsArray() ||
                static_cast<std::uint32_t>(a_index) >= entries.GetArraySize() ||
                !entries.GetElement(static_cast<std::uint32_t>(a_index), &value) || !value.IsObject()) {
                return entry;
            }
            if (value.GetMember("formId", &formId) && formId.IsNumber()) entry.id = ToFormId(formId);
            entry.text = StringMember(value, "text");
            return entry;
        }

        // The committed selection's form (nullptr: none, or an entry without a formId)
        RE::TESForm* CommittedForm()
        {
            return g_menu.sel.index >= 0 && g_menu.sel.id ? RE::TESForm::LookupByID(g_menu.sel.id) : nullptr;
        }

        // review v12 #S3: something is selected - a committed list row, or a tree card (a tree click is a selection too)
        bool HasSelection()
        {
            return g_menu.sel.index >= 0 || g_menu.cardFromTree != 0;
        }

        // G2: the committed selection changes when its row or form does (a_force: the same row again, replacing a tree card).
        // S4: "selection <id> by click|key|tree|open" (build 11's follow-the-highlight is logged by WatchSelection instead)
        bool Commit(const ListEntry& a_entry, int a_index, SelectionRule::Source a_source, bool a_force = false)
        {
            auto& sel = g_menu.sel;
            // review v12 #S3: the committed entry found again after a list re-fill is the same selection at a new row
            if (g_menu.reselecting && a_index >= 0 && sel.id && sel.id == a_entry.id) {
                sel.index = a_index;
                return false;
            }
            // review v12 #S2: back from a tree card to the same row is still a change (WatchSelection drops the tree card); not
            // for build 11's follow-the-highlight, which commits every frame and would drop the tree card at once
            const bool cardElsewhere = g_menu.cardFromTree != 0 && a_source != SelectionRule::Source::kHighlight;
            if (!SelectionRule::CommitChanges(sel.id == a_entry.id, sel.index == a_index, a_force, cardElsewhere)) return false;
            const auto* form = a_entry.id ? RE::TESForm::LookupByID(a_entry.id) : nullptr;
            sel = { a_entry.id, a_index, a_entry.text, form ? form->GetFormType() : RE::FormType::None, a_source };
            ++g_menu.selSeq;
            g_selSeqShared = g_menu.selSeq;
            // Block API v3 (Context::source): an add-on's served kMessageShowSpell, or the player's list (a built-in tree click too)
            g_menu.selFrom = a_source == SelectionRule::Source::kAddon ? BlockApi::SelectionSource::kAddon : BlockApi::SelectionSource::kList;
            if (a_index >= 0 && a_source != SelectionRule::Source::kHighlight) {
                if (g_menu.selectionLogs < kMaxSelectionLogs) {
                    logger::info("Widget: selection {:08X} by {} (list index {}, \"{}\", form type {})", a_entry.id, SelectionRule::Name(a_source),
                        a_index, a_entry.text, static_cast<int>(sel.type));
                } else if (g_menu.selectionLogs == kMaxSelectionLogs) {
                    logger::info("Widget: selection - {} lines logged this menu, the rest are not", kMaxSelectionLogs);
                }
                ++g_menu.selectionLogs;
            }
            return true;
        }

        // S4: "hover <id> ignored", each id once per menu, capped
        void LogHover(const ListEntry& a_entry, int a_index)
        {
            if (g_menu.hoverLogs >= kMaxHoverLogs || !g_menu.hoverLogged.insert(a_entry.id).second) return;
            ++g_menu.hoverLogs;
            logger::info("Widget: hover {:08X} ignored (list index {}, \"{}\"; the selection stays {:08X}){}", a_entry.id, a_index, a_entry.text,
                g_menu.sel.id, g_menu.hoverLogs == kMaxHoverLogs ? " - the last hover line this menu" : "");
        }

        // review v12 #S4: what the swallowed original would have played for the row change, the same way it does it:
        // GameDelegate.call("PlaySound", ["UIMenuFocus"]) (InventoryLists.as:502-503)
        void PlayFocusSound(RE::GFxMovie* a_movie)
        {
            RE::GFxValue delegate;
            std::array<RE::GFxValue, 2> args;
            const bool found = a_movie && a_movie->GetVariable(&delegate, kGameDelegatePath) && delegate.IsObject();
            bool played = false;
            if (found) {
                a_movie->CreateString(&args[0], "PlaySound");
                a_movie->CreateArray(&args[1]);
                RE::GFxValue sound;
                a_movie->CreateString(&sound, kFocusSound);
                args[1].PushBack(sound);
                played = delegate.Invoke("call", nullptr, args.data(), args.size());
            }
            static std::atomic<bool> s_logged{ false };
            if (!played && !s_logged.exchange(true)) {
                logger::warn("Widget: the focus sound for a swallowed hover was not played - {} (logged once)",
                    found ? std::string("GameDelegate.call failed") : std::format("no {}", kGameDelegatePath));
            }
        }

        // G4: the saved original with the this and the arguments it would have had; its return value handed back.
        // selInCall guards re-entry: a selectionChange that comes inside it goes straight to the original
        // (selInCall is also the token the highlight wrap passes on, review v12 #S1: what the original dispatches is ours)
        void CallSaved(const RE::GFxValue& a_this, RE::GFxValue& a_original, const RE::GFxValue* a_args, std::uint32_t a_count, RE::GFxValue* a_ret,
            const char* a_name = kSelectionChangeName)
        {
            std::vector<RE::GFxValue> argv;
            argv.reserve(a_count + 1);
            argv.push_back(a_this);
            for (std::uint32_t i = 0; a_args && i < a_count; ++i) argv.push_back(a_args[i]);
            RE::GFxValue result;
            const bool outer = std::exchange(g_menu.selInCall, true);
            const bool called = a_original.Invoke("call", &result, argv.data(), argv.size());
            g_menu.selInCall = outer;
            if (called) ++g_menu.originalCalls;
            static std::atomic<int> s_failLogs{ 0 };
            if (!called && s_failLogs++ < kMaxHoverLogs) logger::warn("Widget: the saved {} could not be called", a_name);
            if (called && a_ret) *a_ret = result;
        }

        // The original for a_index in the shape the list sends it (ScrollingList.as:372 {type, index, keyboardOrMouse}), so
        // ItemMenu asks for that row's card, 3D and bottom bar (InventoryLists.as:498-504 -> ItemMenu.as:344-357)
        void PassToOriginal(RE::GFxMovie* a_movie, RE::GFxValue& a_lists, int a_index, std::optional<double> a_keyboardOrMouse)
        {
            RE::GFxValue original;
            if (!a_movie || !a_lists.GetMember(kSavedSelectionChange, &original) || !original.IsObject()) return;
            RE::GFxValue change, type;
            a_movie->CreateObject(&change);
            a_movie->CreateString(&type, "selectionChange");
            change.SetMember("type", type);
            change.SetMember("index", static_cast<double>(a_index));
            if (a_keyboardOrMouse) change.SetMember("keyboardOrMouse", *a_keyboardOrMouse);
            CallSaved(a_lists, original, &change, 1, nullptr);
        }

        // G1: installed as inventoryLists.onItemsListSelectionChange. A keyboard/gamepad move, our own marked set or a change
        // without SkyUI's keyboardOrMouse is a selection (committed, then the original); a mouse change that is not ours is
        // a hover (swallowed: no card, no 3D, the bottom bar stays); -1 (PageUp/PageDown, roll-out) changes nothing
        class SelectionChangeWrap final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params& a_params) override
            {
                if (!a_params.thisPtr) return;
                RE::GFxValue original;
                if (!a_params.thisPtr->GetMember(kSavedSelectionChange, &original) || !original.IsObject()) {
                    static std::atomic<bool> s_logged{ false };
                    if (!s_logged.exchange(true)) logger::warn("Widget: selection wrap called without a saved original - the change is dropped (logged once)");
                    return;
                }
                const auto pass = [&] { CallSaved(*a_params.thisPtr, original, a_params.args, a_params.argCount, a_params.retVal); };
                const RE::GFxValue* event = a_params.argCount > 0 ? &a_params.args[0] : nullptr;
                // not ours to judge: re-entry, the menu closing, an event that is not an object
                if (g_menu.selInCall || g_closing || !event || !event->IsObject()) return pass();
                const int index = static_cast<int>(Number(*event, "index").value_or(-1.0));
                const auto flag = Number(*event, "keyboardOrMouse");
                SelectionRule::Input in;
                in.event = SelectionRule::Event::kSelectionChange;
                in.index = index;
                if (flag) in.keyboardOrMouse = static_cast<int>(*flag);
                in.ours = g_menu.selMark;
                RE::GFxValue itemList;
                a_params.thisPtr->GetMember("itemList", &itemList);
                const auto entry = EntryAt(itemList, index);
                in.sameEntry = SelectionRule::SameEntry(entry.id, index, g_menu.sel.id, g_menu.sel.index);
                const auto result = SelectionRule::Decide(in);
                // review v12 #S5: during our own set the list sent another row (the setter scrolled and UpdateList re-selected
                // the row under the cursor, ScrollingList.as:190-193); judged as any other change
                if (in.ours && in.ours->index != index && g_menu.nestedLogs < kMaxNestedLogs) {
                    ++g_menu.nestedLogs;
                    logger::info("Widget: selection - during our set of list index {} the list sent index {} (keyboardOrMouse {}): judged as {}",
                        in.ours->index, index, flag ? std::format("{:.0f}", *flag) : std::string("none"),
                        result.verdict == SelectionRule::Verdict::kSelect ? "a selection" :
                        result.verdict == SelectionRule::Verdict::kHover  ? "a hover" :
                                                                            "no change");
                }
                switch (result.verdict) {
                case SelectionRule::Verdict::kSelect:
                    Commit(entry, index, result.source);
                    pass();
                    return;
                case SelectionRule::Verdict::kHover:
                    LogHover(entry, index);
                    break;
                default:
                    if (in.sameEntry) g_menu.sel.index = index;  // the committed entry, moved by a re-layout
                    break;
                }
                // review v12 #S4: swallowed, but the row changed under the mouse: the original's focus sound still plays
                if (index != -1) PlayFocusSound(a_params.movie);
            }
        };

        // review v12 #S1: installed as Menu_mc.onItemHighlightChange. Passes our own (inside the saved selection-change
        // original, or our own dispatch) and the committed entry's; swallows another row's (InventoryLists' own dispatch
        // after a favourite / key equip carries the row under the mouse, InventoryLists.as:394-397) and, with a selection,
        // a -1 (a category change, :313-327; S3 puts the list back on the selection)
        class HighlightChangeWrap final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params& a_params) override
            {
                if (!a_params.thisPtr) return;
                RE::GFxValue original;
                if (!a_params.thisPtr->GetMember(kSavedHighlightChange, &original) || !original.IsObject()) {
                    static std::atomic<bool> s_logged{ false };
                    if (!s_logged.exchange(true)) logger::warn("Widget: highlight wrap called without a saved original - the change is dropped (logged once)");
                    return;
                }
                const auto pass = [&] {
                    CallSaved(*a_params.thisPtr, original, a_params.args, a_params.argCount, a_params.retVal, kHighlightChangeName);
                };
                const RE::GFxValue* event = a_params.argCount > 0 ? &a_params.args[0] : nullptr;
                if (g_closing || !event || !event->IsObject()) return pass();
                const int index = static_cast<int>(Number(*event, "index").value_or(-1.0));
                RE::GFxValue lists, itemList;
                if (a_params.thisPtr->GetMember("inventoryLists", &lists) && lists.IsObject()) lists.GetMember("itemList", &itemList);
                const auto entry = EntryAt(itemList, index);
                SelectionRule::HighlightInput in;
                in.index = index;
                in.ours = g_menu.selInCall || g_menu.highlightOurs;
                in.cardElsewhere = g_menu.cardFromTree != 0;
                in.committed = g_menu.sel.index >= 0 || in.cardElsewhere;
                in.sameEntry = SelectionRule::SameEntry(entry.id, index, g_menu.sel.id, g_menu.sel.index);
                if (SelectionRule::DecideHighlight(in) == SelectionRule::Highlight::kPass) return pass();
                if (g_menu.highlightLogs < kMaxHighlightLogs) {
                    ++g_menu.highlightLogs;
                    logger::info("Widget: highlight {} not the selection, ignored ({:08X} \"{}\"; the selection stays {:08X}{}){}", index, entry.id, entry.text,
                        g_menu.sel.id, in.cardElsewhere ? std::format(", tree card {:08X}", g_menu.cardFromTree) : std::string{},
                        g_menu.highlightLogs == kMaxHighlightLogs ? " - the last such line this menu" : "");
                }
            }
        };

        // G1: a listener on itemList's itemPress / itemPressAux (BasicList.as:304/315), heard after ItemMenu's own
        // onItemSelect (ItemMenu.as:107; the game's equip, MagicMenu.as:121-128, is not touched): the pressed row is
        // committed and the saved original called for it with the press's own keyboardOrMouse
        class ListPress final : public RE::GFxFunctionHandler
        {
        public:
            explicit ListPress(bool a_aux) :
                _aux(a_aux) {}

            void Call(Params& a_params) override
            {
                if (!a_params.thisPtr || g_closing || a_params.argCount == 0 || !a_params.args[0].IsObject()) return;
                RE::GFxValue lists;
                if (!a_params.thisPtr->GetMember(kPressListsMember, &lists) || !lists.IsObject()) return;
                const auto& event = a_params.args[0];
                const int index = static_cast<int>(Number(event, "index").value_or(-1.0));
                const auto flag = Number(event, "keyboardOrMouse");
                SelectionRule::Input in;
                in.event = _aux ? SelectionRule::Event::kPressAux : SelectionRule::Event::kPress;
                in.index = index;
                if (flag) in.keyboardOrMouse = static_cast<int>(*flag);
                RE::GFxValue itemList, entry, formId;
                lists.GetMember("itemList", &itemList);
                auto picked = EntryAt(itemList, index);
                // the press carries its entry (BasicList.as:304 entry: selectedEntry); preferred over the index lookup
                if (event.GetMember("entry", &entry) && entry.IsObject()) {
                    if (entry.GetMember("formId", &formId) && formId.IsNumber()) picked.id = ToFormId(formId);
                    picked.text = StringMember(entry, "text");
                }
                in.sameEntry = SelectionRule::SameEntry(picked.id, index, g_menu.sel.id, g_menu.sel.index);
                in.cardElsewhere = g_menu.cardFromTree != 0;
                const auto result = SelectionRule::Decide(in);
                if (result.verdict != SelectionRule::Verdict::kSelect) {
                    if (in.sameEntry) g_menu.sel.index = index;  // the committed entry, moved by a re-layout
                    return;
                }
                Commit(picked, index, result.source, true);
                PassToOriginal(a_params.movie, lists, index, flag);
            }

        private:
            bool _aux;
        };

        RE::GPtr<SelectionChangeWrap> g_selectionWrap;
        RE::GPtr<HighlightChangeWrap> g_highlightWrap;
        RE::GPtr<ListPress> g_listPress;
        RE::GPtr<ListPress> g_listPressAux;

        // a_list.addEventListener / removeEventListener(a_event, a_listener, a_name) (CLIK EventDispatcher.as:42/60)
        bool CallListener(RE::GFxMovieView* a_movie, RE::GFxValue& a_list, const char* a_method, const char* a_event, const RE::GFxValue& a_listener,
            const char* a_name)
        {
            std::array<RE::GFxValue, 3> args;
            a_movie->CreateString(&args[0], a_event);
            args[1] = a_listener;
            a_movie->CreateString(&args[2], a_name);
            return a_list.Invoke(a_method, nullptr, args.data(), args.size());
        }

        // review v12 #S1: Menu_mc.onItemHighlightChange wrapped as onItemsListSelectionChange is (own member, the original kept
        // on Menu_mc itself). Without it the selection wrap still works, but InventoryLists' own dispatches reach the card
        void InstallHighlightWrap(RE::GFxMovieView* a_movie)
        {
            RE::GFxValue menu, current, saved, wrap;
            if (!a_movie->GetVariable(&menu, kMenuPath) || !menu.IsObject() || !menu.GetMember(kHighlightChangeName, &current) || !current.IsObject()) {
                logger::warn("Widget: highlight wrap NOT installed - no {}.{}; InventoryLists' own itemHighlightChange (category change, "
                             "InvalidateListData) still reaches the card (review v12 #S1)", kMenuPath, kHighlightChangeName);
                return;
            }
            if (menu.GetMember(kSavedHighlightChange, &saved) && saved.IsObject()) {
                g_menu.highlightWrapped = true;
                logger::info("Widget: highlight wrap already on this movie");
                return;
            }
            a_movie->CreateFunction(&wrap, g_highlightWrap.get());
            if (!menu.SetMember(kSavedHighlightChange, current) || !menu.SetMember(kHighlightChangeName, wrap)) {
                menu.SetMember(kHighlightChangeName, current);
                logger::warn("Widget: highlight wrap NOT installed - SetMember on {} failed (review v12 #S1)", kMenuPath);
                return;
            }
            g_menu.highlightWrapped = true;
            logger::info("Widget: highlight wrap installed - {}.{} wrapped (review v12 #S1)", kMenuPath, kHighlightChangeName);
        }

        // G1/G3/G4: the wrap, once per menu movie, from the frame handler (Menu_mc and its lists exist by then), tried for
        // kSelectionInstallFrames; whatever handler is there is wrapped (another mod's own member included). Not possible ->
        // logged, and the frame handler follows the highlight as build 11 did (FollowHighlight)
        void InstallSelectionWrap(RE::GFxMovieView* a_movie)
        {
            if (g_menu.selInstallDone) return;
            RE::GFxValue lists, itemList, current, saved;
            const bool found = a_movie->GetVariable(&lists, kListsPath) && lists.IsObject() && lists.GetMember("itemList", &itemList) &&
                               itemList.IsObject() && lists.GetMember(kSelectionChangeName, &current) && current.IsObject();
            if (!found) {
                if (--g_menu.selInstallLeft > 0) return;
                g_menu.selInstallDone = true;
                logger::warn("Widget: selection wrap NOT installed - no {}.{} after {} frames; the windows follow the list highlight (no selection wrap, "
                             "spec v12 G3)", kListsPath, kSelectionChangeName, kSelectionInstallFrames);
                return;
            }
            g_menu.selInstallDone = true;
            if (lists.GetMember(kSavedSelectionChange, &saved) && saved.IsObject()) {
                g_menu.selWrapped = true;  // this movie has it already (and our press listeners)
                logger::info("Widget: selection wrap already on this movie");
            } else {
                RE::GFxValue proto, inherited;
                const bool skyuis = lists.GetMember("__proto__", &proto) && proto.IsObject() && proto.GetMember(kSelectionChangeName, &inherited) &&
                                    inherited == current;
                RE::GFxValue wrap, listener, press, pressAux;
                a_movie->CreateFunction(&wrap, g_selectionWrap.get());
                a_movie->CreateFunction(&press, g_listPress.get());
                a_movie->CreateFunction(&pressAux, g_listPressAux.get());
                a_movie->CreateObject(&listener);
                listener.SetMember(kPressListsMember, lists);
                listener.SetMember("onPress", press);
                listener.SetMember("onPressAux", pressAux);
                // all or nothing: without the press listeners a click would never reach the card
                const bool pressOn = CallListener(a_movie, itemList, "addEventListener", "itemPress", listener, "onPress");
                const bool auxOn = pressOn && CallListener(a_movie, itemList, "addEventListener", "itemPressAux", listener, "onPressAux");
                const bool wrapped = auxOn && lists.SetMember(kSavedSelectionChange, current) && lists.SetMember(kSelectionChangeName, wrap);
                if (!wrapped) {
                    if (pressOn) CallListener(a_movie, itemList, "removeEventListener", "itemPress", listener, "onPress");
                    if (auxOn) CallListener(a_movie, itemList, "removeEventListener", "itemPressAux", listener, "onPressAux");
                    lists.SetMember(kSelectionChangeName, current);
                    logger::warn("Widget: selection wrap NOT installed (itemPress listener {}, itemPressAux listener {}, wrap {}); the windows follow "
                                 "the list highlight (no selection wrap, spec v12 G3)",
                        pressOn ? "on" : "failed", auxOn ? "on" : "failed", auxOn ? "failed" : "not tried");
                    return;
                }
                g_menu.selWrapped = true;
                logger::info("Widget: selection wrap installed - {}.{} wrapped ({}), itemList itemPress/itemPressAux observed (spec v12 G1/G4)",
                    kListsPath, kSelectionChangeName, skyuis ? "SkyUI's own" : "not SkyUI's own: another handler, wrapped as it is");
            }
            InstallHighlightWrap(a_movie);
            // S3: the list's highlight when the menu opens is the selection
            const int index = static_cast<int>(Number(itemList, "selectedIndex").value_or(-1.0));
            // (forced when FollowHighlight committed that row in the frames before the wrap went on, so "by open" is logged)
            if (index >= 0) Commit(EntryAt(itemList, index), index, SelectionRule::Source::kOpen, g_menu.sel.source == SelectionRule::Source::kHighlight);
        }

        // G3, and until the wrap is on: build 11's way - the windows follow the list's highlight
        void FollowHighlight(RE::GFxMovieView* a_movie)
        {
            RE::GFxValue itemList;
            if (!a_movie->GetVariable(&itemList, kItemListPath) || !itemList.IsObject()) return;
            const int index = static_cast<int>(Number(itemList, "selectedIndex").value_or(-1.0));
            Commit(index >= 0 ? EntryAt(itemList, index) : ListEntry{}, index, SelectionRule::Source::kHighlight);
        }

        struct ListPick
        {
            bool found = false;
            int index = -1;
            bool took = false;
        };

        // Selects a spell in the list the way SkyUI does it itself: its public selectedIndex setter (an index into
        // entryList; it ignores an entry the current tab filters out, scrolls the entry into view and updates
        // the item card). Selection only: nothing is equipped.
        ListPick SelectInList(RE::FormID a_id, SelectionRule::Source a_source)
        {
            ListPick pick;
            auto* list = ItemList();
            RE::GFxValue entries;
            if (!list || !list->root.IsObject() || !list->root.GetMember("entryList", &entries) || !entries.IsArray()) return pick;
            const auto size = entries.GetArraySize();
            for (std::uint32_t i = 0; i < size; ++i) {
                RE::GFxValue entry, formId;
                if (entries.GetElement(i, &entry) && entry.IsObject() && entry.GetMember("formId", &formId) && formId.IsNumber() &&
                    ToFormId(formId) == a_id) {
                    pick.found = true;
                    pick.index = static_cast<int>(i);
                    break;
                }
            }
            if (!pick.found) return pick;
            // G1: marked BEFORE the setter - its selectionChange (SELECT_MOUSE, BasicList.as:106-108) comes inside SetMember
            const auto callsBefore = g_menu.originalCalls;
            g_menu.selMark = SelectionRule::Mark{ a_source, pick.index };
            list->root.SetMember("selectedIndex", static_cast<double>(pick.index));
            g_menu.selMark.reset();
            RE::GFxValue now;
            pick.took = list->root.GetMember("selectedIndex", &now) && now.IsNumber() &&
                        static_cast<int>(now.GetNumber()) == pick.index;
            // the highlight sat on that row already (a hover we swallowed): the setter sent nothing (ScrollingList.as:328), so
            // the original is called for it here - else the game's card and 3D keep the committed row's. review v12 #S3: judged
            // by whether the setter reached the original, not by the committed index (a re-fill's reselect already moved it)
            if (pick.took && g_menu.selWrapped && g_menu.originalCalls == callsBefore) {
                RE::GFxValue lists;
                if (auto* movie = OpenMagicMenuMovie(); movie && movie->GetVariable(&lists, kListsPath) && lists.IsObject()) {
                    PassToOriginal(movie, lists, pick.index, SelectionRule::kSelectMouse);
                }
            }
            return pick;
        }

        struct TabInfo
        {
            Tab kind = Tab::kFollow;
            std::string school;
            int flag = 0;
        };

        // The selected SkyUI category: its entry's flag (FILTERFLAG_MAGIC_*); All (-257) and Favorites (1) follow the spell
        TabInfo ReadTab(RE::GFxMovieView* a_movie)
        {
            RE::GFxValue flagValue;
            if (!a_movie->GetVariable(&flagValue, kCategoryFlagPath) || !flagValue.IsNumber()) return {};
            TabInfo tab;
            tab.flag = static_cast<int>(flagValue.GetNumber());
            switch (tab.flag) {
            case kFlagAlteration: tab = { Tab::kSchool, "Alteration", tab.flag }; break;
            case kFlagIllusion: tab = { Tab::kSchool, "Illusion", tab.flag }; break;
            case kFlagDestruction: tab = { Tab::kSchool, "Destruction", tab.flag }; break;
            case kFlagConjuration: tab = { Tab::kSchool, "Conjuration", tab.flag }; break;
            case kFlagRestoration: tab = { Tab::kSchool, "Restoration", tab.flag }; break;
            case kFlagShouts:
            case kFlagPowers: tab.kind = Tab::kPowers; break;
            case kFlagActiveEffects: tab.kind = Tab::kEffects; break;
            default: break;
            }
            return tab;
        }

        // ---------------------------------------------------------------------
        // Item card: moved into the spell card's block by its fade holder's _x/_y/_xscale/_yscale only
        // ---------------------------------------------------------------------

        std::optional<RE::GFxValue> FindCard(RE::GFxMovieView* a_movie)
        {
            for (const auto* path : kCardPaths) {
                RE::GFxValue card;
                if (a_movie->GetVariable(&card, path) && card.IsDisplayObject()) return card;
            }
            return std::nullopt;
        }

        // A point of a_from's coordinates in a_to's (through the stage: localToGlobal, globalToLocal)
        std::optional<std::pair<double, double>> MapPoint(RE::GFxMovieView* a_movie, RE::GFxValue& a_from, RE::GFxValue& a_to,
            double a_x, double a_y)
        {
            RE::GFxValue point;
            a_movie->CreateObject(&point);
            point.SetMember("x", a_x);
            point.SetMember("y", a_y);
            const std::array<RE::GFxValue, 1> args{ point };
            if (!a_from.Invoke("localToGlobal", args) || !a_to.Invoke("globalToLocal", args)) return std::nullopt;
            const auto x = Number(point, "x"), y = Number(point, "y");
            if (!x || !y) return std::nullopt;
            return std::pair{ *x, *y };
        }

        struct Bounds
        {
            double xMin, xMax, yMin, yMax;

            void Add(const Bounds& a_other)
            {
                xMin = std::min(xMin, a_other.xMin);
                xMax = std::max(xMax, a_other.xMax);
                yMin = std::min(yMin, a_other.yMin);
                yMax = std::max(yMax, a_other.yMax);
            }
        };

        // AS2 getBounds(a_space): the clip's bounds as Flash gives them in another clip's space
        std::optional<Bounds> GetBounds(RE::GFxValue& a_clip, RE::GFxValue& a_space)
        {
            RE::GFxValue bounds;
            const std::array<RE::GFxValue, 1> args{ a_space };
            if (!a_clip.Invoke("getBounds", &bounds, args) || !bounds.IsObject()) return std::nullopt;
            const auto xMin = Number(bounds, "xMin"), xMax = Number(bounds, "xMax"), yMin = Number(bounds, "yMin"),
                       yMax = Number(bounds, "yMax");
            if (!xMin || !xMax || !yMin || !yMax || *xMax <= *xMin || *yMax <= *yMin) return std::nullopt;
            return Bounds{ *xMin, *xMax, *yMin, *yMax };
        }

        // A clip's bounds in a_space through every transform between them (controller finding 2026-10-30: getBounds(_root) of
        // SkyUI's list clips under Inventory Reforged's sized window - iSize 85 - came out about 0.85 x + (200, 26) off what the
        // screen shows): its bounds in its own space, each corner taken to the stage (localToGlobal) and into a_space
        // (globalToLocal), the box round them (BlockRules::CornerBounds). Without the open menu's movie: getBounds(a_space)
        std::optional<Bounds> BoundsIn(RE::GFxValue& a_clip, RE::GFxValue& a_space)
        {
            auto* movie = OpenMagicMenuMovie();
            if (const auto local = movie ? GetBounds(a_clip, a_clip) : std::nullopt) {
                std::array<BlockRules::Point, 4> corners{};
                const std::array<std::pair<double, double>, 4> at{ { { local->xMin, local->yMin }, { local->xMax, local->yMin }, { local->xMax, local->yMax },
                    { local->xMin, local->yMax } } };
                bool mapped = true;
                for (std::size_t i = 0; i < at.size() && mapped; ++i) {
                    const auto point = MapPoint(movie, a_clip, a_space, at[i].first, at[i].second);
                    mapped = point.has_value();
                    if (point) corners[i] = { point->first, point->second };
                }
                if (const auto box = mapped ? BlockRules::CornerBounds(corners) : std::nullopt) return Bounds{ box->left, box->right, box->top, box->bottom };
            }
            return GetBounds(a_clip, a_space);
        }

        std::optional<std::pair<RE::GFxValue, RE::GFxValue>> CardAndHolder(RE::GFxMovieView* a_movie)
        {
            auto card = FindCard(a_movie);
            RE::GFxValue holder;
            if (!card || !card->GetMember("_parent", &holder) || !holder.IsDisplayObject()) return std::nullopt;
            return std::pair{ *card, holder };
        }

        // [Blocks] sHidden holds the spell card: no card block this menu - SkyUI's item card stays where SkyUI puts it (not moved,
        // not fitted, not hidden), as with no card block at all
        bool SpellCardHidden()
        {
            return g_menu.hidden.contains(Layout::kSpellCard);
        }

        // v7 C1: our own info card replaces SkyUI's (bOwnCard=1), not while Inventory Reforged runs its panel here
        bool OwnCard()
        {
            return Settings::Get().ownCard && !g_menu.irActive && !g_menu.ownCardFallback && !SpellCardHidden();
        }

        // ---------------------------------------------------------------------
        // Blocks (blocks plan Task 2): every panel's rect comes from the saved layout (Layout::Arrange). The ini rects are
        // the blocks' default sizes; at the default layout with no add-on the spell card follows the preview (SelfTest L1)
        // ---------------------------------------------------------------------

        using Layout::BlockId;
        // the built-in blocks in panel creation order (the spell card's panel last, on top). HoM add-on plan Task 5: tree, progress
        // and nodecard are retired ids (BlockRules::kRetiredIds) - not reserved, so a v3 add-on can take them over (legacyId)
        constexpr std::array<const char*, 2> kBlocks{ Layout::kPreview, Layout::kSpellCard };
        constexpr double kRectTolerance = 0.01;  // stage units: two rects this close are the same place

        // ---------------------------------------------------------------------
        // Add-on blocks (blocks plan Task 6, D9): other plugins' blocks (BlockApi.h). Registered by SKSE messages (any thread,
        // under g_registryLock) until the first magic menu open (CloseRegistry); from then on g_addons and g_allBlocks are fixed
        // (written once, before the first AttachWidget's UI task) and read without a lock
        // ---------------------------------------------------------------------

        std::mutex g_registryLock;
        BlockRegistry::Registry g_registry{ std::set<std::string>(kBlocks.begin(), kBlocks.end()) };
        std::vector<BlockRegistry::Block> g_addons;  // the registry's blocks as they were when it closed, in registration order
        std::vector<std::string> g_addonClips;       // each add-on's panel clip name ("mmxAddon0", ...): an id may hold '.'
        std::vector<BlockId> g_allBlocks(kBlocks.begin(), kBlocks.end());
        std::map<std::string, std::string> g_legacyAliases;  // retired id -> final id, fixed when the registry closes ([Blocks] sHidden reads both)

        // Block API v2: kMessageRedraw ids from any thread (RequestAddonRedraw), taken by the frame handler (TakeAddonRedraws);
        // emptied at each menu open
        std::mutex g_redrawLock;
        std::vector<std::string> g_redrawQueue;
        // Block API v2: kMessageHeight requests from any thread (RequestAddonHeight), one per id with the last height
        // (BlockRules::Enqueue), taken by the frame handler (TakeAddonHeights); emptied at each menu open
        std::mutex g_heightLock;
        std::vector<std::pair<std::string, double>> g_heightQueue;
        // Block API v3: kMessageShowSpell (newest per id, with the selection sequence it was sent at), kMessageVisible (last per id)
        // and the keyboard downs (QueueAddonKey) from any thread, taken by the frame handler; emptied at each menu open
        std::mutex g_v3Lock;
        BlockRules::ShowSpellQueue g_showSpellQueue;
        std::vector<std::pair<std::string, bool>> g_visibleQueue;
        std::vector<std::uint32_t> g_keyQueue;
        // Block API v3, per add-on (g_addons' index; sized when the registry closes), kept across menus: its overlay clip's name
        // ("mmxOverlay0", ...: an id may hold '.'), its overlay (BlockRules::OverlayTrack) and its draws into a new content clip
        // (Context::contentGeneration)
        std::vector<std::string> g_overlayClips;
        std::vector<BlockRules::OverlayTrack> g_overlayTracks;
        std::vector<std::uint32_t> g_contentGenerations;

        constexpr std::size_t kMaxQueuedRequests = 64;  // distinct ids of one request kind waiting for one frame; more are dropped
        constexpr int kMaxBadRequestLogs = 5;           // refused or dropped requests logged per kind and game session

        // One add-on request message ('SLMH', 'SLMS', 'SLMV'), any thread: ignored while the magic menu is closed; read by a_read
        // (the request, or nullopt with the reason in its string: refused, logged); queued by a_enqueue (a BlockRules::Queued)
        // under a_lock, a full queue logged. a_name: "height request ('SLMH')" ...; a_badLogs: the kind's log count
        template <class Read, class Enqueue>
        void QueueRequest(std::string_view a_sender, const char* a_name, std::atomic<int>& a_badLogs, std::mutex& a_lock, Read&& a_read, Enqueue&& a_enqueue)
        {
            if (g_closing || !g_attachedMovie.load()) return;  // the next menu opens without it (the add-on sends it again)
            const auto sender = a_sender.empty() ? std::string_view("(no sender)") : a_sender;
            std::string why;
            auto request = a_read(why);
            if (!request) {
                if (a_badLogs.fetch_add(1) < kMaxBadRequestLogs) logger::warn("Widget: a {} from {} refused - {}", a_name, sender, why);
                return;
            }
            const std::string id = request->id;
            const std::scoped_lock lock(a_lock);
            if (a_enqueue(std::move(*request)) == BlockRules::Queued::kFull && a_badLogs.fetch_add(1) < kMaxBadRequestLogs) {
                logger::warn("Widget: {} for {} from {} dropped - {} ids queued already", a_name, id, sender, kMaxQueuedRequests);
            }
        }

        // Every block: the five built-in ones in build 13's panel creation order, then the add-ons in registration order
        const std::vector<BlockId>& AllBlocks()
        {
            return g_allBlocks;
        }

        std::optional<std::size_t> AddonIndex(const BlockId& a_id)
        {
            for (std::size_t i = 0; i < g_addons.size(); ++i) {
                if (g_addons[i].id == a_id) return i;
            }
            return std::nullopt;
        }

        const BlockRegistry::Block* Addon(const BlockId& a_id)
        {
            const auto index = AddonIndex(a_id);
            return index ? &g_addons[*index] : nullptr;
        }

        // The first magic menu open: no more registrations; the add-ons join the block list and the layout (Settings::AddBlocks:
        // one without a saved place goes to the end of the right column; [Debug] bDebugFillerBlock's to the left one after the spell card)
        void CloseRegistry()
        {
            std::vector<BlockRegistry::Block> blocks;
            std::map<std::string, std::string> legacyAliases;
            {
                const std::scoped_lock lock(g_registryLock);
                if (!g_registry.Close()) return;
                blocks = g_registry.Blocks();
                legacyAliases = g_registry.LegacyAliases();
            }
            g_addons = std::move(blocks);
            std::vector<BlockId> ids;
            std::string names;
            for (std::size_t i = 0; i < g_addons.size(); ++i) {
                ids.push_back(g_addons[i].id);
                g_addonClips.push_back(std::format("mmxAddon{}", i));
                g_overlayClips.push_back(std::format("mmxOverlay{}", i));
                g_allBlocks.push_back(g_addons[i].id);
                names += std::format("{}{} ({:.0f} high, from {})", i ? ", " : "", g_addons[i].id, g_addons[i].height, g_addons[i].sender);
            }
            g_overlayTracks.resize(g_addons.size());
            g_contentGenerations.resize(g_addons.size());
            logger::info("Widget: add-on block registration closed at the first magic menu open - {} block(s){}{}", g_addons.size(),
                g_addons.empty() ? "" : ": ", names);
            g_legacyAliases = legacyAliases;
            Settings::SetLegacyAliases(legacyAliases);  // HoM add-on design G-e: before AddBlocks, which renames the layout by it
            Settings::AddBlocks(ids);
        }

        // 2026-10-31: [Blocks] sHidden (BlockHiding::Read in the menu sink) for this menu - the blocks it names (a retired id read as
        // the add-on block that took it over); ids that name no block are ignored and stay in the ini. Logged when the result changes
        void ApplyHiddenSetting()
        {
            const auto setting = BlockHiding::Setting();
            const std::set<BlockId> known(g_allBlocks.begin(), g_allBlocks.end());
            g_menu.hidden = HiddenBlocks::Effective(setting, known, g_legacyAliases);
            static std::optional<std::string> s_logged;  // the UI thread only
            const auto value = HiddenBlocks::Join(setting);
            if (s_logged == value) return;
            s_logged = value;
            const auto unknown = HiddenBlocks::Unknown(setting, known, g_legacyAliases);
            logger::info("Widget: hidden blocks ([Blocks] sHidden): {}{}",
                g_menu.hidden.empty() ? std::string("none") : HiddenBlocks::Join({ g_menu.hidden.begin(), g_menu.hidden.end() }),
                unknown.empty() ? std::string{} : " - not a block here, ignored: " + HiddenBlocks::Join(unknown));
        }

        // [Debug] bDebugFillerBlock: the test add-on's draw callback - a note and three bars of falling length (the title strip
        // "FILLER (TEST)" is the host's, as for any add-on)
        void DrawFiller(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, float a_width, float a_height, void*)
        {
            constexpr double kNoteH = 18.0, kFillerTextSize = 11.0, kBarGap = 10.0, kBarAlpha = 60.0;
            constexpr std::array<double, 3> kBarShares{ 0.9, 0.6, 0.3 };
            Draw::Text(a_movie, a_clip, "note", 0, 0, a_width, kNoteH, "Add-on test block ([Debug] bDebugFillerBlock=1)", kDimText, kFillerTextSize);
            const double barH = std::max(0.0, (a_height - kNoteH - kBarGap * kBarShares.size()) / kBarShares.size());
            Draw::NoLine(a_clip);
            double y = kNoteH + kBarGap;
            for (const double share : kBarShares) {
                Draw::Rect(a_clip, 0, y, a_width * share, barH, kTitleColor, kBarAlpha);
                y += barH + kBarGap;
            }
        }

        bool SameRect(const Settings::Rect& a_a, const Settings::Rect& a_b)
        {
            return std::abs(a_a.left - a_b.left) <= kRectTolerance && std::abs(a_a.top - a_b.top) <= kRectTolerance &&
                   std::abs(a_a.right - a_b.right) <= kRectTolerance && std::abs(a_a.bottom - a_b.bottom) <= kRectTolerance;
        }

        std::string RectText(const Settings::Rect& a_rect)
        {
            return std::format("{:.0f},{:.0f}..{:.0f},{:.0f}", a_rect.left, a_rect.top, a_rect.right, a_rect.bottom);
        }

        // A built-in block's ini rect (its default size and place)
        Settings::Rect IniRect(const Settings::Values& a_values, const BlockId& a_id)
        {
            return a_id == Layout::kPreview ? a_values.preview : a_values.itemCard;
        }

        struct Arrangement
        {
            std::set<BlockId> hidden, fill;
        };

        // Block API v2: the add-ons whose tabs mask leaves out the tab of a_flag (BlockRules::ShownOn). Read on the frame thread
        // only (g_addons is fixed from the first open on)
        RE::FormID ShownForm();  // the form the menu's card shows (with the add-on drawing, below)
        void ShowEmptyLine(RE::GFxValue& a_widget, const BlockId& a_id, bool a_shown);  // the board's empty look (with DrawBoard, below)
        void DrawBoard(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget);              // the board (below)

        // user 2026-10-30 (the board): an add-on drawn per selection (drawSelection) with nothing to show is closed up too
        // (BlockRules::EmptyLookOf: kClosed); a v1 block and a v3 one with kFlagKeepWhenEmpty keep their places
        std::set<BlockId> AddonsHiddenOn(int a_flag)
        {
            std::set<BlockId> hidden;
            const bool nothing = ShownForm() == 0;
            for (const auto& addon : g_addons) {
                if (!BlockRules::ShownOn(addon.tabs, a_flag) ||
                    BlockRules::EmptyLookOf(addon.id, nothing, BlockRules::ClosesWhenEmpty(addon.drawSelection != nullptr, addon.flags), g_menu.layout.active) ==
                        BlockRules::EmptyLook::kClosed) {
                    hidden.insert(addon.id);
                }
            }
            return hidden;
        }

        // review R2: entering or leaving the layout mode changes which add-ons are closed up (nothing shown: closed out of the mode,
        // shown in it so they can be moved)
        bool AddonsCloseUp()
        {
            return ShownForm() == 0 && std::ranges::any_of(g_addons, [](const auto& a_addon) { return BlockRules::ClosesWhenEmpty(a_addon.drawSelection != nullptr, a_addon.flags); });
        }

        // What Arrange is given (D6). Our card (a_own) fills the rest of its column; SkyUI's fitted card (bOwnCard=0) keeps its
        // height. Block API v2: the add-ons the tab's flag leaves out (a_flag; nullopt: the current tab's) close up. HoM add-on plan
        // Task 5: no built-in block is hidden per tab any more (the tree, progress and node card that Powers/Shouts and Active
        // Effects hid are an add-on's blocks now, hidden by their tabs mask)
        Arrangement ArrangementFor(bool a_own, std::optional<int> a_flag = std::nullopt)
        {
            Arrangement arrangement{ {}, a_own ? std::set<BlockId>{ Layout::kSpellCard } : std::set<BlockId>{} };
            arrangement.hidden.merge(AddonsHiddenOn(a_flag.value_or(g_menu.tabFlag.value_or(kFlagUnread))));
            return arrangement;
        }

        // The blocks' heights: the ini rects', the add-ons' registered ones (Task 6), or the height an add-on asked for this menu (kMessageHeight; not in the layout
        // mode: BlockRules::LaidOutHeight). A height that is not finite or negative leaves its add-on out (absent: Arrange
        // would skip it too) - the registry refuses such heights, so this is the last guard; logged once per block
        std::map<BlockId, double> BlockHeights()
        {
            const auto& settings = Settings::Get();
            auto heights = Layout::DefaultHeights(settings);
            for (const auto& addon : g_addons) {
                if (std::isfinite(addon.height) && addon.height >= 0) {
                    heights[addon.id] = addon.height;  // its fit this menu: ArrangeBlocks (BlockRules::ApplyFits)
                    continue;
                }
                static std::set<BlockId> s_logged;
                if (s_logged.insert(addon.id).second) {
                    logger::warn("Widget: add-on block {} has the height {} - left out of the layout (logged once)", addon.id, addon.height);
                }
            }
            HiddenBlocks::Drop(heights, g_menu.hidden);  // [Blocks] sHidden: absent, as an add-on not installed (no place, no gap)
            return heights;
        }

        // blocks spec "Calls made by Claude" (safety net only): Inventory Reforged's magic-menu panel on in its MCM, or our fallback
        // switches off (bOwnCard=0, bMovePreview=0, bDrawPreviewOver=0): the layout mode rests (lock greyed) and the default
        // layout is used; the saved layout is kept. Empty when none applies
        // g_menu.irActive published for the input thread (the hook's refusal log reads SafetyNetReason there; final review): set
        // with it at each menu open, the frame-thread state is not read from another thread
        std::atomic<bool> g_irActiveShared{ false };

        std::string SafetyNetReason()
        {
            const auto& s = Settings::Get();
            std::string why;
            const auto add = [&](const char* a_part) { why += (why.empty() ? "" : ", ") + std::string(a_part); };
            if (g_irActiveShared) add("Inventory Reforged runs its panel in this menu");
            if (!s.ownCard) add("bOwnCard=0");
            if (!s.movePreview) add("bMovePreview=0");
            if (!s.drawPreviewOver) add("bDrawPreviewOver=0");
            return why;
        }

        // The layout the panels follow (blocks plan Task 4): the layout mode's working copy while the mode is on, the default
        // layout in the safety-net states, else the saved one
        const Layout::State& CurrentLayout()
        {
            if (g_menu.layout.active) return g_menu.layout.working;
            if (!SafetyNetReason().empty()) {
                return Settings::DefaultLayout();  // with the add-on blocks (Task 6)
            }
            return Settings::SavedLayout();
        }

        // UNSCROLLED stage rects (Arrange's, plan D4): the scroll is the column clip's (PlaceColumns), BlockRect adds it. a_fits:
        // the blocks' heights this menu (g_menu.fitHeight; false: every block at its full height, g_menu.blocksMax)
        std::map<BlockId, Settings::Rect> ArrangeBlocks(const Arrangement& a_arrangement, bool a_fits = true)
        {
            const auto& settings = Settings::Get();
            auto heights = BlockHeights();
            auto fill = a_arrangement.fill;
            if (a_fits) BlockRules::ApplyFits(heights, fill, g_menu.fitHeight, g_menu.layout.active);
            return Layout::Arrange(CurrentLayout(), heights, a_arrangement.hidden, fill, Layout::DefaultFrames(settings));
        }

        // The current layout's column holding the block (nullopt: not in the layout - Repair keeps every known block in it)
        std::optional<int> ColumnOf(const BlockId& a_id)
        {
            const auto& columns = CurrentLayout().columns;
            for (int column = 0; column < Layout::kColumns; ++column) {
                if (std::ranges::find(columns[column], a_id) != columns[column].end()) return column;
            }
            return std::nullopt;
        }

        // The column's viewport in stage units (blocks plan Task 3): the part of the column that shows - its mask, the wheel's
        // and the node clicks' hit area, the 3D preview's column clip. Fixed on screen: it does not scroll
        Settings::Rect ColumnViewport(int a_column)
        {
            return Layout::ColumnArea(CurrentLayout(), a_column, Layout::DefaultFrames(Settings::Get()));
        }

        // The column's content height (this tab's arranged blocks, unscrolled) and its visible height
        std::pair<double, double> ColumnHeights(int a_column)
        {
            const auto content = Layout::ContentHeight(CurrentLayout(), g_menu.blocks, Layout::DefaultFrames(Settings::Get()));
            const bool valid = a_column >= 0 && a_column < Layout::kColumns;
            return { valid ? content[a_column] : 0.0, ColumnViewport(a_column).Height() };
        }

        // The column is taller than its viewport: the wheel over it scrolls it
        bool ColumnScrolls(int a_column)
        {
            const auto [content, visible] = ColumnHeights(a_column);
            return content > visible + kRectTolerance;
        }

        // The block's current rect in stage units ON SCREEN: Rearrange's unscrolled rect moved up by its column's scroll (plan D4:
        // for everything that needs a screen place - the 3D preview's box, SkyUI's card slot, read-backs in _root). A block the
        // tab hides gets its place with nothing hidden, so its panel still has a size to be drawn in. Panel transforms take it
        // through BlockLocal
        Settings::Rect BlockRect(const BlockId& a_id)
        {
            const auto scrolled = [&](Settings::Rect a_rect) {
                const auto column = ColumnOf(a_id);
                const double offset = column ? g_menu.scroll[*column] : 0.0;
                a_rect.top -= offset;
                a_rect.bottom -= offset;
                return a_rect;
            };
            if (const auto it = g_menu.blocks.find(a_id); it != g_menu.blocks.end()) return scrolled(it->second);
            if (const auto it = g_menu.blocksAll.find(a_id); it != g_menu.blocksAll.end()) return scrolled(it->second);
            if (Addon(a_id) || g_menu.hidden.contains(a_id)) return {};  // no arranged place (no usable height, or hidden): nothing to draw in
            return IniRect(Settings::Get(), a_id);  // before the menu's first Rearrange (Repair keeps every block in the layout)
        }

        // The block is laid out on this tab (D6); ApplyVisibility adds "something chosen"
        bool BlockShown(const BlockId& a_id)
        {
            return g_menu.blocks.contains(a_id);
        }

        // SkyUI's item card slot (bOwnCard=0): the spell card block without growing - right under the preview at the default
        // layout with no add-on, the item card's ini height (user 2026-10-25). Task 2 review:
        // the arranged block (Rearrange arranges with own = OwnCard(), again at each tab change and at the card fallback), so
        // SkyUI's card is fitted to it only while it is the card; on screen (scrolled), as the fit maps it from _root
        Settings::Rect CardRect()
        {
            return BlockRect(Layout::kSpellCard);
        }

        // Our card's box per tab (C2): the spell card block, grown to the rest of its column (on Active Effects the panel is then
        // cut to its content, RedrawOwnCard a_fit). The same block as CardRect, arranged with
        // our card. Its drawn bottom (ownCardBottom) is the last drawing's: OwnCardBox pairs the two only once they match
        Settings::Rect OwnCardRect()
        {
            return BlockRect(Layout::kSpellCard);
        }

        // Build 16: our card's box as last drawn, UNSCROLLED (the arranged block with the drawn panel's height) - nullopt until
        // RedrawOwnCard drew it on this tab in a rect of this block's size. A tab change rearranges (WatchTab) before
        // WatchOwnCard draws the card again, so until then ownCardBottom is the last tab's (Active Effects' cut card at 67 + 90
        // = 157 read against Powers' top 306 was build 15's inverted "648,306..969,157")
        std::optional<Settings::Rect> OwnCardBox()
        {
            const auto it = g_menu.blocks.find(Layout::kSpellCard);
            if (it == g_menu.blocks.end() || g_menu.ownCardDrawnTab != static_cast<int>(g_menu.tab)) return std::nullopt;
            const auto& drawn = g_menu.ownCardDrawnIn;
            if (std::abs(drawn.Width() - it->second.Width()) > kRectTolerance || std::abs(drawn.Height() - it->second.Height()) > kRectTolerance) {
                return std::nullopt;
            }
            auto box = it->second;
            box.bottom = box.top + (g_menu.ownCardBottom - drawn.top);
            return box;
        }

        // The item SkyUI's card shows: LastUpdateObj (ItemCard.as:66), what its itemInfo getter returns (ItemCard.as:172-175)
        RE::GFxValue CardInfo(const RE::GFxValue& a_card)
        {
            RE::GFxValue info;
            if (!a_card.GetMember("LastUpdateObj", &info) || !info.IsObject()) return {};
            return info;
        }

        double CardType(const RE::GFxValue& a_card)
        {
            constexpr double kNoType = -1.0;
            return Number(CardInfo(a_card), "type").value_or(kNoType);
        }

        // The card's frame and item: SkyUI's itemInfo setter (ItemCard.as:177-455) picks a frame per item type and
        // fills that frame's fields
        std::string CardSignature(const RE::GFxValue& a_card)
        {
            return std::format("{:.0f}|{:.0f}|{}", Number(a_card, "_currentframe").value_or(-1), CardType(a_card),
                StringMember(CardInfo(a_card), "name"));
        }

        // The card's parts by name: SkyUI's own (ItemCard.as:16-43: its text fields, ItemText's clip) and the installed
        // card's ("background": the clip that loads the external itemcard_1_BG.swf; "SkillLevelLabel": DefineEditText
        // #169 of Weapon Damage Display Fix's itemcard.swf; both read from the SWF bytes, codex_card_result.md 4,
        // UNVERIFIED for other card SWFs). A part not on the card's current frame does not exist and is skipped;
        // named children this list does not have are found by VisitMembers.
        constexpr auto kCardParts = std::to_array<const char*>({ "background", "ItemText", "ItemName", "ItemValueText",
            "ItemWeightText", "MagicEffectsLabel", "MagicCostLabel", "MagicCostValue", "MagicCostTimeLabel", "MagicCostTimeValue",
            "MagicCostPerSec", "SecsText", "SkillLevelText", "SkillLevelLabel", "SkillTextInstance", "ApparelArmorValue",
            "ApparelEnchantedLabel", "WeaponDamageValue", "WeaponEnchantedLabel", "PotionsLabel", "BookDescriptionLabel",
            "EnchantmentLabel", "ShoutCostValue", "ShoutEffectsLabel", "ActiveEffectTimeValue", "SoulLevel", "TotalChargesValue",
            "StolenTextInstance", "MessageText", "SliderValueText", "PoisonInstance" });

        // a_part is a direct child of the card: VisitMembers also lists class members that point elsewhere (PrevFocus,
        // ItemCard.as:58). AS2 _target is the slash path ("/Menu_mc/itemCardFadeHolder/ItemCard_mc/background").
        bool ChildOf(const RE::GFxValue& a_part, const std::string& a_cardTarget)
        {
            const auto target = StringMember(a_part, "_target");
            const auto from = a_cardTarget.size() + 1;
            return !a_cardTarget.empty() && target.size() > from && target.starts_with(a_cardTarget) && target[from - 1] == '/' &&
                   target.find('/', from) == std::string::npos;
        }

        // Drawn now: visible, not transparent, and a text field only with text (its own border/background not checked)
        bool PartShows(const RE::GFxValue& a_part)
        {
            RE::GFxValue visible, text;
            if (a_part.GetMember("_visible", &visible) && visible.IsBool() && !visible.GetBool()) return false;
            if (Number(a_part, "_alpha").value_or(Draw::kAlphaFull) <= 0) return false;
            if (a_part.GetMember("text", &text) && text.IsString()) return text.GetString() && *text.GetString();
            return true;
        }

        // A part's box in a_space: a clip's getBounds; else (an AS2 TextField has no getBounds) its field box, _x/_y/
        // _width/_height in the card, mapped through the stage. Limit: the field box, not the text in it (a wide field
        // with short text counts in full; textWidth with the field's alignment would be the upgrade).
        std::optional<Bounds> PartBounds(RE::GFxMovieView* a_movie, RE::GFxValue& a_card, RE::GFxValue& a_part, RE::GFxValue& a_space)
        {
            if (auto clip = BoundsIn(a_part, a_space)) return clip;
            const auto x = Number(a_part, "_x"), y = Number(a_part, "_y"), w = Number(a_part, "_width"), h = Number(a_part, "_height");
            if (!x || !y || !w || !h || *w <= 0 || *h <= 0) return std::nullopt;
            const auto a = MapPoint(a_movie, a_card, a_space, *x, *y);
            const auto b = MapPoint(a_movie, a_card, a_space, *x + *w, *y + *h);
            if (!a || !b) return std::nullopt;
            return Bounds{ std::min(a->first, b->first), std::max(a->first, b->first), std::min(a->second, b->second),
                std::max(a->second, b->second) };
        }

        struct CardUnion
        {
            Bounds bounds;
            std::string parts;  // "name WxH" of every part counted, for the logs
        };

        // What the card draws: the union of its visible parts (background and text fields) in a_space. The background
        // alone gave a false PASS (game run 2026-10-25, LEDGER Confirmed 10).
        std::optional<CardUnion> VisibleUnion(RE::GFxMovieView* a_movie, RE::GFxValue& a_card, RE::GFxValue& a_space)
        {
            const auto cardTarget = StringMember(a_card, "_target");
            std::vector<std::string> names(kCardParts.begin(), kCardParts.end());
            a_card.VisitMembers([&](const char* a_name, const RE::GFxValue& a_value) {
                if (a_name && a_value.IsDisplayObject() && std::ranges::find(names, a_name) == names.end()) names.emplace_back(a_name);
            });
            std::optional<CardUnion> seen;
            for (std::size_t i = 0; i < names.size(); ++i) {
                RE::GFxValue part;
                if (!a_card.GetMember(names[i].c_str(), &part) || !part.IsDisplayObject()) continue;
                // without the card's _target only the known names count (they are the card's own)
                const bool own = cardTarget.empty() ? i < kCardParts.size() : ChildOf(part, cardTarget);
                if (!own || !PartShows(part)) continue;
                const auto box = PartBounds(a_movie, a_card, part, a_space);
                if (!box) continue;
                if (seen) {
                    seen->bounds.Add(*box);
                } else {
                    seen = CardUnion{ *box, {} };
                }
                seen->parts += std::format("{}{} {:.0f}x{:.0f}", seen->parts.empty() ? "" : ", ", names[i], box->xMax - box->xMin,
                    box->yMax - box->yMin);
            }
            return seen;
        }

        // The black box that stays under our card with SkyUI's card holder hidden (game run 2026-10-25 23:44, stage about
        // 735..1203 x 427..682; in build 6 frames too): which clip it is was not known, so every display object directly
        // under _root and _root.Menu_mc is logged with its _visible, _alpha and box in _root when the box meets that area,
        // plus the card / holder / background chain. Twice a menu (kAreaDumpFrames); the log names the clip to hide
        void DumpCardArea(RE::GFxMovieView* a_movie)
        {
            constexpr Bounds kArea{ 640.0, 1280.0, 300.0, 760.0 };
            RE::GFxValue root;
            if (!a_movie->GetVariable(&root, "_root") || !root.IsDisplayObject()) return;
            // a text field has no getBounds in AS2: PartBounds maps its field box through its parent (review)
            const auto describe = [&](const std::string& a_what, RE::GFxValue& a_parent, RE::GFxValue& a_object) {
                RE::GFxValue visible;
                const bool shown = !a_object.GetMember("_visible", &visible) || !visible.IsBool() || visible.GetBool();
                const auto box = PartBounds(a_movie, a_parent, a_object, root);
                return std::format("{} {} visible {} alpha {:.0f} box {}", a_what, StringMember(a_object, "_target"), shown,
                    Number(a_object, "_alpha").value_or(-1.0),
                    box ? std::format("{:.0f}..{:.0f} x {:.0f}..{:.0f}", box->xMin, box->xMax, box->yMin, box->yMax) : std::string("-"));
            };
            const auto sweep = [&](RE::GFxValue& a_parent, const std::string& a_parentTarget) {
                std::vector<std::string> names;
                a_parent.VisitMembers([&](const char* a_name, const RE::GFxValue& a_value) {
                    if (a_name && a_value.IsDisplayObject()) names.emplace_back(a_name);
                });
                int listed = 0;
                for (const auto& name : names) {
                    RE::GFxValue child;
                    if (!a_parent.GetMember(name.c_str(), &child) || !child.IsDisplayObject()) continue;
                    const auto target = StringMember(child, "_target");
                    // under _root ("/"): "/name" only; else ChildOf's "<parent>/name"
                    const bool direct = a_parentTarget.empty() ? target.size() > 1 && target[0] == '/' && target.find('/', 1) == std::string::npos
                                                               : ChildOf(child, a_parentTarget);
                    if (!direct) continue;
                    const auto box = PartBounds(a_movie, a_parent, child, root);
                    if (!box || box->xMax < kArea.xMin || box->xMin > kArea.xMax || box->yMax < kArea.yMin || box->yMin > kArea.yMax) continue;
                    logger::info("Widget: C1 area {}", describe(name, a_parent, child));
                    ++listed;
                }
                logger::info("Widget: C1 area - {} of {} display members of {} meet stage {:.0f}..{:.0f} x {:.0f}..{:.0f}", listed, names.size(),
                    a_parentTarget.empty() ? "_root" : a_parentTarget, kArea.xMin, kArea.xMax, kArea.yMin, kArea.yMax);
            };
            sweep(root, std::string{});
            RE::GFxValue menu;
            if (a_movie->GetVariable(&menu, kMenuPath) && menu.IsDisplayObject()) sweep(menu, StringMember(menu, "_target"));
            if (auto pair = CardAndHolder(a_movie)) {
                auto& [card, holder] = *pair;
                RE::GFxValue holderParent;
                if (!holder.GetMember("_parent", &holderParent) || !holderParent.IsDisplayObject()) holderParent = root;
                logger::info("Widget: C1 area chain - {}", describe("holder", holderParent, holder));
                logger::info("Widget: C1 area chain - {}", describe("card", holder, card));
                RE::GFxValue background;
                if (card.GetMember("background", &background) && background.IsDisplayObject()) {
                    logger::info("Widget: C1 area chain - {}", describe("background", card, background));
                }
            }
        }

        // "레벨 " in the installed card's SkillLevelLabel (23 x 22.95, wordWrap on, autoSize off) wraps its last
        // character under the field's fixed height, so only "레" shows (LEDGER Confirmed 10, codex_card_result.md 4):
        // no wrap, growing leftwards from its right edge. Checked on every measured frame, since SkyUI's gotoAndStop
        // per item can make the field anew. A spell card without the field is logged once.
        // The field's box and its text's size (textWidth/textHeight, AS2 TextField): text wider or taller than the box
        // is cut, so the log alone tells whether the level label fits (R14)
        std::string FieldSize(const RE::GFxValue& a_field)
        {
            constexpr double kNone = -1.0;
            return std::format("box {:.1f} x {:.1f}, text {:.1f} x {:.1f}", Number(a_field, "_width").value_or(kNone),
                Number(a_field, "_height").value_or(kNone), Number(a_field, "textWidth").value_or(kNone),
                Number(a_field, "textHeight").value_or(kNone));
        }

        void FixLevelLabel(RE::GFxValue& a_card)
        {
            RE::GFxValue label, wrap;
            const bool spellCard = CardType(a_card) == kCardTypeSpell;
            if (!a_card.GetMember("SkillLevelLabel", &label) || !label.IsDisplayObject()) {
                if (!g_menu.levelLabelLogged && spellCard) {
                    g_menu.levelLabelLogged = true;
                    logger::info("Widget: the item card has no SkillLevelLabel on its spell frame - nothing to fix");
                }
                return;
            }
            const bool wraps = !label.GetMember("wordWrap", &wrap) || !wrap.IsBool() || wrap.GetBool();
            const bool right = StringMember(label, "autoSize") == "right";
            const bool fix = wraps || !right;
            const auto before = FieldSize(label);
            if (fix) {
                label.SetMember("wordWrap", false);
                label.SetMember("autoSize", "right");
            }
            // once per menu, on a spell card (its text is the level label then)
            if (g_menu.levelLabelLogged || !spellCard) return;
            g_menu.levelLabelLogged = true;
            logger::info("Widget: item card SkillLevelLabel '{}' - {}: {} -> {}", StringMember(label, "text"),
                fix ? "wordWrap off, autoSize right" : "already no wrap and autoSize right", before, FieldSize(label));
        }

        // The holder transform that puts the card's visible union into the slot, top-aligned and centred across.
        // Spaces: the slot is in _root stage units; SkyUI places the holder in its parent (Menu_mc, which follows the
        // visible rect: on 16:10 it is not _root's frame, game run 2026-10-25), so the slot's corners are mapped
        // _root -> parent with localToGlobal/globalToLocal, and the union is measured in the holder's own
        // (unscaled) space; _x/_y/_xscale are then all parent-space values, the ones SkyUI writes too.
        std::optional<CardFit> ComputeCardFit(RE::GFxMovieView* a_movie, RE::GFxValue& a_root, RE::GFxValue& a_card, RE::GFxValue& a_holder)
        {
            constexpr double kPercent = 100.0;
            RE::GFxValue parent;
            if (!a_holder.GetMember("_parent", &parent) || !parent.IsDisplayObject()) return std::nullopt;
            const auto seen = VisibleUnion(a_movie, a_card, a_holder);
            const auto rect = CardRect();
            const auto topLeft = MapPoint(a_movie, a_root, parent, rect.left, rect.top);
            const auto bottomRight = MapPoint(a_movie, a_root, parent, rect.right, rect.bottom);
            if (!seen || !topLeft || !bottomRight) return std::nullopt;
            const auto& local = seen->bounds;
            const double targetW = bottomRight->first - topLeft->first, targetH = bottomRight->second - topLeft->second;
            const double w = local.xMax - local.xMin, h = local.yMax - local.yMin;
            const double scale = std::min(targetW / w, targetH / h);
            if (!(scale > 0)) return std::nullopt;
            const CardFit fit{ topLeft->first + (targetW - w * scale) / 2 - local.xMin * scale, topLeft->second - local.yMin * scale,
                scale * kPercent, { local.xMin, local.xMax, local.yMin, local.yMax } };
            if (++g_menu.cardFitLogs <= kMaxFitLogs) {
                logger::info("Widget: item card fit ({}): visible union {:.0f}x{:.0f} in the holder ({}), slot {:.0f},{:.0f}..{:.0f},{:.0f} "
                             "in _root = {:.1f},{:.1f}..{:.1f},{:.1f} in the holder's parent -> scale {:.3f}, holder at {:.1f},{:.1f}",
                    "the spell card's block", w, h, seen->parts, rect.left, rect.top,
                    rect.right, rect.bottom, topLeft->first, topLeft->second, bottomRight->first, bottomRight->second, scale, fit.x, fit.y);
            }
            return fit;
        }

        std::string BoundsText(const std::optional<Bounds>& a_bounds)
        {
            return a_bounds ? std::format("{:.1f},{:.1f}..{:.1f},{:.1f}", a_bounds->xMin, a_bounds->yMin, a_bounds->xMax, a_bounds->yMax) : "none";
        }

        // P4 self-test, read back in _root after the measure window's last re-fit. PASS needs all of: the card's visible
        // union inside the slot on all four edges (kSlack stage units) and at its top (the fit top-aligns it, R13); the
        // whole card (ItemCard_mc's getBounds, hidden parts too) and the fade holder inside the slot (R1: build 3's
        // union passed while the black panel on screen reached ~1112, so whatever the holder draws counts). A missing
        // bound is a FAIL, not a pass. Not filling the slot across or down (the card keeps its aspect) is a WARN line.
        void VerifyCardPlacement(RE::GFxMovieView* a_movie, RE::GFxValue& a_root, RE::GFxValue& a_card)
        {
            constexpr double kSlack = 2.0;
            const auto seen = VisibleUnion(a_movie, a_card, a_root);
            const auto whole = BoundsIn(a_card, a_root);
            RE::GFxValue holder;
            const auto held = a_card.GetMember("_parent", &holder) && holder.IsDisplayObject() ? BoundsIn(holder, a_root) : std::nullopt;
            const auto slot = CardRect();
            if (!seen) {
                SelfTest("P4 FAIL - no visible part of the item card to read back in _root");
                return;
            }
            const auto inside = [&](const std::optional<Bounds>& a_bounds) {
                return a_bounds && a_bounds->xMin >= slot.left - kSlack && a_bounds->xMax <= slot.right + kSlack &&
                       a_bounds->yMin >= slot.top - kSlack && a_bounds->yMax <= slot.bottom + kSlack;
            };
            const auto& u = seen->bounds;
            const bool left = u.xMin >= slot.left - kSlack, right = u.xMax <= slot.right + kSlack, top = u.yMin >= slot.top - kSlack,
                       bottom = u.yMax <= slot.bottom + kSlack;
            const bool topAligned = std::abs(u.yMin - slot.top) <= kSlack;
            const bool wholeIn = inside(whole), heldIn = inside(held);
            const bool fills = std::abs(u.xMax - u.xMin - slot.Width()) <= kSlack || std::abs(u.yMax - u.yMin - slot.Height()) <= kSlack;
            const bool unionIn = left && right && top && bottom && topAligned;
            const bool pass = unionIn && wholeIn && heldIn;
            // the fit places the visible union only: when only the rest is outside, the line says so (second review I4)
            const char* why = pass ? "" : unionIn ? " - hidden part outside, not fitted (the fit counts the visible union only)" : "";
            SelfTest(std::format("P4 {}{} - item card visible union in _root {}, slot {:.0f},{:.0f}..{:.0f},{:.0f} (left {} right {} top {} "
                                 "bottom {}, top-aligned {}, fills {}); whole card {} (inside {}); holder {} (inside {}); parts {}",
                pass ? "PASS" : "FAIL", why, BoundsText(u), slot.left, slot.top, slot.right, slot.bottom, left, right, top, bottom, topAligned,
                fills, BoundsText(whole), wholeIn, BoundsText(held), heldIn, seen->parts));
            if (!fills) {
                SelfTest(std::format("P4 WARN - the visible union {:.1f} x {:.1f} fills the slot {:.0f} x {:.0f} neither across nor down",
                    u.xMax - u.xMin, u.yMax - u.yMin, slot.Width(), slot.Height()));
            }
        }

        // The display-object children of a_container with their _root bounds ("name x,y..x,y", "(hidden)" when not drawn)
        std::string ChildBounds(RE::GFxMovieView* a_movie, RE::GFxValue& a_container, RE::GFxValue& a_root)
        {
            const auto target = StringMember(a_container, "_target");
            std::vector<std::string> names;
            a_container.VisitMembers([&](const char* a_name, const RE::GFxValue& a_value) {
                if (a_name && a_value.IsDisplayObject()) names.emplace_back(a_name);
            });
            std::string out;
            for (const auto& name : names) {
                RE::GFxValue child;
                if (!a_container.GetMember(name.c_str(), &child) || !child.IsDisplayObject() || !ChildOf(child, target)) continue;
                const auto box = PartBounds(a_movie, a_container, child, a_root);
                out += std::format("{}{} {}{}", out.empty() ? "" : ", ", name, BoundsText(box), PartShows(child) ? "" : " (hidden)");
            }
            return out.empty() ? "none" : out;
        }

        // During a measure window, once per change: where the holder's children, the whole card, Menu_mc and Menu_mc's
        // children are in _root, so the clip of a panel drawn outside the slot can be named from the log (R1)
        void LogCardLayout(RE::GFxMovieView* a_movie, RE::GFxValue& a_root, RE::GFxValue& a_card, RE::GFxValue& a_holder)
        {
            if (g_menu.cardLayoutLogs >= kMaxCardLayoutLogs || g_menu.cardLayoutTick++ % kCardLayoutEvery != 0) return;
            RE::GFxValue menu;
            const bool hasMenu = a_holder.GetMember("_parent", &menu) && menu.IsDisplayObject();
            auto layout = std::format("holder {} [{}]; card {}; {} {} [{}]", BoundsText(BoundsIn(a_holder, a_root)),
                ChildBounds(a_movie, a_holder, a_root), BoundsText(BoundsIn(a_card, a_root)), hasMenu ? StringMember(menu, "_target") : "(no parent)",
                hasMenu ? BoundsText(BoundsIn(menu, a_root)) : "none", hasMenu ? ChildBounds(a_movie, menu, a_root) : "none");
            if (layout == g_menu.cardLayout) return;
            g_menu.cardLayout = std::move(layout);
            ++g_menu.cardLayoutLogs;
            logger::info("Widget: item card layout in _root (#{}): {}", g_menu.cardLayoutLogs, g_menu.cardLayout);
        }

        // The card's visible union changed size or place inside the holder (SkyUI lays the card out per item, the
        // external background loads late): measured in the holder's own space, so where SkyUI puts the holder does not count
        bool CardContentChanged(RE::GFxMovieView* a_movie, RE::GFxValue& a_card, RE::GFxValue& a_holder, const CardFit& a_fit)
        {
            constexpr double kChange = 1.0;
            const auto seen = VisibleUnion(a_movie, a_card, a_holder);
            if (!seen) return false;
            const auto& u = seen->bounds;
            const std::array<double, 4> now{ u.xMin, u.xMax, u.yMin, u.yMax };
            for (std::size_t i = 0; i < now.size(); ++i) {
                if (std::abs(now[i] - a_fit.local[i]) > kChange) return true;
            }
            return false;
        }

        // A new item or frame on the card: measure every frame again for kCardMeasureFrames
        void WatchCardContent(const RE::GFxValue& a_card)
        {
            auto signature = CardSignature(a_card);
            if (signature == g_menu.cardSignature) return;
            g_menu.cardSignature = std::move(signature);
            g_menu.cardMeasureLeft = kCardMeasureFrames;
        }

        // Puts our transform on the holder when its _x/_y/_xscale/_yscale (parent space, where both SkyUI and we
        // write) differ from it. True when it wrote.
        bool ApplyCardFit(RE::GFxValue& a_holder, const CardFit& a_fit, bool a_fresh)
        {
            constexpr double kTolerance = 0.5;
            const auto nowX = Number(a_holder, "_x"), nowY = Number(a_holder, "_y"), nowXs = Number(a_holder, "_xscale"),
                       nowYs = Number(a_holder, "_yscale");
            const auto Matches = [&](const std::optional<double>& a_now, double a_want) { return a_now && std::abs(*a_now - a_want) < kTolerance; };
            if (Matches(nowX, a_fit.x) && Matches(nowY, a_fit.y) && Matches(nowXs, a_fit.scale) && Matches(nowYs, a_fit.scale)) return false;
            a_holder.SetMember("_x", a_fit.x);
            a_holder.SetMember("_y", a_fit.y);
            a_holder.SetMember("_xscale", a_fit.scale);
            a_holder.SetMember("_yscale", a_fit.scale);
            ++g_menu.fits;
            if (!a_fresh && g_menu.fits > 1) ++g_menu.movedAway;
            if (g_menu.fits <= kMaxFitLogs) {
                logger::info("Widget: item card {} (#{}, moved away {} times): holder was {:.1f},{:.1f} {:.1f}%",
                    a_fresh ? "placed" : "put back after something moved it", g_menu.fits, g_menu.movedAway, nowX.value_or(-1),
                    nowY.value_or(-1), nowXs.value_or(-1));
            }
            return true;
        }

        // A measure window ended: the P4 check, once per window in which our transform was applied
        void EndCardMeasure(RE::GFxMovieView* a_movie, RE::GFxValue& a_root, RE::GFxValue& a_card)
        {
            const bool due = g_menu.cardVerifyDue || !g_menu.cardFit;
            g_menu.cardVerifyDue = false;
            if (!due || !Settings::Get().selfTest || ++g_menu.cardVerifies > kMaxCardVerifyLogs) return;
            if (!g_menu.cardFit) {
                SelfTest("P4 FAIL - the item card was not fitted in its measure window (no visible part measured in the holder)");
                return;
            }
            VerifyCardPlacement(a_movie, a_root, a_card);
        }

        struct RootText
        {
            double size{}, yscale{}, toRoot{}, stage{};
        };

        // A text field's font size in a_root's units: its getTextFormat().size (the first character's when the field mixes
        // formats, as an HTML field can), times the field's _yscale and its parent clip's scale to a_root (a 100-unit segment
        // mapped through the stage). nullopt when the size or the mapping cannot be read.
        std::optional<RootText> TextSizeIn(RE::GFxMovieView* a_movie, RE::GFxValue& a_parent, RE::GFxValue& a_field, RE::GFxValue& a_root)
        {
            constexpr double kSegment = 100.0, kPercent = 100.0;
            RE::GFxValue format;
            std::optional<double> size;
            if (a_field.Invoke("getTextFormat", &format) && format.IsObject()) size = Number(format, "size");
            if (!size) {
                const std::array<RE::GFxValue, 2> firstChar{ 0.0, 1.0 };
                if (a_field.Invoke("getTextFormat", &format, firstChar) && format.IsObject()) size = Number(format, "size");
            }
            const auto x = Number(a_field, "_x"), y = Number(a_field, "_y");
            if (!size || *size <= 0 || !x || !y) return std::nullopt;
            const double yscale = Number(a_field, "_yscale").value_or(kPercent) / kPercent;
            const auto a = MapPoint(a_movie, a_parent, a_root, *x, *y);
            const auto b = MapPoint(a_movie, a_parent, a_root, *x, *y + kSegment * yscale);
            if (!a || !b) return std::nullopt;
            const double toRoot = std::hypot(b->first - a->first, b->second - a->second) / kSegment;  // the field's _yscale included
            return RootText{ *size, yscale, yscale > 0 ? toRoot / yscale : 0.0, *size * toRoot };
        }

        // Once per menu, at the end of a measure window with the card fitted: how large the card's description text is in
        // _root units, next to the list's rows (round 2 #5: fitted at about 0.58 its text is small; the layout stays, the log
        // says how small)
        void LogCardTextSize(RE::GFxMovieView* a_movie, RE::GFxValue& a_root, RE::GFxValue& a_card)
        {
            if (g_menu.cardTextLogged || !g_menu.cardFit) return;
            for (const auto* name : kCardTextFields) {
                RE::GFxValue field;
                if (!a_card.GetMember(name, &field) || !field.IsDisplayObject() || !PartShows(field)) continue;
                const auto text = TextSizeIn(a_movie, a_card, field, a_root);
                if (!text) continue;
                g_menu.cardTextLogged = true;
                logger::info("Widget: Q4 item card text {} - format size {:.1f}, field _yscale {:.2f}, card to _root x{:.3f} (holder scale "
                             "{:.1f}%) -> {:.1f} stage units; list rows {:.1f}{}",
                    name, text->size, text->yscale, text->toRoot, g_menu.cardFit->scale, text->stage, TextFit::Size(Metrics(), 0.0),
                    g_menu.rowSize > 0 ? "" : " (not measured yet: the fallback)");
                return;
            }
        }

        // Every frame: SkyUI places and slides the holder itself, so our transform is put back whenever the holder
        // differs from it. The card's visible union is measured every frame for kCardMeasureFrames after the menu
        // opens and after each content change (new item or frame), every kPollFrames frames otherwise; the transform
        // is worked out again when the union changes or the tab moves the slot. The P4 check runs at a window's end.
        void FitItemCard(RE::GFxMovieView* a_movie, RE::GFxValue& a_root)
        {
            if (!Settings::Get().moveItemCard || g_menu.irActive || OwnCard() || SpellCardHidden()) return;  // our card replaces SkyUI's (v7 C1)
            auto pair = CardAndHolder(a_movie);
            if (!pair) return;
            auto& [card, holder] = *pair;
            WatchCardContent(card);
            const bool measuring = g_menu.cardMeasureLeft > 0;
            if (measuring || ++g_menu.cardChecks % kPollFrames == 0) {
                FixLevelLabel(card);  // first: the label's new size is part of the union
                if (g_menu.cardFit && CardContentChanged(a_movie, card, holder, *g_menu.cardFit)) {
                    g_menu.cardFit.reset();
                    if (!measuring) g_menu.cardMeasureLeft = kCardMeasureFrames;
                    if (++g_menu.refits <= kMaxFitLogs) logger::info("Widget: item card content changed size - fitted again (#{})", g_menu.refits);
                }
            }
            const bool fresh = !g_menu.cardFit;
            if (fresh) g_menu.cardFit = ComputeCardFit(a_movie, a_root, card, holder);
            if (g_menu.cardFit && ApplyCardFit(holder, *g_menu.cardFit, fresh)) {
                g_menu.cardVerifyDue = true;
                if (g_menu.cardMeasureLeft == 0) g_menu.cardMeasureLeft = kCardSettleFrames;
            }
            if (g_menu.cardMeasureLeft > 0) LogCardLayout(a_movie, a_root, card, holder);
            if (g_menu.cardMeasureLeft > 0 && --g_menu.cardMeasureLeft == 0) {
                LogCardTextSize(a_movie, a_root, card);
                EndCardMeasure(a_movie, a_root, card);
            }
        }

        // ---------------------------------------------------------------------
        // Item card for a tree click on a spell the list does not show (spec v4 P2)
        // ---------------------------------------------------------------------
        // SkyUI fills the card only from a list highlight (ItemMenu.as:326-357 onItemHighlightChange -> GameDelegate
        // "RequestItemCardInfo" -> UpdateItemCardInfo(obj) -> itemCard.itemInfo = obj, ItemMenu.as:228-232), so a spell
        // that is not in the list never reached it (LEDGER Confirmed 4). We call the card's itemInfo setter
        // (ItemCard.as:177-455) ourselves; not UpdateItemCardInfo, which also rewrites the bottom bar.
        // Thread: the frame handler runs inside the movie's Advance, where the game's own RequestItemCardInfo callback
        // fills the card natively too: SetForm, CalculateMagickaCost and GetMagicItemDescription are called from the
        // context the game calls them from, and the GFx write has to be here anyway.

        std::string KeysOf(const RE::GFxValue& a_object)
        {
            std::string keys;
            a_object.VisitMembers([&](const char* a_name, const RE::GFxValue&) {
                if (a_name) keys += std::format("{}{}", keys.empty() ? "" : " ", a_name);
            });
            return keys;
        }

        bool Has(const RE::GFxValue& a_object, const char* a_member)
        {
            RE::GFxValue value;
            return a_object.IsObject() && a_object.GetMember(a_member, &value) && !value.IsUndefined() && !value.IsNull();
        }

        // A member as log text: a number, a quoted string, or "-"
        std::string MemberText(const RE::GFxValue& a_object, const char* a_member)
        {
            if (const auto number = Number(a_object, a_member)) return std::format("{:g}", *number);
            return Has(a_object, a_member) ? "'" + StringMember(a_object, a_member) + "'" : "-";
        }

        void PutString(RE::GFxMovieView* a_movie, RE::GFxValue& a_object, const char* a_member, const std::string& a_text)
        {
            RE::GFxValue value;
            a_movie->CreateString(&value, a_text.c_str());
            a_object.SetMember(a_member, value);
        }

        // 1. The game's own filler: the menu's native card (CommonLib ItemCard.h:21 SetForm, IDs 51020/51898; MagicMenu.h
        //    RUNTIME_DATA itemCard) filled for this spell and its obj handed to SkyUI's setter (Inventory Reforged does the
        //    same with SetItem for its outfit rows, main.cpp OutfitCard; our own code). UNVERIFIED that SetForm fills a
        //    spell's card fields, so it is used only when type, name and effects are there. It reuses the game's own card
        //    object; the next list highlight fills it again (ItemMenu.as:357).
        std::optional<RE::GFxValue> GameCardInfo(RE::GFxMovieView* a_movie, RE::SpellItem* a_spell)
        {
            auto* ui = RE::UI::GetSingleton();
            auto menu = ui ? ui->GetMenu<RE::MagicMenu>() : nullptr;
            auto* native = menu ? menu->GetRuntimeData().itemCard : nullptr;
            if (!native || native->view.get() != a_movie) {
                SelfTest(std::format("P2 SetForm skipped - {}", native ? "the native card is on another movie" : "no native item card"));
                return std::nullopt;
            }
            native->SetForm(a_spell);
            const auto& obj = native->obj;
            const bool usable = Has(obj, "type") && Has(obj, "name") && Has(obj, "effects");
            SelfTest(std::format("P2 SetForm keys [{}] type {} name {} effects {} castLevel {} spellCost {} castTime {} -> {}",
                obj.IsObject() ? KeysOf(obj) : "no object", MemberText(obj, "type"), MemberText(obj, "name"), MemberText(obj, "effects"),
                MemberText(obj, "castLevel"), MemberText(obj, "spellCost"), MemberText(obj, "castTime"),
                usable ? "used" : "not used (type, name or effects missing)"));
            if (!usable) return std::nullopt;
            return obj;
        }

        // Vanilla's level names (Skyrim.esm GMSTs, read with houseCARL 2026-10-25: sNoviceDisplayName, sApprentice-,
        // sJourneyman- (the Adept tier), sExpert-, sMasterDisplayName) by the costliest effect's minimum skill level
        // (CommonLib MagicItem.h:116 GetCostliestEffectItem, EffectSetting.h:140 GetMinimumSkillLevel). The cut points
        // are the vanilla spells' levels, a convention, not read from the engine; UNVERIFIED that the game's card uses
        // these strings. Empty when no string is found (logged by the caller).
        std::string LevelText(const RE::SpellItem* a_spell)
        {
            constexpr std::array<std::pair<std::int32_t, const char*>, 5> kLevels{ { { 0, "sNoviceDisplayName" },
                { 25, "sApprenticeDisplayName" }, { 50, "sJourneymanDisplayName" }, { 75, "sExpertDisplayName" },
                { 100, "sMasterDisplayName" } } };
            const auto* costliest = a_spell->GetCostliestEffectItem();
            if (!costliest || !costliest->baseEffect) return {};
            const auto level = costliest->baseEffect->GetMinimumSkillLevel();
            const char* name = kLevels.front().second;
            for (const auto& [from, setting] : kLevels) {
                if (level >= from) name = setting;
            }
            return GameString(name);
        }

        // 2. Built the way SkyUI reads a spell card (ItemCard.as:259-274): type 7, name, effects (finished HTML),
        //    castLevel (text), spellCost, castTime (0 picks the per-second cost frame)
        RE::GFxValue BuiltCardInfo(RE::GFxMovieView* a_movie, RE::SpellItem* a_spell)
        {
            RE::GFxValue info;
            a_movie->CreateObject(&info);
            RE::BSString description;
            // MagicSystem.h:100 (IDs 11299/11427); the tag formats vanilla passes are UNVERIFIED: none, magnitudes unmarked
            RE::MagicSystem::GetMagicItemDescription(description, a_spell, "", "");
            const std::string effects = description.c_str() ? description.c_str() : "";
            const std::string level = LevelText(a_spell);
            auto* player = RE::PlayerCharacter::GetSingleton();
            // a whole number as the list shows it (the UI's own rounding is UNVERIFIED)
            const double cost = player ? std::round(a_spell->CalculateMagickaCost(player)) : 0.0;
            const bool perSecond = a_spell->GetCastingType() == RE::MagicSystem::CastingType::kConcentration;
            const char* name = a_spell->GetName();
            PutString(a_movie, info, "name", name ? name : "");
            PutString(a_movie, info, "effects", effects);
            PutString(a_movie, info, "castLevel", level);
            info.SetMember("type", kCardTypeSpell);
            info.SetMember("spellCost", cost);
            info.SetMember("castTime", perSecond ? 0.0 : 1.0);
            SelfTest(std::format("P2 built card: effects '{}', castLevel '{}'{}, spellCost {:.0f}, castTime {}", effects, level,
                level.empty() ? " (no game string found)" : "", cost, perSecond ? "0 (concentration: per second)" : "1"));
            return info;
        }

        struct CardRead
        {
            std::string info;   // itemInfo.name through SkyUI's getter
            std::string last;   // LastUpdateObj.name: set at the setter's end (ItemCard.as:454), so the setter ran
            std::string drawn;  // the name field's text (ItemText.ItemTextField, ItemCard.as:445-449)
        };

        CardRead ReadCard(const RE::GFxValue& a_card)
        {
            CardRead read;
            RE::GFxValue info, text, field;
            if (a_card.GetMember("itemInfo", &info)) read.info = StringMember(info, "name");
            read.last = StringMember(CardInfo(a_card), "name");
            if (a_card.GetMember("ItemText", &text) && text.IsObject() && text.GetMember("ItemTextField", &field)) {
                read.drawn = StringMember(field, "text");
            }
            return read;
        }

        // The setter took the spell and the card draws its name (SkyUI upper-cases it unless upperCaseName is false,
        // ItemCard.as:447; our Upper is ASCII only, enough for the Korean and English names here)
        bool Shows(const CardRead& a_read, const std::string& a_name)
        {
            return a_read.last == a_name && (a_read.drawn == a_name || a_read.drawn == Upper(a_name));
        }

        void ShowTreeSpellCard(RE::GFxMovieView* a_movie, RE::SpellItem* a_spell)
        {
            const char* rawName = a_spell->GetName();
            const std::string name = rawName ? rawName : "";
            auto card = FindCard(a_movie);
            if (!card) {
                SelfTest(std::format("P2 FAIL - no item card on the movie for {:08X} {}", a_spell->GetFormID(), name));
                return;
            }
            auto info = GameCardInfo(a_movie, a_spell);
            const bool game = info.has_value();
            if (!game) info = BuiltCardInfo(a_movie, a_spell);
            const bool set = card->SetMember("itemInfo", *info);
            card->Invoke("FadeInCard");  // ItemCard.as:145-152: does nothing when the card is already in
            // FadeInCard flips only the card's own bFadedIn. The menu keeps its own flag (ItemMenu.as:344-367): with it
            // false (no list selection when we showed our card) a later highlight of -1 skips onHideItemsList and our
            // card stays up (review G3), so it is set the way onItemHighlightChange sets it before its FadeInCard
            RE::GFxValue menu, flag;
            const bool hasFlag = a_movie->GetVariable(&menu, kMenuPath) && menu.IsObject() && menu.GetMember(kCardFadeFlag, &flag) && flag.IsBool();
            const bool wasIn = hasFlag && flag.GetBool();
            if (hasFlag && !wasIn) menu.SetMember(kCardFadeFlag, true);
            g_menu.cardFromTree = a_spell->GetFormID();
            g_menu.cardFromTreeTab = g_menu.tabFlag;
            g_menu.cardHoldLeft = kP2HoldFrames;
            logger::info("Widget: P2 card up; {}.{} {}", kMenuPath, kCardFadeFlag,
                !hasFlag ? "not found (not SkyUI's ItemMenu): the no-selection case is watched by our frame handler only" :
                wasIn    ? "was already true" :
                           "was false - set true, so SkyUI's own no-selection path fades the card out");
            const auto read = ReadCard(*card);
            SelfTest(std::format("P2 {} - card for {:08X} '{}' from {} (SetMember {}): itemInfo.name '{}', LastUpdateObj.name '{}', "
                                 "drawn name '{}'",
                Shows(read, name) ? "PASS" : "FAIL", a_spell->GetFormID(), name, game ? "the game's SetForm" : "our built object",
                set ? "ok" : "failed", read.info, read.last, read.drawn));
        }

        // kP2HoldFrames after we filled the card: does it still show the tree spell? (SkyUI's AS2 has no loop that refills
        // the card, codex_card_result.md 1; a native refresh is UNVERIFIED, this line answers it)
        void HoldCardCheck(RE::GFxMovieView* a_movie)
        {
            if (!g_menu.cardFromTree || g_menu.cardHoldLeft <= 0 || --g_menu.cardHoldLeft > 0) return;
            auto card = FindCard(a_movie);
            auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(g_menu.cardFromTree);
            if (!card || !spell) return;
            const char* rawName = spell->GetName();
            const std::string name = rawName ? rawName : "";
            const auto read = ReadCard(*card);
            SelfTest(std::format("P2 hold {} - {} frames later the card shows '{}' (drawn '{}'), wanted '{}'",
                Shows(read, name) ? "PASS" : "FAIL", kP2HoldFrames, read.last, read.drawn, name));
        }

        // ---------------------------------------------------------------------
        // The 3D preview for the same click (spec v4 P3: a test, behind bPreviewUnlearned)
        // ---------------------------------------------------------------------
        // Inventory3DManager::LoadInventoryItem(object, extra) (CommonLib Inventory3DManager.cpp, IDs 50885/51758; a
        // SpellItem is a TESBoundObject through MagicItem), called from the frame handler, inside the movie's Advance:
        // where the game's own UpdateItem3D runs (SkyUI's GameDelegate "UpdateItem3D", ItemMenu.as:352) and where
        // Inventory Reforged calls it (its OutfitModel GFx function, main.cpp:789). Build 4 first ran it as an SKSE
        // main-thread task while the frame handler ran on another thread (game log 2026-10-25: 27728 and 23368; second
        // review R1/R2); load and checks are frame-handler only now, so nothing queued can outlive a cancel.
        // UNVERIFIED that the game builds a magic preview this way: P3 PASS when a loadedModels entry whose itemBase is
        // the spell appears within kP3Frames frames. The list path puts the list's own model back (SkyUI's UpdateItem3D
        // on its highlight, ItemMenu.as:335-336).

        void LoadUnlearned(RE::FormID a_spell)
        {
            auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(a_spell);
            auto* manager = RE::Inventory3DManager::GetSingleton();
            if (!spell || !manager) {
                g_p3.failed = true;
                SelfTest(std::format("P3 FAIL - {:08X}: {}; no more tries this menu", a_spell, !spell ? "not a spell" : "no Inventory3DManager"));
                return;
            }
            // written through before the call: an UNVERIFIED engine call on by default, so a crash in it leaves this line (R7)
            logger::info("Widget: P3 calling LoadInventoryItem({:08X} {}, no extra) on thread {} (frame handler)", a_spell, spell->GetName(),
                REX::W32::GetCurrentThreadId());
            FlushLog();
            manager->LoadInventoryItem(spell, nullptr);
            g_p3.checks = 0;
            g_p3.spell = a_spell;
            logger::info("Widget: P3 LoadInventoryItem returned for {:08X}; its model is checked for {} frames", a_spell, kP3Frames);
        }

        RE::NiAVObject* UnlearnedModel(RE::Inventory3DManager& a_manager, RE::FormID a_spell)
        {
            for (const auto& loaded : a_manager.GetRuntimeData().loadedModels) {
                if (loaded.spModel && loaded.itemBase && loaded.itemBase->GetFormID() == a_spell) return loaded.spModel.get();
            }
            return nullptr;
        }

        // Every frame: the waiting spell's model is up -> PASS; the last of kP3Frames frames without it -> FAIL, and no
        // more tries for the rest of this menu
        void CheckUnlearned()
        {
            const auto spell = g_p3.spell;
            if (!spell) return;
            const int check = ++g_p3.checks;
            auto* manager = RE::Inventory3DManager::GetSingleton();
            auto* model = manager ? UnlearnedModel(*manager, spell) : nullptr;
            if (!model && check < kP3Frames) return;
            g_p3.spell = 0;
            if (model) {
                SelfTest(std::format("P3 PASS - {:08X}: model '{}' up after {} frames", spell, model->name.c_str(), check));
                return;
            }
            g_p3.failed = true;
            int count = 0;
            if (manager) {
                for (const auto& loaded : manager->GetRuntimeData().loadedModels) count += loaded.spModel ? 1 : 0;
            }
            SelfTest(std::format("P3 FAIL - {:08X}: no loadedModels entry with it as itemBase in {} frames ({} models up); no more "
                                 "tries this menu",
                spell, kP3Frames, count));
        }

        void CancelUnlearnedPreview(const char* a_why)
        {
            if (const auto spell = std::exchange(g_p3.spell, 0)) logger::info("Widget: P3 for {:08X} not judged - {}", spell, a_why);
        }

        void RequestUnlearnedPreview(RE::FormID a_spell)
        {
            if (!g_p3.allowed || g_p3.failed) return;
            CancelUnlearnedPreview("a newer tree click");
            LoadUnlearned(a_spell);
        }

        // A new menu: P3 starts again (a FAIL stops it for one menu only)
        void ResetUnlearnedPreview(bool a_irActive)
        {
            g_p3 = UnlearnedPreview{};
            const bool setting = Settings::Get().previewUnlearned, vr = REL::Module::IsVR();
            g_p3.allowed = setting && !vr && !a_irActive;
            logger::info("Widget: a tree click on a spell the list does not show fills the item card (P2) and {} its 3D (P3: {})",
                g_p3.allowed ? "asks for" : "does not ask for",
                !setting   ? "bPreviewUnlearned=0" :
                vr         ? "VR, its loadedModels has another layout" :
                a_irActive ? "Inventory Reforged runs its panel in this menu" :
                             "LoadInventoryItem in the frame handler, checked for 30 frames");
        }

        // Back to the list's own card: SkyUI's own highlight event for the current selection (the shape InventoryLists
        // sends, InventoryLists.as:327/397/500), heard by ItemMenu.onItemHighlightChange, which asks for the card and
        // the 3D again (ItemMenu.as:326-357). Setting the same selectedIndex again does nothing: doSetSelectedIndex
        // returns early for the same index (ScrollingList.as:326-329). No selection (-1): SkyUI's own no-selection path
        // (onHideItemsList: UpdateItem3D(false) and FadeOutCard, ItemMenu.as:375-379), so a P3 model goes too. A P3
        // check still waiting is not judged (second review X1)
        void RestoreListCard(RE::GFxMovieView* a_movie)
        {
            const auto shown = std::exchange(g_menu.cardFromTree, 0);
            g_menu.cardHoldLeft = 0;
            CancelUnlearnedPreview("the tree card left");
            RE::GFxValue lists, list, index;
            if (!a_movie->GetVariable(&lists, kListsPath) || !lists.IsObject() || !lists.GetMember("itemList", &list) ||
                !list.IsObject() || !list.GetMember("selectedIndex", &index) || !index.IsNumber()) {
                logger::info("Widget: item card back to the list - no {}.itemList.selectedIndex (the card keeps {:08X})", kListsPath, shown);
                return;
            }
            // spec v12 G2: the highlight may sit on a hovered row (or -1 after PageUp/PageDown) while the committed selection
            // is another; the game's card is of the list's selected entry (the request carries no FormID, ItemMenu.as:357), so
            // the list is put back on the committed row - marked as ours, which sends the original for it
            if (g_menu.selWrapped && g_menu.sel.index >= 0 && g_menu.sel.id && static_cast<int>(index.GetNumber()) != g_menu.sel.index) {
                if (const auto pick = SelectInList(g_menu.sel.id, g_menu.sel.source); pick.took) {
                    logger::info("Widget: item card back to the list - the list put back on the selection {:08X} (index {}; was {:08X})",
                        g_menu.sel.id, pick.index, shown);
                    return;
                }
            }
            if (index.GetNumber() < 0) {
                // the menu's own flag goes with it (onItemHighlightChange's -1 path, ItemMenu.as:363-365), or its next
                // highlight would take the card for faded in and not fade it in (ShowTreeSpellCard may have set it)
                RE::GFxValue menu, flag;
                const bool skyui = a_movie->GetVariable(&menu, kMenuPath) && menu.IsObject();
                if (skyui && menu.GetMember(kCardFadeFlag, &flag) && flag.IsBool()) menu.SetMember(kCardFadeFlag, false);
                const bool hidden = skyui && menu.Invoke("onHideItemsList");
                if (!hidden) {
                    if (auto card = FindCard(a_movie)) card->Invoke("FadeOutCard");  // ItemCard.as:154-160
                }
                logger::info("Widget: item card back to the list - no list selection, {}", hidden ? "SkyUI's onHideItemsList (3D off, card faded out)" :
                                                                                                      "card faded out (no onHideItemsList; the 3D stays)");
                return;
            }
            RE::GFxValue event, type;
            a_movie->CreateObject(&event);
            a_movie->CreateString(&type, "itemHighlightChange");
            event.SetMember("type", type);
            event.SetMember("index", index);
            const bool outer = std::exchange(g_menu.highlightOurs, true);  // review v12 #S1: ours, the highlight wrap passes it
            const bool sent = lists.Invoke("dispatchEvent", nullptr, &event, 1);
            g_menu.highlightOurs = outer;
            logger::info("Widget: item card back to the list - itemHighlightChange for index {:.0f} {} (was {:08X})", index.GetNumber(),
                sent ? "sent" : "NOT sent", shown);
        }

        // While our tree card (P2) is up: the menu faded the card out itself (its no-selection path, onHideItemsList), or
        // a category change left the list with no selection (-1, where SkyUI sends no highlight our WatchSelection
        // would see: FormID 0 -> 0) - fade out and drop our state (review G3)
        void WatchTreeCard(RE::GFxMovieView* a_movie)
        {
            if (!g_menu.cardFromTree) return;
            auto card = FindCard(a_movie);
            RE::GFxValue in, index;
            if (card && card->GetMember("bFadedIn", &in) && in.IsBool() && !in.GetBool()) {
                const auto shown = std::exchange(g_menu.cardFromTree, 0);
                g_menu.cardHoldLeft = 0;
                CancelUnlearnedPreview("the menu faded the tree card out");
                logger::info("Widget: item card faded out by the menu (no list selection) - our tree card for {:08X} dropped", shown);
                return;
            }
            if (g_menu.tabFlag == g_menu.cardFromTreeTab) return;
            // review v12 #S3: with the highlight wrap the category's -1 is swallowed and the tree card (a selection) stays
            if (g_menu.highlightWrapped) return;
            if (a_movie->GetVariable(&index, kListIndexPath) && index.IsNumber() && index.GetNumber() < 0) {
                logger::info("Widget: category changed to one with no list selection while our tree card was up");
                RestoreListCard(a_movie);  // fades the card out for -1, drops our state and P3's wait
            }
        }


        // P2/P3 for a tree click: a spell the list does not select (not in it, or filtered out by the tab) gets our card
        // and the 3D request; one the list took gets the list's own card back if ours was showing. A spell whose name HoM
        // keeps hidden (a_nameShown false) gets neither: the item card would name it and list its effects, the 3D show it
        // (review v11 #1; HoM spellCardView.js:194/213-228) - a tree card of an earlier click goes back to the list's
        void FollowClick(RE::GFxMovieView* a_movie, RE::FormID a_id, const ListPick& a_pick, bool a_nameShown)
        {
            if (a_pick.took) {
                CancelUnlearnedPreview("the list took the clicked spell");
                // only when the list already had this spell: the same index sends no highlight (ScrollingList.as:326-329);
                // a real change sends SkyUI's own (InventoryLists.as:500) and WatchSelection drops our card
                if (g_menu.cardFromTree && a_id == g_menu.listSelection) RestoreListCard(a_movie);
                return;
            }
            if (!a_nameShown) {
                CancelUnlearnedPreview("the clicked spell's name is hidden");
                if (g_menu.cardFromTree) RestoreListCard(a_movie);
                return;
            }
            auto* spell = a_id ? RE::TESForm::LookupByID<RE::SpellItem>(a_id) : nullptr;
            if (!spell) return;
            ShowTreeSpellCard(a_movie, spell);
            RequestUnlearnedPreview(a_id);
        }

        void SetHandler(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, const char* a_event, RE::GFxFunctionHandler* a_handler)
        {
            RE::GFxValue fn;
            a_movie->CreateFunction(&fn, a_handler);
            a_clip.SetMember(a_event, fn);
        }

        RE::GFxValue Child(RE::GFxValue& a_parent, const char* a_name)
        {
            RE::GFxValue child;
            if (!a_parent.GetMember(a_name, &child) || !child.IsDisplayObject()) return {};
            return child;
        }

        // The block's panel clip name: the spell card's panel is "infocard" (v7 C1), the others are named by their block id
        const char* PanelName(const BlockId& a_id)
        {
            if (a_id == Layout::kSpellCard) return "infocard";
            if (const auto index = AddonIndex(a_id)) return g_addonClips[*index].c_str();  // Task 6: ids may hold '.'
            return a_id.c_str();
        }

        // blocks plan Task 3: each column is a clip of the widget (kColumnClips) masked by a sibling clip (kColumnMasks) that
        // covers the column's viewport and stays put; the column clip sits at its viewport's top-left moved up by the scroll
        constexpr std::array<const char*, Layout::kColumns> kColumnClips{ "column0", "column1" };
        constexpr std::array<const char*, Layout::kColumns> kColumnMasks{ "columnMask0", "columnMask1" };
        constexpr std::uint32_t kMaskFill = 0xFFFFFF;  // a mask's shape counts, not its colour

        // blocks plan Task 3 / D4: the clip a block's panel is made in - its column's clip (not a display object when the column
        // clips are missing: AttachWidget makes them first and stops without them)
        RE::GFxValue BlockParent(RE::GFxValue& a_widget, const BlockId& a_id)
        {
            const auto column = ColumnOf(a_id);
            return column ? Child(a_widget, kColumnClips[*column]) : RE::GFxValue{};
        }

        // D4: every lookup of a block's panel goes through here
        RE::GFxValue BlockClip(RE::GFxValue& a_widget, const BlockId& a_id)
        {
            auto parent = BlockParent(a_widget, a_id);
            return parent.IsDisplayObject() ? Child(parent, PanelName(a_id)) : RE::GFxValue{};
        }

        // D4: an ON-SCREEN (scrolled) stage rect, as BlockRect gives it, in the coordinates of the block's column clip (at the
        // viewport's top-left, moved up by the scroll): BlockLocal(id, BlockRect(id)) is the panel's _x/_y and stays the same
        // while the column scrolls
        Settings::Rect BlockLocal(const BlockId& a_id, const Settings::Rect& a_stage)
        {
            const auto column = ColumnOf(a_id);
            if (!column) return a_stage;
            const auto viewport = ColumnViewport(*column);
            const double x = viewport.left, y = viewport.top - g_menu.scroll[*column];
            return { a_stage.left - x, a_stage.top - y, a_stage.right - x, a_stage.bottom - y };
        }

        // D4: the one place a scroll is applied - each column clip at its viewport moved up by its scroll (clamped first: a tab
        // change or a new width can shorten the content), its mask drawn again when the viewport changed
        void PlaceColumns(RE::GFxValue& a_widget)
        {
            for (int column = 0; column < Layout::kColumns; ++column) {
                const auto [content, visible] = ColumnHeights(column);
                const double clamped = Layout::ClampScroll(g_menu.scroll[column], content, visible);
                if (std::abs(clamped - g_menu.scroll[column]) > kRectTolerance) ++g_menu.scrollMoves[column];  // Block API v2 (a press cancels)
                g_menu.scroll[column] = clamped;
                const auto viewport = ColumnViewport(column);
                if (auto clip = Child(a_widget, kColumnClips[column]); clip.IsDisplayObject()) {
                    clip.SetMember("_x", viewport.left);
                    clip.SetMember("_y", viewport.top - g_menu.scroll[column]);
                }
                if (g_menu.masked[column] && SameRect(*g_menu.masked[column], viewport)) continue;
                if (auto mask = Child(a_widget, kColumnMasks[column]); mask.IsDisplayObject()) {
                    Draw::Call(mask, "clear", {});
                    Draw::NoLine(mask);
                    Draw::Rect(mask, viewport.left, viewport.top, viewport.Width(), viewport.Height(), kMaskFill, Draw::kAlphaFull);
                    g_menu.masked[column] = viewport;
                }
            }
        }

        // The column clips and their masks, made before the panels (AttachWidget). False when one could not be made
        bool MakeColumns(RE::GFxValue& a_widget)
        {
            for (int column = 0; column < Layout::kColumns; ++column) {
                auto mask = Draw::Clip(a_widget, kColumnMasks[column]);
                auto clip = Draw::Clip(a_widget, kColumnClips[column]);
                if (!mask.IsDisplayObject() || !clip.IsDisplayObject()) {
                    logger::error("Widget: could not create the column clip {} or its mask - nothing drawn", column);
                    return false;
                }
                const std::array<RE::GFxValue, 1> args{ mask };
                if (!clip.Invoke("setMask", nullptr, args.data(), args.size())) {
                    logger::warn("Widget: column {} setMask failed - the column is not cut to its area", column);
                }
            }
            return true;
        }

        // A box's fill and title (if any) in the top-left corner, drawn in the panel's own graphics
        void DrawPanelBox(RE::GFxMovieView* a_movie, RE::GFxValue& a_panel, const Settings::Rect& a_rect, const char* a_title, double a_alpha)
        {
            // no border (user 2026-10-25: "각창에 노란 태두리있는거 없애"); the fill alone marks the box
            Draw::NoLine(a_panel);
            Draw::Rect(a_panel, 0, 0, a_rect.Width(), a_rect.Height(), kPanelFill, a_alpha);
            if (a_title) {
                Draw::Text(a_movie, a_panel, "title", kPad, kPad / 2, a_rect.Width() - 2 * kPad, kTitleH, a_title, kTitleColor,
                    kTitleSize);
            }
        }

        // A block's box look: the preview's fill per the over-pass (AttachWidget), the titled preview and tree windows
        const char* PanelTitle(const BlockId& a_id)
        {
            if (a_id == Layout::kPreview) return Text(Str::kPreviewTitle);
            if (const auto* addon = Addon(a_id); addon && !addon->title.empty()) return addon->title.c_str();
            return nullptr;
        }

        double PanelAlpha(const BlockId& a_id)
        {
            return a_id == Layout::kPreview ? g_menu.previewAlpha : kPanelAlpha;
        }

        // A block's box clip at its rect
        void Panel(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget, const BlockId& a_id)
        {
            if ((Addon(a_id) || g_menu.hidden.contains(a_id)) && !g_menu.blocksAll.contains(a_id)) return;  // no usable height, or hidden: no panel
            auto parent = BlockParent(a_widget, a_id);
            if (!parent.IsDisplayObject()) return;
            auto panel = Draw::Clip(parent, PanelName(a_id));
            if (!panel.IsDisplayObject()) return;
            if (const auto column = ColumnOf(a_id)) ++g_menu.panelsMade[*column];  // Block API v3: above an add-on's overlay there
            const auto rect = BlockRect(a_id);
            const auto local = BlockLocal(a_id, rect);
            panel.SetMember("_x", local.left);
            panel.SetMember("_y", local.top);
            DrawPanelBox(a_movie, panel, rect, PanelTitle(a_id), PanelAlpha(a_id));
        }

        // A panel's fresh "content" child (the old one removed); not a display object when the panel is missing
        RE::GFxValue Content(RE::GFxValue& a_widget, const BlockId& a_id)
        {
            auto panel = BlockClip(a_widget, a_id);
            if (!panel.IsDisplayObject()) return {};
            Draw::Remove(panel, "content");
            return Draw::Clip(panel, "content");
        }

        void SetVisible(RE::GFxValue& a_widget, const BlockId& a_id, bool a_visible)
        {
            if (auto panel = BlockClip(a_widget, a_id); panel.IsDisplayObject()) panel.SetMember("_visible", a_visible);
        }

        // ---------------------------------------------------------------------
        // Add-on block drawing and clicks (Task 6; Block API v2: drawSelection, click, kMessageRedraw). Everything here runs on
        // the frame thread: the frame handler and the hit clips' handlers (which only record, as the tree's node clicks do)
        // ---------------------------------------------------------------------

        constexpr const char* kAddonHitName = "mmxHit";     // a v2 click block's transparent hit clip, in its panel above the content
        constexpr const char* kAddonHitBlock = "mmxBlock";  // the hit clip's block id
        constexpr int kMaxAddonClickLogs = 20;              // add-on clicks and cancels logged per menu

        std::optional<std::pair<double, double>> MouseAt(RE::GFxMovie* a_movie);  // the pointer on the stage (the layout mode's helpers)

        // An add-on block's content clip in its panel: below its title strip (when it has one) and inside the padding (kPad, or a
        // v3 block's own pad >= 0) - where DrawAddons puts the lent clip, the hit clip and the overlay (x = left, y = top) and
        // the size it hands the callbacks
        BlockRules::ContentBox AddonBoxOf(const Settings::Rect& a_rect, const BlockRegistry::Block& a_addon)
        {
            return BlockRules::ContentBoxOf(a_rect.Width(), a_rect.Height(), !a_addon.title.empty(), kTitleH, a_addon.pad, kPad);
        }

        // The same content clip on screen (stage, scrolled); its top-left is the clip's 0,0
        Settings::Rect AddonContentRect(const BlockId& a_id, const BlockRegistry::Block& a_addon)
        {
            const auto rect = BlockRect(a_id);
            const auto box = AddonBoxOf(rect, a_addon);
            return { rect.left + box.left, rect.top + box.top, rect.left + box.left + box.width, rect.top + box.top + box.height };
        }

        // Block API v3: kFlagOwnMouse - no hit clip, no click call
        bool OwnMouse(const BlockRegistry::Block& a_addon)
        {
            return (a_addon.flags & BlockApi::kFlagOwnMouse) != 0;
        }

        // The form the menu's card shows (the spell research design's "which form"): the tree card's spell when a tree click put
        // one on it (P2), else the committed list selection's form, else 0 - CardSpell's order
        RE::FormID ShownForm()
        {
            if (g_menu.cardFromTree) return g_menu.cardFromTree;
            return g_menu.sel.index >= 0 ? g_menu.sel.id : 0;
        }

        // The host's state a press in the block is judged by (BlockRules::Judge), now
        BlockRules::Moment AddonMoment(const BlockId& a_id, const BlockRegistry::Block& a_addon)
        {
            BlockRules::Moment at;
            const auto column = ColumnOf(a_id);
            if (column && *column >= 0 && *column < Layout::kColumns) {
                at.scrollMoves = g_menu.scrollMoves[*column];
                at.viewport = ColumnViewport(*column);
            }
            const auto draws = g_menu.addonDraws.find(a_id);
            at.draws = draws == g_menu.addonDraws.end() ? 0 : draws->second;
            at.modeChanges = g_menu.layoutModeChanges;
            at.layoutMode = g_menu.layout.active;
            at.content = AddonContentRect(a_id, a_addon);
            return at;
        }

        // The hit clip's press: recorded with the host's state (a press while the layout mode is on is not taken: its covers lie
        // above, this is the second guard)
        class AddonPress final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params& a_params) override
            {
                if (!a_params.thisPtr || g_closing || g_menu.layout.active) return;
                const auto id = StringMember(*a_params.thisPtr, kAddonHitBlock);
                const auto* addon = id.empty() ? nullptr : Addon(id);
                const auto at = MouseAt(a_params.movie);
                if (!addon || !addon->click || OwnMouse(*addon) || !at) return;
                g_menu.addonPress = std::pair{ id, BlockRules::Press{ AddonMoment(id, *addon), { at->first, at->second } } };
            }
        };

        // onRelease (a_over true) and onReleaseOutside: judged now against the press (the host's state at this moment), the call
        // itself made by the frame handler (AddonClicks)
        class AddonRelease final : public RE::GFxFunctionHandler
        {
        public:
            explicit AddonRelease(bool a_over) : _over(a_over) {}

            void Call(Params& a_params) override
            {
                if (g_closing || !g_menu.addonPress) return;
                const auto [id, press] = *std::exchange(g_menu.addonPress, std::nullopt);
                const auto* addon = Addon(id);
                if (!addon) return;
                const auto at = MouseAt(a_params.movie);
                BlockRules::Release release{ AddonMoment(id, *addon), {}, _over && at.has_value() };
                if (at) release.point = { at->first, at->second };
                g_menu.addonClickDue = { id, BlockRules::Judge(press, release), BlockRules::ToContent(press.point, press.at.content),
                    BlockRules::ToContent(release.point, release.at.content) };
            }

        private:
            bool _over;
        };

        RE::GPtr<AddonPress> g_addonPress;
        RE::GPtr<AddonRelease> g_addonRelease, g_addonReleaseOutside;

        // A v2 click block's hit clip made again on top of its panel (above the content just drawn): an alpha-0 rect over the
        // content clip, so every press there reaches AddonPress; no hand cursor (the add-on's buttons are its own drawing)
        void MakeAddonHit(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget, const BlockId& a_id, double a_x, double a_y, double a_w, double a_h)
        {
            auto panel = BlockClip(a_widget, a_id);
            if (!panel.IsDisplayObject()) return;
            Draw::Remove(panel, kAddonHitName);
            auto hit = Draw::Clip(panel, kAddonHitName);
            if (!hit.IsDisplayObject()) {
                logger::warn("Widget: add-on block {} - its hit clip could not be made, it takes no clicks", a_id);
                return;
            }
            hit.SetMember("_x", a_x);
            hit.SetMember("_y", a_y);
            Draw::NoLine(hit);
            Draw::Rect(hit, 0, 0, a_w, a_h, kPanelFill, 0.0);  // alpha 0 still takes the mouse
            PutString(a_movie, hit, kAddonHitBlock, a_id);
            SetHandler(a_movie, hit, "onPress", g_addonPress.get());
            SetHandler(a_movie, hit, "onRelease", g_addonRelease.get());
            SetHandler(a_movie, hit, "onReleaseOutside", g_addonReleaseOutside.get());
            hit.SetMember("useHandCursor", false);
        }

        // An add-on's callback run, whatever it throws caught (another plugin's code must not leave the frame handler): the error
        // text, "" when it returned normally
        template <class Fn>
        std::string GuardedCall(Fn&& a_call)
        {
            try {
                a_call();
            } catch (const std::exception& e) {
                return e.what();
            } catch (...) {
                return "an exception that is not a std::exception";
            }
            return {};
        }

        // An add-on's callback threw (a_callback: "draw", "click", "key", "frame"): no draw, click, key or frame call for the block
        // again this menu, its hit clip removed, the error logged with "SelfTest: <a_test> addon <id> <a_what> FAIL"
        void FailAddon(RE::GFxValue& a_widget, const BlockRegistry::Block& a_addon, const char* a_callback, const char* a_test, const char* a_what,
            const std::string& a_error)
        {
            g_menu.addonFailed.insert(a_addon.id);
            if (auto panel = BlockClip(a_widget, a_addon.id); panel.IsDisplayObject()) Draw::Remove(panel, kAddonHitName);
            logger::error("Widget: add-on block {} (from {}) threw from its {} callback: {} - not drawn or called again this menu", a_addon.id,
                a_addon.sender, a_callback, a_error);
            SelfTest(std::format("{} addon {} {} FAIL - its {} callback threw: {}", a_test, a_addon.id, a_what, a_callback, a_error));
        }

        // An add-on request ('SLMD', 'SLMH', 'SLMS', 'SLMV') names a registered block; one that does not is logged once per game
        // session and request kind (a_request: "redraw request ('SLMD')" ...)
        const BlockRegistry::Block* KnownAddon(const std::string& a_id, const char* a_request)
        {
            if (const auto* addon = Addon(a_id)) return addon;
            static std::set<std::pair<std::string, std::string>> s_unknown;
            if (s_unknown.emplace(a_request, a_id).second) logger::warn("Widget: a {} for {} - no add-on block has that id; ignored (logged once)", a_request, a_id);
            return nullptr;
        }

        // Block API v2: the kMessageRedraw ids queued since the last frame, due for DrawAddons; an id that is no registered block
        // is logged once per game session
        void TakeAddonRedraws()
        {
            std::vector<std::string> ids;
            {
                const std::scoped_lock lock(g_redrawLock);
                ids.swap(g_redrawQueue);
            }
            for (const auto& id : ids) {
                if (KnownAddon(id, "redraw request ('SLMD')") && g_menu.blocksAll.contains(id)) g_menu.addonDrawDue.insert(id);
            }
        }

        // Block API v2: the release judged since the last frame - a click call (content clip coordinates) or the cancel logged
        void AddonClicks(RE::GFxValue& a_widget)
        {
            if (!g_menu.addonClickDue) return;
            const auto due = *std::exchange(g_menu.addonClickDue, std::nullopt);
            const auto* addon = Addon(due.id);
            if (!addon || !addon->click || OwnMouse(*addon) || g_menu.addonFailed.contains(due.id)) return;
            const bool log = ++g_menu.addonClickLogs <= kMaxAddonClickLogs;
            const char* more = g_menu.addonClickLogs == kMaxAddonClickLogs ? " - later ones this menu not logged" : "";
            // judged at the release; the layout mode may have come on since (a key in the same frame): never called in it
            const auto verdict = g_menu.layout.active ? BlockRules::Verdict::kLayoutMode : due.verdict;
            if (verdict != BlockRules::Verdict::kClick) {
                if (log) logger::info("Widget: add-on block {} press cancelled - {}{}", due.id, BlockRules::VerdictText(verdict), more);
                return;
            }
            if (log) {
                logger::info("Widget: add-on block {} click - press {:.1f},{:.1f} release {:.1f},{:.1f} (content clip){}", due.id, due.press.x, due.press.y,
                    due.release.x, due.release.y, more);
            }
            const auto error = GuardedCall([&] {
                addon->click(static_cast<float>(due.press.x), static_cast<float>(due.press.y), static_cast<float>(due.release.x),
                    static_cast<float>(due.release.y), addon->user);
            });
            if (!error.empty()) FailAddon(a_widget, *addon, "click", "L6", "click", error);
        }

        // Block API v2: a drawSelection block whose last draw had another form than the card's now is due again - only while it
        // has a place on this tab (g_menu.blocks): one its tabs mask hides waits, and is drawn once when a tab change brings it
        // back (Rearrange has updated g_menu.blocks by then, so the next call here sees the old form)
        void WatchShownForm()
        {
            const RE::FormID shown = ShownForm();
            for (const auto& addon : g_addons) {
                if (!addon.drawSelection || g_menu.addonFailed.contains(addon.id) || !g_menu.blocks.contains(addon.id)) continue;
                const auto last = g_menu.addonShown.find(addon.id);
                if (last == g_menu.addonShown.end() || last->second != shown) g_menu.addonDrawDue.insert(addon.id);
            }
        }

        // Task 6: the add-on blocks due (after the panels are made, a size change, a move to the other column; v2: a new form on
        // the card for a drawSelection block, a kMessageRedraw) drawn by their own callbacks, each into a fresh content clip below
        // its title strip and inside the padding, lent for the call only (BlockApi.h "When the block is drawn"). Called from the
        // frame handler (twice a frame: after the layout mode, and after the selection handling). An exception from a callback
        // is logged and that block is not drawn again this menu (a crash inside it cannot be caught). A v2 block with click gets
        // its hit clip again on top
        void DrawAddons(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            WatchShownForm();
            if (g_menu.addonDrawDue.empty()) return;
            const auto due = std::exchange(g_menu.addonDrawDue, {});
            for (const auto& id : due) {
                const auto* addon = Addon(id);
                if (!addon || g_menu.addonFailed.contains(id) || !g_menu.blocksAll.contains(id)) continue;
                const RE::FormID shown = ShownForm();
                // recorded at the try, drawn or not: a content clip that cannot be made is tried again at the next form change,
                // not on every DrawAddons call
                if (addon->drawSelection) g_menu.addonShown[id] = shown;
                auto content = Content(a_widget, id);
                if (!content.IsDisplayObject()) {
                    logger::warn("Widget: add-on block {} - its panel's content clip could not be made, not drawn", id);
                    continue;
                }
                if (const auto index = AddonIndex(id)) ++g_contentGenerations[*index];  // Block API v3: a new content clip
                const auto rect = BlockRect(id);
                const auto [left, top, width, height] = AddonBoxOf(rect, *addon);
                content.SetMember("_x", left);
                content.SetMember("_y", top);
                const auto error = GuardedCall([&] {
                    if (addon->drawSelection) {
                        addon->drawSelection(a_movie, content, static_cast<float>(width), static_cast<float>(height), shown, addon->user);
                    } else {
                        addon->draw(a_movie, content, static_cast<float>(width), static_cast<float>(height), addon->user);
                    }
                });
                ++g_menu.addonDraws[id];  // a press made before this draw is cancelled (BlockRules::Judge), whether it threw or not
                if (!error.empty()) {
                    FailAddon(a_widget, *addon, "draw", "L6", "drawn", error);
                    continue;
                }
                if (addon->click && !OwnMouse(*addon)) MakeAddonHit(a_movie, a_widget, id, left, top, width, height);
                if (g_menu.addonDrawn.insert(id).second) {
                    // "v<apiVersion>:" (SR's v2 line as before); a v3 block adds its v3 fields
                    const std::string v3 = addon->apiVersion < BlockApi::kVersion ? std::string{} :
                        std::format("{}{}, pad {}, flags 0x{:X}", addon->frame ? ", frame" : "", addon->key ? ", key" : "",
                            addon->pad < 0 ? std::string("the host's") : std::format("{:.1f}", addon->pad), addon->flags);
                    SelfTest(std::format("L6 addon {} drawn - {:.0f} x {:.0f} content in column {} (block {}){}", id, width, height,
                        ColumnOf(id).value_or(-1), RectText(rect),
                        addon->apiVersion >= BlockApi::kVersionV2 ?
                            std::format("; v{}: {}{}, tabs 0x{:X}{}", addon->apiVersion, addon->drawSelection ? std::format("drawSelection for {:08X}", shown) : "draw",
                                addon->click ? ", click" : "", addon->tabs, v3) :
                            std::string{}));
                }
            }
        }

        // blocks plan Task 2 / D4, D11: this tab's rects worked out again (Arrange) and every panel put at its place - a move
        // changes _x/_y only. A block whose size changed since it was last placed is drawn again once: its box and an add-on's
        // content (DrawAddons); our card through its signature (it holds the rect, WatchOwnCard), SkyUI's card through a new fit
        // when its slot moved. The first placing of a menu only
        // places (the panels are drawn new after it)
        // SkyUI's card is fitted to its slot: a moved slot (Powers/Shouts and back, a new width, a scroll) is fitted again
        // final review 3: a slot that only moved (a fit above it, a scroll) moves SkyUI's card by the same step at once, mapped into
        // the holder's parent - no new measure (that put the card a frame late at its old place); a new size fits it again
        bool ShiftCardFit(const Settings::Rect& a_from, const Settings::Rect& a_to)
        {
            if (!g_menu.cardFit || !Settings::Get().moveItemCard || g_menu.irActive || OwnCard() || SpellCardHidden()) return false;
            auto* movie = OpenMagicMenuMovie();
            RE::GFxValue root, parent;
            auto pair = movie ? CardAndHolder(movie) : std::nullopt;
            if (!pair || !movie->GetVariable(&root, "_root") || !pair->second.GetMember("_parent", &parent) || !parent.IsDisplayObject()) return false;
            const auto from = MapPoint(movie, root, parent, a_from.left, a_from.top), to = MapPoint(movie, root, parent, a_to.left, a_to.top);
            if (!from || !to) return false;
            g_menu.cardFit->x += to->first - from->first;
            g_menu.cardFit->y += to->second - from->second;
            pair->second.SetMember("_x", g_menu.cardFit->x);
            pair->second.SetMember("_y", g_menu.cardFit->y);
            return true;
        }

        void CheckCardSlot()
        {
            const auto slot = CardRect();
            if (g_menu.cardSlot && !SameRect(*g_menu.cardSlot, slot)) {
                const bool sameSize = std::abs(g_menu.cardSlot->Width() - slot.Width()) <= kRectTolerance &&
                                      std::abs(g_menu.cardSlot->Height() - slot.Height()) <= kRectTolerance;
                if (!sameSize || !ShiftCardFit(*g_menu.cardSlot, slot)) g_menu.cardFit.reset();
            }
            g_menu.cardSlot = slot;
        }

        void PlaceLayoutChrome(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget);  // the layout mode's buttons, handle, covers (Task 4)

        double FullHeight(const BlockId& a_id);  // a block's full height on this tab (with FitBlock, below)

        void Rearrange(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            g_menu.blocksMax = ArrangeBlocks(ArrangementFor(OwnCard()), false);
            g_menu.blocksMaxAll = ArrangeBlocks({}, false);
            // review R8: a fit from another tab or column width is never taller than the block's full height here
            for (auto& [id, fit] : g_menu.fitHeight) {
                if (!Addon(id)) fit = std::min(fit, FullHeight(id));
            }
            g_menu.blocksAll = ArrangeBlocks({});
            g_menu.blocks = ArrangeBlocks(ArrangementFor(OwnCard()));
            PlaceColumns(a_widget);  // the scrolls clamped to this tab's content before any BlockRect below
            for (const BlockId& id : AllBlocks()) {
                const auto rect = BlockRect(id);
                auto panel = BlockClip(a_widget, id);
                if (panel.IsDisplayObject()) {
                    const auto local = BlockLocal(id, rect);
                    panel.SetMember("_x", local.left);
                    panel.SetMember("_y", local.top);
                }
                const auto last = g_menu.placed.find(id);
                const bool resized = last != g_menu.placed.end() &&
                                     (std::abs(last->second.Width() - rect.Width()) > kRectTolerance ||
                                         std::abs(last->second.Height() - rect.Height()) > kRectTolerance);
                if (resized && panel.IsDisplayObject() && id != Layout::kSpellCard) {  // our card draws its own box (RedrawOwnCard)
                    Draw::Call(panel, "clear", {});
                    if (auto title = Child(panel, "title"); title.IsDisplayObject()) title.Invoke("removeTextField");
                    DrawPanelBox(a_movie, panel, rect, PanelTitle(id), PanelAlpha(id));
                }
                // Task 2 review #1: a new height redraws the content too (an add-on's; our card follows its block itself)
                if (resized) {
                    constexpr int kMaxResizeLogs = 24;  // a fitted card is resized with its content: one line per change, capped
                    if (++g_menu.resizeLogs <= kMaxResizeLogs) {
                        logger::info("Widget: block {} is {:.0f} x {:.0f} now (was {:.0f} x {:.0f}) - drawn again{}", id, rect.Width(), rect.Height(),
                            last->second.Width(), last->second.Height(), g_menu.resizeLogs == kMaxResizeLogs ? " - later ones this menu not logged" : "");
                    }
                    if (Addon(id)) g_menu.addonDrawDue.insert(id);
                }
                g_menu.placed[id] = rect;
            }
            CheckCardSlot();
            PlaceLayoutChrome(a_movie, a_widget);  // Task 4: the buttons and the border handle follow the border, the covers the blocks
        }

        // A block's full height on this tab: an add-on's registered one, a built-in block's as arranged with no fit (its ini height,
        // our card's fill); the most its fit may take (user 2026-10-30)
        double FullHeight(const BlockId& a_id)
        {
            if (const auto* addon = Addon(a_id)) return addon->height;
            if (const auto it = g_menu.blocksMax.find(a_id); it != g_menu.blocksMax.end()) return it->second.Height();
            // a block this tab hides: its height with nothing hidden and no fit (never blocksAll: that one has the fits in it)
            if (const auto it = g_menu.blocksMaxAll.find(a_id); it != g_menu.blocksMaxAll.end()) return it->second.Height();
            return BlockRect(a_id).Height();
        }

        // A block whose height this menu (an add-on's kMessageHeight, a card's fit) is not its full one - the layout mode lays it
        // out at its full height, so entering and leaving the mode arranges the columns again
        bool HeightsFitted()
        {
            for (const auto& [id, height] : g_menu.fitHeight) {
                if (std::abs(FullHeight(id) - height) > BlockRules::kHeightTolerance) return true;
            }
            return false;
        }

        // user 2026-10-30: a built-in card (our spell card since HoM add-on plan Task 5; the 3D preview keeps its ini height) fitted to the content it just laid out - needed (stage units,
        // from its top) and its full height give its height this menu (BlockRules::FittedHeight). A new height lays the columns
        // out at once (the blocks below move up or down this frame); the block itself places its bottom-anchored parts at the
        // returned height and keeps its rooms from its full height, so drawing at the new height asks for the same one (no
        // loop). In the layout mode the height is kept for the mode's end (every block shows at its full size there). Returns
        // the height the block shows at now
        // A block's fit and waiting shrink as BlockRules::FitState, and back (the two maps of g_menu)
        BlockRules::FitState FitStateOf(const BlockId& a_id)
        {
            BlockRules::FitState state;
            if (const auto it = g_menu.fitHeight.find(a_id); it != g_menu.fitHeight.end()) state.current = it->second;
            if (const auto it = g_menu.heldShrink.find(a_id); it != g_menu.heldShrink.end()) state.held = it->second;
            return state;
        }

        void StoreHeld(const BlockId& a_id, const BlockRules::FitState& a_state)
        {
            if (a_state.held) {
                g_menu.heldShrink[a_id] = *a_state.held;
            } else {
                g_menu.heldShrink.erase(a_id);
            }
        }

        // The host's frame clock in milliseconds (steady_clock, as the redraw timings)
        double NowMs()
        {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        // user 2026-10-30: a held shrink whose delay has passed - its block is drawn again, and that draw's FitBlock applies it (one
        // Rearrange: our card's own); an add-on's is applied by TakeAddonHeights
        void WatchHeldShrinks()
        {
            if (g_menu.heldShrink.empty() || g_menu.layout.active) return;
            const double now = NowMs();
            for (const auto& [id, held] : g_menu.heldShrink) {
                // our spell card is the one built-in block with a fit (HoM add-on plan Task 5: the progress and node cards left)
                if (!Addon(id) && id == Layout::kSpellCard && BlockRules::DueShrink(FitStateOf(id), now, false)) g_menu.ownCardSig.clear();
            }
        }

        double FitBlock(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget, const BlockId& a_id, double a_needed)
        {
            constexpr int kMaxBlockFitLogs = 24;
            const double full = FullHeight(a_id);
            const double fitted = BlockRules::FittedHeight(a_needed, full);
            const auto current = g_menu.fitHeight.find(a_id);
            const bool first = current == g_menu.fitHeight.end();
            const double before = first ? full : current->second;
            // user 2026-10-30: a grow now, a shrink once it held kShrinkDelayMs (not the menu's first fit, not in the layout mode);
            // any height that applies drops a waiting shrink (BlockRules::AskHeight)
            {
                auto state = FitStateOf(a_id);
                const bool applies = BlockRules::AskHeight(state, fitted, NowMs(), g_menu.layout.active);
                StoreHeld(a_id, state);
                if (!applies) return before;  // drawn at its present height until then
            }
            const bool changed = std::abs(fitted - before) > BlockRules::kHeightTolerance;
            // review GPT 2: a first fit equal to the full height is recorded too - it takes a fill block (our card) out of the fill,
            // which would otherwise grow it past the height it is drawn at when a block above it fits
            if (!changed && !first) return BlockRules::LaidOutHeight(full, fitted, g_menu.layout.active);
            g_menu.fitHeight[a_id] = fitted;
            if (changed && ++g_menu.fitLogs <= kMaxBlockFitLogs) {
                SelfTest(std::format("L7 fit {} - {:.1f} high (content needs {:.1f}, full {:.1f}, was {:.1f}){}{}", a_id, fitted, a_needed, full, before,
                    g_menu.layout.active ? " - kept for the layout mode's end" : "", g_menu.fitLogs == kMaxBlockFitLogs ? " - later ones this menu not logged" : ""));
            }
            if (g_menu.layout.active) return full;
            // laid out again when the block's arranged height is not the fitted one (this block is being drawn for its new height now)
            const auto arranged = g_menu.blocks.contains(a_id) ? g_menu.blocks.at(a_id) : g_menu.blocksAll.contains(a_id) ? g_menu.blocksAll.at(a_id) : Settings::Rect{};
            if (std::abs(arranged.Height() - fitted) <= BlockRules::kHeightTolerance) return fitted;
            Rearrange(a_movie, a_widget);
            return fitted;
        }

        // Block API v2: the kMessageHeight requests queued since it last ran. A new height lays the columns out again
        // (Rearrange: the blocks below move, the column's scroll is clamped, the resized block is due to be drawn again with its
        // hit clip); in the layout mode it is kept for the mode's end. Unknown ids (once per id) and heights over the
        // registered one (once per block per menu) are logged. True when the columns were laid out again
        bool TakeAddonHeights(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            constexpr int kMaxHeightLogs = 16;
            std::vector<std::pair<std::string, double>> requests;
            {
                const std::scoped_lock lock(g_heightLock);
                requests.swap(g_heightQueue);
            }
            // user 2026-10-30: an add-on's held shrink whose delay has passed comes back as its request (the add-on sent it once)
            if (!g_menu.layout.active) {
                const double now = NowMs();
                for (const auto& [id, held] : g_menu.heldShrink) {
                    const bool queued = std::ranges::any_of(requests, [&](const auto& a_request) { return a_request.first == id; });
                    if (!Addon(id) || queued) continue;
                    if (const auto due = BlockRules::DueShrink(FitStateOf(id), now, false)) requests.emplace_back(id, *due);
                }
            }
            bool changed = false;
            for (const auto& [id, height] : requests) {
                const auto* addon = KnownAddon(id, "height request ('SLMH')");
                if (!addon) continue;
                const auto current = g_menu.fitHeight.find(id);
                const auto verdict = BlockRules::JudgeHeight(height, addon->height,
                    current != g_menu.fitHeight.end() ? std::optional{ current->second } : std::nullopt);
                if (verdict == BlockRules::HeightVerdict::kTooTall) {
                    if (g_menu.addonTooTallLogged.insert(id).second) {
                        logger::warn("Widget: add-on block {} asked for the height {:.1f} - more than its registered {:.1f}; refused (logged once this menu)",
                            id, height, addon->height);
                    }
                    continue;
                }
                if (verdict == BlockRules::HeightVerdict::kUnchanged) {
                    g_menu.heldShrink.erase(id);
                    continue;
                }
                // user 2026-10-30: a shrink waits kShrinkDelayMs (a grow applies at once; in the layout mode every height applies, kept
                // for the mode's end); a height that applies drops a waiting shrink (BlockRules::AskHeight)
                {
                    auto state = FitStateOf(id);
                    const bool applies = BlockRules::AskHeight(state, height, NowMs(), g_menu.layout.active);
                    StoreHeld(id, state);
                    if (!applies) continue;
                }
                if (g_menu.addonHeightLogs < kMaxHeightLogs) {
                    ++g_menu.addonHeightLogs;
                    logger::info("Widget: add-on block {} height {:.1f} (registered {:.1f}){}{}", id, height, addon->height,
                        g_menu.layout.active ? " - kept for the layout mode's end" : "", g_menu.addonHeightLogs == kMaxHeightLogs ? " - later ones this menu not logged" : "");
                }
                g_menu.fitHeight[id] = height;
                changed = true;
            }
            if (!changed || g_menu.layout.active) return false;
            Rearrange(a_movie, a_widget);
            return true;
        }

        // ---------------------------------------------------------------------
        // Column scroll (blocks plan Task 3, D7): the wheel over one of our columns scrolls that column only, in normal mode
        // too. Our own Mouse listener scrolls; SkyUI's ItemMenu wheel handler (the preview zoom, which fires wherever the
        // topmost entity under the mouse is in mouseRotationRect - our panels have no mouse handlers, so the chain goes
        // through to it over most of our columns - read from SkyUI's source, not seen in game) is wrapped so it does not also zoom while the wheel scrolls a column.
        // SkyUI's list scrolls only when it is under the mouse (ScrollingList.as:303-318): never over our columns.
        // ---------------------------------------------------------------------

        // The column under the mouse whose content overflows its viewport, while our widget shows; nullopt otherwise. The hit
        // area is both columns' viewports and the gap between them (split at its middle, Layout::SnapSlot's rule): blocks, gaps
        // and empty space alike. Both the scroll and the zoom wrap ask this, so they agree on every notch
        std::optional<int> WheelColumn(RE::GFxMovie* a_movie)
        {
            if (g_closing || !a_movie || a_movie != static_cast<RE::GFxMovie*>(OpenMagicMenuMovie())) return std::nullopt;
            RE::GFxValue visible, x, y;
            // Task 4 (D5): the "nothing chosen" hide hides the column clips, not mmx_root (the layout buttons stay)
            const auto shown = std::format("_root.{}.{}._visible", kRootName, kColumnClips[0]);
            if (!a_movie->GetVariable(&visible, shown.c_str()) || !visible.IsBool() || !visible.GetBool()) return std::nullopt;
            if (!a_movie->GetVariable(&x, "_root._xmouse") || !x.IsNumber() || !a_movie->GetVariable(&y, "_root._ymouse") || !y.IsNumber()) {
                return std::nullopt;
            }
            const auto left = ColumnViewport(0), right = ColumnViewport(1);
            const double mx = x.GetNumber(), my = y.GetNumber();
            if (!(mx >= left.left && mx <= right.right && my >= left.top && my <= left.bottom)) return std::nullopt;
            const int column = mx < (left.right + right.left) / 2 ? 0 : 1;
            return ColumnScrolls(column) ? std::optional{ column } : std::nullopt;
        }

        // The AS2 Mouse object (a global; the "_global." path as the second try)
        bool MouseObject(RE::GFxMovie* a_movie, RE::GFxValue* a_mouse)
        {
            return (a_movie->GetVariable(a_mouse, "Mouse") && a_mouse->IsObject()) || (a_movie->GetVariable(a_mouse, "_global.Mouse") && a_mouse->IsObject());
        }

        // The column moved by a_delta stage units (clamped); false when it did not move (at its end already). a_wheel: a wheel
        // notch (counted and logged); the layout mode's drag scrolls without (Task 4, D8)
        bool ScrollColumn(RE::GFxValue& a_widget, int a_column, double a_delta, bool a_wheel = true)
        {
            const auto [content, visible] = ColumnHeights(a_column);
            const double before = g_menu.scroll[a_column];
            const double after = Layout::ClampScroll(before + a_delta, content, visible);
            if (std::abs(after - before) <= kRectTolerance) return false;
            g_menu.scroll[a_column] = after;
            ++g_menu.scrollMoves[a_column];  // Block API v2: a press in this column is cancelled
            PlaceColumns(a_widget);
            CheckCardSlot();
            if (a_wheel && ++g_menu.wheelScrolls <= kMaxWheelLogs) {
                logger::info("Widget: column {} scrolled to {:.0f} of {:.0f} (content {:.0f}, visible {:.0f}){}", a_column, after, content - visible,
                    content, visible, g_menu.wheelScrolls == kMaxWheelLogs ? " - later scrolls counted only" : "");
            }
            return true;
        }

        // Our Mouse listener's onMouseWheel(delta): SkyUI's sign (ScrollingList.as:310-313: a negative delta goes down the list)
        class WheelListener final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params& a_params) override
            {
                if (a_params.argCount < 1 || !a_params.args[0].IsNumber()) return;
                const double delta = a_params.args[0].GetNumber();
                const auto column = WheelColumn(a_params.movie);
                if (!column || delta == 0.0) return;
                RE::GFxValue widget;
                const auto path = std::format("_root.{}", kRootName);
                if (!a_params.movie->GetVariable(&widget, path.c_str()) || !widget.IsDisplayObject()) return;
                ScrollColumn(widget, *column, delta < 0 ? kWheelStep : -kWheelStep);
            }
        };

        // Menu_mc.onMouseWheel wrapped: over a column the wheel scrolls (WheelColumn) the zoom is not called; anywhere else
        // SkyUI's own handler runs as before (the default layout never scrolls, so nothing changes there)
        class WheelZoomWrap final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params& a_params) override
            {
                if (!a_params.thisPtr) return;
                if (WheelColumn(a_params.movie)) {
                    if (++g_menu.wheelZoomsKept == 1) logger::info("Widget: SkyUI's wheel zoom not called - the wheel scrolls our column (later ones counted)");
                    return;
                }
                RE::GFxValue original;
                if (!a_params.thisPtr->GetMember(kSavedWheel, &original) || !original.IsObject()) return;
                std::vector<RE::GFxValue> argv;
                argv.reserve(a_params.argCount + 1);
                argv.push_back(*a_params.thisPtr);
                for (std::uint32_t i = 0; i < a_params.argCount; ++i) argv.push_back(a_params.args[i]);
                RE::GFxValue result;
                if (original.Invoke("call", &result, argv.data(), argv.size()) && a_params.retVal) *a_params.retVal = result;
            }
        };
        RE::GPtr<WheelListener> g_wheelListener;
        RE::GPtr<WheelZoomWrap> g_wheelZoomWrap;

        // Once per menu movie, from the frame handler (Menu_mc is there by then): our Mouse listener (a member of our widget clip,
        // not removed at close: it does nothing while g_closing and goes with the movie - final review ruling) and the zoom wrap. Either missing is logged; the other still works
        void InstallWheel(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            if (g_menu.wheelInstallDone) return;
            // tried for kSelectionInstallFrames frames until Menu_mc's wheel handler is there (as the selection wrap is); then
            // whatever is there is used and what is missing is logged
            RE::GFxValue probe, probeWheel;
            const bool menuReady = a_movie->GetVariable(&probe, kMenuPath) && probe.IsObject() && probe.GetMember(kWheelName, &probeWheel) && probeWheel.IsObject();
            if (!menuReady && --g_menu.wheelInstallLeft > 0) return;
            g_menu.wheelInstallDone = true;
            RE::GFxValue mouse, existing;
            if (a_widget.GetMember(kWheelListener, &existing) && existing.IsObject()) {
                logger::info("Widget: wheel listener already on this movie");
            } else if (!MouseObject(a_movie, &mouse)) {
                logger::warn("Widget: no AS2 Mouse object - the wheel does not scroll our columns this menu");
            } else {
                RE::GFxValue listener, handler;
                a_movie->CreateObject(&listener);
                a_movie->CreateFunction(&handler, g_wheelListener.get());
                listener.SetMember(kWheelName, handler);
                const std::array<RE::GFxValue, 1> args{ listener };
                if (mouse.Invoke("addListener", nullptr, args.data(), args.size()) && a_widget.SetMember(kWheelListener, listener)) {
                    logger::info("Widget: wheel listener added (Mouse.addListener) - the wheel over a column taller than its area scrolls it");
                } else {
                    mouse.Invoke("removeListener", nullptr, args.data(), args.size());
                    logger::warn("Widget: wheel listener NOT added (Mouse.addListener or keeping it failed) - the wheel does not scroll our columns");
                }
            }
            RE::GFxValue menu, current, saved, wrap;
            if (!a_movie->GetVariable(&menu, kMenuPath) || !menu.IsObject() || !menu.GetMember(kWheelName, &current) || !current.IsObject()) {
                logger::warn("Widget: wheel zoom wrap NOT installed - no {}.{}; a wheel that scrolls our column may also zoom the preview", kMenuPath,
                    kWheelName);
                return;
            }
            if (menu.GetMember(kSavedWheel, &saved) && saved.IsObject()) {
                logger::info("Widget: wheel zoom wrap already on this movie");
                return;
            }
            a_movie->CreateFunction(&wrap, g_wheelZoomWrap.get());
            if (!menu.SetMember(kSavedWheel, current) || !menu.SetMember(kWheelName, wrap)) {
                menu.SetMember(kWheelName, current);
                logger::warn("Widget: wheel zoom wrap NOT installed - SetMember on {} failed", kMenuPath);
                return;
            }
            logger::info("Widget: wheel zoom wrap installed - {}.{} is not called while the wheel scrolls one of our columns", kMenuPath, kWheelName);
        }

        const char* TabName(Tab a_tab)
        {
            switch (a_tab) {
            case Tab::kSchool: return "school";
            case Tab::kPowers: return "powers/shouts";
            case Tab::kEffects: return "active effects";
            default: return "all/favorites";
            }
        }

        // Which panels the current tab shows (design 3.7, spec v7 C2): the preview and our card on every tab (the add-ons by their
        // tabs masks). Without our card (bOwnCard=0) Active Effects shows the preview only (SkyUI's card is its own). Our card
        // shows only while SkyUI's would (WatchOwnCard).
        // What shows now (ApplyVisibility's rules): each block, and the column clips (any)
        struct Showing
        {
            bool preview{}, card{}, any{};
        };

        Showing Visibility()
        {
            const bool own = OwnCard();
            // user 2026-10-30 (the board): the columns stay up with nothing chosen too - each block shows its empty look
            // (BlockRules::EmptyLookOf); before, nothing chosen hid them all
            Showing shown;
            shown.preview = BlockShown(Layout::kPreview);
            shown.any = shown.preview || own || !g_menu.blocks.empty();  // [Blocks] sHidden: the other blocks show with the preview hidden too
            shown.card = own && g_menu.ownCardShown;
            return shown;
        }

        // The blocks the player sees now (Task 4 review: the layout mode's covers and the Task 5 focus model take only these - an
        // invisible block, such as our card before its first item or an add-on its kMessageVisible hid, cannot be grabbed or focused)
        std::set<BlockId> VisibleBlocks()
        {
            const auto shown = Visibility();
            std::set<BlockId> ids;
            if (!shown.any) return ids;  // the column clips are hidden
            if (shown.preview) ids.insert(Layout::kPreview);
            if (shown.card) ids.insert(Layout::kSpellCard);
            // Task 6: an add-on shows while the columns do and it has a place (v2: on its tabs; v3: not hidden by kMessageVisible)
            for (const auto& addon : g_addons) {
                if (g_menu.blocks.contains(addon.id) && !g_menu.addonHidden.contains(addon.id)) ids.insert(addon.id);
            }
            return ids;
        }

        void ApplyVisibility(RE::GFxValue& a_widget)
        {
            const bool own = OwnCard();
            const bool effects = g_menu.tab == Tab::kEffects;
            const auto [preview, card, any] = Visibility();
            g_previewShown = preview && g_menu.chosen;  // the over-pass draws the 3D only for a chosen item (the box stays up without one)
            ShowEmptyLine(a_widget, Layout::kPreview, preview && !g_menu.chosen);
            // Task 4 (D5): the column clips are hidden, not mmx_root - the layout buttons below the left column stay up
            for (const char* column : kColumnClips) {
                if (auto clip = Child(a_widget, column); clip.IsDisplayObject()) clip.SetMember("_visible", any);
            }
            SetVisible(a_widget, Layout::kPreview, preview);
            SetVisible(a_widget, Layout::kSpellCard, card);
            // Block API v2: an add-on shows while it has a place on this tab (its tabs mask; a v1 block: on every tab), as before;
            // v3: a block its kMessageVisible hid keeps its place, invisible
            for (const auto& addon : g_addons) SetVisible(a_widget, addon.id, g_menu.blocks.contains(addon.id) && !g_menu.addonHidden.contains(addon.id));
            // with our card the layout is logged once its first item is drawn on the tab (review v7: not at the tab change; build
            // 16: drawn on THIS tab - OwnCardBox - not the last tab's drawing still up while WatchTab rearranges). FAIL: the card's
            // box (ours as drawn, else SkyUI's slot; unscrolled) is inverted, empty or outside its column (Layout::CardBoxProblem)
            const auto box = own ? OwnCardBox() : std::nullopt;
            if (g_menu.chosen && !SpellCardHidden() && (!own || (card && box)) && g_menu.c2Logged.insert(static_cast<int>(g_menu.tab)).second) {
                const auto rect = OwnCardRect();
                const auto arranged = g_menu.blocks.find(Layout::kSpellCard);
                const auto column = ColumnOf(Layout::kSpellCard);
                std::string problem = "the spell card is not arranged";
                if (box || arranged != g_menu.blocks.end()) {
                    problem = column ? Layout::CardBoxProblem(box ? *box : arranged->second, ColumnViewport(*column), ColumnHeights(*column).first)
                                     : std::string("the spell card is in no column");
                }
                SelfTest(std::format("C2 tab {} layout {} - preview {}, our card {} at {}{}{}",
                    TabName(g_menu.tab), problem.empty() ? "PASS" : "FAIL", preview ? "on" : "off", !own ? "off (bOwnCard=0, Inventory Reforged or no card info) - its slot" : "shown", RectText(rect),
                    box ? std::format(", drawn to {:.0f} (unscrolled {}{})", rect.top + box->Height(), RectText(*box), effects ? ", cut to its content" : "")
                        : std::string{},
                    problem.empty() ? std::string{} : " - " + problem));
            }
        }

        // ---------------------------------------------------------------------
        // Block API v3 on the frame thread (HoM add-on design 2026-10-30): kMessageVisible, the key and frame calls, the overlay.
        // Here and not in a file of its own: it works on g_menu, the panels (BlockClip, BlockLocal, BlockRect) and the columns
        // ---------------------------------------------------------------------

        constexpr std::size_t kMaxQueuedKeys = 32;  // keyboard downs waiting for one frame; more are dropped
        constexpr int kMaxAddonKeyLogs = 20;        // key calls logged per menu
        constexpr int kMaxOverlayLogs = 20;         // overlay clears logged per menu

        // kMessageVisible: the requests queued since the last frame applied (BlockRules::SetShown); a change shows or hides the
        // panel at once (ApplyVisibility - the place stays, the columns are not laid out again). Unknown ids logged once each
        void TakeAddonVisible(RE::GFxValue& a_widget)
        {
            std::vector<std::pair<std::string, bool>> requests;
            {
                const std::scoped_lock lock(g_v3Lock);
                requests.swap(g_visibleQueue);
            }
            bool changed = false;
            for (const auto& [id, shown] : requests) {
                if (!KnownAddon(id, "visible request ('SLMV')") || !BlockRules::SetShown(g_menu.addonHidden, id, shown)) continue;
                changed = true;
                logger::info("Widget: add-on block {} {} by its kMessageVisible{}", id, shown ? "shown again" : "hidden",
                    shown ? "" : " (its place in the column kept)");
            }
            if (changed) ApplyVisibility(a_widget);
        }

        // The keyboard downs queued since the last frame (QueueAddonKey), taken once per frame
        std::vector<std::uint32_t> TakeAddonKeys()
        {
            std::vector<std::uint32_t> keys;
            const std::scoped_lock lock(g_v3Lock);
            keys.swap(g_keyQueue);
            return keys;
        }

        // This frame's keys handed to one shown v3 block's key callback, in order (BlockRules::DeliversKey - the caller checked it).
        // The return value is logged only: a key the block used, one line (kMaxAddonKeyLogs per menu); one it did not use is only
        // counted, for the close summary (final review M1: arrow-key list steps filled the cap). False when the callback threw
        // (the block failed)
        bool AddonKeys(RE::GFxValue& a_widget, const BlockRegistry::Block& a_addon, const std::vector<std::uint32_t>& a_keys)
        {
            for (const auto key : a_keys) {
                bool used = false;
                const auto error = GuardedCall([&] { used = a_addon.key(key, a_addon.user); });
                if (!error.empty()) {
                    FailAddon(a_widget, a_addon, "key", "L9", "key", error);
                    return false;
                }
                if (!used) {
                    ++g_menu.addonKeysUnused;
                } else if (++g_menu.addonKeyLogs <= kMaxAddonKeyLogs) {
                    logger::info("Widget: add-on block {} key {} -> used{}", a_addon.id, key,
                        g_menu.addonKeyLogs == kMaxAddonKeyLogs ? " - later ones this menu not logged" : "");
                }
            }
            return true;
        }

        // An add-on's overlay clip removed from both column clips (the block may have moved)
        void RemoveOverlay(RE::GFxValue& a_widget, std::size_t a_index)
        {
            for (const char* columnName : kColumnClips) {
                if (auto column = Child(a_widget, columnName); column.IsDisplayObject()) Draw::Remove(column, g_overlayClips[a_index].c_str());
            }
        }

        // The block's overlay clip: an empty clip of its column made now (on top of every panel there), its origin on the content
        // clip's origin. Not a display object when it could not be made
        RE::GFxValue MakeOverlay(RE::GFxValue& a_widget, std::size_t a_index, const BlockRegistry::Block& a_addon)
        {
            auto parent = BlockParent(a_widget, a_addon.id);
            if (!parent.IsDisplayObject()) return {};
            auto overlay = Draw::Clip(parent, g_overlayClips[a_index].c_str());
            if (!overlay.IsDisplayObject()) return {};
            const auto rect = BlockRect(a_addon.id);
            const auto local = BlockLocal(a_addon.id, rect);
            const auto box = AddonBoxOf(rect, a_addon);
            overlay.SetMember("_x", local.left + box.left);
            overlay.SetMember("_y", local.top + box.top);
            return overlay;
        }

        // The Context of one frame call (BlockApi.h "Context")
        BlockApi::Context ContextOf(std::size_t a_index, const BlockRegistry::Block& a_addon, int a_column)
        {
            BlockApi::Context context;
            context.shown = ShownForm();
            context.listForm = g_menu.sel.index >= 0 ? g_menu.sel.id : 0;
            context.selectionSeq = static_cast<std::uint32_t>(g_menu.selSeq);
            context.source = g_menu.selFrom;
            context.servedRequest = g_menu.servedRequest;
            context.tabBits = BlockRules::TabBit(g_menu.tabFlag.value_or(kFlagUnread));
            context.tabSchool = g_menu.tabSchool.c_str();
            context.layoutMode = g_menu.layout.active;
            context.overlayGeneration = g_overlayTracks[a_index].generation;
            context.contentGeneration = g_contentGenerations[a_index];
            const auto view = BlockRules::ViewInContent(AddonContentRect(a_addon.id, a_addon), ColumnViewport(a_column));
            context.viewLeft = static_cast<float>(view.left);
            context.viewTop = static_cast<float>(view.top);
            context.viewRight = static_cast<float>(view.right);
            context.viewBottom = static_cast<float>(view.bottom);
            context.rowSize = static_cast<float>(g_menu.rowSize);
            context.lineFirst = static_cast<float>(g_menu.lineFirst);
            context.linePitch = static_cast<float>(g_menu.linePitch);
            return context;
        }

        // SelfTest L9 (once per block and menu, at its first frame call with an overlay - none in the layout mode): the overlay's 0,0
        // and the content clip's 0,0 at one place in _root, and the Context's visible part inside the content clip
        void CheckFirstFrame(RE::GFxMovieView* a_movie, const BlockRegistry::Block& a_addon, RE::GFxValue& a_content, RE::GFxValue* a_overlay,
            const BlockApi::Context& a_context)
        {
            if (!Settings::Get().selfTest || !a_overlay || !g_menu.addonFramed.insert(a_addon.id).second) return;
            constexpr double kSlack = 0.5;
            RE::GFxValue root;
            const bool rooted = a_movie->GetVariable(&root, "_root");
            const auto content = rooted ? MapPoint(a_movie, a_content, root, 0, 0) : std::nullopt;
            const auto overlay = rooted && a_overlay ? MapPoint(a_movie, *a_overlay, root, 0, 0) : std::nullopt;
            const auto box = AddonBoxOf(BlockRect(a_addon.id), a_addon);
            const bool viewOk = a_context.viewLeft >= 0 && a_context.viewTop >= 0 && a_context.viewRight <= box.width + kSlack &&
                                a_context.viewBottom <= box.height + kSlack;
            const bool originOk = content && overlay && std::abs(content->first - overlay->first) <= kSlack && std::abs(content->second - overlay->second) <= kSlack;
            SelfTest(std::format("L9 addon {} frame {} - content 0,0 in _root {}, overlay 0,0 {}; view {:.0f},{:.0f}..{:.0f},{:.0f} of {:.0f} x {:.0f}, "
                                 "selection seq {}, tab bits 0x{:X}, layout mode {}, generations overlay {} content {}",
                a_addon.id, originOk && viewOk ? "PASS" : "FAIL", content ? std::format("{:.1f},{:.1f}", content->first, content->second) : std::string("not read"),
                overlay ? std::format("{:.1f},{:.1f}", overlay->first, overlay->second) : std::string("not read"), a_context.viewLeft,
                a_context.viewTop, a_context.viewRight, a_context.viewBottom, box.width, box.height, a_context.selectionSeq, a_context.tabBits,
                a_context.layoutMode, a_context.overlayGeneration, a_context.contentGeneration));
        }

        // Block API v3: the key calls, then each frame block's frame call with its present content clip, its overlay and the Context.
        // Last in the frame's block work (after every draw and fit that may move the panels), the layout mode included. A
        // block that does not show (no place on this tab, hidden by kMessageVisible, the columns down, failed) gets no call, and
        // its overlay goes; the overlay is made again (a new generation) when the block moved, scrolled, was drawn again or a
        // panel was made above it (BlockRules::StepOverlay). In the layout mode there is none (a_overlay null): one made there
        // would lie above the mode's covers in the column clip
        void AddonFrames(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            // fix round 1 (HoM add-on Task 7): the keys go to the blocks shown this frame only, right before their frame calls - a key
            // pressed on a tab a block is not on is never handed to it later
            const auto keys = TakeAddonKeys();
            const bool columnsUp = Visibility().any;
            for (std::size_t i = 0; i < g_addons.size(); ++i) {
                const auto& addon = g_addons[i];
                if (!addon.frame && !addon.key) continue;
                const auto column = ColumnOf(addon.id);
                const bool shown = columnsUp && column && g_menu.blocks.contains(addon.id) && !g_menu.addonHidden.contains(addon.id) &&
                                   !g_menu.addonFailed.contains(addon.id);
                const bool deliver = !keys.empty() && BlockRules::DeliversKey(addon.key != nullptr, shown, g_menu.layout.active);
                if (!addon.frame) {  // a key-only block
                    if (deliver) AddonKeys(a_widget, addon, keys);
                    continue;
                }
                BlockRules::OverlaySnapshot now{ shown && !g_menu.layout.active, column.value_or(-1), BlockRect(addon.id), g_contentGenerations[i],
                    g_menu.layoutModeChanges, column ? g_menu.panelsMade[*column] : 0 };
                auto& track = g_overlayTracks[i];
                const auto step = BlockRules::StepOverlay(track, now, kRectTolerance);
                if (step.remove) {
                    RemoveOverlay(a_widget, i);
                    if (++g_menu.overlayLogs <= kMaxOverlayLogs) {
                        logger::info("Widget: add-on block {} overlay cleared - {}{}", addon.id, BlockRules::OverlayEventText(step.why),
                            g_menu.overlayLogs == kMaxOverlayLogs ? " - later ones this menu not logged" : "");
                    }
                }
                if (!shown) continue;
                RE::GFxValue overlay;
                if (step.make) {
                    overlay = MakeOverlay(a_widget, i, addon);
                    if (!overlay.IsDisplayObject()) {
                        track.made.reset();  // tried again next frame
                        static std::set<BlockId> s_logged;
                        if (s_logged.insert(addon.id).second) logger::warn("Widget: add-on block {} - its overlay clip could not be made (logged once)", addon.id);
                    }
                } else if (auto parent = BlockParent(a_widget, addon.id); track.made && parent.IsDisplayObject()) {
                    overlay = Child(parent, g_overlayClips[i].c_str());
                }
                auto panel = BlockClip(a_widget, addon.id);
                auto content = panel.IsDisplayObject() ? Child(panel, "content") : RE::GFxValue{};
                if (!content.IsDisplayObject()) continue;  // not drawn yet (DrawAddons logs a content clip it could not make)
                // the keys right before the frame call (after every check that could skip it): a key acts in the frame it is given in
                if (deliver && !AddonKeys(a_widget, addon, keys)) continue;  // its key callback threw: failed, no frame call
                const auto context = ContextOf(i, addon, *column);
                RE::GFxValue* overlayArg = overlay.IsDisplayObject() ? &overlay : nullptr;
                const auto error = GuardedCall([&] { addon.frame(a_movie, content, overlayArg, context, addon.user); });
                if (!error.empty()) {
                    FailAddon(a_widget, addon, "frame", "L9", "frame", error);
                    continue;
                }
                CheckFirstFrame(a_movie, addon, content, overlayArg, context);
            }
        }

        // SelfTest L1 reference (HoM add-on plan Task 5): the core's two blocks at the default layout with nothing ahead of the spell
        // card in the left column - the preview at its ini rect, the spell card one gap below it; ours (a_own) fills the column down
        // to the item card's ini bottom, SkyUI's keeps the item card's ini height. The same on every tab (no built-in block is
        // hidden per tab since Task 5). Formulas over the ini rects, not Arrange's
        Settings::Rect CoreRect(bool a_own, const BlockId& a_id)
        {
            const auto& s = Settings::Get();
            if (a_id == Layout::kPreview) return s.preview;
            const auto& card = s.itemCard;
            const double top = s.preview.bottom + s.panelGap;
            return { card.left, top, card.right, a_own ? card.bottom : top + card.Height() };
        }

        // SelfTest L1 (blocks plan Task 2, D6): each tab's arranged rects (our card and SkyUI's) logged, PASS when they are the
        // core's reference (CoreRect), FAIL with the differences; SKIP when the layout file is not the default layout. Per block
        // (Task 8): a block with an add-on ahead of it in its column (the Heart of Magic block's progress card takes its retired
        // place above the spell card), or after our card that fills its column, is not compared - its rect differs by design
        // then; SKIP when no block is compared. The ini and the layout file are read once per game session, so this runs at the
        // first menu open only
        void CheckLayoutRects()
        {
            static bool s_checked = false;
            if (!Settings::Get().selfTest || std::exchange(s_checked, true)) return;
            const bool custom = !(CurrentLayout() == Settings::DefaultLayout());
            constexpr std::array kTabs{ Tab::kFollow, Tab::kSchool, Tab::kPowers, Tab::kEffects };
            for (const Tab tab : kTabs) {
                for (const bool own : { true, false }) {
                    // Block API v2: a flag of the tab kind for the add-ons' masks (kPowers stands for Powers and Shouts)
                    const int flag = tab == Tab::kSchool ? kFlagDestruction : tab == Tab::kPowers ? kFlagPowers :
                                     tab == Tab::kEffects ? kFlagActiveEffects : kFlagUnread;
                    const auto arranged = ArrangeBlocks(ArrangementFor(own, flag));
                    const auto arrangedAddon = [&](const BlockId& a_id) {
                        return arranged.contains(a_id) && std::ranges::any_of(g_addons, [&](const auto& a_addon) { return a_addon.id == a_id; });
                    };
                    std::string rects, diffs, skipped;
                    int compared = 0;
                    for (const BlockId id : kBlocks) {
                        const auto it = arranged.find(id);
                        const std::optional<Settings::Rect> now = it != arranged.end() ? std::optional{ it->second } : std::nullopt;
                        const auto then = CoreRect(own, id);
                        rects += std::format(" {} {}", id, now ? RectText(*now) : std::string("not arranged"));
                        // Task 8: per block - the preview is compared while it is first in its column, the spell card while no add-on
                        // is ahead of it (our card: none after it either, BlockRules::CoreCompared)
                        const auto column = ColumnOf(id);
                        if (custom || !column || g_menu.hidden.contains(id) ||
                            !BlockRules::CoreCompared(CurrentLayout().columns[*column], id, own && id == Layout::kSpellCard, arrangedAddon)) {
                            skipped += std::format(" {}", id);
                            continue;
                        }
                        ++compared;
                        if (!now || !SameRect(*now, then)) {
                            diffs += std::format("; {} {} (core {})", id, now ? RectText(*now) : std::string("not arranged"), RectText(then));
                        }
                    }
                    const char* verdict = compared == 0 ? "SKIP" : diffs.empty() ? "PASS" : "FAIL";
                    SelfTest(std::format("L1 layout {} {} ({}) -{}{}{}", TabName(tab), verdict, own ? "our card" : "SkyUI's card", rects,
                        custom            ? " (the layout file is not the default layout: the core's rects are no reference)" :
                        !skipped.empty()  ? std::format(" (not compared:{} - hidden ([Blocks] sHidden), an add-on block ahead of it in its column, or after our card that fills it)", skipped) :
                                            std::string{},
                        diffs));
                }
            }
        }

        // SelfTest L1 places (Task 2 review #2, blocks plan Task 3): each panel read back - its _x/_y against BlockLocal(BlockRect),
        // and its top-left mapped to _root (through the column clip) against BlockRect's, so a misplaced column clip fails too.
        // Every menu open, after the panels are made
        void CheckPanelPlaces(RE::GFxMovieView* a_movie, RE::GFxValue& a_root, RE::GFxValue& a_widget)
        {
            if (!Settings::Get().selfTest) return;
            constexpr double kSlack = 0.5;
            std::string places, diffs;
            for (const BlockId& id : AllBlocks()) {
                if (g_menu.hidden.contains(id)) continue;  // [Blocks] sHidden: no panel this menu
                auto panel = BlockClip(a_widget, id);
                const auto rect = BlockRect(id);
                const auto local = BlockLocal(id, rect);
                const auto x = panel.IsDisplayObject() ? Number(panel, "_x") : std::nullopt;
                const auto y = panel.IsDisplayObject() ? Number(panel, "_y") : std::nullopt;
                const auto onRoot = panel.IsDisplayObject() ? MapPoint(a_movie, panel, a_root, 0, 0) : std::nullopt;
                if (!x || !y || !onRoot) {
                    diffs += std::format("; {} not read back", id);
                    continue;
                }
                places += std::format(" {} {:.1f},{:.1f} (_root {:.1f},{:.1f})", id, *x, *y, onRoot->first, onRoot->second);
                if (std::abs(*x - local.left) > kSlack || std::abs(*y - local.top) > kSlack) {
                    diffs += std::format("; {} _x/_y {:.1f},{:.1f} want {:.1f},{:.1f}", id, *x, *y, local.left, local.top);
                }
                if (std::abs(onRoot->first - rect.left) > kSlack || std::abs(onRoot->second - rect.top) > kSlack) {
                    diffs += std::format("; {} in _root {:.1f},{:.1f} want {:.1f},{:.1f}", id, onRoot->first, onRoot->second, rect.left, rect.top);
                }
            }
            SelfTest(std::format("L1 places {} - panel _x/_y in its column clip (_root top-left):{}{}", diffs.empty() ? "PASS" : "FAIL", places, diffs));
        }

        // SelfTest L2 mask (blocks plan Task 3): each column's mask read back in _root against the column's viewport
        void CheckMasks(RE::GFxValue& a_root, RE::GFxValue& a_widget)
        {
            if (!Settings::Get().selfTest) return;
            constexpr double kSlack = 0.5;
            for (int column = 0; column < Layout::kColumns; ++column) {
                auto mask = Child(a_widget, kColumnMasks[column]);
                const auto bounds = mask.IsDisplayObject() ? BoundsIn(mask, a_root) : std::nullopt;
                const auto want = ColumnViewport(column);
                const bool ok = bounds && std::abs(bounds->xMin - want.left) <= kSlack && std::abs(bounds->xMax - want.right) <= kSlack &&
                                std::abs(bounds->yMin - want.top) <= kSlack && std::abs(bounds->yMax - want.bottom) <= kSlack;
                const auto [content, visible] = ColumnHeights(column);
                SelfTest(std::format("L2 mask column {} {} - mask bounds in _root {} (the column's viewport {}); content {:.0f} of {:.0f} visible, scroll {:.0f}",
                    column, ok ? "PASS" : "FAIL",
                    bounds ? std::format("{:.1f},{:.1f}..{:.1f},{:.1f}", bounds->xMin, bounds->yMin, bounds->xMax, bounds->yMax) : std::string("none"),
                    RectText(want), content, visible, g_menu.scroll[column]));
            }
        }

        // ---------------------------------------------------------------------
        // Our own info card (spec v7 C1): what SkyUI's item card would show - its info object, which the game fills through
        // RequestItemCardInfo (so other mods' descriptions are in it) and P2 fills for a tree click - drawn in our look.
        // SkyUI's card stays as it is (data, fade flag and timeline untouched); only its fade holder is hidden.
        // ---------------------------------------------------------------------

        // A panel's "content" child's child, not a display object when any step is missing
        RE::GFxValue ContentChild(RE::GFxValue& a_widget, const BlockId& a_id, const char* a_name, RE::GFxValue& a_content)
        {
            auto panel = BlockClip(a_widget, a_id);
            a_content = panel.IsDisplayObject() ? Child(panel, "content") : RE::GFxValue{};
            return a_content.IsDisplayObject() ? Child(a_content, a_name) : RE::GFxValue{};
        }

        struct CardView
        {
            int type = -1;
            std::string name, sub, words, effects, footLeft, footRight;
            bool negative = false;
        };

        bool BoolMember(const RE::GFxValue& a_object, const char* a_member)
        {
            RE::GFxValue value;
            return a_object.IsObject() && a_object.GetMember(a_member, &value) && value.IsBool() && value.GetBool();
        }

        // The spell the card is about, for what its info object does not carry (school, cast type): the tree spell P2 put on
        // SkyUI's card, else the list's selection - used only when its name is the card's (a stale or other form is ignored)
        const RE::SpellItem* CardSpell(const std::string& a_name)
        {
            RE::TESForm* form = nullptr;
            if (g_menu.cardFromTree) {
                form = RE::TESForm::LookupByID(g_menu.cardFromTree);
            } else {
                form = CommittedForm();  // spec v12 G2
            }
            const auto* spell = form ? form->As<RE::SpellItem>() : nullptr;
            return spell && spell->GetName() && a_name == spell->GetName() ? spell : nullptr;
        }

        // The shout's three words, known ones bright, the others dim (an HTML line)
        std::string ShoutWords(const RE::GFxValue& a_info)
        {
            std::string line;
            for (int i = 0; i < kShoutWords; ++i) {
                const auto word = StringMember(a_info, std::format("word{}", i).c_str());
                const auto dragon = StringMember(a_info, std::format("dragonWord{}", i).c_str());
                if (word.empty() && dragon.empty()) continue;
                const bool known = BoolMember(a_info, std::format("unlocked{}", i).c_str());
                const auto text = Upper(dragon) + (dragon.empty() || word.empty() ? "" : " ") + word;
                if (!line.empty()) line += "  \xC2\xB7  ";
                line += std::format("<font color='{}'>{}</font>", ColorHex(known ? kWhite : kDimText), HtmlEscape(text));
            }
            return line;
        }

        // A shout's recharge: SkyUI's list passes it as a string of per-word values ("5 , 10 , 15", MagicDataSetter split on
        // " , "), sometimes a plain number; shown as "5 / 10 / 15초". Empty when there is none.
        std::string RechargeText(const RE::GFxValue& a_info)
        {
            RE::GFxValue value;
            if (!a_info.IsObject() || !a_info.GetMember("spellCost", &value)) return {};
            const auto number = [](double a_v) { return a_v == std::floor(a_v) ? std::format("{:.0f}", a_v) : std::format("{:.1f}", a_v); };
            std::vector<std::string> parts;
            if (value.IsNumber()) {
                if (value.GetNumber() > 0) parts.push_back(number(value.GetNumber()));
            } else if (value.IsString() && value.GetString()) {
                const std::string_view all = value.GetString();
                for (std::size_t from = 0; from <= all.size();) {
                    const auto comma = all.find(',', from);
                    auto part = all.substr(from, comma == std::string_view::npos ? std::string_view::npos : comma - from);
                    while (!part.empty() && part.front() == ' ') part.remove_prefix(1);
                    while (!part.empty() && part.back() == ' ') part.remove_suffix(1);
                    double v{};
                    if (!part.empty()) {
                        const auto [end, ec] = std::from_chars(part.data(), part.data() + part.size(), v);
                        parts.push_back(ec == std::errc{} && end == part.data() + part.size() ? number(v) : std::string(part));
                    }
                    if (comma == std::string_view::npos) break;
                    from = comma + 1;
                }
            }
            if (parts.empty()) return {};
            std::string joined;
            for (const auto& part : parts) joined += (joined.empty() ? "" : " / ") + part;
            return Fill(Str::kCardSeconds, { { "{n}", joined } });
        }

        CardView ViewOf(const RE::GFxValue& a_info)
        {
            CardView view;
            view.type = static_cast<int>(Number(a_info, "type").value_or(-1.0));
            view.name = StringMember(a_info, "name");
            if (const auto count = Number(a_info, "count"); count && *count > 1) view.name += std::format(" ({:.0f})", *count);
            view.negative = BoolMember(a_info, "negativeEffect");
            view.effects = StringMember(a_info, "effects");
            const auto cost = Number(a_info, "spellCost");
            const auto costText = cost ? std::format("{:.0f}", *cost) : std::string{};
            switch (view.type) {
            case kTypeSpell: {
                const auto* spell = CardSpell(StringMember(a_info, "name"));
                auto school = StringMember(a_info, "magicSchoolName");
                if (school.empty() && spell) school = SchoolText(SpellSchool::Of(spell));  // the spell's own school (HoM add-on design)
                auto level = StringMember(a_info, "castLevel");
                if (level.empty() && spell) level = LevelText(spell);
                view.sub = school.empty() ? level : level.empty() ? school : Fill(Str::kCardSub, { { "{school}", school }, { "{level}", level } });
                if (cost) {
                    const bool perSecond = Number(a_info, "castTime").value_or(1.0) == 0.0;
                    view.footLeft = Fill(perSecond ? Str::kCardMagickaPerSec : Str::kCardMagicka, { { "{n}", costText } });
                }
                if (spell) view.footRight = CastTypeText(spell);
                break;
            }
            case kTypePower:
                view.sub = PowerText(CardSpell(StringMember(a_info, "name")));
                if (cost && *cost > 0) view.footLeft = Fill(Str::kCardMagicka, { { "{n}", costText } });
                break;
            case kTypeShout:
                view.sub = Text(Str::kCardShout);
                view.words = ShoutWords(a_info);
                if (const auto recharge = RechargeText(a_info); !recharge.empty()) view.footLeft = Fill(Str::kCardShoutRecharge, { { "{t}", recharge } });
                break;
            case kTypeEffect: {
                const double left = Number(a_info, "timeRemaining").value_or(0.0);
                view.sub = left > 0 ? Fill(Str::kCardTimeLeft, { { "{t}", TimeText(left) } }) : Text(Str::kCardActiveEffect);
                break;
            }
            default: break;  // name and description only (logged once per type)
            }
            return view;
        }

        // The game's description HTML with our font and size on the whole text and its paragraphs left-aligned; its colour
        // markup (magnitudes) stays: a TextFormat with only font, size and align set changes only those (review: the
        // description is never cut as a string)
        void RestyleWhole(RE::GFxMovieView* a_movie, RE::GFxValue& a_field, double a_size)
        {
            RE::GFxValue format;
            // a new TextFormat: every property but these three undefined, so colours are left alone (review v7 round 1)
            a_movie->CreateObject(&format, "TextFormat");
            if (!format.IsObject()) return;
            format.SetMember("font", Draw::kFont);
            format.SetMember("size", a_size);
            format.SetMember("align", "left");
            a_field.Invoke("setTextFormat", nullptr, &format, 1);
        }

        // The field's text set from HTML, then our font, size and left alignment on all of it (RestyleWhole). The HTML goes in
        // inside a white font tag: text the game's HTML leaves without a colour took the html default, black, not the field's
        // setNewTextFormat white (game run 2026-10-25 23:44, only the coloured magnitudes showed); its own colours sit inside
        // and still win
        void SetHtml(RE::GFxMovieView* a_movie, RE::GFxValue& a_field, const std::string& a_html, double a_size)
        {
            a_field.SetMember("html", true);
            RE::GFxValue html;
            const auto wrapped = std::format("<font color=\"#{:06X}\">{}</font>", kWhite, a_html);
            a_movie->CreateString(&html, wrapped.c_str());
            a_field.SetMember("htmlText", html);
            RestyleWhole(a_movie, a_field, a_size);
        }

        // A field's characters against the colours they may have (a_allowed, "RRGGBB" upper case): how many are in another
        // colour or none, how many were read, of how many. Each character's getTextFormat(i, i + 1).color (invisible text on
        // the black card, game run 2026-10-25 23:44). Spaces count too: they take the colour of the text round them
        struct ColourCount
        {
            int off = 0, read = 0, length = 0;
        };

        ColourCount OffColours(RE::GFxValue& a_field, const std::set<std::string>& a_allowed)
        {
            ColourCount count;
            count.length = static_cast<int>(Number(a_field, "length").value_or(0.0));
            for (int i = 0; i < count.length; ++i) {
                const std::array<RE::GFxValue, 2> range{ RE::GFxValue(static_cast<double>(i)), RE::GFxValue(static_cast<double>(i + 1)) };
                RE::GFxValue format;
                if (!a_field.Invoke("getTextFormat", &format, range) || !format.IsObject()) continue;
                ++count.read;
                const auto colour = Number(format, "color");
                if (!colour || !a_allowed.contains(std::format("{:06X}", static_cast<std::uint32_t>(*colour)))) ++count.off;
            }
            return count;
        }

        // Everything from character a_at on replaced with "..." (TextField.replaceText: what stays keeps its markup)
        void CutFrom(RE::GFxMovieView* a_movie, RE::GFxValue& a_field, double a_at)
        {
            const double length = Number(a_field, "length").value_or(0.0);
            RE::GFxValue dots;
            a_movie->CreateString(&dots, kEllipsis);
            const std::array<RE::GFxValue, 3> args{ RE::GFxValue(a_at), RE::GFxValue(length), dots };
            a_field.Invoke("replaceText", nullptr, args);
        }

        // An HTML field at a_y, a_w wide, at most a_room high, at the reading size: the lines that fit; when the text needs more,
        // it is cut at a character (plain-text position, found by bisection on the laid-out field) and ends in "...", its markup
        // kept (review v7 round 1: hidden lines gave no sign). With a_colourType >= 0 the description's colours are checked once
        // for that card type. Returns the height it takes.
        // a_shown (optional): the lines the field shows (all its text's, or the room's when cut) - a fitted card's room is
        // FieldH(a_size, *a_shown), which LinesFit reads back as the same count
        double DrawHtmlText(RE::GFxMovieView* a_movie, RE::GFxValue& a_content, const char* a_name, double a_y, double a_w, double a_room,
            const std::string& a_html, double a_size, int a_colourType = -1, int* a_shown = nullptr)
        {
            const int lines = LayoutMath::LinesFit(a_room, TextFit::FieldH(Metrics(), a_size), TextFit::LineH(Metrics(), a_size));
            if (a_shown) *a_shown = lines;
            auto field = Draw::Text(a_movie, a_content, a_name, 0, a_y, a_w, TextFit::FieldH(Metrics(), a_size, lines), "", kWhite, a_size);
            if (!field.IsDisplayObject()) return 0.0;
            field.SetMember("wordWrap", true);
            SetHtml(a_movie, field, a_html, a_size);
            if (a_colourType >= 0 && g_menu.colourLogged.insert(a_colourType).second) {
                const auto before = HtmlColours(a_html), after = HtmlColours(StringMember(field, "htmlText"));
                const bool kept = std::ranges::includes(after, before);
                std::string list;
                for (const auto& colour : before) list += (list.empty() ? "#" : ", #") + colour;
                SelfTest(std::format("C1 colour {} - card type {}: the game's description sets {} colour(s){}{}", before.empty() ? "SKIP" : kept ? "PASS" : "FAIL",
                    a_colourType, before.size(), list.empty() ? "" : " (" + list + ")",
                    before.empty() ? "" : kept ? ", all still in the field" : ", some lost after our font and size"));
                // every character read, each in our white or a colour the game's HTML sets (review: a count of none read, or a
                // dark colour other than black, passed before)
                auto allowed = before;
                allowed.insert(std::format("{:06X}", kWhite));
                const auto count = OffColours(field, allowed);
                const bool readable = count.length > 0 && count.read == count.length && count.off == 0;
                SelfTest(std::format("C1 readable {} - card type {}: {} of {} description character(s) read, {} in a colour neither our white nor the game's",
                    readable ? "PASS" : "FAIL", a_colourType, count.read, count.length, count.off));
            }
            const int needed = TextFit::TextLines(Metrics(), field, a_size);
            if (a_shown) *a_shown = std::clamp(needed, 1, lines);
            if (needed > lines) {
                const auto fitsAt = [&](int a_at) {
                    SetHtml(a_movie, field, a_html, a_size);
                    CutFrom(a_movie, field, a_at);
                    return TextFit::TextLines(Metrics(), field, a_size) <= lines;
                };
                int lo = 0, hi = static_cast<int>(Number(field, "length").value_or(0.0));  // fitsAt(lo) taken as true
                while (lo < hi) {
                    const int mid = lo + (hi - lo + 1) / 2;
                    if (fitsAt(mid)) {
                        lo = mid;
                    } else {
                        hi = mid - 1;
                    }
                }
                fitsAt(lo);
                if (g_menu.textLog.cut.insert(std::string("c1|") + a_name).second) {
                    logger::info("Widget: C1 card {} cut to {} of its {} lines at size {:.1f} (character {}, then \"{}\")", a_name, lines, needed, a_size, lo,
                        kEllipsis);
                }
            }
            return std::min(TextH(field), TextFit::FieldH(Metrics(), a_size, lines));
        }

        // Our card in a_rect (render layout F): name (size 19, two lines), sub line, the shout's words, a rule, the description,
        // a rule and the footer (cost left, cast type right). User 2026-10-30, every tab (Active Effects' a_fit of build 16 made
        // general): the block fits its content - the rooms from its full height, the footer under the description with the
        // card's rule gap (FitBlock); a long description still takes the room up to the full height and is cut as before
        void RedrawOwnCard(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget, const CardView& a_view, const Settings::Rect& a_rect)
        {
            auto panel = BlockClip(a_widget, Layout::kSpellCard);
            if (!panel.IsDisplayObject()) return;
            Draw::Call(panel, "clear", {});
            const auto local = BlockLocal(Layout::kSpellCard, a_rect);
            panel.SetMember("_x", local.left);
            panel.SetMember("_y", local.top);
            auto content = Content(a_widget, Layout::kSpellCard);
            if (!content.IsDisplayObject()) return;
            content.SetMember("_x", kPad);
            const double w = a_rect.Width() - 2 * kPad, maxH = FullHeight(Layout::kSpellCard);
            double y = kCardTop;
            auto name = TextFit::FitLines(Metrics(), g_menu.textLog, a_movie, content, "name", 0, y, w, kNameSize, kNameLines, a_view.name, a_view.negative ? kNotYet : kWhite);
            y += name.IsDisplayObject() ? std::clamp(TextH(name), TextFit::FieldH(Metrics(), kNameSize), TextFit::FieldH(Metrics(), kNameSize, kNameLines)) : TextFit::FieldH(Metrics(), kNameSize);
            const double subSize = TextFit::Size(Metrics(), kSubSize), bodySize = TextFit::Size(Metrics(), kRowSize);
            if (!a_view.sub.empty()) {
                TextFit::FitLine(Metrics(), g_menu.textLog, a_movie, content, "sub", 0, y, w, subSize, a_view.sub, kTitleColor);
                y += TextFit::FieldH(Metrics(), subSize);
            }
            if (!a_view.words.empty()) y += DrawHtmlText(a_movie, content, "words", y, w, TextFit::FieldH(Metrics(), subSize), a_view.words, subSize);
            y += kCardRuleGap;
            Draw::LineStyle(content, kEdgeWidth, kPanelEdge);
            Draw::MoveTo(content, 0, y);
            Draw::LineTo(content, w, y);
            y += kCardRuleGap;
            const bool foot = !a_view.footLeft.empty() || !a_view.footRight.empty();
            const double footH = TextFit::FieldH(Metrics(), subSize), footReserve = foot ? 2 * kCardRuleGap + footH + kPad : kPad;
            double used = y;
            if (!a_view.effects.empty() && maxH - footReserve - y >= TextFit::FieldH(Metrics(), kMinFitSize)) {
                int shown = 0;
                DrawHtmlText(a_movie, content, "desc", y, w, maxH - footReserve - y, a_view.effects, bodySize, a_view.type, &shown);
                used = y + TextFit::FieldH(Metrics(), bodySize, shown);
            }
            const double h = FitBlock(a_movie, a_widget, Layout::kSpellCard, used + footReserve);
            if (foot) {
                const double ruleY = h - footReserve + kCardRuleGap;
                Draw::LineStyle(content, kEdgeWidth, kPanelEdge);
                Draw::MoveTo(content, 0, ruleY);
                Draw::LineTo(content, w, ruleY);
                const double footY = h - kPad - footH;
                if (!a_view.footLeft.empty()) TextFit::FitLine(Metrics(), g_menu.textLog, a_movie, content, "cost", 0, footY, w * kCardFootShare, subSize, a_view.footLeft, kLabelColor);
                if (!a_view.footRight.empty()) {
                    TextFit::FitLine(Metrics(), g_menu.textLog, a_movie, content, "cast", w * kCardFootShare, footY, w * (1 - kCardFootShare), subSize, a_view.footRight, kDimText, "right");
                }
            }
            Draw::NoLine(panel);
            Draw::Rect(panel, 0, 0, a_rect.Width(), h, kPanelFill, kPanelAlpha);
            const auto drawnIn = BlockRect(Layout::kSpellCard);  // FitBlock may have laid the columns out again
            g_menu.ownCardBottom = drawnIn.top + h;
            g_menu.ownCardDrawnTab = static_cast<int>(g_menu.tab);
            g_menu.ownCardDrawnIn = drawnIn;
        }

        // user 2026-10-30 (the board): our card with nothing chosen - one dim line, the card fitted to it (FitBlock)
        void RedrawEmptyCard(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget, const Settings::Rect& a_rect)
        {
            auto panel = BlockClip(a_widget, Layout::kSpellCard);
            if (!panel.IsDisplayObject()) return;
            Draw::Call(panel, "clear", {});
            const auto local = BlockLocal(Layout::kSpellCard, a_rect);
            panel.SetMember("_x", local.left);
            panel.SetMember("_y", local.top);
            auto content = Content(a_widget, Layout::kSpellCard);
            if (!content.IsDisplayObject()) return;
            content.SetMember("_x", kPad);
            const double size = TextFit::Size(Metrics(), kRowSize);
            TextFit::FitLine(Metrics(), g_menu.textLog, a_movie, content, "empty", 0, kCardTop, a_rect.Width() - 2 * kPad, size, Text(Str::kEmptyChoose), kDimText);
            const double h = FitBlock(a_movie, a_widget, Layout::kSpellCard, kCardTop + TextFit::FieldH(Metrics(), size) + kPad);
            Draw::NoLine(panel);
            Draw::Rect(panel, 0, 0, a_rect.Width(), h, kPanelFill, kPanelAlpha);
            const auto drawnIn = BlockRect(Layout::kSpellCard);
            g_menu.ownCardBottom = drawnIn.top + h;
            g_menu.ownCardDrawnTab = static_cast<int>(g_menu.tab);
            g_menu.ownCardDrawnIn = drawnIn;
        }

        // The selection our card should be showing: the tree card's spell (P2), else the AS2 list's selectedIndex and entry text
        // (the list works for every form; the native SelectedSpell is a spell only, review v7 #3). Returns the key and the name
        // the card is expected to carry (empty when the entry's text is not the card's name: active effects name their source).
        std::pair<std::string, std::string> CardSelection(RE::GFxMovieView* a_movie)
        {
            if (g_menu.cardFromTree) {
                const auto* form = RE::TESForm::LookupByID(g_menu.cardFromTree);
                return { std::format("tree|{:08X}", g_menu.cardFromTree), form && form->GetName() ? form->GetName() : "" };
            }
            RE::GFxValue list;
            if (!a_movie->GetVariable(&list, kItemListPath) || !list.IsObject()) return { "none", "" };
            // spec v12 G2: the committed selection (as it was when committed), not the list's highlight
            const auto& sel = g_menu.sel;
            const std::string text = sel.index >= 0 ? sel.text : std::string{};
            // index | FormID | text: two spells of one name are two selections (review v7 round 1 #3)
            return { std::format("{}|{:08X}|{}", sel.index, sel.index >= 0 ? sel.id : 0, text), g_menu.tab == Tab::kEffects ? std::string{} : text };
        }

        // The selection key's FormID part ("" for a tree card or no list)
        std::string SelectionForm(const std::string& a_key)
        {
            const auto first = a_key.find('|');
            const auto second = first == std::string::npos ? std::string::npos : a_key.find('|', first + 1);
            return second == std::string::npos || a_key.starts_with("tree|") ? std::string{} : a_key.substr(first + 1, second - first - 1);
        }

        // Our description's text against SkyUI's own (its hidden card is still filled): the effects label of the card's frame
        // (ItemCard.as MagicEffectsLabel; ShoutEffectsLabel for a shout), whitespace left out, ours without its "..." when cut
        std::string CheckDescription(RE::GFxValue& a_widget, RE::GFxMovieView* a_movie, int a_type)
        {
            const auto squeeze = [](std::string a_text) {
                std::erase_if(a_text, [](char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; });
                return a_text;
            };
            RE::GFxValue content, label;
            auto field = ContentChild(a_widget, Layout::kSpellCard, "desc", content);
            auto card = FindCard(a_movie);
            const char* labelName = a_type == kTypeShout ? "ShoutEffectsLabel" : "MagicEffectsLabel";
            if (!card || !card->GetMember(labelName, &label) || !label.IsDisplayObject()) return std::format("SKIP (SkyUI's {} not on its card)", labelName);
            const auto theirs = squeeze(StringMember(label, "text"));
            if (!field.IsDisplayObject()) return theirs.empty() ? "PASS (both empty)" : std::format("FAIL (ours empty, SkyUI's {} characters)", theirs.size());
            auto ours = squeeze(StringMember(field, "text"));
            const bool cut = ours.ends_with(kEllipsis);
            if (cut) ours.resize(ours.size() - std::string_view(kEllipsis).size());
            const bool same = cut ? theirs.starts_with(ours) : ours == theirs;
            return std::format("{} (ours {} bytes{}, SkyUI's {} bytes)", same ? "PASS" : "FAIL", ours.size(), cut ? ", cut" : "", theirs.size());
        }

        // Every frame with our card: SkyUI's card holder hidden, our card redrawn when what SkyUI's card would show changes
        // (or the tab, or the reading size). C1 self-test per new selection: once the card's info has changed since the selection
        // came and (when the entry names it) carries the entry's name, our name field is read back; FAIL when that has not
        // happened within kC1Frames frames (stale data) or the field shows another name.
        void WatchOwnCard(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            if (!OwnCard()) return;
            if (std::ranges::find(kAreaDumpFrames, g_menu.frame) != kAreaDumpFrames.end()) DumpCardArea(a_movie);
            auto pair = CardAndHolder(a_movie);
            RE::GFxValue info;
            bool faded = false;
            if (pair) {
                auto& [card, holder] = *pair;
                RE::GFxValue visible;
                // the holder, not the card: SkyUI's FadeInCard sets the card's own _visible (ItemCard.as:149) and never the holder's
                if (!holder.GetMember("_visible", &visible) || !visible.IsBool() || visible.GetBool()) holder.SetMember("_visible", false);
                // and the card's own frame, every frame, as Inventory Reforged keeps it down (a reskin can put it back when it
                // fills the card: panel 36-window.as:1330-1339 keepFrameHidden; StolenTextInstance is left alone - the fallback
                // could not know whether to show it again). A black box
                // about 468 wide stayed with the holder hidden (game run 2026-10-25 23:44); whether it is this background is
                // not proven - a hidden parent hides its children, so it may sit outside the holder: DumpCardArea logs it
                RE::GFxValue background;
                if (card.GetMember("background", &background) && background.IsDisplayObject()) {
                    RE::GFxValue shown;
                    if (!background.GetMember("_visible", &shown) || !shown.IsBool() || shown.GetBool()) background.SetMember("_visible", false);
                }
                if (!g_menu.skyCardHidden) logger::info("Widget: C1 SkyUI's item card hidden (its holder and its background); our info card shows its item");
                g_menu.skyCardHidden = true;
                info = CardInfo(card);
                faded = BoolMember(card, "bFadedIn");
            }
            // moreHUD Inventory Edition's own wider card (a clip it loads under _root, drawn beside SkyUI's): the black box that
            // stayed (game run 2026-10-26 01:06, DumpCardArea: /AHZItemCardContainer 757..1225 x 404..704, visible). Made fully
            // transparent every frame, as Inventory Reforged does (panel 36-window.as:296-345, _alpha 0): moreHUD sets its
            // _visible per card itself; its alpha it leaves alone. Its first alpha is kept for the fallback
            RE::GFxValue moreHud;
            if (a_movie->GetVariable(&moreHud, kMoreHudCardPath) && moreHud.IsDisplayObject()) {
                const auto alpha = Number(moreHud, "_alpha").value_or(Draw::kAlphaFull);
                if (!g_menu.moreHudAlpha) {
                    g_menu.moreHudAlpha = alpha;
                    logger::info("Widget: C1 moreHUD's item card ({}) made transparent (its alpha was {:.0f})", kMoreHudCardPath, alpha);
                }
                if (alpha != 0.0) moreHud.SetMember("_alpha", 0.0);
            }
            const auto view = faded && info.IsObject() ? ViewOf(info) : CardView{};
            const bool show = !view.name.empty() || !view.effects.empty();
            // infoSig: the card info object's own fields only - sub (school) and footRight (cast type) can be filled from the
            // selected form, so they would make a stale card look updated (review v7 round 2)
            const auto infoSig = std::format("{}|{}|{}|{}|{}", view.type, view.name, view.words, view.effects, view.footLeft);
            // blocks plan D11: the card's rect is in it too - a new width (or the tab's grown height) draws the card again. With
            // our card on, Rearrange arranged with it (own = OwnCard(), again at each tab change and the fallback), so this is
            // OwnCardRect() without a new Arrange per frame
            // Task 3: its size only - a scroll moves the column clip, not the panel (BlockLocal stays), so it draws nothing again
            // user 2026-10-30: the full size, not the fitted one (the card's own fit would draw it again next frame for nothing),
            // and the layout mode (the card shows at its full height there)
            const auto cardRect = BlockRect(Layout::kSpellCard);
            const auto full = g_menu.blocksMax.find(Layout::kSpellCard);
            const auto fullRect = full != g_menu.blocksMax.end() ? full->second : cardRect;
            const auto sig = show ? std::format("{}|{:.1f}x{:.1f}|{:.1f}|{}|{}|{}|{}", static_cast<int>(g_menu.tab), fullRect.Width(), fullRect.Height(),
                                        g_menu.rowSize, infoSig, view.sub, view.footRight, g_menu.layout.active)
                                  : std::string{};

            // user 2026-10-30 (the board): nothing chosen - our card collapses to one dim line
            if (BlockRules::EmptyLookOf(Layout::kSpellCard, !g_menu.chosen, false) == BlockRules::EmptyLook::kLineOnly) {
                const auto emptySig = std::format("empty|{}|{:.1f}x{:.1f}|{:.1f}|{}", static_cast<int>(g_menu.tab), fullRect.Width(), fullRect.Height(),
                    g_menu.rowSize, g_menu.layout.active);
                if (emptySig != g_menu.ownCardSig) {
                    g_menu.ownCardSig = emptySig;
                    g_menu.ownCardShown = true;
                    RedrawEmptyCard(a_movie, a_widget, cardRect);
                    ApplyVisibility(a_widget);
                }
                return;
            }
            const auto [selection, expected] = CardSelection(a_movie);
            const bool chosen = selection != "none" && !selection.starts_with("-1|");
            if (selection != g_menu.c1Selection) {
                const auto form = SelectionForm(selection);
                g_menu.c1SameForm = !form.empty() && form != "00000000" && form == g_menu.c1Form;
                g_menu.c1Form = form;
                g_menu.c1Selection = selection;
                g_menu.c1InfoAtSelection = g_menu.c1LastInfo;
                g_menu.c1SelectionFrame = g_menu.frame;
                g_menu.c1Pending = chosen;
            }
            // safety net (review v7 round 1): a selection whose card info is no object for kC1NoInfoFrames frames - SkyUI's card is
            // shown again (bOwnCard=0's way) for the rest of this menu
            if (chosen && !info.IsObject()) {
                if (g_menu.c1NoInfoSince < 0) g_menu.c1NoInfoSince = g_menu.frame;
                if (g_menu.frame - g_menu.c1NoInfoSince >= kC1NoInfoFrames) {
                    g_menu.ownCardFallback = true;
                    DrawBoard(a_movie, a_widget);  // review R3: SkyUI's card is back under the board - edge only
                    if (pair) {
                        pair->second.SetMember("_visible", true);
                        RE::GFxValue background;
                        if (pair->first.GetMember("background", &background) && background.IsDisplayObject()) background.SetMember("_visible", true);
                    }
                    if (g_menu.moreHudAlpha && a_movie->GetVariable(&moreHud, kMoreHudCardPath) && moreHud.IsDisplayObject()) {
                        moreHud.SetMember("_alpha", *g_menu.moreHudAlpha);
                    }
                    logger::warn("Widget: C1 no card info for selection {} in {} frames - SkyUI's own card is back for this menu", selection, kC1NoInfoFrames);
                    SelfTest(std::format("C1 card FAIL - no card info object for {} frames; fell back to SkyUI's card", kC1NoInfoFrames));
                    g_menu.fitHeight.erase(Layout::kSpellCard);  // review GPT 3: SkyUI's card keeps its slot's size, not our card's fit
                    g_menu.heldShrink.erase(Layout::kSpellCard);  // and no shrink of our card left waiting (Task 8)
                    Rearrange(a_movie, a_widget);  // SkyUI's card does not fill its column (ArrangementFor)
                    ApplyVisibility(a_widget);
                    return;
                }
            } else {
                g_menu.c1NoInfoSince = -1;
            }
            if (sig != g_menu.ownCardSig) {
                g_menu.ownCardSig = sig;
                g_menu.ownCardShown = show;
                if (show) {
                    if (g_menu.cardTypesLogged.insert(view.type).second) {
                        const bool known = view.type == kTypeSpell || view.type == kTypePower || view.type == kTypeShout || view.type == kTypeEffect;
                        logger::info("Widget: C1 card type {}{} - fields: {}", view.type, known ? "" : " (not known: name and description only)", KeysOf(info));
                    }
                    RedrawOwnCard(a_movie, a_widget, view, cardRect);
                }
                ApplyVisibility(a_widget);
            }
            g_menu.c1LastInfo = infoSig;
            if (!g_menu.c1Pending) return;
            const int frames = g_menu.frame - g_menu.c1SelectionFrame;
            // the card counts as this selection's once its info changed after the selection came (a name alone could be another
            // spell's, review v7 round 1 #3) and carries the entry's name when the entry names it; an unchanged card counts only
            // as the same item again - the same form, or on Active Effects two effects alike (logged as such)
            const bool updated = show && infoSig != g_menu.c1InfoAtSelection;
            const bool named = expected.empty() || view.name == expected;
            const bool identity = show && named && !updated && frames > kC1Frames && (g_menu.c1SameForm || g_menu.tab == Tab::kEffects);
            if (show && named && (updated || identity)) {
                g_menu.c1Pending = false;
                RE::GFxValue content;
                auto field = ContentChild(a_widget, Layout::kSpellCard, "name", content);
                const auto shown = field.IsDisplayObject() ? StringMember(field, "text") : std::string("(no field)");
                const bool cut = shown.size() > 3 && shown.ends_with(kEllipsis) && view.name.starts_with(shown.substr(0, shown.size() - 3));
                const bool pass = shown == view.name || cut;
                const auto desc = view.effects.empty() ? std::string("SKIP (no description)") : CheckDescription(a_widget, a_movie, view.type);
                const bool descFail = desc.starts_with("FAIL");
                if (!pass || descFail || ++g_menu.c1Logs <= kMaxC1Logs) {
                    SelfTest(std::format("C1 card {} - type {} \"{}\" (field \"{}\"{}), selection {} after {} frame(s){}", pass ? "PASS" : "FAIL", view.type,
                        view.name, shown, cut ? ", cut" : "", selection, frames,
                        identity ? (g_menu.c1SameForm ? ", the same form again (card unchanged)" : ", an effect alike (card unchanged)") : ""));
                    SelfTest(std::format("C1 desc {}", desc));
                }
            } else if (frames > kC1Frames) {
                g_menu.c1Pending = false;
                SelfTest(std::format("C1 card FAIL - selection {} (expects \"{}\") not on the card after {} frames: it shows \"{}\" (type {}{})", selection,
                    expected, frames, view.name, view.type,
                    !show ? ", nothing shown" : !updated ? ", the card did not change (stale, or another item exactly alike)" : ""));
            }
        }

        // ---------------------------------------------------------------------
        // Layout mode (blocks plan Task 4, D5/D8/D10/D11; spec "Input boundary (Task 0)"): the lock and reset buttons below the
        // left column, the mouse shield (layer 3), a cover per shown block that drags it, the column border handle, save and
        // cancel. Layer 1 of the input boundary is Input.cpp's MenuControls hook; layer 2 the handleInput and AttemptEquip wraps
        // here. The hook flips g_layoutMode at once (later events of its batch see it) and queues the UI work; the frame handler
        // does every change of the movie (LayoutFrame), as for the node clicks
        // ---------------------------------------------------------------------

        constexpr const char* kShieldName = "layoutShield";    // full stage, the lowest depth in mmx_root, shown in the mode only
        constexpr const char* kBorderName = "layoutBorder";    // the border handle in the gap between the columns (the mode only)
        constexpr const char* kBoardName = "board";             // user 2026-10-30: the board behind the columns (DrawBoard)
        constexpr const char* kGearName = "layoutGear";        // user 2026-10-30: the layout mode's toggle, under SkyUI's list (always)
        constexpr double kGearSize = 18.0;                     // its square's side
        constexpr double kBoardStripGap = 8.0;                 // the key-hint strip under the board
        constexpr double kGearGap = 4.0;                       // the gear one row under the strip, its left edge on the strip's start
        constexpr double kStripTolerance = 0.5;                // the panel's origin is where it should be, in its parent's space (Flash
                                                               // keeps _x in twips; under Inventory Reforged's 0.85 about 0.43 on screen)
        constexpr double kStripCheckTolerance = 1.0;           // the read-back after a move (SelfTest L8)
        constexpr const char* kButtonPanelPath = "_root.Menu_mc.bottomBar.buttonPanel";  // the key-hint strip (ItemMenu.as:73 navPanel)
        constexpr int kMaxStripMoveLogs = 5;
        constexpr int kClipDumpFrame = 30;                     // the frame of a menu the one-time clip dump runs at (SkyUI built by then)
        constexpr double kGearLineW = 1.3;     // its line, scaled with the gear (noScale "normal"), round caps and joins
        constexpr double kGearMiterLimit = 3.0;  // lineStyle's miterLimit (unused with round joins; AS2 wants the argument)
        constexpr const char* kOverlayName = "layoutOverlay";  // the drag ghost, the insertion line, the border guide: on top
        constexpr const char* kCoverPrefix = "mmxCover_";      // a block's drag cover, in its column clip above the panels
        constexpr const char* kCoverBlock = "mmxBlock";        // a cover's block id
        constexpr std::string_view kGamepadSource = Widget::kGamepadToggleSource;
        constexpr const char* kHandleInputName = "handleInput";  // ItemMenu.as:205 / MagicMenu.as:80
        constexpr const char* kSavedHandleInput = "mmxSavedHandleInput";
        constexpr const char* kAttemptEquipName = "AttemptEquip";  // MagicMenu.as:111
        constexpr const char* kSavedAttemptEquip = "mmxSavedAttemptEquip";
        constexpr const char* kShowPanelPath = "_global.InventoryLists.SHOW_PANEL";  // InventoryLists.as:34 (static, 1)
        constexpr const char* kResetName = "layoutReset";       // user 2026-10-30: the reset button right of the gear (the edit mode)
        constexpr double kResetGap = 4.0, kResetPadX = 8.0, kResetTextSize = 12.0, kResetAlpha = 92.0;
        constexpr double kGreyAlpha = 40.0, kDraggedAlpha = 45.0, kGhostAlpha = 25.0, kLineW = 2.0, kHandleLineAlpha = 80.0;
        constexpr double kMinHandleW = 6.0;  // the border handle's hit strip at least this wide (fPanelGap may be 0)
        constexpr double kAutoScrollEdge = 28.0, kAutoScrollStep = 8.0;  // D8: within this of a column's top/bottom the column scrolls, per frame
        constexpr int kMaxDragLogs = 24;
        // Task 5: the focus look (on the block's cover), the pick's keep-in-view margin around its insertion line, the right stick.
        // No accent colour (UI polish 2026-10-29): what rests (focus outline, drag ghost) is kTitleColor, what acts (the picked
        // block's edge, the insertion line, the border guide) the brighter kWhite
        constexpr double kFocusEdgeW = 2.0, kPickEdgeW = 3.0, kLiftFillAlpha = 12.0;
        constexpr double kLineViewMargin = 24.0;  // stage units kept in view above and below the pick's insertion line
        constexpr double kStickStep = 14.0;       // stage units per frame at full tilt (unverified feel: eyes-only)
        constexpr int kMaxPadLogs = 40;

        std::atomic<bool> g_layoutMode{ false };            // the input boundary's view: kept from the accepted toggle to the leave
        std::atomic<std::uint64_t> g_menuGeneration{ 0 };   // bumped at every attach (D8: a drag session is of one menu)
        bool g_inHandleInput = false, g_inAttemptEquip = false;  // CallSavedMember's re-entry guards (UI thread only)

        // Why the mode may not start (spec "Entry refusals"). The frame handler publishes the movie's part each frame (the hook
        // cannot read the movie on its thread); the toggle adds what it sees live
        enum : std::uint32_t
        {
            kBlockNoWidget = 1u << 0,
            kBlockNoHook = 1u << 1,
            kBlockNoInputWrap = 1u << 2,
            kBlockNoEquipWrap = 1u << 3,
            kBlockTyping = 1u << 4,
            kBlockListBusy = 1u << 5,
            kBlockFading = 1u << 6,
            kBlockPanelState = 1u << 7,
            kBlockSafetyNet = 1u << 8,
            kBlockOtherMenu = 1u << 9,
            kBlockHeld = 1u << 10,
        };
        std::atomic<std::uint32_t> g_layoutBlock{ kBlockNoWidget };
        // "mode refused" lines per menu: a K typed into another menu's text field is refused each time (later ones not logged)
        constexpr int kMaxRefusalLogs = 8;
        std::atomic<int> g_refusalLogs{ 0 };


        enum class LayoutRequestKind
        {
            kEnter,
            kSave,
            kCancel,
            kNav,  // Task 5: a key of the focus model
        };
        struct LayoutRequest
        {
            LayoutRequestKind kind;
            std::string why;
            Layout::Nav nav{};
            std::uint64_t seq = 0;  // kEnter / kSave / kCancel: the mode ledger's number (final review: the newest intent wins)
        };
        std::mutex g_layoutRequestLock;
        std::vector<LayoutRequest> g_layoutRequests;
        // the input's view of the mode (g_layoutMode mirrors its flag), under g_layoutRequestLock: every change of intent takes a
        // number, the frame handler's result is written only for the newest one (Layout::ModeLedger)
        Layout::ModeLedger g_modeLedger;

        // Menus that pause the game, use the menu context or are modal, opened while the magic menu is open (the mode ends as
        // Cancel when one opens, and does not start while one is up)
        std::mutex g_blockingLock;
        std::set<std::string> g_blockingMenus;

        void QueueLayout(LayoutRequestKind a_kind, std::string a_why, Layout::Nav a_nav = {})
        {
            const std::scoped_lock lock(g_layoutRequestLock);
            g_layoutRequests.push_back({ a_kind, std::move(a_why), a_nav });
        }

        // g_layoutRequestLock held: a mode request (kEnter on, kSave / kCancel off) - the flag set at once, the request numbered
        void QueueModeLocked(LayoutRequestKind a_kind, std::string a_why)
        {
            const auto seq = g_modeLedger.Request(a_kind == LayoutRequestKind::kEnter);
            g_layoutMode = g_modeLedger.on;
            g_layoutRequests.push_back({ a_kind, std::move(a_why), {}, seq });
        }

        // The frame handler's own change (the lock button, a tab change, the leave by B / Esc): the newest intent, numbered
        std::uint64_t BeginModeChange(bool a_on)
        {
            const std::scoped_lock lock(g_layoutRequestLock);
            const auto seq = g_modeLedger.Request(a_on);
            g_layoutMode = g_modeLedger.on;
            return seq;
        }

        // The frame handler's result for request a_seq: whether the mode is on now (g_menu.layout.active); kept only while no newer
        // request was made (that one settles it when it runs)
        void SettleMode(std::uint64_t a_seq, bool a_on)
        {
            const std::scoped_lock lock(g_layoutRequestLock);
            g_modeLedger.Settle(a_seq, a_on);
            g_layoutMode = g_modeLedger.on;
        }

        // After the frame's mode work (the end of LayoutFrame, a tab change): with no request left queued, the frame's state
        // (g_menu.layout.active) is the input's flag (Layout::ModeLedger::Sync); a queued one settles it when it runs
        void SyncMode(bool a_on)
        {
            const std::scoped_lock lock(g_layoutRequestLock);
            if (g_closing) return;  // a frame overlapping the close must not turn the mode back on after LayoutAtClose
            g_modeLedger.Sync(!g_layoutRequests.empty(), a_on);
            g_layoutMode = g_modeLedger.on;
        }

        // g_layoutRequestLock held: no mode and no queued request (a menu's open and close); a request still running settles nothing
        void ResetModeLocked()
        {
            g_modeLedger.Request(false);
            g_layoutMode = false;
            g_layoutRequests.clear();
        }

        // blocks plan Task 5: the focus model - the focused block and, while it is picked up, its target slot. The frame handler's
        // (PadKey, PadFrame); reset at each attach, mode entry and leave
        Layout::Focus g_layoutFocus;
        // the right stick's y while the mode keeps it (the hook writes it, 0 = neutral; the frame handler scrolls by it)
        std::atomic<float> g_layoutStickY{ 0.0f };

        std::string BlockText(std::uint32_t a_bits)
        {
            constexpr std::array<std::pair<std::uint32_t, const char*>, 11> kReasons{ {
                { kBlockNoWidget, "the widget is not up in this menu" },
                { kBlockNoHook, "the MenuControls hook is not installed (SkyUI could not be stopped)" },
                { kBlockNoInputWrap, "the handleInput wrap is not installed (no leak detector)" },
                { kBlockNoEquipWrap, "the AttemptEquip wrap is not installed" },
                { kBlockTyping, "a text field takes keys (the search box)" },
                { kBlockListBusy, "the list takes no input (disableInput / disableSelection: a dialog or the 3D rotation)" },
                { kBlockFading, "the menu is fading (bFadedIn false)" },
                { kBlockPanelState, "the list panel is not shown (inventoryLists.currentState is not SHOW_PANEL)" },
                { kBlockSafetyNet, "safety net" },
                { kBlockOtherMenu, "another menu is open over this one" },
                { kBlockHeld, "another button is held" },
            } };
            std::string text;
            for (const auto& [bit, reason] : kReasons) {
                if (!(a_bits & bit)) continue;
                text += (text.empty() ? "" : "; ") + std::string(reason);
                if (bit == kBlockSafetyNet) text += " (" + SafetyNetReason() + ")";
            }
            return text;
        }

        void LogRefusal(std::uint32_t a_bits, const char* a_source)
        {
            const int logged = ++g_refusalLogs;
            if (logged > kMaxRefusalLogs) return;
            logger::info("Layout: mode refused - {} ({}; the press is passed on){}", BlockText(a_bits), a_source,
                logged == kMaxRefusalLogs ? " - the last such line this menu" : "");
        }

        bool TypingText()
        {
            auto* controls = RE::ControlMap::GetSingleton();
            return controls && controls->GetRuntimeData().textEntryCount > 0;
        }

        // The movie's flags as last published, and what is seen now
        std::uint32_t LiveBlock(bool a_heldOther)
        {
            std::uint32_t bits = g_layoutBlock;
            if (!Input::HookInstalled()) bits |= kBlockNoHook;
            if (TypingText()) bits |= kBlockTyping;
            if (a_heldOther) bits |= kBlockHeld;
            {
                const std::scoped_lock lock(g_blockingLock);
                if (!g_blockingMenus.empty()) bits |= kBlockOtherMenu;
            }
            return bits;
        }

        std::string Describe(const Layout::State& a_state)
        {
            std::string text;
            for (const auto& column : a_state.columns) {
                text += text.empty() ? "{" : " / {";
                for (std::size_t i = 0; i < column.size(); ++i) text += (i > 0 ? ", " : "") + column[i];
                text += "}";
            }
            return std::format("{} border {:.1f}", text, a_state.border);
        }

        std::optional<std::pair<double, double>> MouseAt(RE::GFxMovie* a_movie)
        {
            RE::GFxValue x, y;
            if (!a_movie || !a_movie->GetVariable(&x, "_root._xmouse") || !x.IsNumber() || !a_movie->GetVariable(&y, "_root._ymouse") || !y.IsNumber()) {
                return std::nullopt;
            }
            return std::pair{ x.GetNumber(), y.GetNumber() };
        }

        // The game window has the focus (Main's gameActive, set by its activate handler - unverified; D8: a drag ends when it goes)
        bool GameActive()
        {
            auto* main = RE::Main::GetSingleton();
            return !main || main->GetRuntimeData().gameActive;
        }

        // gameActive is sampled at each menu open, when the window plainly has the focus: if it reads false then, it does not mean
        // the focus, and the drags do not use it for the rest of the game session (logged once)
        std::atomic<bool> g_focusCheck{ true };

        void CalibrateFocusCheck()
        {
            if (!g_focusCheck || GameActive()) return;
            g_focusCheck = false;
            logger::warn("Layout: Main gameActive reads false with the menu just opened - it is not the window focus here; a drag no longer ends on "
                         "focus loss (only on release, Esc or the menu's close) for this game session");
        }

        bool FocusGone()
        {
            return g_focusCheck && !GameActive();
        }

        // --- handlers (they only record; LayoutFrame acts) ---

        class CoverPress final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params& a_params) override
            {
                if (!a_params.thisPtr || g_closing || !g_menu.layout.active) return;
                const auto at = MouseAt(a_params.movie);
                const auto id = StringMember(*a_params.thisPtr, kCoverBlock);
                if (at && !id.empty()) g_menu.layout.pressDue = { id, at->first, at->second };
            }
        };

        // onRelease and onReleaseOutside (D8: the mouse up anywhere ends the drag)
        class CoverRelease final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params&) override
            {
                if (!g_closing && g_menu.layout.active) g_menu.layout.releaseDue = true;
            }
        };

        class BorderPress final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params& a_params) override
            {
                if (g_closing || !g_menu.layout.active) return;
                if (const auto at = MouseAt(a_params.movie)) {
                    g_menu.layout.borderPressDue = true;
                    g_menu.layout.borderPressX = at->first;
                }
            }
        };

        class BorderRelease final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params&) override
            {
                if (!g_closing && g_menu.layout.active) g_menu.layout.borderReleaseDue = true;
            }
        };

        // The lock button's and the gear's click (a_source names it in the log lines)
        class LockClick final : public RE::GFxFunctionHandler
        {
        public:
            explicit LockClick(const char* a_source) : _source(a_source) {}

            void Call(Params&) override
            {
                if (g_closing) return;
                g_menu.layout.lockDue = true;
                g_menu.layout.lockSource = _source;
            }

        private:
            const char* _source;
        };

        class ResetClick final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params&) override
            {
                // user 2026-10-30: the reset button takes a click only once fully out (a click while it unfolds is ignored)
                if (g_closing) return;
                if (!BlockRules::ResetClickable(g_menu.resetShownW, g_menu.resetFullW, g_menu.layout.active)) {
                    ++g_menu.resetIgnored;
                    return;
                }
                g_menu.layout.resetDue = true;
                ++g_menu.resetTaken;
                // Task 8 T2 (L9 reset): the widths at the click itself, not the rule - a taken click with the box not fully out fails
                if (!g_menu.layout.active || g_menu.resetShownW < g_menu.resetFullW - BlockRules::kHeightTolerance) ++g_menu.resetTakenEarly;
            }
        };

        // Layer 3: the shield's press does nothing - it is there so Mouse.getTopMostEntity() is never one of SkyUI's clips
        class ShieldPress final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params&) override { ++g_menu.layout.shieldPresses; }
        };

        // A_this's saved member called with the this and the arguments it would have had; its return value handed back.
        // a_busy guards re-entry (Inventory Reforged 1.6.2 cardInfoUnder, its 10-core.as:922): a mod that wraps the same
        // member over ours and calls ours again from inside its own (Follower Equip Control did so to IR's card wrap) would
        // go round in a circle until Flash drops the call. A call that comes back in while ours runs goes to the class's own
        // member (this.__proto__[a_name]) instead; when that cannot be read the call is dropped, never looped. Limit: a mod
        // under ours that calls this.handleInput again with other arguments (a remapped key) now skips the wraps between us
        // and SkyUI on that second call; none in TAKEALOOK does (reviewer, 2026-10-28)
        enum class SavedCall
        {
            kCalled,
            kNoSaved,  // no saved original on this object
            kDropped,  // re-entered and the class's own member not readable (logged here)
            kFailed    // Invoke returned false (Flash refused the call)
        };
        SavedCall CallSavedMember(RE::GFxFunctionHandler::Params& a_params, const char* a_saved, const char* a_name, bool& a_busy)
        {
            RE::GFxValue original;
            if (a_busy) {
                RE::GFxValue proto;
                const bool found = a_params.thisPtr->GetMember("__proto__", &proto) && proto.IsObject() && proto.GetMember(a_name, &original) &&
                                   original.IsObject();
                static std::atomic<int> s_reentryLogs{ 0 };
                if (s_reentryLogs++ < kMaxHoverLogs) {
                    logger::warn("Widget: {} came back into our wrap while it ran (another mod wraps it over ours) - {}", a_name,
                        found ? "the class's own one called" : "the class's own one not readable, the call dropped");
                }
                if (!found) return SavedCall::kDropped;
            } else if (!a_params.thisPtr->GetMember(a_saved, &original) || !original.IsObject()) {
                return SavedCall::kNoSaved;
            }
            std::vector<RE::GFxValue> argv;
            argv.reserve(a_params.argCount + 1);
            argv.push_back(*a_params.thisPtr);
            for (std::uint32_t i = 0; i < a_params.argCount; ++i) argv.push_back(a_params.args[i]);
            RE::GFxValue result;
            const bool outer = std::exchange(a_busy, true);
            const bool called = original.Invoke("call", &result, argv.data(), argv.size());
            a_busy = outer;
            if (called && a_params.retVal) *a_params.retVal = result;
            return called ? SavedCall::kCalled : SavedCall::kFailed;
        }

        // A failed CallSavedMember's own warning, once per kind and member (a dropped re-entry is logged in CallSavedMember)
        void WarnSavedCall(SavedCall a_outcome, const char* a_name, std::atomic<bool>& a_noSavedLogged, std::atomic<bool>& a_failedLogged)
        {
            if (a_outcome == SavedCall::kNoSaved && !a_noSavedLogged.exchange(true)) {
                logger::warn("Layout: {} wrap without its saved original - the call is dropped (logged once)", a_name);
            } else if (a_outcome == SavedCall::kFailed && !a_failedLogged.exchange(true)) {
                logger::warn("Layout: {} wrap could not call its original (Flash refused the call) - dropped (logged once)", a_name);
            }
        }

        // Layer 2 (spec): Menu_mc.handleInput - FocusHandler sends every Scaleform key to path[0].handleInput only (FocusHandler.as:
        // 71-108), which is Menu_mc. In the mode SkyUI's is not called (true returned: handled) and the call is counted as a leak
        // (layer 1 should have kept the key); off, SkyUI's runs, and its keyboard keyDowns are noted for the route-parity test
        class HandleInputWrap final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params& a_params) override
            {
                if (!a_params.thisPtr) return;
                const RE::GFxValue* details = a_params.argCount > 0 && a_params.args[0].IsObject() ? &a_params.args[0] : nullptr;
                auto& m = g_menu.layout;
                // the live mode alone (Task 4 review): after the toggle's leave the hook passes keys while m.active waits for the frame
                // handler; such a key is SkyUI's, not a leak
                if (g_layoutMode) {
                    if (++m.inputLeaks == 1) {
                        logger::warn("Layout: input leak code {} value {} nav {} - reached SkyUI's handleInput in the layout mode, not called (later ones "
                                     "counted)",
                            details ? Number(*details, "code").value_or(-1) : -1, details ? StringMember(*details, "value") : std::string{},
                            details ? StringMember(*details, "navEquivalent") : std::string{});
                    }
                    if (a_params.retVal) *a_params.retVal = true;
                    return;
                }
                if (details && StringMember(*details, "value") == "keyDown") {
                    if (const auto code = Number(*details, "skseKeycode")) {
                        if (*code >= 0 && *code < static_cast<double>(SKSE::InputMap::kMacro_NumKeyboardKeys)) {
                            m.wrapKeysOff.insert(static_cast<std::uint32_t>(*code));
                            ++m.wrapKeyDownsOff;
                        }
                    } else {
                        ++m.wrapNoSkseCode;
                    }
                }
                const auto outcome = CallSavedMember(a_params, kSavedHandleInput, kHandleInputName, g_inHandleInput);
                if (outcome != SavedCall::kCalled) {
                    // handled, so FocusHandler does not move the focus for a key nobody saw (FocusHandler.as:76, 101-104)
                    if (a_params.retVal) *a_params.retVal = true;
                    static std::atomic<bool> s_noSaved{ false }, s_failed{ false };
                    WarnSavedCall(outcome, kHandleInputName, s_noSaved, s_failed);
                }
            }
        };

        // Layer 2: Menu_mc.AttemptEquip (the game calls it for LT/RT and mouse equips; on the gamepad platform SkyUI does not check
        // the mouse is over the list, ItemMenu.as:498-523) - a no-op in the mode
        class AttemptEquipWrap final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params& a_params) override
            {
                if (!a_params.thisPtr) return;
                if (g_layoutMode) {  // the live mode alone, as handleInput (an LT/RT the hook passed after the leave is an equip)
                    if (++g_menu.layout.equipKept == 1) logger::info("Layout: AttemptEquip in the layout mode - not called (later ones counted)");
                    return;
                }
                const auto outcome = CallSavedMember(a_params, kSavedAttemptEquip, kAttemptEquipName, g_inAttemptEquip);
                if (outcome != SavedCall::kCalled) {
                    static std::atomic<bool> s_noSaved{ false }, s_failed{ false };
                    WarnSavedCall(outcome, kAttemptEquipName, s_noSaved, s_failed);
                }
            }
        };

        RE::GPtr<CoverPress> g_coverPress;
        RE::GPtr<CoverRelease> g_coverRelease;
        RE::GPtr<BorderPress> g_borderPress;
        RE::GPtr<BorderRelease> g_borderRelease;
        RE::GPtr<ResetClick> g_resetClick;
        RE::GPtr<ShieldPress> g_shieldPress;
        RE::GPtr<HandleInputWrap> g_handleInputWrap;
        RE::GPtr<AttemptEquipWrap> g_attemptEquipWrap;

        // a_object's own member a_name replaced by a wrap, the original kept as a_saved (the SelectionChangeWrap pattern). True
        // when the wrap is on (already, or now)
        bool WrapMember(RE::GFxMovieView* a_movie, RE::GFxValue& a_object, const char* a_name, const char* a_saved, RE::GFxFunctionHandler* a_handler)
        {
            RE::GFxValue current, saved, wrap;
            if (a_object.GetMember(a_saved, &saved) && saved.IsObject()) return true;
            if (!a_object.GetMember(a_name, &current) || !current.IsObject()) return false;
            // The re-entry guard's target (CallSavedMember) is this.__proto__[a_name]: the log says whether it can be read and
            // whether it is the function saved here (it differs when another mod wrapped the member before us)
            RE::GFxValue proto, inherited;
            const bool readable = a_object.GetMember("__proto__", &proto) && proto.IsObject() && proto.GetMember(a_name, &inherited) &&
                                  inherited.IsObject();
            logger::info("Layout: {} - the class's own one {}", a_name,
                !readable                ? "NOT readable (a re-entry would be dropped)" :
                inherited == current     ? "is the one saved (SkyUI's own)" :
                                           "differs from the one saved (another mod wrapped it first)");
            a_movie->CreateFunction(&wrap, a_handler);
            if (a_object.SetMember(a_saved, current) && a_object.SetMember(a_name, wrap)) return true;
            a_object.SetMember(a_name, current);
            return false;
        }

        // Once per menu movie from the frame handler, tried for kSelectionInstallFrames frames until Menu_mc.handleInput is there
        void InstallInputWraps(RE::GFxMovieView* a_movie)
        {
            auto& m = g_menu.layout;
            if (m.wrapsDone) return;
            RE::GFxValue menu, probe;
            const bool ready = a_movie->GetVariable(&menu, kMenuPath) && menu.IsObject() && menu.GetMember(kHandleInputName, &probe) && probe.IsObject();
            if (!ready && --m.wrapsLeft > 0) return;
            m.wrapsDone = true;
            if (menu.IsObject()) {
                m.inputWrapped = WrapMember(a_movie, menu, kHandleInputName, kSavedHandleInput, g_handleInputWrap.get());
                m.equipWrapped = WrapMember(a_movie, menu, kAttemptEquipName, kSavedAttemptEquip, g_attemptEquipWrap.get());
            }
            RE::GFxValue showPanel;
            const bool showRead = a_movie->GetVariable(&showPanel, kShowPanelPath) && showPanel.IsNumber();
            if (showRead) m.showPanel = showPanel.GetNumber();
            logger::info("Layout: input wraps - {}.{} {}, {}.{} {}; SHOW_PANEL {}{}", kMenuPath, kHandleInputName, m.inputWrapped ? "wrapped" : "NOT wrapped",
                kMenuPath, kAttemptEquipName, m.equipWrapped ? "wrapped" : "NOT wrapped", m.showPanel,
                showRead ? "" : std::format(" (assumed: {} not readable)", kShowPanelPath));
        }

        // Each frame: the movie's part of the entry refusals, for the hook (which may not read the movie)
        void PublishLayoutBlock(RE::GFxMovieView* a_movie)
        {
            const auto& m = g_menu.layout;
            std::uint32_t bits = 0;
            if (!m.inputWrapped) bits |= kBlockNoInputWrap;
            if (!m.equipWrapped) bits |= kBlockNoEquipWrap;
            if (!SafetyNetReason().empty()) bits |= kBlockSafetyNet;
            RE::GFxValue menu, lists, itemList, faded;
            if (a_movie->GetVariable(&menu, kMenuPath) && menu.IsObject() && menu.GetMember("bFadedIn", &faded) && faded.IsBool() && !faded.GetBool()) {
                bits |= kBlockFading;
            }
            if (a_movie->GetVariable(&lists, kListsPath) && lists.IsObject()) {
                if (const auto state = Number(lists, "currentState"); state && *state != m.showPanel) bits |= kBlockPanelState;
                if (lists.GetMember("itemList", &itemList) && (BoolMember(itemList, "disableInput") || BoolMember(itemList, "disableSelection"))) {
                    bits |= kBlockListBusy;
                }
            }
            g_layoutBlock = bits;
        }

        // --- the chrome: shield, buttons, border handle, covers, overlay ---

        void ShowShield(RE::GFxValue& a_widget, bool a_shown)
        {
            if (auto shield = Child(a_widget, kShieldName); shield.IsDisplayObject()) shield.SetMember("_visible", a_shown);
        }

        // Made first in mmx_root (the lowest depth: blocks, buttons and handle lie above it), over the movie's visible stage
        void MakeShield(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            auto shield = Draw::Clip(a_widget, kShieldName);
            if (!shield.IsDisplayObject()) {
                logger::warn("Layout: no mouse shield - the layout mode will not start this menu");
                return;
            }
            const auto area = a_movie->GetVisibleFrameRect();
            Draw::NoLine(shield);
            Draw::Rect(shield, area.left, area.top, area.right - area.left, area.bottom - area.top, kPanelFill, 0.0);  // alpha 0 still takes the mouse
            SetHandler(a_movie, shield, "onPress", g_shieldPress.get());
            shield.SetMember("useHandCursor", false);
            shield.SetMember("_visible", false);
        }

        std::string CoverName(const BlockId& a_id)
        {
            return kCoverPrefix + std::string(PanelName(a_id));  // Task 6: an add-on's id may hold '.'; its clip name does not
        }

        void RemoveCovers(RE::GFxValue& a_widget)
        {
            for (const char* columnName : kColumnClips) {
                auto column = Child(a_widget, columnName);
                if (!column.IsDisplayObject()) continue;
                for (const BlockId& id : AllBlocks()) Draw::Remove(column, CoverName(id).c_str());
            }
        }

        // Task 5: the focus outline and the picked block's lifted look, drawn on its cover (in the column clip: masked and scrolled
        // with the block). Picked: a thicker, brighter edge and a light fill (no shadow band: UI polish 2026-10-29)
        void DrawFocusLook(RE::GFxValue& a_cover, double a_w, double a_h, bool a_picked)
        {
            if (!a_picked) {
                Draw::LineStyle(a_cover, kFocusEdgeW, kTitleColor);
                Draw::Rect(a_cover, kFocusEdgeW / 2, kFocusEdgeW / 2, a_w - kFocusEdgeW, a_h - kFocusEdgeW, kPanelFill, 0.0);
                return;
            }
            Draw::LineStyle(a_cover, kPickEdgeW, kWhite);
            Draw::Rect(a_cover, kPickEdgeW / 2, kPickEdgeW / 2, a_w - kPickEdgeW, a_h - kPickEdgeW, kWhite, kLiftFillAlpha);
        }

        // In the mode: a transparent cover over each visible block (VisibleBlocks: ApplyVisibility's rules), above every panel of its
        // column (made again after any panel is made again), carrying the drag and the focus look (Task 5). It also takes the
        // add-on blocks' clicks and roll-overs (off in the mode). Scrolls with its column clip
        void SetCovers(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            RemoveCovers(a_widget);
            g_menu.layout.coverSet.clear();
            if (!g_menu.layout.active) return;
            const auto visible = VisibleBlocks();
            g_menu.layout.coverSet = visible;
            for (const BlockId& id : AllBlocks()) {
                const auto column = ColumnOf(id);
                if (!column || !visible.contains(id)) continue;
                auto parent = Child(a_widget, kColumnClips[*column]);
                if (!parent.IsDisplayObject()) continue;
                const auto name = CoverName(id);
                auto cover = Draw::Clip(parent, name.c_str());
                if (!cover.IsDisplayObject()) continue;
                const auto rect = BlockRect(id);
                const auto local = BlockLocal(id, rect);
                cover.SetMember("_x", local.left);
                cover.SetMember("_y", local.top);
                Draw::NoLine(cover);
                Draw::Rect(cover, 0, 0, rect.Width(), rect.Height(), kPanelFill, 0.0);
                if (g_layoutFocus.focus == id) DrawFocusLook(cover, rect.Width(), rect.Height(), g_layoutFocus.Picked());
                PutString(a_movie, cover, kCoverBlock, id);
                SetHandler(a_movie, cover, "onPress", g_coverPress.get());
                SetHandler(a_movie, cover, "onRelease", g_coverRelease.get());
                SetHandler(a_movie, cover, "onReleaseOutside", g_coverRelease.get());
            }
        }

        // The gear (BlockRules::GearOutline, shape B) in a_color, centred on a_c: the SR block's line (Flash 8 lineStyle with round
        // caps and joins, BlockDraw.cpp), each outline closed on its first point
        void DrawGear(RE::GFxValue& a_clip, double a_c, double a_r, std::uint32_t a_color)
        {
            Draw::Call(a_clip, "lineStyle", { kGearLineW, static_cast<double>(a_color), Draw::kAlphaFull, false, "normal", "round", "round", kGearMiterLimit });
            const auto shape = BlockRules::GearOutline(a_c, a_c, a_r);
            for (const auto* outline : { &shape.rim, &shape.hole }) {
                if (outline->empty()) continue;
                Draw::MoveTo(a_clip, outline->front().x, outline->front().y);
                for (std::size_t i = 1; i < outline->size(); ++i) Draw::LineTo(a_clip, (*outline)[i].x, (*outline)[i].y);
            }
        }

        // The gear's look: lit (kWhite) on hover and for the whole edit mode, its hint beside it on hover - not while the reset
        // button is out or moving (it sits where the hint would go)
        void ShowGearLook(RE::GFxValue& a_gear)
        {
            const bool lit = g_menu.gearHover || g_menu.layout.active;
            RE::GFxValue dim, bright, hint;
            if (a_gear.GetMember("dim", &dim) && dim.IsDisplayObject()) dim.SetMember("_visible", !lit);
            if (a_gear.GetMember("lit", &bright) && bright.IsDisplayObject()) bright.SetMember("_visible", lit);
            if (a_gear.GetMember("hint", &hint) && hint.IsDisplayObject()) {
                hint.SetMember("_visible", g_menu.gearHover && (g_menu.resetShownW <= 0.0 || g_menu.gearHintLeft));
            }
        }

        class GearHover final : public RE::GFxFunctionHandler
        {
        public:
            explicit GearHover(bool a_over) : _over(a_over) {}

            void Call(Params& a_params) override
            {
                if (!a_params.thisPtr || g_closing) return;
                g_menu.gearHover = _over;
                ShowGearLook(*a_params.thisPtr);
            }

        private:
            bool _over;
        };
        RE::GPtr<GearHover> g_gearOver, g_gearOut;
        RE::GPtr<LockClick> g_gearClick;

        // The gear made in mmx_root above the shield (it works in the mode too): a transparent square that takes the mouse, the
        // gear in kTitleColor and, on hover, in kWhite; its click toggles the mode (LockClick: the K path through lockDue).
        // Placed by PlaceStripAndGear under the key-hint strip
        void MakeGear(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            auto gear = Draw::Clip(a_widget, kGearName);
            if (!gear.IsDisplayObject()) {
                logger::warn("Layout: the gear button could not be made - the layout mode starts with the K key only this menu");
                return;
            }
            Draw::NoLine(gear);
            Draw::Rect(gear, 0, 0, kGearSize, kGearSize, kPanelFill, 0.0);  // alpha 0 still takes the mouse
            auto dim = Draw::Clip(gear, "dim");
            auto lit = Draw::Clip(gear, "lit");
            if (dim.IsDisplayObject()) DrawGear(dim, kGearSize / 2, kGearSize / 2, kTitleColor);
            if (lit.IsDisplayObject()) {
                DrawGear(lit, kGearSize / 2, kGearSize / 2, kWhite);
                lit.SetMember("_visible", false);
            }
            SetHandler(a_movie, gear, "onRelease", g_gearClick.get());
            SetHandler(a_movie, gear, "onRollOver", g_gearOver.get());
            SetHandler(a_movie, gear, "onRollOut", g_gearOut.get());
            gear.SetMember("useHandCursor", false);
            if (!SafetyNetReason().empty()) gear.SetMember("_alpha", kGreyAlpha);  // the mode rests in the safety-net states
            gear.SetMember("_visible", false);  // until PlaceStripAndGear has a place
        }

        // A clip's own transform for the dump: _x, _y, _xscale, _yscale, _rotation
        std::string TransformText(const RE::GFxValue& a_clip)
        {
            const auto get = [&](const char* a_member) { return Number(a_clip, a_member).value_or(std::numeric_limits<double>::quiet_NaN()); };
            return std::format("at {:.1f},{:.1f} scale {:.1f}/{:.1f} rot {:.1f}", get("_x"), get("_y"), get("_xscale"), get("_yscale"), get("_rotation"));
        }

        void DumpMenuClips(RE::GFxMovieView* a_movie, RE::GFxValue& a_root, RE::GFxValue& a_widget)
        {
            constexpr int kDumpDepth = 3, kMaxDumpLines = 80;
            static bool s_dumped = false;
            if (std::exchange(s_dumped, true)) return;
            RE::GFxValue menu;
            if (!a_movie->GetVariable(&menu, kMenuPath) || !menu.IsDisplayObject()) {
                logger::info("Layout: menu clip dump - no {}", kMenuPath);
                return;
            }
            // the chain above the dumped clips (each dumped clip's line has its own transform, so any clip's chain up to _root reads
            // off the log): _root, our widget (the host's stage space) and Menu_mc
            logger::info("Layout: menu clip _root {} | widget _root.{} {} | {} {}", TransformText(a_root), kRootName, TransformText(a_widget), kMenuPath,
                TransformText(menu));
            int lines = 0, seen = 0;
            std::function<void(RE::GFxValue&, const std::string&, int)> visit = [&](RE::GFxValue& a_clip, const std::string& a_path, int a_depth) {
                std::vector<std::pair<std::string, RE::GFxValue>> children;
                a_clip.VisitMembers([&](const char* a_name, const RE::GFxValue& a_value) {
                    if (a_name && *a_name != '_' && a_value.IsDisplayObject()) children.emplace_back(a_name, a_value);
                });
                for (auto& [name, child] : children) {
                    ++seen;
                    const auto path = a_path + "." + name;
                    if (lines < kMaxDumpLines) {
                        ++lines;
                        const auto visible = BoolMember(child, "_visible");
                        // local (its own space), in the host's stage space through every parent (what the host uses), and Flash's
                        // getBounds(_root) (what the host used before), with the clip's own transform
                        logger::info("Layout: menu clip {} local {} | host {} | getBounds(_root) {} | {}{}", path, BoundsText(GetBounds(child, child)),
                            BoundsText(BoundsIn(child, a_widget)), BoundsText(GetBounds(child, a_root)), TransformText(child), visible ? "" : " hidden");
                    }
                    if (a_depth + 1 < kDumpDepth) visit(child, path, a_depth + 1);
                }
            };
            visit(menu, kMenuPath, 0);
            logger::info("Layout: menu clip dump - {} clip(s) seen to depth {}, {} logged", seen, kDumpDepth, lines);
        }

        Settings::Rect VisibleStage(RE::GFxMovieView* a_movie)  // the movie's visible frame (stage units)
        {
            const auto area = a_movie->GetVisibleFrameRect();
            return { area.left, area.top, area.right, area.bottom };
        }

        // the two columns' full areas' union, the board's base (DrawBoard; SelfTest L9 board recomputes the board from it)
        Settings::Rect BoardColumns()
        {
            const auto left = ColumnViewport(0), right = ColumnViewport(Layout::kColumns - 1);
            return { left.left, std::min(left.top, right.top), right.right, std::max(left.bottom, right.bottom) };
        }

        // user 2026-10-30 (the board, mock board.png): a fixed panel behind both columns - their full areas (the frames: the border
        // between them does not change the union) BlockRules::kBoardPad larger, cut to the visible stage; lighter than the cards
        // (black kBoardAlpha, a kPanelEdge edge at kBoardEdgeAlpha), no title. Made once a menu, under the columns; it does not
        // scroll and never changes size when cards fit. The key-hint strip and the gear hang from its bottom
        // review R3: filled only when our card and the 3D over-pass are on - SkyUI's own card (bOwnCard=0, Inventory Reforged, the
        // C1 fallback) and the game's 3D drawn under the movie (no over-pass) would be darkened by the fill: an edge only then.
        // Drawn again at the C1 fallback
        void DrawBoard(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            constexpr double kBoardAlpha = 38.0, kBoardEdgeAlpha = 75.0;
            auto board = Child(a_widget, kBoardName);
            if (!board.IsDisplayObject()) board = Draw::Clip(a_widget, kBoardName);
            const auto columns = BoardColumns();
            g_menu.board = BlockRules::BoardRect(columns, BlockRules::kBoardPad, VisibleStage(a_movie));
            if (!board.IsDisplayObject()) {
                logger::warn("Layout: the board could not be made - the columns show without it this menu");
                return;
            }
            const auto& r = g_menu.board;
            // [Blocks] sHidden: a hidden preview turns the over-pass off (AttachWidget), so the game's own 3D would sit under the fill
            const bool fill = OwnCard() && Settings::Get().drawPreviewOver && g_postDisplayHooked && !g_menu.irActive && !g_menu.hidden.contains(Layout::kPreview);
            Draw::Call(board, "clear", {});
            Draw::LineStyle(board, kEdgeWidth, kPanelEdge, kBoardEdgeAlpha);
            Draw::Rect(board, r.left, r.top, r.Width(), r.Height(), kPanelFill, fill ? kBoardAlpha : 0.0);
            logger::info("Layout: board at {} (the columns' areas {} + {:.0f}){}", RectText(r), RectText(columns), BlockRules::kBoardPad,
                fill ? "" : " - edge only (SkyUI's card or the game's own 3D pass would show through a darkened fill)");
        }

        // user 2026-10-30 (the board): a dim centred line in a block that keeps its size while nothing is chosen (the 3D preview's
        // box); made again each time it shows, so it follows a new size or column
        void ShowEmptyLine(RE::GFxValue& a_widget, const BlockId& a_id, bool a_shown)
        {
            auto panel = BlockClip(a_widget, a_id);
            if (!panel.IsDisplayObject()) return;
            Draw::Remove(panel, "mmxEmpty");  // a clip of its own (review G1: a text field is not removed by removeMovieClip)
            auto* movie = OpenMagicMenuMovie();
            if (!a_shown || !movie) return;
            auto holder = Draw::Clip(panel, "mmxEmpty");
            if (!holder.IsDisplayObject()) return;
            const auto rect = BlockRect(a_id);
            const double size = TextFit::Size(Metrics(), kRowSize), h = TextFit::FieldH(Metrics(), size);
            const double top = kTitleH + std::max(0.0, (rect.Height() - kTitleH - h) / 2);
            TextFit::FitLine(Metrics(), g_menu.textLog, movie, holder, "line", kPad, top, std::max(0.0, rect.Width() - 2 * kPad), size, Text(Str::kEmptyChoose), kDimText, "center");
        }

        // user 2026-10-30 (the board): the columns laid out again when the menu's card starts or stops showing a form - the add-ons
        // drawn per selection close up while nothing is shown (AddonsHiddenOn)
        void WatchEmpty(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            const bool nothing = ShownForm() == 0;
            if (g_menu.nothingLaid == nothing) return;
            const bool first = !g_menu.nothingLaid.has_value();
            g_menu.nothingLaid = nothing;
            const bool addons = std::ranges::any_of(g_addons, [](const auto& a_addon) { return BlockRules::ClosesWhenEmpty(a_addon.drawSelection != nullptr, a_addon.flags); });
            if (first && !nothing) return;
            if (!addons) return;
            Rearrange(a_movie, a_widget);
            ApplyVisibility(a_widget);
        }

        // review R6: the button panel's direct children (name, clip), one VisitMembers pass
        std::vector<std::pair<std::string, RE::GFxValue>> PanelChildren(RE::GFxValue& a_clip)
        {
            std::vector<std::pair<std::string, RE::GFxValue>> children;
            a_clip.VisitMembers([&](const char* a_name, const RE::GFxValue& a_value) {
                if (a_name && *a_name != '_' && a_value.IsDisplayObject()) children.emplace_back(a_name, a_value);
            });
            return children;
        }

        // How the panel's content is read: partial (Task 8 R1: the content in use was built without a shown child whose bounds do
        // not read, BlockRules::ReadUnion), and through Flash's own getBounds (the L8 last read-back's other path; else BoundsIn)
        struct ContentRead
        {
            bool partial = false;
            bool flash = false;
        };

        // The children as BlockRules::ChildBox: each one's _visible and, when shown, its bounds in a_space (a hidden child's bounds
        // are not read)
        std::vector<BlockRules::ChildBox> ChildBoxes(std::vector<std::pair<std::string, RE::GFxValue>>& a_children, RE::GFxValue& a_space, ContentRead a_how = {})
        {
            std::vector<BlockRules::ChildBox> boxes;
            for (auto& [name, child] : a_children) {
                if (!BoolMember(child, "_visible")) {
                    boxes.push_back({ {}, false });
                    continue;
                }
                const auto bounds = a_how.flash ? GetBounds(child, a_space) : BoundsIn(child, a_space);
                boxes.push_back(bounds ? BlockRules::ChildBox{ { bounds->xMin, bounds->yMin, bounds->xMax, bounds->yMax }, true, true } : BlockRules::ChildBox{ {}, true, false });
            }
            return boxes;
        }

        // The panel's visible content: the union of its shown children (BlockRules::VisibleUnion: getBounds counts hidden ones too,
        // and SkyUI hides the buttons it does not use, which would widen the strip and push it and the gear left). nullopt when no
        // child shows (review of 4d50080a: hideButtons / clearButtons, ItemMenu.as:115, MagicMenu.as:195 - the panel's own bounds
        // would count the hidden buttons), and when a shown child's bounds do not read - unless a_how.partial
        std::optional<Bounds> VisibleBounds(RE::GFxValue& a_clip, RE::GFxValue& a_space, ContentRead a_how = {})
        {
            auto children = PanelChildren(a_clip);
            const auto boxes = ChildBoxes(children, a_space, a_how);
            const auto united = a_how.partial ? BlockRules::ReadUnion(boxes) : BlockRules::VisibleUnion(boxes);
            if (!united) return std::nullopt;
            return Bounds{ united->left, united->right, united->top, united->bottom };
        }

        // review of 4d50080a (cost): the strip's visible content read again (ChildBoxes: getBounds and four mapped corners per
        // shown button) only when its signature changed - the panel's origin on the stage and each child's _visible and, when shown,
        // _x, _y, _width, _height (a label change alters _width). A value that does not read (NaN) never matches, so it is read again.
        // A shown button whose bounds do not read leaves nothing cached (no move this frame, read again the next) - Task 8 R1: until
        // that has failed kPollFrames frames in a row under one look (BlockRules::StripLook: the signature without the panel's
        // origin, so our own move and SkyUI's positionElements do not restart the count; NaN-equal there); then the buttons that
        // read place the strip, cached with the signature, and the unread child is logged once this menu
        std::optional<Bounds> StripContent(RE::GFxValue& a_panel, const BlockRules::Point& a_nowStage, RE::GFxValue& a_widget)
        {
            constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
            auto children = PanelChildren(a_panel);
            std::vector<double> signature{ a_nowStage.x, a_nowStage.y };
            for (const auto& [name, child] : children) {
                const bool shown = BoolMember(child, "_visible");
                signature.push_back(shown ? 1.0 : 0.0);
                if (!shown) continue;
                for (const char* member : { "_x", "_y", "_width", "_height" }) signature.push_back(Number(child, member).value_or(kNaN));
            }
            if (g_menu.stripSignature == signature) {
                if (!g_menu.stripContent) return std::nullopt;
                return Bounds{ g_menu.stripContent->left, g_menu.stripContent->right, g_menu.stripContent->top, g_menu.stripContent->bottom };
            }
            const auto boxes = ChildBoxes(children, a_widget);
            auto look = BlockRules::StripLook(signature);
            const auto read = BlockRules::ReadStrip(boxes, BlockRules::SameSignature(look, g_menu.stripFailLook), g_menu.stripFailsInRow, kPollFrames);
            // the look is kept while a child stays unread (a partial content too): a move of the panel then takes the partial at once
            if (read.partial || !read.cache) {
                g_menu.stripFailLook = std::move(look);
            } else {
                g_menu.stripFailLook.clear();
            }
            if (!read.cache) {  // GPT on 4846c6a5: a failed read is not kept - nothing moves this frame, the next one reads again
                ++g_menu.stripReadFails;
                g_menu.stripSignature.clear();
                g_menu.stripContent.reset();
                return std::nullopt;
            }
            for (std::size_t i = 0; read.partial && i < boxes.size(); ++i) {
                if (boxes[i].visible && !boxes[i].read && g_menu.stripUnreadLogged.insert(children[i].first).second) {
                    logger::warn("Layout: key-hint strip - the bounds of its shown child {} did not read on {} frames in a row; the strip is placed by "
                                 "the buttons that read (logged once this menu)", children[i].first, g_menu.stripFailsInRow);
                }
            }
            g_menu.stripPartial = read.partial;
            g_menu.stripSignature = std::move(signature);
            g_menu.stripContent = read.content;
            if (!read.content) return std::nullopt;
            return Bounds{ read.content->left, read.content->right, read.content->top, read.content->bottom };
        }

        // The key-hint strip moved (PlaceStripAndGear): BlockRules::PlacePanel with our stage -> the panel's parent through MapPoint,
        // _x / _y set only when they are off; each set read back (SelfTest L8) and logged (the first kMaxStripMoveLogs)
        void MoveStrip(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget, RE::GFxValue& a_panel, RE::GFxValue& a_parent, const BlockRules::Point& a_now,
            const BlockRules::Point& a_nowStage, const Bounds& a_content, const BlockRules::HintRow& a_row, double a_below)
        {
            const auto toParent = [&](const BlockRules::Point& a_point) -> std::optional<BlockRules::Point> {
                const auto mapped = MapPoint(a_movie, a_widget, a_parent, a_point.x, a_point.y);
                if (!mapped) return std::nullopt;
                return BlockRules::Point{ mapped->first, mapped->second };
            };
            const auto& target = a_row.strip;
            const Settings::Rect content{ a_content.xMin, a_content.yMin, a_content.xMax, a_content.yMax };
            const auto place =
                BlockRules::PlacePanel(a_now, a_nowStage, g_menu.stripInPlace, content, BlockRules::Point{ target.left, target.top }, toParent, kStripTolerance);
            if (!place) return;
            const bool first = !g_menu.stripInPlace;
            g_menu.stripInPlace = place->set ? place->want : a_now;  // where it is in place from here on
            g_menu.stripTarget = target;                             // Task 8 T1: the target in effect (L8 last)
            if (!place->set) return;
            a_panel.SetMember("_x", place->want.x);
            a_panel.SetMember("_y", place->want.y);
            ++g_menu.stripApplies;
            g_menu.stripMoveFrame = g_menu.frame;
            // review R4: read back where its visible part landed (through every parent), PASS / FAIL against the target
            if (g_menu.stripChecks < kMaxStripMoveLogs) {
                ++g_menu.stripChecks;
                const auto landed = VisibleBounds(a_panel, a_widget, { .partial = g_menu.stripPartial });
                const bool pass = landed && std::abs(landed->xMin - target.left) <= kStripCheckTolerance && std::abs(landed->yMin - target.top) <= kStripCheckTolerance;
                SelfTest(std::format("L8 strip {} {} - at {} (wanted {:.1f},{:.1f})", kButtonPanelPath, pass ? "PASS" : "FAIL",
                    landed ? std::format("{:.1f},{:.1f}", landed->xMin, landed->yMin) : std::string("no bounds (no button shown, or one did not read)"), target.left, target.top));
            }
            if (++g_menu.stripMoveLogs <= kMaxStripMoveLogs) {
                const double right = ColumnViewport(Layout::kColumns - 1).right;
                const auto why = first ? std::string("first place")
                                 : place->rePlaced ? std::format("SkyUI set its _x to {:.1f}", a_now.x)
                                                   : std::string("its visible buttons changed");
                logger::info("Layout: key-hint strip moved to {} under the board's bottom {:.0f} ({}: origin {:.1f},{:.1f} -> {:.1f},{:.1f} in its parent), "
                             "the gear under it at {}{}{}",
                    RectText(target), a_below, why, a_now.x, a_now.y, place->want.x, place->want.y, RectText(a_row.gear),
                    target.right > right + kStripTolerance ? std::format(" (too wide: runs {:.0f} past the right column)", target.right - right) : "",
                    g_menu.stripMoveLogs == kMaxStripMoveLogs ? " - later moves not logged" : "");
            }
        }

        // Task 8 R2: the strip's last visible size (any menu this game session) - the gear's row while this menu's is not known
        // yet, so the gear does not open at the board and jump a row down once the first button shows (a size from an earlier
        // menu; the first menu of a session still has none)
        std::optional<std::pair<double, double>> g_lastStripSize;

        // Task 8 T1 (L8 last, bSelfTest): where the strip's visible content is, read on a frame after its last move (the frame
        // after each move and every kPollFrames frames) through Flash's own getBounds - not the corner mapping that placed it;
        // under Inventory Reforged's window getBounds is off (BoundsIn), so BoundsIn there. Judged at the close
        void ReadStripBack(RE::GFxValue& a_panel, RE::GFxValue& a_widget)
        {
            if (!Settings::Get().selfTest || !g_menu.stripTarget || g_menu.frame <= g_menu.stripMoveFrame) return;
            if (g_menu.frame != g_menu.stripMoveFrame + 1 && g_menu.frame % kPollFrames != 0) return;
            const auto seen = VisibleBounds(a_panel, a_widget, { .partial = g_menu.stripPartial, .flash = !g_menu.irActive });
            if (!seen) return;
            g_menu.stripRead = BlockRules::Point{ seen->xMin, seen->yMin };
            g_menu.stripReadFrame = g_menu.frame;
        }

        // user 2026-10-30 (sketch), redesigned from SkyUI's source (BottomBar.as:26-53, ButtonPanel.as, ItemMenu.as:73): the
        // key-hint strip is ONE clip, the bottom bar's buttonPanel. SkyUI sets its _x only in positionElements and lays its
        // buttons out inside it; STB Hotkey System adds its hints as buttons of the same panel, so they move with it. Each frame
        // the panel's visible content goes to one row under the board from the left column's left edge
        // (BlockRules::HintRowRects), through every parent (Inventory Reforged's scale); never drawn again. No panel: SkyUI's
        // strip is left alone (logged at the close). No button shown (nothing chosen): the panel is not moved and the gear keeps
        // the row of the strip's last size this menu (review of 4d50080a: no jump while the buttons are cleared). The gear goes
        // one row under the strip, its left edge on the strip's start (nothing else right of it on that row: the gear's flyout
        // will open there). STB's keycaps in the list rows and the skill / XP row stay where their owners put them
        // 2026-10-31 (KeyHints): every add-on in registration order and whether it shows now - its key hints show with it
        std::vector<BlockRules::HintBlock> HintBlocks()
        {
            const auto shown = VisibleBlocks();
            std::vector<BlockRules::HintBlock> blocks;
            for (const auto& addon : g_addons) blocks.push_back({ addon.id, shown.contains(addon.id) });
            return blocks;
        }

        void PlaceStripAndGear(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            const auto visible = VisibleStage(a_movie);
            const double left = ColumnViewport(0).left;
            const double below = g_menu.board.bottom;  // user 2026-10-30: the board's bottom (fixed), not the columns' (fitted cards)
            RE::GFxValue panel, parent;
            const bool found = a_movie->GetVariable(&panel, kButtonPanelPath) && panel.IsDisplayObject() && panel.GetMember("_parent", &parent) &&
                               parent.IsDisplayObject();
            if (found && g_menu.stripFoundFrame < 0) {
                g_menu.stripFoundFrame = g_menu.frame;
                logger::info("Layout: key-hint strip {} found (frame {} of the menu)", kButtonPanelPath, g_menu.frame);
            }
            const std::optional<double> x = found ? Number(panel, "_x") : std::nullopt, y = found ? Number(panel, "_y") : std::nullopt;
            const auto nowStage = x && y ? MapPoint(a_movie, parent, a_widget, *x, *y) : std::nullopt;
            const auto content = nowStage ? StripContent(panel, BlockRules::Point{ nowStage->first, nowStage->second }, a_widget) : std::nullopt;
            if (content) g_menu.stripSize = g_lastStripSize = std::pair{ content->xMax - content->xMin, content->yMax - content->yMin };
            const auto [stripW, stripH] = BlockRules::RowStripSize(g_menu.stripSize, g_lastStripSize);
            auto row = BlockRules::HintRowRects(left, below, stripW, stripH, kGearSize, kBoardStripGap, kGearGap, visible);
            // Task 8 R3: a row pulled up into the board (the columns reach the stage's bottom) - SkyUI's strip is left where it is (our
            // board and panels in mmx_root would cover it) and the gear goes alone under the board
            const bool covers = BlockRules::StripCoversBoard(row, below, kBoardStripGap, kStripCheckTolerance);
            if (covers) row = BlockRules::HintRowRects(left, below, 0.0, 0.0, kGearSize, kBoardStripGap, kGearGap, visible);
            static bool s_coverWarned = false;  // once a game session: the ini that sets the columns is read once too
            if (content && covers) g_menu.stripLeft = true;
            if (content && covers && !std::exchange(s_coverWarned, true)) {
                logger::warn("Layout: key-hint strip left where SkyUI put it - placed under the board's bottom {:.0f} it would be pulled up into the "
                             "board (the stage ends at {:.0f}, the strip is {:.0f} high); the gear goes alone under the board (logged once)",
                    below, visible.bottom, stripH);
            }
            if (content && !covers) {
                MoveStrip(a_movie, a_widget, panel, parent, BlockRules::Point{ *x, *y }, BlockRules::Point{ nowStage->first, nowStage->second }, *content, row, below);
            }
            if (found) ReadStripBack(panel, a_widget);
            auto gear = Child(a_widget, kGearName);
            if (!gear.IsDisplayObject()) return;
            const auto& rect = row.gear;
            if (g_menu.gearLast && SameRect(*g_menu.gearLast, rect)) return;
            if (!g_menu.gearLast) {
                logger::info("Layout: gear button at {} ({})", RectText(rect),
                    covers              ? "under the board at the left column's left edge (the key-hint strip stays at SkyUI's place)" :
                    g_menu.stripSize    ? "one row under the key-hint strip, on its start" :
                    g_lastStripSize     ? "one row under where the key-hint strip goes (its size from an earlier menu)" :
                                          "under the columns at the left column's left edge (no key-hint strip yet)");
            }
            gear.SetMember("_x", rect.left);
            gear.SetMember("_y", rect.top);
            gear.SetMember("_visible", true);
            g_menu.gearLast = rect;
            g_menu.gearDrawnMode.reset();  // the hint's side follows the new place (UpdateGear)
        }

        // user 2026-10-30: the gear's hover hint ("배치 모드 [K]", in the mode "배치 모드 끝내기 [K]", the toggle key as the host
        // names it) in the host's tooltip look, beside the gear on the side that stays on screen (BlockRules::GearHintX), and its
        // lit look for the whole mode. Made again when the mode changes (by any path: the gear, K, a tab, the close) or the gear
        // moved; checked every frame
        void UpdateGear(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            constexpr double kHintGap = 4.0;
            auto gear = Child(a_widget, kGearName);
            if (!gear.IsDisplayObject() || !g_menu.gearLast) return;
            if (g_menu.gearDrawnMode == g_menu.layout.active) return;
            g_menu.gearDrawnMode = g_menu.layout.active;
            Draw::Remove(gear, "hint");
            auto hint = Draw::Clip(gear, "hint");
            if (hint.IsDisplayObject()) {
                const auto text = Fill(g_menu.layout.active ? Str::kLayoutGearLeave : Str::kLayoutGearEnter, { { "{key}", KeyLabel(Settings::Get().keyLayout) } });
                const double size = TextFit::Size(Metrics(), kLabelSize), h = TextFit::FieldH(Metrics(), size) + 2 * kTooltipPad;
                auto field = Draw::Text(a_movie, hint, "text", kTooltipPad, kTooltipPad, kTooltipW, TextFit::FieldH(Metrics(), size), text, kWhite, size);
                const double w = (field.IsDisplayObject() ? TextW(field) : kTooltipW) + 2 * kTooltipPad;
                const auto visible = VisibleStage(a_movie);
                const double hintX = BlockRules::GearHintX(*g_menu.gearLast, w, kHintGap, visible, g_menu.layout.active);
                g_menu.gearHintLeft = hintX < 0.0;  // review I10: in the edit mode on the gear's left, clear of the reset button
                hint.SetMember("_x", hintX);
                hint.SetMember("_y", (kGearSize - h) / 2);
                Draw::Box(hint, w, h, kPanelFill, kTooltipAlpha, kPanelEdge, kEdgeWidth);
            }
            ShowGearLook(gear);
        }

        // user 2026-10-30: "처음 배치로" (the reset, one-shot) right of the gear on its row in the edit mode - a box in the mock's
        // look (black kResetAlpha, a kPanelEdge edge, kLabelColor text) that unfolds out of the gear (its mask's width eased over
        // kResetOpenMs) when the mode starts and folds back (kResetCloseMs) when it ends; the text never scales. A click counts
        // only once it is fully out (ResetClick, BlockRules::ResetClickable). Greyed in the safety-net states, as the old button
        void ResetButtonFrame(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            auto button = Child(a_widget, kResetName);
            if (!button.IsDisplayObject() || !g_menu.gearLast) return;
            RE::GFxValue inner, mask;
            if (g_menu.resetFullW <= 0.0) {  // made once a menu: the box at its full width, its mask grows over it
                // GPT on 8897d3db (Draw::NextDepth can fail): made whole or not at all - a part that did not come is removed with the
                // rest and the whole button tried again next frame, never kept as a clickable box with no label
                const auto unmake = [&] {
                    Draw::Remove(button, "inner");
                    Draw::Remove(button, "mask");
                };
                inner = Draw::Clip(button, "inner");
                mask = Draw::Clip(button, "mask");
                if (!inner.IsDisplayObject() || !mask.IsDisplayObject()) {
                    unmake();
                    return;
                }
                const double h = kGearSize;
                auto field = Draw::Text(a_movie, inner, "label", kResetPadX, (h - TextFit::FieldH(Metrics(), kResetTextSize)) / 2, kTooltipW, TextFit::FieldH(Metrics(), kResetTextSize),
                    Text(Str::kLayoutReset), kLabelColor, kResetTextSize);
                if (!field.IsDisplayObject()) {
                    unmake();
                    return;
                }
                g_menu.resetFullW = TextW(field) + 2 * kResetPadX;
                Draw::Box(inner, g_menu.resetFullW, h, kPanelFill, kResetAlpha, kPanelEdge, kEdgeWidth);
                SetHandler(a_movie, inner, "onRelease", g_resetClick.get());
                inner.SetMember("useHandCursor", false);
                if (!SafetyNetReason().empty()) inner.SetMember("_alpha", kGreyAlpha);
                const std::array<RE::GFxValue, 1> args{ mask };
                inner.Invoke("setMask", nullptr, args.data(), args.size());
                button.SetMember("_visible", false);
            } else if (!button.GetMember("mask", &mask) || !mask.IsDisplayObject()) {
                return;
            }
            const auto visible = VisibleStage(a_movie);
            const auto rect = BlockRules::ResetButtonRect(*g_menu.gearLast, g_menu.resetFullW, kGearSize, kResetGap, visible);
            if (!g_menu.resetPlaced || !SameRect(*g_menu.resetPlaced, rect)) {
                button.SetMember("_x", rect.left);
                button.SetMember("_y", rect.top);
                g_menu.resetPlaced = rect;
            }
            const double now = NowMs();
            const double want = g_menu.layout.active ? g_menu.resetFullW : 0.0;
            if (std::abs(want - g_menu.resetTo) > kRectTolerance) {  // the mode changed: unfold or fold from where it is now
                g_menu.resetFrom = g_menu.resetShownW;
                g_menu.resetTo = want;
                g_menu.resetStartMs = now;
                g_menu.resetDurationMs = want > 0.0 ? BlockRules::kResetOpenMs : BlockRules::kResetCloseMs;
            }
            const double shown = BlockRules::AnimWidth(g_menu.resetFrom, g_menu.resetTo, g_menu.resetStartMs, now, g_menu.resetDurationMs);
            const double next = shown <= kRectTolerance ? 0.0 : shown;
            if (g_menu.resetDrawn && std::abs(next - g_menu.resetShownW) <= kRectTolerance) return;
            g_menu.resetDrawn = true;
            g_menu.resetShownW = next;
            Draw::Call(mask, "clear", {});
            if (g_menu.resetShownW > 0.0) {
                Draw::NoLine(mask);
                Draw::Rect(mask, 0, 0, g_menu.resetShownW + kEdgeWidth, kGearSize + kEdgeWidth, kMaskFill, Draw::kAlphaFull);
            }
            button.SetMember("_visible", g_menu.resetShownW > 0.0);
            if (auto gear = Child(a_widget, kGearName); gear.IsDisplayObject()) ShowGearLook(gear);
        }

        // Task 8 T2 (SelfTest L9, bSelfTest): the board, the gear and the reset button read back every kPollFrames frames on the
        // frame thread - each against the rule that placed it (BlockRules::BoardProblem / GearProblem / ResetProblem); the counts
        // and the first failure are logged at the close (CloseChecks: no GFx call there)
        void CheckChrome(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            constexpr double kReadTolerance = 0.5;  // stage units: Flash keeps _x / _width in twips (0.05)
            if (!Settings::Get().selfTest || g_menu.frame % kPollFrames != 0) return;
            const auto visible = VisibleStage(a_movie);
            const auto note = [](auto& a_check, const std::string& a_problem) {
                ++a_check.checks;
                if (a_problem.empty()) return;
                ++a_check.fails;
                if (a_check.firstFail.empty()) a_check.firstFail = std::format("frame {}: {}", g_menu.frame, a_problem);
            };
            const auto point = [](RE::GFxValue& a_clip) -> std::optional<BlockRules::Point> {
                const auto x = Number(a_clip, "_x"), y = Number(a_clip, "_y");
                if (!x || !y) return std::nullopt;
                return BlockRules::Point{ *x, *y };
            };
            if (auto board = Child(a_widget, kBoardName); board.IsDisplayObject()) {
                const auto drawn = BoundsIn(board, a_widget);
                note(g_menu.boardCheck, BlockRules::BoardProblem(g_menu.board, BlockRules::BoardRect(BoardColumns(), BlockRules::kBoardPad, visible),
                                            drawn ? std::optional{ Settings::Rect{ drawn->xMin, drawn->yMin, drawn->xMax, drawn->yMax } } : std::nullopt, kEdgeWidth,
                                            kReadTolerance));
            }
            auto gear = Child(a_widget, kGearName);
            if (!gear.IsDisplayObject() || !g_menu.gearLast) return;
            note(g_menu.gearCheck,
                BlockRules::GearProblem(point(gear), *g_menu.gearLast, g_menu.board.bottom, kBoardStripGap, ColumnViewport(0).left, visible, kReadTolerance));
            auto button = Child(a_widget, kResetName);
            RE::GFxValue mask, shown;
            if (!button.IsDisplayObject() || g_menu.resetFullW <= 0.0 || !button.GetMember("mask", &mask) || !mask.IsDisplayObject()) return;
            const auto want = BlockRules::ResetButtonRect(*g_menu.gearLast, g_menu.resetFullW, kGearSize, kResetGap, visible);
            const std::optional<bool> isShown = button.GetMember("_visible", &shown) && shown.IsBool() ? std::optional{ shown.GetBool() } : std::nullopt;
            note(g_menu.resetCheck, BlockRules::ResetProblem(point(button), want, isShown, g_menu.resetShownW, Number(mask, "_width"), kEdgeWidth, kReadTolerance));
        }

        // At the close (the close event's thread: g_menu only, no GFx call): SelfTest L8 last (the strip's last target against a
        // read on a later frame, ReadStripBack) and L9 board / gear / reset (CheckChrome, the reset button's clicks)
        void CloseChecks()
        {
            using BlockRules::Verdict3;
            const auto at = [](const BlockRules::Point& a_point) { return std::format("{:.1f},{:.1f}", a_point.x, a_point.y); };
            if (g_menu.stripFoundFrame < 0) {
                SelfTest(std::format("L8 last SKIP - no key-hint strip ({}) this menu", kButtonPanelPath));
            } else if (g_menu.stripLeft) {
                SelfTest("L8 last SKIP - the key-hint strip was left at SkyUI's place (under the board it would be pulled up into it, Task 8 R3)");
            } else {
                const bool after = g_menu.stripReadFrame > g_menu.stripMoveFrame;
                const auto verdict = BlockRules::StripLastCheck(g_menu.stripTarget, g_menu.stripRead, after, kStripCheckTolerance);
                SelfTest(std::format("L8 last {} - target {}, read at {} on frame {} (last move on frame {}){}", BlockRules::VerdictText(verdict),
                    g_menu.stripTarget ? RectText(*g_menu.stripTarget) : std::string("none (never placed: no button shown)"),
                    g_menu.stripRead ? at(*g_menu.stripRead) : std::string("-"), g_menu.stripReadFrame, g_menu.stripMoveFrame,
                    verdict == Verdict3::kSkip ? " - nothing to compare" : g_menu.stripPartial ? " (placed by the buttons that read, R1)" : ""));
            }
            const auto line = [](const char* a_what, const MenuState::ChromeCheck& a_check, const std::string& a_extra, bool a_extraFails) {
                const char* verdict = a_check.fails > 0 || a_extraFails ? "FAIL" : a_check.checks == 0 ? "SKIP" : "PASS";
                SelfTest(std::format("L9 {} {} - {} read-back(s) every {} frames, {} failed{}{}", a_what, verdict, a_check.checks, kPollFrames, a_check.fails,
                    a_check.firstFail.empty() ? std::string{} : "; first: " + a_check.firstFail, a_extra));
            };
            line("board", g_menu.boardCheck, "", false);
            line("gear", g_menu.gearCheck, "", false);
            line("reset", g_menu.resetCheck,
                std::format("; clicks {} taken, {} ignored (unfolding or out of the edit mode), {} taken before it was fully out (want 0)", g_menu.resetTaken,
                    g_menu.resetIgnored, g_menu.resetTakenEarly),
                g_menu.resetTakenEarly > 0);
        }

        // The border handle: a strip over the gap between the columns (at least kMinHandleW wide), a thin line in its middle;
        // shown in the mode only
        void DrawBorderHandle(RE::GFxValue& a_widget)
        {
            auto handle = Child(a_widget, kBorderName);
            if (!handle.IsDisplayObject()) return;
            const auto left = ColumnViewport(0), right = ColumnViewport(1);
            double x0 = left.right, x1 = right.left;
            if (x1 - x0 < kMinHandleW) {
                const double mid = (x0 + x1) / 2;
                x0 = mid - kMinHandleW / 2;
                x1 = mid + kMinHandleW / 2;
            }
            Draw::Call(handle, "clear", {});
            Draw::NoLine(handle);
            Draw::Rect(handle, x0, left.top, x1 - x0, left.Height(), kPanelFill, 0.0);
            Draw::LineStyle(handle, kEdgeWidth, kPanelEdge, kHandleLineAlpha);
            Draw::MoveTo(handle, (x0 + x1) / 2, left.top);
            Draw::LineTo(handle, (x0 + x1) / 2, left.bottom);
            handle.SetMember("_visible", g_menu.layout.active);
        }

        // Rearrange's tail: the handle follows the border; the covers follow the blocks
        void PlaceLayoutChrome(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            DrawBorderHandle(a_widget);
            if (g_menu.layout.active) SetCovers(a_movie, a_widget);
        }

        // After the panels (AttachWidget): the border handle, the overlay, then the gear and its reset button on top of everything
        void MakeLayoutChrome(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            auto handle = Draw::Clip(a_widget, kBorderName);
            auto overlay = Draw::Clip(a_widget, kOverlayName);
            MakeGear(a_movie, a_widget);  // user 2026-10-30: above the shield and the overlay
            Draw::Clip(a_widget, kResetName);  // its reset button (ResetButtonFrame draws it), above the gear
            if (handle.IsDisplayObject()) {
                SetHandler(a_movie, handle, "onPress", g_borderPress.get());
                SetHandler(a_movie, handle, "onRelease", g_borderRelease.get());
                SetHandler(a_movie, handle, "onReleaseOutside", g_borderRelease.get());
                handle.SetMember("useHandCursor", false);
            }
            if (!handle.IsDisplayObject() || !overlay.IsDisplayObject()) {
                logger::warn("Layout: the border handle or overlay could not be made - the layout mode will not start this menu");
            }
            PlaceLayoutChrome(a_movie, a_widget);
            if (const auto why = SafetyNetReason(); !why.empty() && !std::exchange(g_menu.layout.restLogged, true)) {
                logger::info("Layout: the layout mode rests this menu - {}; the gear is greyed, the default layout is used (the saved one is "
                             "kept)",
                    why);
            }
        }

        bool ChromeReady(RE::GFxValue& a_widget)
        {
            return Child(a_widget, kShieldName).IsDisplayObject() && Child(a_widget, kBorderName).IsDisplayObject() && Child(a_widget, kOverlayName).IsDisplayObject();
        }

        void ClearOverlay(RE::GFxValue& a_widget, const char* a_name)
        {
            if (auto overlay = Child(a_widget, kOverlayName); overlay.IsDisplayObject()) Draw::Remove(overlay, a_name);
        }

        // --- moving blocks ---

        // A block moved to the other column: AS2 cannot move a clip to another parent, so its panel is made again in its new
        // column clip (Panel) and its content drawn again (D11: a width change redraws it once anyway)
        void RebuildPanel(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget, const BlockId& a_id)
        {
            Panel(a_movie, a_widget, a_id);
            const auto rect = BlockRect(a_id);
            g_menu.placed[a_id] = rect;
            if (a_id == Layout::kSpellCard) g_menu.ownCardSig.clear();  // WatchOwnCard draws it again next frame
            if (Addon(a_id)) g_menu.addonDrawDue.insert(a_id);  // Task 6: its callback draws it again (DrawAddons)
            logger::info("Layout: block {} is in column {} now - its panel made there again ({:.0f} x {:.0f}), drawn again", a_id,
                ColumnOf(a_id).value_or(-1), rect.Width(), rect.Height());
        }

        // The panels put to a_state: the blocks that change column are made again there, then Rearrange places every panel and
        // draws again only what changed size (D11), the chrome follows
        void ApplyLayout(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget, const Layout::State& a_state)
        {
            auto& m = g_menu.layout;
            std::vector<BlockId> moved;
            RemoveCovers(a_widget);
            for (const BlockId& id : AllBlocks()) {
                const auto before = ColumnOf(id);
                std::optional<int> after;
                for (int column = 0; column < Layout::kColumns; ++column) {
                    if (std::ranges::find(a_state.columns[column], id) != a_state.columns[column].end()) after = column;
                }
                if (before == after) continue;
                if (auto parent = BlockParent(a_widget, id); parent.IsDisplayObject()) Draw::Remove(parent, PanelName(id));
                moved.push_back(id);
            }
            m.working = a_state;
            Rearrange(a_movie, a_widget);
            for (const auto& id : moved) RebuildPanel(a_movie, a_widget, id);
            if (!moved.empty()) SetCovers(a_movie, a_widget);  // above the panels made again
            ApplyVisibility(a_widget);
        }

        // The slot under the pointer (stage units, on screen): the column by SnapSlot's own x rule, then the pointer's y in that
        // column's unscrolled space (plan D4: stage y + the column's scroll)
        Layout::Slot SlotAt(const BlockId& a_dragged, double a_x, double a_y)
        {
            const auto& state = CurrentLayout();
            const auto frames = Layout::DefaultFrames(Settings::Get());
            const int column = Layout::SnapSlot(state, a_dragged, a_x, a_y + g_menu.scroll[0], g_menu.blocks, frames).column;
            return Layout::SnapSlot(state, a_dragged, a_x, a_y + g_menu.scroll[column], g_menu.blocks, frames);
        }

        // The insertion line's y (unscrolled): half a gap above the block it goes before, else half a gap below the column's
        // last shown block (the dragged one left out), else the column's top
        double SlotLineY(const Layout::Slot& a_slot, const BlockId& a_dragged)
        {
            return Layout::SlotLineY(CurrentLayout(), a_slot, a_dragged, g_menu.blocks, Layout::DefaultFrames(Settings::Get()));
        }

        void DrawSlotLine(RE::GFxValue& a_widget, const Layout::Slot& a_slot, const BlockId& a_dragged)
        {
            auto overlay = Child(a_widget, kOverlayName);
            if (!overlay.IsDisplayObject()) return;
            Draw::Remove(overlay, "line");
            auto line = Draw::Clip(overlay, "line");
            if (!line.IsDisplayObject()) return;
            const auto viewport = ColumnViewport(a_slot.column);
            const double y = std::clamp(SlotLineY(a_slot, a_dragged) - g_menu.scroll[a_slot.column], viewport.top, viewport.bottom);
            Draw::LineStyle(line, kLineW, kWhite);
            Draw::MoveTo(line, viewport.left, y);
            Draw::LineTo(line, viewport.right, y);
        }

        // --- Task 5: the gamepad / keyboard focus model (the pure part is Layout::Navigate) ---

        const char* NavName(Layout::Nav a_nav)
        {
            switch (a_nav) {
            case Layout::Nav::kUp: return "up";
            case Layout::Nav::kDown: return "down";
            case Layout::Nav::kLeft: return "left";
            case Layout::Nav::kRight: return "right";
            case Layout::Nav::kAccept: return "accept";
            case Layout::Nav::kCancel: return "cancel";
            case Layout::Nav::kBorderLeft: return "border left";
            default: return "border right";
            }
        }

        std::string FocusText(const Layout::Focus& a_focus)
        {
            if (!a_focus.focus) return "no focus";
            if (!a_focus.target) return "focus " + *a_focus.focus;
            return std::format("{} picked, target column {} {}", *a_focus.focus, a_focus.target->column,
                a_focus.target->before ? "before " + *a_focus.target->before : std::string("at its end"));
        }

        void ClearPadLine(RE::GFxValue& a_widget)
        {
            if (std::exchange(g_menu.layout.padLine, std::nullopt)) ClearOverlay(a_widget, "line");
        }

        // A pick ended by something else (a mouse drag, the reset, the mode's leave): nothing moved
        void EndPick(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget, const char* a_why)
        {
            if (!g_layoutFocus.Picked()) return;
            logger::info("Layout: the pick of {} ended by {} - not moved", *g_layoutFocus.focus, a_why);
            g_layoutFocus.target.reset();
            ClearPadLine(a_widget);
            if (g_menu.layout.active && !g_menu.layout.drag) SetCovers(a_movie, a_widget);
        }

        // The focused block - while it is picked, its insertion line (kLineViewMargin each way) - scrolled into its column's view
        void KeepInView(RE::GFxValue& a_widget)
        {
            const auto& focus = g_layoutFocus;
            if (!focus.focus) return;
            int column = 0;
            double top = 0.0, bottom = 0.0;
            if (focus.target) {
                column = focus.target->column;
                const double y = Layout::SlotLineY(CurrentLayout(), *focus.target, *focus.focus, g_menu.blocks, Layout::DefaultFrames(Settings::Get()));
                top = y - kLineViewMargin;
                bottom = y + kLineViewMargin;
            } else {
                const auto it = g_menu.blocks.find(*focus.focus);
                const auto at = ColumnOf(*focus.focus);
                if (it == g_menu.blocks.end() || !at) return;
                column = *at;
                top = it->second.top;
                bottom = it->second.bottom;
            }
            if (column < 0 || column >= Layout::kColumns) return;
            const auto [content, visible] = ColumnHeights(column);
            const double before = g_menu.scroll[column];
            const double want = Layout::ScrollToShow(before, top, bottom, ColumnViewport(column).top, visible, content);
            if (std::abs(want - before) > kRectTolerance) ScrollColumn(a_widget, column, want - before, false);
        }

        void EndDrag(RE::GFxValue& a_widget)
        {
            auto& m = g_menu.layout;
            if (!m.drag) return;
            if (auto panel = BlockClip(a_widget, m.drag->id); panel.IsDisplayObject()) panel.SetMember("_alpha", Draw::kAlphaFull);
            ClearOverlay(a_widget, "ghost");
            ClearOverlay(a_widget, "line");
            m.drag.reset();
        }

        void StartDrag(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget, const BlockId& a_id, double a_x, double a_y)
        {
            auto& m = g_menu.layout;
            if (m.drag || m.borderGrab || !VisibleBlocks().contains(a_id)) return;
            EndPick(a_movie, a_widget, "a mouse drag");  // Task 5: one move at a time
            const auto rect = BlockRect(a_id);
            // D8: the grab offset is where in the block the pointer took it - a block taller than its column's viewport too
            m.drag = LayoutDrag{ a_id, a_x - rect.left, a_y - rect.top, g_menuGeneration.load(), std::nullopt, 0.0 };
            if (auto panel = BlockClip(a_widget, a_id); panel.IsDisplayObject()) panel.SetMember("_alpha", kDraggedAlpha);
            if (auto overlay = Child(a_widget, kOverlayName); overlay.IsDisplayObject()) {
                Draw::Remove(overlay, "ghost");
                auto ghost = Draw::Clip(overlay, "ghost");
                if (ghost.IsDisplayObject()) {
                    Draw::Box(ghost, rect.Width(), rect.Height(), kPanelFill, kGhostAlpha, kTitleColor, kLineW);
                    ghost.SetMember("_x", rect.left);
                    ghost.SetMember("_y", rect.top);
                }
            }
            if (m.drops + m.borderMoves < kMaxDragLogs) logger::info("Layout: drag of {} begun (column {}, grabbed at {:.0f},{:.0f} in it)", a_id, ColumnOf(a_id).value_or(-1), m.drag->grabX, m.drag->grabY);
        }

        // D8: the drag session's frame - the ghost follows the pointer, the insertion line marks the slot, the column under the
        // pointer scrolls near its top / bottom edge. Ends (no move) on focus loss or a stale menu generation
        void DragFrame(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            auto& m = g_menu.layout;
            if (!m.drag) return;
            if (m.drag->generation != g_menuGeneration.load() || FocusGone()) {
                logger::info("Layout: drag of {} ended without a move - {}", m.drag->id, FocusGone() ? "the game window lost the focus" : "another menu's drag");
                EndDrag(a_widget);
                return;
            }
            const auto at = MouseAt(a_movie);
            if (!at) return;
            const auto [x, y] = *at;
            if (auto overlay = Child(a_widget, kOverlayName); overlay.IsDisplayObject()) {
                if (auto ghost = Child(overlay, "ghost"); ghost.IsDisplayObject()) {
                    ghost.SetMember("_x", x - m.drag->grabX);
                    ghost.SetMember("_y", y - m.drag->grabY);
                }
            }
            auto slot = SlotAt(m.drag->id, x, y);
            const auto viewport = ColumnViewport(slot.column);
            const bool overColumns = x >= ColumnViewport(0).left && x <= ColumnViewport(1).right;
            if (overColumns && y < viewport.top + kAutoScrollEdge) {
                ScrollColumn(a_widget, slot.column, -kAutoScrollStep, false);
            } else if (overColumns && y > viewport.bottom - kAutoScrollEdge) {
                ScrollColumn(a_widget, slot.column, kAutoScrollStep, false);
            }
            slot = SlotAt(m.drag->id, x, y);
            const double scroll = g_menu.scroll[slot.column];
            if (m.drag->slot == slot && std::abs(m.drag->lineScroll - scroll) <= kRectTolerance) return;
            m.drag->slot = slot;
            m.drag->lineScroll = scroll;
            DrawSlotLine(a_widget, slot, m.drag->id);
        }

        void Drop(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            auto& m = g_menu.layout;
            if (!m.drag) return;
            const auto id = m.drag->id;
            const auto at = MouseAt(a_movie);
            EndDrag(a_widget);
            if (!at) {
                logger::warn("Layout: {} released, but the pointer could not be read - not moved", id);
                return;
            }
            const auto slot = SlotAt(id, at->first, at->second);
            const auto moved = Layout::Move(m.working, id, slot);
            if (moved == m.working) {
                if (m.drops + m.borderMoves < kMaxDragLogs) logger::info("Layout: {} dropped where it was", id);
                return;
            }
            const auto started = std::chrono::steady_clock::now();
            ApplyLayout(a_movie, a_widget, moved);
            ++m.drops;
            logger::info("Layout: {} dropped in column {} {} - {} ({:.1f} ms)", id, slot.column, slot.before ? "before " + *slot.before : std::string("at its end"),
                Describe(m.working), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
        }

        // The border drag: a guide line while dragged; ClampBorder applied and the blocks redrawn once on release (D11)
        void BorderFrame(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            auto& m = g_menu.layout;
            if (!m.borderGrab) return;
            if (FocusGone()) {
                logger::info("Layout: border drag ended without a move - the game window lost the focus");
                m.borderGrab.reset();
                m.guideAt.reset();
                ClearOverlay(a_widget, "guide");
                return;
            }
            const auto at = MouseAt(a_movie);
            if (!at) return;
            const auto frames = Layout::DefaultFrames(Settings::Get());
            const double border = Layout::ClampBorder(at->first - *m.borderGrab, frames[0], frames[1]);
            if (m.guideAt && std::abs(*m.guideAt - border) <= kRectTolerance) return;
            m.guideAt = border;
            auto overlay = Child(a_widget, kOverlayName);
            if (!overlay.IsDisplayObject()) return;
            Draw::Remove(overlay, "guide");
            auto guide = Draw::Clip(overlay, "guide");
            if (!guide.IsDisplayObject()) return;
            Draw::LineStyle(guide, kLineW, kWhite);
            Draw::MoveTo(guide, border, frames[0].top);
            Draw::LineTo(guide, border, frames[0].bottom);
        }

        void BorderDrop(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            auto& m = g_menu.layout;
            if (!m.borderGrab) return;
            const auto frames = Layout::DefaultFrames(Settings::Get());
            const auto at = MouseAt(a_movie);
            const double border = at ? Layout::ClampBorder(at->first - *m.borderGrab, frames[0], frames[1]) : m.guideAt.value_or(m.working.border);
            m.borderGrab.reset();
            m.guideAt.reset();
            ClearOverlay(a_widget, "guide");
            const double before = Layout::ClampBorder(m.working.border, frames[0], frames[1]);
            if (std::abs(border - before) <= kRectTolerance) return;
            auto state = m.working;
            state.border = border;
            const auto started = std::chrono::steady_clock::now();
            ApplyLayout(a_movie, a_widget, state);
            ++m.borderMoves;
            logger::info("Layout: border moved {:.1f} -> {:.1f} - the columns {:.0f} / {:.0f} wide, their blocks drawn again once ({:.1f} ms)", before, border,
                ColumnViewport(0).Width(), ColumnViewport(1).Width(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
        }

        // --- entering and leaving ---

        void EnterLayoutMode(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget, const std::string& a_why)
        {
            auto& m = g_menu.layout;
            if (m.active) return;
            if (!ChromeReady(a_widget)) {
                logger::warn("Layout: mode refused - the shield, border handle or overlay is missing in this menu ({})", a_why);
                return;
            }
            m.active = true;
            ++g_menu.layoutModeChanges;  // Block API v2: a press in an add-on block is cancelled
            m.entry = m.working = Settings::SavedLayout();
            ++m.entered;
            // Task 5: entered with the gamepad, the first visible block has the focus at once (there is no pointer); otherwise the
            // first arrow / D-pad press gives it, so a mouse user sees no outline
            g_layoutFocus = {};
            g_layoutStickY = 0.0f;
            if (a_why == kGamepadSource) g_layoutFocus.focus = Layout::FirstFocus(m.working, VisibleBlocks());
            if (HeightsFitted() || AddonsCloseUp()) Rearrange(a_movie, a_widget);  // every block at its full height in the mode, closed-up add-ons shown
            ShowShield(a_widget, true);
            PlaceLayoutChrome(a_movie, a_widget);
            ApplyVisibility(a_widget);
            KeepInView(a_widget);
            logger::info("Layout: mode ON by {} - the entry layout {}{}", a_why, Describe(m.entry),
                g_layoutFocus.focus ? "; " + FocusText(g_layoutFocus) : std::string{});
            FlushLog();
        }

        // Save: the working layout written (D10); a failed write keeps the mode on with its State (to retry), a newer build's file
        // is not written (the edit is kept for this game session). Cancel: the entry layout back (SelfTest L4)
        void LeaveLayoutMode(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget, bool a_save, const std::string& a_why)
        {
            auto& m = g_menu.layout;
            if (!m.active) return;
            EndDrag(a_widget);
            m.borderGrab.reset();
            m.guideAt.reset();
            ClearOverlay(a_widget, "guide");
            if (a_save) {
                if (m.working == Settings::SavedLayout()) {
                    logger::info("Layout: mode OFF by {} - the layout is unchanged, nothing written", a_why);
                } else {
                    std::string detail;
                    const auto result = Settings::SaveLayout(m.working, &detail);
                    if (result == Settings::SaveResult::kFailed) {
                        logger::error("Layout: save FAILED - {}. The layout mode stays on with the edited layout (the gear or {} again to retry, "
                                      "Esc to leave without saving)",
                            detail, KeyLabel(Settings::Get().keyLayout));
                        SelfTest(std::format("L3 layout saved FAIL - not written: {}", detail));
                        return;  // m.active stays true: the caller settles the mode on
                    }
                    if (result == Settings::SaveResult::kRefused) {
                        Settings::SetSavedLayout(m.working);
                        logger::warn("Layout: not saved - {}; the edited layout is used until the game is closed", detail);
                        SelfTest(std::format("L3 layout saved SKIP - refused: {}", detail));
                    } else {
                        std::string why;
                        const auto back = Settings::ReadLayoutFile(&why);
                        logger::info("Layout: mode OFF by {} - saved: {} - {}", a_why, detail, Describe(m.working));
                        SelfTest(std::format("L3 layout saved {} - the file read again {} the layout ({}{})", back && *back == m.working ? "PASS" : "FAIL",
                            back ? (*back == m.working ? "equals" : "differs from") : "does not read as", Describe(m.working),
                            back ? (*back == m.working ? std::string{} : "; file: " + Describe(*back)) : "; " + why));
                    }
                }
            } else {
                if (!(m.working == m.entry)) ApplyLayout(a_movie, a_widget, m.entry);
            }
            EndPick(a_movie, a_widget, "the mode's end");  // Task 5: a pick not dropped is not saved
            g_layoutFocus = {};
            g_layoutStickY = 0.0f;
            m.active = false;  // the caller settles the input's flag (SettleMode)
            ++g_menu.layoutModeChanges;  // Block API v2: a press in an add-on block is cancelled
            if (HeightsFitted() || AddonsCloseUp()) Rearrange(a_movie, a_widget);  // the fitted heights and the closed-up add-ons back
            if (!a_save) {
                const bool same = CurrentLayout() == m.entry && Settings::SavedLayout() == m.entry;
                const auto arranged = ArrangeBlocks(ArrangementFor(OwnCard()));
                std::string diffs;
                for (const auto& [id, rect] : arranged) {
                    const auto it = g_menu.blocks.find(id);
                    if (it == g_menu.blocks.end() || !SameRect(it->second, rect)) diffs += std::format("; {} not at {}", id, RectText(rect));
                }
                logger::info("Layout: mode OFF by {} - cancelled, the entry layout is back", a_why);
                SelfTest(std::format("L4 cancel {} - the layout {} the entry layout ({}), the panels' rects {}{}", same && diffs.empty() ? "PASS" : "FAIL",
                    same ? "equals" : "differs from", Describe(m.entry), diffs.empty() ? "are the entry layout's" : "differ", diffs));
                RE::GFxValue root;
                if (a_movie->GetVariable(&root, "_root")) CheckPanelPlaces(a_movie, root, a_widget);
            }
            RemoveCovers(a_widget);
            ShowShield(a_widget, false);
            PlaceLayoutChrome(a_movie, a_widget);
            ApplyVisibility(a_widget);
            FlushLog();
        }

        // Task 5 SelfTest L6 after a pad drop: every block once in the layout, the dropped block at the slot it was aimed at, the
        // focus still on it; then the panels read back (L1 places)
        void CheckPadDrop(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget, const BlockId& a_id, const Layout::Slot& a_slot)
        {
            auto& m = g_menu.layout;
            std::string bad;
            for (const BlockId& id : AllBlocks()) {
                int count = 0;
                for (const auto& column : m.working.columns) count += static_cast<int>(std::ranges::count(column, id));
                if (count != 1) bad += std::format("; {} {} time(s) in the layout", id, count);
            }
            const auto at = Layout::SlotOf(m.working, a_id, VisibleBlocks());
            if (!at || !(*at == a_slot)) bad += std::format("; {} is not at its target", a_id);
            if (g_layoutFocus.focus != a_id) bad += "; the focus left it";
            if (!bad.empty()) ++m.padChecksFailed;
            SelfTest(std::format("L6 pad drop {} - {} in column {} {}: every block once, at its target, the focus on it{} ({})", bad.empty() ? "PASS" : "FAIL",
                a_id, a_slot.column, a_slot.before ? "before " + *a_slot.before : std::string("at its end"), bad, Describe(m.working)));
            RE::GFxValue root;
            if (a_movie->GetVariable(&root, "_root")) CheckPanelPlaces(a_movie, root, a_widget);
        }

        // Task 5: one key of the focus model (D-pad / arrows, A / Enter, B / Esc, LB / RB), queued by the hook. Layout::Navigate
        // decides; this does what it says to the movie
        void PadKey(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget, Layout::Nav a_nav, const std::string& a_source)
        {
            auto& m = g_menu.layout;
            ++m.padKeys;
            if (m.active && (m.drag || m.borderGrab) && a_nav != Layout::Nav::kCancel) {
                ++m.padIgnored;  // one move at a time: the mouse drag goes on; B / Esc still leaves
                return;
            }
            const auto frames = Layout::DefaultFrames(Settings::Get());
            const auto visible = VisibleBlocks();
            const auto before = Layout::Normalize(m.working, g_layoutFocus, visible);  // as Navigate sees it (a drop's slot)
            const auto result = Layout::Navigate(m.working, g_layoutFocus, a_nav, m.active, visible, g_menu.blocks, frames);
            using Effect = Layout::NavEffect;
            if (result.effect == Effect::kIgnored) {
                ++m.padIgnored;
                return;
            }
            g_layoutFocus = result.focus;
            const auto log = [&](const std::string& a_what) {
                if (++m.padLogs <= kMaxPadLogs) {
                    logger::info("Layout: {} ({}) - {}{}", NavName(a_nav), a_source, a_what, m.padLogs == kMaxPadLogs ? " - later keys counted only" : "");
                }
            };
            switch (result.effect) {
            case Effect::kLeave: {
                const auto seq = BeginModeChange(false);
                LeaveLayoutMode(a_movie, a_widget, false, a_source);
                SettleMode(seq, m.active);
                return;
            }
            case Effect::kNone: log("nothing to do (" + FocusText(g_layoutFocus) + ")"); return;
            case Effect::kFocused: ++m.padFocusMoves; log(FocusText(g_layoutFocus)); break;
            case Effect::kPicked: ++m.padPicks; log(FocusText(g_layoutFocus)); break;
            case Effect::kTargeted: log(FocusText(g_layoutFocus)); break;
            case Effect::kPickCancelled: ++m.padPickCancels; log(std::format("the pick of {} cancelled, nothing moved", *g_layoutFocus.focus)); break;
            case Effect::kDroppedInPlace: ++m.padInPlace; log(std::format("{} dropped where it was", *g_layoutFocus.focus)); break;
            case Effect::kDropped: {
                const auto started = std::chrono::steady_clock::now();
                ApplyLayout(a_movie, a_widget, result.state);
                ++m.drops;
                ++m.padDrops;
                log(std::format("{} dropped in column {} {} - {} ({:.1f} ms)", *g_layoutFocus.focus, before.target->column,
                    before.target->before ? "before " + *before.target->before : std::string("at its end"), Describe(m.working),
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count()));
                CheckPadDrop(a_movie, a_widget, *g_layoutFocus.focus, *before.target);
                break;
            }
            case Effect::kBorder: {
                const double was = Layout::ClampBorder(m.working.border, frames[0], frames[1]);
                ApplyLayout(a_movie, a_widget, result.state);
                ++m.borderMoves;
                ++m.padBorders;
                log(std::format("border {:.1f} -> {:.1f} - the columns {:.0f} / {:.0f} wide", was, m.working.border, ColumnViewport(0).Width(),
                    ColumnViewport(1).Width()));
                break;
            }
            default: break;
            }
            KeepInView(a_widget);
            ClearPadLine(a_widget);  // drawn again by PadFrame for the new target (or gone)
            if (!(g_layoutFocus == before) || result.effect == Effect::kDropped || result.effect == Effect::kBorder) SetCovers(a_movie, a_widget);
        }

        // Task 5, each frame in the mode: the focus kept on a visible block (our card may come or go, the school may go), the covers
        // made again when the visible blocks changed, the right stick's scroll of the focused block's column, the pick's insertion line
        void PadFrame(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            auto& m = g_menu.layout;
            const auto visible = VisibleBlocks();
            const auto normal = Layout::Normalize(m.working, g_layoutFocus, visible);
            const bool focusChanged = !(normal == g_layoutFocus);
            if (focusChanged) {
                if (++m.padLogs <= kMaxPadLogs) logger::info("Layout: {} - not visible any more; now {}", FocusText(g_layoutFocus), FocusText(normal));
                g_layoutFocus = normal;
            }
            // not during a mouse drag: the pressed cover must stay (its release ends the drag); done after it
            if ((focusChanged || visible != m.coverSet) && !m.drag) SetCovers(a_movie, a_widget);
            if (const float y = g_layoutStickY; y != 0.0f && !m.drag) {
                if (const auto column = g_layoutFocus.focus ? ColumnOf(*g_layoutFocus.focus) : std::nullopt) {
                    // XInput y is positive up; up shows what is above (unverified sign: eyes-only)
                    if (ScrollColumn(a_widget, *column, -static_cast<double>(y) * kStickStep, false) && ++m.stickScrolls == 1) {
                        logger::info("Layout: the right stick scrolls column {} (the focused block's) - later scroll frames counted only", *column);
                    }
                } else if (!std::exchange(m.stickNoFocusLogged, true)) {
                    logger::info("Layout: the right stick with no block focused - nothing scrolled (a D-pad press gives the focus)");
                }
            }
            if (!g_layoutFocus.Picked() || m.drag) {
                if (!m.drag) ClearPadLine(a_widget);
                return;
            }
            const auto& slot = *g_layoutFocus.target;
            const double scroll = g_menu.scroll[slot.column];
            if (m.padLine && m.padLine->first == slot && std::abs(m.padLine->second - scroll) <= kRectTolerance) return;
            m.padLine = std::pair{ slot, scroll };
            DrawSlotLine(a_widget, slot, *g_layoutFocus.focus);
        }

        // Each frame (Frame handler): the wraps, the published refusals, the queued requests, the buttons, the drags
        void LayoutFrameWork(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget);

        void LayoutFrame(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            if (g_closing) return;  // a frame still running when the close came: no mode change, no save (the next open resets)
            LayoutFrameWork(a_movie, a_widget);
            SyncMode(g_menu.layout.active);  // fix wave 2: the frame's state is the final truth when nothing is queued
        }

        void LayoutFrameWork(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            auto& m = g_menu.layout;
            InstallInputWraps(a_movie);
            PublishLayoutBlock(a_movie);
            std::vector<LayoutRequest> requests;
            {
                const std::scoped_lock lock(g_layoutRequestLock);
                requests.swap(g_layoutRequests);
            }
            // final review: the frame's result is the source of truth - each mode request settles the input's flag, unless a newer
            // one was made since (a quick K off/on: the Save's leave no longer turns the flag off under the Enter that follows it)
            for (const auto& request : requests) {
                switch (request.kind) {
                case LayoutRequestKind::kEnter: EnterLayoutMode(a_movie, a_widget, request.why); break;
                case LayoutRequestKind::kSave: LeaveLayoutMode(a_movie, a_widget, true, request.why); break;
                case LayoutRequestKind::kCancel: LeaveLayoutMode(a_movie, a_widget, false, request.why); break;
                case LayoutRequestKind::kNav: PadKey(a_movie, a_widget, request.nav, request.why); break;
                }
                if (request.kind != LayoutRequestKind::kNav) SettleMode(request.seq, m.active);
            }
            if (std::exchange(m.lockDue, false)) {
                if (m.active) {
                    const auto seq = BeginModeChange(false);
                    LeaveLayoutMode(a_movie, a_widget, true, m.lockSource);
                    SettleMode(seq, m.active);
                } else if (const auto bits = LiveBlock(Input::HeldCount() > 0); bits != 0) {
                    LogRefusal(bits, m.lockSource);
                } else if (!g_layoutMode) {  // on already: an Enter is queued (the next frame runs it)
                    const auto seq = BeginModeChange(true);
                    EnterLayoutMode(a_movie, a_widget, m.lockSource);
                    SettleMode(seq, m.active);
                }
            }
            if (std::exchange(m.resetDue, false) && m.active) {
                const auto reset = Settings::DefaultLayout();  // with the add-on blocks (Task 6)
                if (!(reset == m.working)) {
                    EndDrag(a_widget);
                    EndPick(a_movie, a_widget, "the reset");
                    ApplyLayout(a_movie, a_widget, reset);
                    ++m.resets;
                    logger::info("Layout: reset to the default layout {} (saved when the mode is left with the lock)", Describe(reset));
                }
            }
            if (!m.active) return;
            if (auto press = std::exchange(m.pressDue, std::nullopt)) StartDrag(a_movie, a_widget, std::get<0>(*press), std::get<1>(*press), std::get<2>(*press));
            if (std::exchange(m.borderPressDue, false) && !m.drag && !m.borderGrab) m.borderGrab = m.borderPressX - ColumnViewport(0).right;
            DragFrame(a_movie, a_widget);
            BorderFrame(a_movie, a_widget);
            if (std::exchange(m.releaseDue, false)) Drop(a_movie, a_widget);
            if (std::exchange(m.borderReleaseDue, false)) BorderDrop(a_movie, a_widget);
            if (m.active) PadFrame(a_movie, a_widget);  // Task 5 (after the drop: the covers are made again first)
        }

        // At the menu's close (the close event; the movie is going, so nothing is drawn): a mode still on ends as Cancel
        // (SelfTest L4: nothing was written, the saved layout is the entry layout), and the input boundary's tests
        // final review ruling: this runs on the close event's thread, so it only flips atomics (the mode off for the hook and the
        // wraps, the requests dropped, the entry refusals back to "no widget"; the hook's held / owned tails are left as they are)
        // and logs; the frame-thread state (m.active, the drag, g_layoutFocus) is left to the next AttachWidget's reset, and the
        // frame handler and the layout handlers do nothing once g_closing is set. Nothing is saved: a mode still on ends as Cancel
        void LayoutAtClose()
        {
            const auto& m = g_menu.layout;
            bool wasOn = false;
            {
                const std::scoped_lock lock(g_layoutRequestLock);
                wasOn = g_modeLedger.on;
                ResetModeLocked();
            }
            g_layoutBlock = kBlockNoWidget;
            if (wasOn) logger::info("Layout: mode ended by the menu's close - cancelled, nothing written");
            const auto counts = Input::ReadCounters();
            SelfTest(std::format("L5 input passthrough {} - {} event(s) judged with the layout mode off, {} kept though neither the toggle nor an owned "
                                 "tail (want 0); {} toggle press(es) and {} owned tail event(s) kept",
                counts.strayOff == 0 ? "PASS" : "FAIL", counts.eventsOff, counts.strayOff, counts.toggles, counts.ownedOff));
            if (m.entered > 0 || wasOn) {
                SelfTest(std::format("L5 input leak {} - in the layout mode ({} time(s) this menu) the hook kept {} button down(s); SkyUI's handleInput "
                                     "got {} call(s) (want 0), AttemptEquip {} (not called); the shield took {} press(es)",
                    m.inputLeaks == 0 ? "PASS" : "FAIL", m.entered, counts.downsOn, m.inputLeaks, m.equipKept, m.shieldPresses));
            }
            if (m.inputWrapped) {
                std::string unseen;
                for (const auto code : m.wrapKeysOff) {
                    if (!Input::KeyboardDownSeenOff(code)) unseen += std::format(" {}", code);
                }
                const char* verdict = !unseen.empty() ? "FAIL" : m.wrapKeyDownsOff == 0 ? "SKIP" : "PASS";
                SelfTest(std::format("L5 route parity {} - mode off: {} keyboard down(s) passed the hook; SkyUI's handleInput got {} keyboard keyDown(s) of {} "
                                     "key(s){}{}; keys it got that never passed the hook:{} (want none: the menu's keys come from below "
                                     "MenuControls::ProcessEvent)",
                    verdict, counts.keyboardDownsOff, m.wrapKeyDownsOff, m.wrapKeysOff.size(),
                    m.wrapKeyDownsOff == 0 ? " (no keyboard key used: nothing to compare)" : "",
                    m.wrapNoSkseCode > 0 ? std::format(", {} keyDown(s) without skseKeycode not compared", m.wrapNoSkseCode) : std::string{},
                    unseen.empty() ? " none" : unseen));
            }
            if (m.drops + m.borderMoves + m.resets > 0) {
                logger::info("Layout: this menu {} drop(s), {} border move(s), {} reset(s)", m.drops, m.borderMoves, m.resets);
            }
            // Task 5: the focus model's keys this menu (each pad drop has its own L6 pad drop line)
            if (m.padKeys > 0) {
                SelfTest(std::format("L6 pad keys {} - {} key(s) to the focus model: {} focus move(s), {} pick(s), {} drop(s) ({} where it was), {} pick(s) "
                                     "cancelled, {} border step(s), {} ignored (the mode off or a mouse drag on); {} drop check(s) failed (want 0); the "
                                     "right stick scrolled {} frame(s)",
                    m.padChecksFailed == 0 ? "PASS" : "FAIL", m.padKeys, m.padFocusMoves, m.padPicks, m.padDrops, m.padInPlace, m.padPickCancels,
                    m.padBorders, m.padIgnored, m.padChecksFailed, m.stickScrolls));
            }
            g_layoutStickY = 0.0f;
        }

        // ---------------------------------------------------------------------
        // Per-frame work
        // ---------------------------------------------------------------------

        // A category tab change (design 3.7) is acted on at once
        void WatchTab(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            const auto tab = ReadTab(a_movie);
            if (g_menu.tabFlag && *g_menu.tabFlag == tab.flag) return;
            const bool first = !g_menu.tabFlag.has_value();
            g_menu.tabFlag = tab.flag;
            g_menu.tab = tab.kind;
            // spec v13 B1: Powers only, not Shouts (64); off by default since build 15 (ini bHidePowers3D)
            g_hide3D = Settings::Get().hidePowers3D && tab.flag == kFlagPowers;
            if (g_hide3D && !std::exchange(g_menu.powersHideLogged, true)) logger::info("Widget: Powers tab: 3D hidden (user 2026-10-26)");
            logger::info("Widget: category flag {} ({})", tab.flag,
                tab.kind == Tab::kSchool ? tab.school : tab.kind == Tab::kPowers ? "powers/shouts" :
                                                tab.kind == Tab::kEffects        ? "active effects" :
                                                                                   "all/favorites");
            g_menu.tabSchool = tab.kind == Tab::kSchool ? tab.school : std::string{};
            // review v12 #S3: a category change keeps the selection - the list is put back on it once it is filled again
            // (WatchReselect). Without the wrap (G3) or a selection: as before
            if (!first && g_menu.selWrapped && HasSelection()) {
                g_menu.reselectLeft = kReselectWaitFrames + kReselectFrames;
                g_menu.reselectSeq = g_menu.selSeq;
                logger::info("Widget: category change keeps the selection {:08X}{} (the list is put back on it once filled)", g_menu.sel.id,
                    g_menu.cardFromTree ? std::format(", tree card {:08X}", g_menu.cardFromTree) : std::string{});
            }
            // blocks plan Task 4: a tab change ends the layout mode as Cancel (the hook and the shield keep the tabs from changing
            // in the mode; whatever got through, the entry layout is put back first)
            if (!first && g_menu.layout.active) {
                const auto seq = BeginModeChange(false);
                LeaveLayoutMode(a_movie, a_widget, false, "the tab change");
                SettleMode(seq, g_menu.layout.active);
                SyncMode(g_menu.layout.active);
            }
            // D6: the tab's hidden blocks close up; the item card's slot moves between the Powers/Shouts tabs and the others, so
            // its transform is worked out again (Rearrange)
            Rearrange(a_movie, a_widget);
            ApplyVisibility(a_widget);
        }

        // review v12 #S3: after a category change the list is filled again later (BasicList.as:224 requestUpdate, through an
        // interval) and starts with no selection (InventoryLists.as:315). Once the committed entry is in it, it is selected
        // again, marked as ours, so the game's card and 3D come back for it (without a new selection: Commit keeps it as the
        // same selection at its new row). Not in the list (or still filtered out after kReselectFrames): its 3D comes
        // through the tree-click path (P3) when that is on, else it is logged that the 3D stays as it is
        void WatchReselect()
        {
            if (g_menu.reselectLeft <= 0) return;
            if (g_menu.selSeq != g_menu.reselectSeq || !HasSelection()) {
                g_menu.reselectLeft = 0;
                logger::info("Widget: category change - a new selection came before the list was put back; nothing to put back");
                return;
            }
            if (--g_menu.reselectLeft >= kReselectFrames) return;  // the re-fill's interval first
            if (g_menu.cardFromTree) {
                g_menu.reselectLeft = 0;
                logger::info("Widget: category change - the tree card {:08X} stays (it is the selection; the list is not moved)", g_menu.cardFromTree);
                return;
            }
            const bool outer = std::exchange(g_menu.reselecting, true);
            const auto pick = SelectInList(g_menu.sel.id, g_menu.sel.source);
            g_menu.reselecting = outer;
            if (pick.took) {
                g_menu.reselectLeft = 0;
                logger::info("Widget: category change - the list put back on the selection {:08X} (index {})", g_menu.sel.id, pick.index);
                return;
            }
            if (pick.found && g_menu.reselectLeft > 0) return;  // filtered out so far: the re-fill may not have run yet
            g_menu.reselectLeft = 0;
            auto* form = CommittedForm();
            auto* spell = form ? form->As<RE::SpellItem>() : nullptr;
            const bool p3 = spell && g_p3.allowed && !g_p3.failed;
            logger::info("Widget: category change - the selection {:08X} is {} the new list: the card stays; its 3D {}", g_menu.sel.id,
                pick.found ? "filtered out of" : "not in",
                p3     ? "through the tree-click path (P3 LoadInventoryItem)" :
                !spell ? "stays as it is (not a spell: no tree-click path)" :
                         "stays as it is (the tree-click path P3 is off or failed this menu)");
            if (p3) RequestUnlearnedPreview(spell->GetFormID());
        }

        // Nothing chosen (no list row highlighted - the menu opens so, the mouse not on a row yet - and no tree card): the
        // right side shows nothing (user 2026-10-26: "아무것도 선택하지 않았거나 호버하지 않은 상태에서는 오른쪽의 창들이
        // 비가시상태여야해"); the preview's 3D goes with it (g_previewShown)
        void WatchChosen(RE::GFxMovieView*, RE::GFxValue& a_widget)
        {
            const bool row = g_menu.sel.index >= 0;  // spec v12 G2: the committed selection, not the highlight (a hover)
            const bool chosen = row || g_menu.cardFromTree != 0;
            if (chosen == g_menu.chosen && !g_menu.firstFrame) return;
            g_menu.chosen = chosen;
            ApplyVisibility(a_widget);
            if (g_menu.chosenLogs++ < kMaxChosenLogs) {
                logger::info("Widget: {} - the blocks show {}", chosen ? "something chosen" : "nothing chosen (no list row, no tree card)",
                    chosen ? "their content" : "their empty look");
            }
        }

        // spec v12 G2: follows the committed selection (a click, a key, a tree click, the menu's opening), not the list's
        // highlight; a change of it is a new selection even for the same spell (another row, or a press replacing a tree card)
        void WatchSelection()
        {
            if (g_menu.selSeq == g_menu.selSeen && !g_menu.firstFrame) return;
            g_menu.selSeen = g_menu.selSeq;
            auto* form = CommittedForm();
            auto* spell = form ? form->As<RE::SpellItem>() : nullptr;
            const RE::FormID id = spell ? spell->GetFormID() : 0;
            g_menu.listSelection = id;
            // a real list change: SkyUI's own highlight event already asked for its card and 3D (InventoryLists.as:500,
            // ItemMenu.as:326-357), so our tree card (P2) is gone and a waiting P3 check is about another spell now
            if (std::exchange(g_menu.cardFromTree, 0)) {
                g_menu.cardHoldLeft = 0;
                logger::info("Widget: item card back to the list's own (the list selection changed)");
            }
            CancelUnlearnedPreview("the list selection changed");
            const bool ours = id && id == g_menu.selectedByUs;
            g_menu.selectedByUs = 0;  // one change only
            logger::info("Widget: list selection {:08X} {} (school {}{})", id, spell ? spell->GetName() : "(none)",
                spell ? SpellSchool::Of(spell) : std::string("-"), ours ? ", from an add-on block's show request" : "");
        }

        // An add-on block's kMessageShowSpell (Block API v3; what the built-in tree's node click did until HoM add-on plan Task 5):
        // the spell selected in the list when it is there (committed as the add-on's, a_requestId), else on the tree card (P2)
        // and, with bPreviewUnlearned, in the 3D (P3) - FollowClick; a_nameShown false keeps the card and the 3D off it. a_what
        // names the request in the log. A served request is Context::source kAddon with its requestId, whether the list took the
        // spell or the tree card shows it
        void ShowSpellFromAddon(RE::GFxMovieView* a_movie, RE::FormID a_id, bool a_nameShown, std::uint32_t a_requestId, const std::string& a_what)
        {
            constexpr auto source = SelectionRule::Source::kAddon;
            g_menu.servedRequest = a_requestId;
            const auto pick = a_id ? SelectInList(a_id, source) : ListPick{};
            if (pick.took) {
                g_menu.selectedByUs = a_id;
                // spec v12 S3/G1: a show request stays a selection; committed here too, since a set to the row the list already
                // had sends no selectionChange for the wrap to see (BasicList.as:341 / ScrollingList.as:328)
                RE::GFxValue itemList;
                if (a_movie->GetVariable(&itemList, kItemListPath)) Commit(EntryAt(itemList, pick.index), pick.index, source);
            } else if (a_id) {
                // not in the list (or filtered out by the tab): the tree card (P2) shows it; the committed list selection stays
                logger::info("Widget: selection {:08X} by {} (not in the list: the tree card shows it)", a_id, SelectionRule::Name(source));
            }
            logger::info("Widget: {}: in list {}, index {}, member selectedIndex, took {}", a_what, pick.found ? "yes" : "no", pick.index,
                pick.took ? "yes" : pick.found ? "no (filtered out by the tab)" : "-");
            FollowClick(a_movie, a_id, pick, a_nameShown);
            g_menu.selFrom = BlockApi::SelectionSource::kAddon;  // every path, the tree card's too (no Commit there)
        }

        // Block API v3: the kMessageShowSpell requests queued since the last frame. At most one is served - the newest, while it is
        // due (the list selection has not changed since it was sent; BlockRules::PickShowSpell) - as a click on that spell in the
        // add-on's block would be (ShowSpellFromAddon); the others are dropped, in one log line. All are dropped in the layout
        // mode (it owns the input); an unknown id is logged once. True when one was served
        bool TakeAddonShowSpells(RE::GFxMovieView* a_movie)
        {
            constexpr int kMaxShowLogs = 20;
            BlockRules::ShowSpellQueue queue;
            {
                const std::scoped_lock lock(g_v3Lock);
                queue.swap(g_showSpellQueue);
            }
            std::erase_if(queue, [](const BlockRules::QueuedShowSpell& a_queued) { return !KnownAddon(a_queued.request.id, "show request ('SLMS')"); });
            if (queue.empty()) return false;
            const bool mode = g_menu.layout.active;
            const auto pick = mode ? BlockRules::ShowSpellPick{ std::nullopt, queue } : BlockRules::PickShowSpell(queue, g_menu.selSeq);
            if (!pick.dropped.empty() && ++g_menu.addonShowLogs <= kMaxShowLogs) {
                std::string dropped;
                for (const auto& queued : pick.dropped) {
                    dropped += std::format("{}{} {:08X} (request {}, sent at seq {})", dropped.empty() ? "" : ", ", queued.request.id, queued.request.formId,
                        queued.request.requestId, queued.seqAtQueue);
                }
                logger::info("Widget: show request(s) dropped - {}: {}{}",
                    mode        ? std::string("the layout mode is on") :
                    pick.serve  ? std::string("a newer one is served this frame") :
                                  std::format("the list selection changed after they were sent (seq {} now)", g_menu.selSeq),
                    dropped, g_menu.addonShowLogs == kMaxShowLogs ? " - later ones this menu not logged" : "");
            }
            if (!pick.serve) return false;
            const auto& request = pick.serve->request;
            ShowSpellFromAddon(a_movie, request.formId, request.nameShown, request.requestId,
                std::format("add-on block {} show spell {:08X} (request {}{})", request.id, request.formId, request.requestId, request.nameShown ? "" : ", name hidden"));
            return true;
        }

        // Q4: the left SkyUI list's row text size in _root stage units, where our widget draws: the selected row's clip
        // (BasicList.as:123 selectedClip) -> its text field that shows the entry's text (else its largest) -> the field's
        // getTextFormat().size, times the field's _yscale and the row clip's scale to _root (a 100-unit segment mapped through
        // the stage, as the item card's fit does). Needs a list selection: tried every frame for kListMeasureFrames frames.
        // Once read, our card follows (WatchOwnCard) and every v3 add-on block is drawn again at the new size.
        void MeasureListRow(RE::GFxMovieView* a_movie, RE::GFxValue& a_root)
        {
            if (g_menu.rowSize > 0 || g_menu.listMeasureLeft <= 0) return;
            if (--g_menu.listMeasureLeft == 0) {
                logger::info("Widget: the list's row text size was not found in {} frames (no list selection?) - reading sizes use {:.0f}",
                    kListMeasureFrames, kListRowFallback);
                return;
            }
            RE::GFxValue list, clip, entry;
            if (!a_movie->GetVariable(&list, kItemListPath) || !list.IsObject() || !list.GetMember("selectedClip", &clip) || !clip.IsDisplayObject()) {
                return;
            }
            const std::string entryText = list.GetMember("selectedEntry", &entry) ? StringMember(entry, "text") : std::string{};
            const auto clipTarget = StringMember(clip, "_target");
            std::vector<std::string> names;
            clip.VisitMembers([&](const char* a_name, const RE::GFxValue& a_value) {
                if (a_name && a_value.IsDisplayObject()) names.emplace_back(a_name);
            });
            struct Seen
            {
                std::string name, text;
                double size{}, yscale{}, toRoot{}, stage{};
                bool match{};
            };
            std::optional<Seen> best;
            for (const auto& name : names) {
                RE::GFxValue field;
                if (!clip.GetMember(name.c_str(), &field) || !field.IsDisplayObject()) continue;
                // the row's own fields only (VisitMembers also lists members that point elsewhere); a field without _target is taken
                const auto fieldTarget = StringMember(field, "_target");
                if (!fieldTarget.empty() && !clipTarget.empty() && !ChildOf(field, clipTarget)) continue;
                const auto text = StringMember(field, "text");
                if (text.empty() || !PartShows(field)) continue;
                const auto size = TextSizeIn(a_movie, clip, field, a_root);
                if (!size) continue;
                Seen seen{ name, text, size->size, size->yscale, size->toRoot, size->stage, !entryText.empty() && text.starts_with(entryText) };
                if (!best || (seen.match && !best->match) || (seen.match == best->match && seen.stage > best->stage)) best = std::move(seen);
            }
            if (!best || !(best->stage > 0)) return;  // tried again next frame
            g_menu.rowSize = std::clamp(best->stage, kListRowMin, kListRowMax);
            logger::info("Widget: list row text size {:.1f} stage units - field {} '{}' ({}), format size {:.1f}, field _yscale {:.2f}, row clip "
                         "to _root x{:.3f}{}; reading sizes are at least {:.1f} (were {:.0f} until now)",
                best->stage, best->name, best->text, best->match ? "the entry's text" : "the row's largest field", best->size, best->yscale,
                best->toRoot, best->stage != g_menu.rowSize ? std::format(" - CLAMPED to {:.0f}..{:.0f}", kListRowMin, kListRowMax) : "",
                g_menu.rowSize, kListRowFallback);
            // Block API v3: every v3 block is drawn again with the measured size (Context::rowSize)
            for (const auto& addon : g_addons) {
                if (addon.apiVersion >= BlockApi::kVersion && g_menu.blocksAll.contains(addon.id)) g_menu.addonDrawDue.insert(addon.id);
            }
        }

        // Q4 (review round 1 #1): the game font's line height, measured once per menu before anything is fitted. Two hidden
        // probe fields at kProbeSize ("가Ag": a Korean glyph, a capital and a descender), one and two lines: the first line's
        // height and the step to the next as shares of the size (the installed font, $EverywhereMediumFont, is a font mod's
        // choice). kLineFactor stays when the probe cannot be read or gives a share outside kMetricMin..kMetricMax.
        void MeasureTextMetrics(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            constexpr double kProbeBox = 400.0;
            constexpr const char* kSample = "\xEA\xB0\x80" "Ag";  // "가Ag"
            const std::string two = std::string(kSample) + "\r" + kSample;
            auto one = Draw::Text(a_movie, a_widget, "mmxProbe1", 0, 0, kProbeBox, kProbeBox, kSample, kWhite, kProbeSize);
            auto pair = Draw::Text(a_movie, a_widget, "mmxProbe2", 0, 0, kProbeBox, kProbeBox, two, kWhite, kProbeSize);
            const auto h1 = one.IsDisplayObject() ? Number(one, "textHeight") : std::nullopt;
            const auto h2 = pair.IsDisplayObject() ? Number(pair, "textHeight") : std::nullopt;
            for (auto* field : { &one, &pair }) {
                if (field->IsDisplayObject()) field->Invoke("removeTextField");
            }
            const double first = h1 ? *h1 / kProbeSize : 0.0, pitch = h1 && h2 ? (*h2 - *h1) / kProbeSize : 0.0;
            const bool usable = first >= kMetricMin && first <= kMetricMax && pitch >= kMetricMin && pitch <= kMetricMax;
            if (usable) {
                g_menu.lineFirst = first;
                g_menu.linePitch = pitch;
            }
            logger::info("Widget: Q4 font line height at size {:.0f}: one line {:.1f}, two lines {:.1f} -> first line {:.3f}, step {:.3f} of the "
                         "size{}",
                kProbeSize, h1.value_or(-1.0), h2.value_or(-1.0), first, pitch,
                usable ? "" : std::format(" - NOT usable, layout keeps {:.2f}", kLineFactor));
        }

        class Frame final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params& a_params) override
            {
                if (g_closing) return;
                auto* movie = OpenMagicMenuMovie();
                RE::GFxValue root;
                // only the open magic menu's own movie (a handler left on another movie does nothing)
                if (!movie || a_params.movie != static_cast<RE::GFxMovie*>(movie) || !a_params.thisPtr ||
                    !movie->GetVariable(&root, "_root")) {
                    return;
                }
                auto& widget = *a_params.thisPtr;
                if (g_menu.firstFrame) {
                    logger::info("Widget: first frame on thread {} (main thread {})", REX::W32::GetCurrentThreadId(), GameThread::Id());
                }
                // spec v12 G1/G3: the selection wrap goes on in the first frames; without it the highlight is followed (build 11)
                InstallSelectionWrap(movie);
                InstallWheel(movie, widget);  // blocks plan Task 3 (D7)
                WatchHeldShrinks();           // user 2026-10-30: a shrink that has waited its delay draws its block again
                if (!g_menu.selWrapped) FollowHighlight(movie);
                WatchTab(movie, widget);  // first: the item card's slot depends on the tab
                WatchChosen(movie, widget);
                FitItemCard(movie, root);
                WatchOwnCard(movie, widget);  // v7 C1 (after WatchTab: its box follows the tab)
                MeasureListRow(movie, root);  // Q4: our reading sizes follow the list's rows
                AddonClicks(widget);         // Block API v2: a release judged since the last frame (before any redraw of this frame)
                const auto modeChanges = g_menu.layoutModeChanges;
                LayoutFrame(movie, widget);  // blocks plan Task 4: the layout mode (after WatchTab: a tab change cancels it first)
                // review R4: the mode entered or left lays every block out at its full / fitted height - our card is drawn for it in
                // this frame, not the next
                if (g_menu.layoutModeChanges != modeChanges) {
                    g_menu.ownCardSig.clear();
                    WatchOwnCard(movie, widget);
                }
                TakeAddonHeights(movie, widget);  // Block API v2: the add-ons' kMessageHeight requests (may lay the columns out again)
                TakeAddonRedraws();          // Block API v2: the add-ons' kMessageRedraw requests
                TakeAddonVisible(widget);    // Block API v3: kMessageVisible (hidden blocks keep their places)
                DrawAddons(movie, widget);   // blocks plan Task 6: after the layout mode (it may make a panel again in its new column)
                WatchSelection();
                // Block API v3: an add-on's kMessageShowSpell, as a click on the spell - after a list change already pending is settled
                // above (it would undo the request's tree card); its own list selection is settled at once
                if (TakeAddonShowSpells(movie) && g_menu.selSeq != g_menu.selSeen) WatchSelection();
                WatchReselect();        // review v12 #S3: after a category change, the list back on the selection
                WatchTreeCard(movie);   // P2: our tree card leaves when the menu has no selection to show
                WatchChosen(movie, widget);  // again: the tree card may have left this frame (GPT review: one frame late)
                WatchEmpty(movie, widget);   // user 2026-10-30 (the board): add-ons drawn per selection close up with nothing shown
                HoldCardCheck(movie);   // P2: the tree card read back again
                DrawAddons(movie, widget);   // Block API v2: after the selection and tree-click handling - the card's form may have changed
                // Block API v2: a height asked for by those draws applies in this frame (a block drawn for a new spell at its old
                // height would show past its box, over the block below, for one frame): laid out again and drawn once more - that
                // draw is at the asked size, so it asks for nothing new
                if (TakeAddonHeights(movie, widget)) DrawAddons(movie, widget);
                // fix round 1 (HoM add-on Task 7): a kMessageVisible sent from one of those draws applies before this frame is shown
                TakeAddonVisible(widget);
                CheckUnlearned();       // P3: one model check per frame
                g_menu.firstFrame = false;
                ++g_menu.frame;
                // Block API v3: key and frame calls with this frame's content clips and overlays - after every draw and fit of this frame
                // (nothing below moves a block)
                AddonFrames(movie, widget);
                // 2026-10-31: our key hints in SkyUI's strip (the layout key, the add-ons' 'SLMK'), before the strip is placed by them
                KeyHints::Frame(movie, HintBlocks(), LayoutModeOn());
                // user 2026-10-30: the key-hint strip and the gear under the columns as this frame left them
                PlaceStripAndGear(movie, widget);
                UpdateGear(movie, widget);
                ResetButtonFrame(movie, widget);
                CheckChrome(movie, widget);  // Task 8 T2: SelfTest L9 read-backs (bSelfTest)
                if (g_menu.frame == kClipDumpFrame && Settings::Get().selfTest) DumpMenuClips(movie, root, widget);
                // review GPT 1: the preview's place after every fit of this frame (a fitted block above it moves it)
                PreviewFrame(movie, root, widget);
            }

        private:
            static void PreviewFrame(RE::GFxMovieView* a_movie, RE::GFxValue& a_root, RE::GFxValue& a_widget)
            {
                Preview::FrameInfo info;
                info.visible = a_movie->GetVisibleFrameRect();
                RE::GViewport viewport;
                a_movie->GetViewport(&viewport);
                const double visibleH = info.visible.bottom - info.visible.top;
                info.pixelsPerUnit = visibleH > 0 ? viewport.height / visibleH : 0.0;
                info.box = Settings::PanelContent(BlockRect(Layout::kPreview));  // below the "PREVIEW" title band (R9); scrolled (on screen)
                // blocks plan Task 3 (D2): the preview column's viewport - the scissor and the alpha restore take box cut to it
                if (const auto column = ColumnOf(Layout::kPreview)) {
                    info.columnClip = ColumnViewport(*column);
                    // SelfTest L2 preview clip (final review): the mask as Scaleform has it, against the clip the scissor takes
                    if (Settings::Get().selfTest) {
                        if (auto mask = Child(a_widget, kColumnMasks[*column]); mask.IsDisplayObject()) {
                            if (const auto bounds = BoundsIn(mask, a_root)) info.columnMask = Settings::Rect{ bounds->xMin, bounds->yMin, bounds->xMax, bounds->yMax };
                        }
                    }
                }
                info.viewport = { static_cast<float>(viewport.left), static_cast<float>(viewport.top),
                    static_cast<float>(viewport.left + viewport.width), static_cast<float>(viewport.top + viewport.height) };
                // GFxMovieView::TranslateToScreen is not called: with a null user matrix it crashed the game on the first
                // magic menu frame (crash-2026-10-25-21-04-09: SkyrimSE+0FC2803 mulss xmm0,[rdx], rdx = 0, called from here).
                // NaN makes PreviewMath::ScreenAgrees refuse it, so Preview uses the visible-rect mapping.
                constexpr float kNoScreenBox = std::numeric_limits<float>::quiet_NaN();
                info.boxScreen = { kNoScreenBox, kNoScreenBox, kNoScreenBox, kNoScreenBox };
                // the clip's pixels are of this buffer; the render target bound at the over-pass can be another size (v5 Q3)
                info.bufferW = viewport.bufferWidth;
                info.bufferH = viewport.bufferHeight;
                // the selection whose model Preview may hide from the game's own pass (v5 Q3): the list's item of any form
                // (a shout's entry formId is its TESShout), and the tree card's spell when P3 asked for its 3D. Spec v12 G2: the
                // committed selection's FormID - the game's 3D follows it too, since a hover no longer reaches ItemMenu
                info.selected = g_menu.sel.index >= 0 ? g_menu.sel.id : 0;
                info.treeSpell = g_menu.cardFromTree && g_p3.allowed && !g_p3.failed ? g_menu.cardFromTree : 0;
                Preview::OnFrame(info);
            }
        };
        RE::GPtr<Frame> g_frame;

        // ---------------------------------------------------------------------
        // Preview over-pass: MagicMenu::PostDisplay (vfunc 6)
        // ---------------------------------------------------------------------

        // The item menus draw their 3D over the menu picture themselves; the magic menu draws the spell's art before
        // its movie, so our black preview box covers it. After the menu's own PostDisplay the preview is drawn once
        // more (the method Inventory Reforged uses for its magic menu, main.cpp MenuDisplay; our own code, GPL).
        // Chain rule: we always call the original we replaced, so whichever of us and IR hooks later wraps the other;
        // IR's own pass runs only while its bMagicMenu is on, and then g_overPass is off (no second Render).
        // Order: original -> Preview (main thread): give back the model it hid -> place/fit -> scissor -> Render ->
        // scissor restored -> hide the selection's model from the game's next own pass (spec v5 Q3), so this pass draws
        // the fitted model inside the box. The hook carries either job alone (review G4: with bDrawPreviewOver=0 it was
        // not installed, so bMovePreview did nothing): place/fit with bMovePreview, Render with bDrawPreviewOver.
        // Not active (the preview box hidden - nothing chosen, or the Powers hide with bHidePowers3D=1 -, the menu closing,
        // another movie): Preview keeps what it hid
        // until the box shows again; close and reset give everything back.
        struct PostDisplayHook
        {
            static void thunk(RE::IMenu* a_menu)
            {
                original(a_menu);
                const bool fit = g_fitPass, over = g_overPass;
                // review v13 round 2 #1: with neither job (bMovePreview=0, bDrawPreviewOver=0, or IR/VR) Preview is called
                // only for the Powers hide: while the tab shows, and after it until a call for our movie gave the models back
                const bool hideOnly = !fit && !over;
                if (hideOnly && (!g_powersHideCalls || (!g_hide3D && !g_powersHidePending))) return;
                auto* manager = RE::Inventory3DManager::GetSingleton();
                if (!manager) return;
                const bool ourMovie = a_menu && a_menu->uiMovie.get() == g_attachedMovie.load();
                if (hideOnly && ourMovie && !g_closing) g_powersHidePending = g_hide3D.load();
                // spec v13 B1: on the Powers tab the 3D goes as on a tab without the box (the box itself stays shown)
                const bool active = g_previewShown && !g_hide3D && !g_closing && ourMovie;
                const bool noBox = (!g_previewShown || g_hide3D) && !g_closing && ourMovie;
                const bool hide3D = g_hide3D && !g_closing && ourMovie;  // review v13 #3: Powers hides even with bMovePreview=0
                if (!Preview::OnPostDisplay(*manager, fit, over, active, noBox, hide3D)) return;
                if (!g_overPassLogged.exchange(true)) {
                    logger::info("Widget: preview over-pass drew the 3D preview after the menu movie (thread {})",
                        REX::W32::GetCurrentThreadId());
                }
            }

            static inline REL::Relocation<decltype(thunk)> original;
        };

        void InstallPostDisplayHook()
        {
            constexpr std::size_t kPostDisplay = 6;  // IMenu::PostDisplay (CommonLib IMenu.h)
            REL::Relocation<std::uintptr_t> table{ RE::VTABLE_MagicMenu[0] };
            PostDisplayHook::original = table.write_vfunc(kPostDisplay, PostDisplayHook::thunk);
            g_postDisplayHooked = true;
            const auto& settings = Settings::Get();
            logger::info("Widget: MagicMenu PostDisplay hooked (preview place/fit {}, over-pass {}, hide the game's own pass {}, clip the over-pass {}, "
                         "hide the art's backdrop {}, its glow {})",
                settings.movePreview ? "on" : "off", settings.drawPreviewOver ? "on" : "off",
                settings.drawPreviewOver && settings.hideGamePass ? "on" : "off", settings.drawPreviewOver && settings.clipPreview ? "on" : "off",
                settings.hideBackdrop ? "on" : "off", settings.hideGlow ? "on" : "off");
        }

        // ---------------------------------------------------------------------
        // Menu open / close
        // ---------------------------------------------------------------------

        // Inventory Reforged runs its panel in this magic menu (it can be off for the magic menu: bMagicMenu = 0,
        // which this mod ships in MCM/Settings/InventoryReforged.ini). Read only; never touched.
        bool IrActive(RE::GFxMovieView* a_movie)
        {
            RE::GFxValue script;
            return a_movie->GetVariable(&script, kIrPanelPath) && !script.IsUndefined() && !script.IsNull();
        }

        void AttachWidget()
        {
            auto* movie = OpenMagicMenuMovie();
            if (!movie) return;  // closed meanwhile
            RE::GFxValue root, existing;
            if (!movie->GetVariable(&root, "_root") || !root.IsDisplayObject()) return;
            if (root.GetMember(kRootName, &existing) && existing.IsDisplayObject()) return;  // already on this movie

            g_menu = MenuState{};
            ApplyHiddenSetting();  // 2026-10-31: [Blocks] sHidden as the menu sink read it, before anything is arranged
            KeyHints::OnMenuOpen();
            {
                const std::scoped_lock lock(g_redrawLock);  // Block API v2: a request of the last menu is not carried over
                g_redrawQueue.clear();
            }
            {
                const std::scoped_lock lock(g_heightLock);  // nor a height: every menu opens at the registered heights
                g_heightQueue.clear();
            }
            {
                const std::scoped_lock lock(g_v3Lock);  // Block API v3: nor a show, visible or key request
                g_showSpellQueue.clear();
                g_visibleQueue.clear();
                g_keyQueue.clear();
            }
            g_selSeqShared = 0;                                       // g_menu.selSeq is 0 again
            for (auto& track : g_overlayTracks) track.made.reset();   // the last menu's overlays went with its movie
            g_menu.irActive = IrActive(movie);
            g_irActiveShared = g_menu.irActive;
            // blocks plan Task 4: a new menu - no mode, no queued request, the input tests counted afresh; the wraps are not on yet
            ++g_menuGeneration;
            {
                const std::scoped_lock lock(g_layoutRequestLock);
                ResetModeLocked();
            }
            g_layoutFocus = {};
            g_layoutStickY = 0.0f;
            g_layoutBlock = kBlockNoWidget | kBlockNoInputWrap | kBlockNoEquipWrap;  // the widget clears its bit once it is up
            g_refusalLogs = 0;
            Input::ResetCounters();
            logger::info("Widget: Inventory Reforged {} in this magic menu{}", g_menu.irActive ? "IS active" : "is not active",
                g_menu.irActive ? " - the item card and the preview are not moved" : "");
            g_closing = false;
            ResetUnlearnedPreview(g_menu.irActive);
            Preview::OnMenuOpen(g_menu.irActive);
            if (!g_frame) {
                g_frame = RE::make_gptr<Frame>();
                g_selectionWrap = RE::make_gptr<SelectionChangeWrap>();
                g_highlightWrap = RE::make_gptr<HighlightChangeWrap>();
                g_listPress = RE::make_gptr<ListPress>(false);
                g_listPressAux = RE::make_gptr<ListPress>(true);
                g_wheelListener = RE::make_gptr<WheelListener>();
                g_wheelZoomWrap = RE::make_gptr<WheelZoomWrap>();
                g_coverPress = RE::make_gptr<CoverPress>();
                g_coverRelease = RE::make_gptr<CoverRelease>();
                g_borderPress = RE::make_gptr<BorderPress>();
                g_borderRelease = RE::make_gptr<BorderRelease>();
                g_gearClick = RE::make_gptr<LockClick>("the gear button");
                g_resetClick = RE::make_gptr<ResetClick>();
                g_shieldPress = RE::make_gptr<ShieldPress>();
                g_gearOver = RE::make_gptr<GearHover>(true);
                g_gearOut = RE::make_gptr<GearHover>(false);
                g_addonPress = RE::make_gptr<AddonPress>();
                g_addonRelease = RE::make_gptr<AddonRelease>(true);
                g_addonReleaseOutside = RE::make_gptr<AddonRelease>(false);
                g_handleInputWrap = RE::make_gptr<HandleInputWrap>();
                g_attemptEquipWrap = RE::make_gptr<AttemptEquipWrap>();
            }
            auto widget = Draw::Clip(root, kRootName);
            if (!widget.IsDisplayObject()) {
                logger::error("Widget: could not create _root.{}", kRootName);
                return;
            }
            MakeShield(movie, widget);  // blocks plan Task 4: first, so it is the lowest clip of mmx_root
            DrawBoard(movie, widget);   // user 2026-10-30: behind the columns
            MeasureTextMetrics(movie, widget);  // before anything is fitted (review round 1 #1)
            const auto& settings = Settings::Get();
            // the over-pass: the magic menu draws its 3D before the movie, under our black preview box
            g_overPassLogged = false;
            g_previewShown = true;
            g_hide3D = false;
            g_powersHidePending = false;
            g_attachedMovie = movie;
            // [Blocks] sHidden holds the preview: no box this menu, and the game's own pass draws its 3D where the game puts it
            const bool previewHidden = g_menu.hidden.contains(Layout::kPreview);
            g_overPass = settings.drawPreviewOver && g_postDisplayHooked && !g_menu.irActive && !previewHidden;
            logger::info("Widget: preview over-pass {} ({})", g_overPass ? "ON" : "off",
                previewHidden             ? "the preview block is hidden ([Blocks] sHidden)" :
                !settings.drawPreviewOver ? "bDrawPreviewOver=0" :
                !g_postDisplayHooked      ? "PostDisplay not hooked" :
                g_menu.irActive           ? "Inventory Reforged draws the magic menu's 3D itself" :
                                            "the 3D preview is drawn again after the menu movie");
            const bool vr = REL::Module::IsVR();
            g_fitPass = settings.movePreview && g_postDisplayHooked && !g_menu.irActive && !vr && !previewHidden;
            g_powersHideCalls = g_postDisplayHooked && !g_menu.irActive && !vr;
            logger::info("Widget: preview place/fit {} ({})", g_fitPass ? "ON" : "off",
                previewHidden         ? "the preview block is hidden ([Blocks] sHidden)" :
                !settings.movePreview ? "bMovePreview=0" :
                !g_postDisplayHooked  ? "PostDisplay not hooked" :
                g_menu.irActive       ? "Inventory Reforged runs its panel in this menu" :
                vr                    ? "Skyrim VR: its loadedModels layout differs" :
                g_overPass            ? "in PostDisplay, before the over-pass draws it" :
                                        "in PostDisplay; without the over-pass the game's own next pass draws it");
            // without the over-pass the game's 3D (drawn before the movie) shows only through a see-through box: the preview
            // panel keeps its edge and title, no fill (second review B1); with IR its own pass draws over the menu
            const bool seeThrough = !g_overPass && !g_menu.irActive;
            if (seeThrough) logger::info("Widget: preview box drawn without its black fill (no over-pass: the 3D under the movie shows through)");
            g_menu.previewAlpha = seeThrough ? 0.0 : kPreviewAlpha;
            // blocks plan Task 3: the two masked column clips first, the panels inside them
            if (!MakeColumns(widget)) {
                Draw::Remove(root, kRootName);
                return;
            }
            Rearrange(movie, widget);  // blocks plan Task 2: the rects of the layout before the panels are made
            CheckLayoutRects();
            for (const BlockId& id : AllBlocks()) Panel(movie, widget, id);
            for (const auto& addon : g_addons) {  // Task 6: drawn by their callbacks in the first frame (DrawAddons)
                if (g_menu.blocksAll.contains(addon.id)) g_menu.addonDrawDue.insert(addon.id);
            }
            CheckPanelPlaces(movie, root, widget);
            CheckMasks(root, widget);
            // v7 C1: our info card's panel (drawn again per item; hidden until SkyUI's card would show)
            SetVisible(widget, Layout::kSpellCard, false);
            logger::info("Widget: info card {}", OwnCard() ? "ours (bOwnCard=1): SkyUI's item card is hidden" :
                                                  g_menu.irActive ? "SkyUI's, left to Inventory Reforged" :
                                                  SpellCardHidden() ? "none - the spell card is hidden ([Blocks] sHidden): SkyUI's item card stays where SkyUI puts it" :
                                                                    "SkyUI's, moved and fitted (bOwnCard=0)");
            MakeLayoutChrome(movie, widget);  // blocks plan Task 4 (D5): the border handle, the drag overlay, the gear and its reset button
            CalibrateFocusCheck();
            g_layoutBlock = g_layoutBlock.load() & ~kBlockNoWidget;
            RE::GFxValue fn;
            movie->CreateFunction(&fn, g_frame.get());
            widget.SetMember("onEnterFrame", fn);
            const auto area = movie->GetVisibleFrameRect();
            logger::info("Widget: attached on thread {} (visible stage x {:.0f}..{:.0f} y {:.0f}..{:.0f}, {} add-on block(s))",
                REX::W32::GetCurrentThreadId(), area.left, area.right, area.top, area.bottom, g_addons.size());
        }

        class MenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
        {
        public:
            static MenuSink* GetSingleton()
            {
                static MenuSink singleton;
                return &singleton;
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
            {
                if (!a_event) return RE::BSEventNotifyControl::kContinue;
                if (a_event->menuName != RE::MagicMenu::MENU_NAME) {
                    OtherMenu(*a_event);
                    return RE::BSEventNotifyControl::kContinue;
                }
                logger::info("Widget: magic menu {} (thread {})", a_event->opening ? "opened" : "closed", REX::W32::GetCurrentThreadId());
                {
                    const std::scoped_lock lock(g_blockingLock);
                    g_blockingMenus.clear();
                }
                if (a_event->opening) {
                    // 2026-10-31 (the settings tab writes them while the game runs): [Display] sLanguage and [Blocks] sHidden read again
                    // here, before the UI task that builds the widget
                    LangGame::Refresh(WidgetText::g_catalog, WidgetText::kLangDir);
                    BlockHiding::Read("menu open");
                    CloseRegistry();  // blocks plan Task 6: the first open ends add-on registration (before the widget is made)
                    SKSE::GetTaskInterface()->AddUITask([] { AttachWidget(); });
                } else {
                    g_closing = true;  // first: no frame work from here on
                    KeyHints::OnMenuClose();
                    LayoutAtClose();   // blocks plan Task 4: a mode still on is cancelled; the input tests
                    // the wheel listener is not removed here (final review ruling: no GFx call on the close thread) - it does nothing
                    // while g_closing (WheelColumn) and goes with the movie
                    if (g_menu.stripApplies > 0) {  // review R4: how often the strip was moved (again) this menu
                        logger::info("Widget: key-hint strip moved {} time(s) this menu", g_menu.stripApplies);
                    }
                    if (g_menu.stripReadFails > 0) {
                        logger::info("Widget: key-hint strip - a shown button's bounds did not read on {} frame(s) this menu (not moved on those frames; "
                                     "after {} in a row under one look the buttons that read place it)",
                            g_menu.stripReadFails, kPollFrames);
                    }
                    if (g_menu.stripFoundFrame < 0 && g_menu.frame > 0) {
                        logger::info("Widget: no key-hint strip ({}) in {} frame(s) this menu - SkyUI's strip was left where it is", kButtonPanelPath,
                            g_menu.frame);
                    }
                    CloseChecks();  // Task 8: SelfTest L8 last, L9 board / gear / reset
                    if (g_menu.addonKeysUnused > 0) {
                        logger::info("Widget: add-on keys this menu: {} key call(s) not used by a block (not logged one by one)",
                            g_menu.addonKeysUnused);
                    }
                    if (g_menu.wheelScrolls > 0 || g_menu.wheelZoomsKept > 0) {
                        logger::info("Widget: wheel this menu: {} column scroll(s), SkyUI's zoom kept from firing {} time(s)", g_menu.wheelScrolls,
                            g_menu.wheelZoomsKept);
                    }
                    g_overPass = false;
                    g_fitPass = false;
                    g_powersHideCalls = false;
                    g_attachedMovie = nullptr;
                    Preview::OnMenuClose();
                    if (g_menu.fits > 0) {
                        logger::info("Widget: item card this menu: placed/put back {} times, moved away by something else {} times, "
                                     "fitted again for new content {} times",
                            g_menu.fits, g_menu.movedAway, g_menu.refits);
                    }
                    FlushLog();
                }
                return RE::BSEventNotifyControl::kContinue;
            }

            MenuSink(const MenuSink&) = delete;
            MenuSink& operator=(const MenuSink&) = delete;

        private:
            MenuSink() = default;

            // blocks spec "Entry refusals and exits": another menu that pauses the game, uses the menu context or is modal opening
            // over the magic menu ends the layout mode as Cancel and keeps it from starting while it is up. A menu whose flags
            // cannot be read counts as such
            static void OtherMenu(const RE::MenuOpenCloseEvent& a_event)
            {
                auto* ui = RE::UI::GetSingleton();
                if (!ui || !ui->IsMenuOpen(RE::MagicMenu::MENU_NAME)) return;
                const std::string name = a_event.menuName.c_str() ? a_event.menuName.c_str() : "";
                if (!a_event.opening) {
                    const std::scoped_lock lock(g_blockingLock);
                    g_blockingMenus.erase(name);
                    return;
                }
                const auto menu = ui->GetMenu(a_event.menuName);
                const bool blocking = !menu || menu->PausesGame() || menu->UsesMenuContext() || menu->Modal();
                if (!blocking) return;
                {
                    const std::scoped_lock lock(g_blockingLock);
                    g_blockingMenus.insert(name);
                }
                // each name once per game session: which menus keep the layout mode from starting (a companion menu that always
                // opens with the magic menu would show here and refuse the mode)
                static std::mutex s_loggedLock;
                static std::set<std::string> s_logged;
                const std::scoped_lock lock(s_loggedLock);
                if (s_logged.insert(name).second) {
                    logger::info("Layout: menu \"{}\" opened over the magic menu ({}) - the layout mode ends as Cancel and does not start while it is up "
                                 "(logged once per menu)",
                        name, !menu ? "flags not readable" : std::format("pauses {}, menu context {}, modal {}", menu->PausesGame(), menu->UsesMenuContext(), menu->Modal()));
                }
                CancelLayoutMode(std::format("the menu \"{}\" opening over the magic menu", name).c_str());
            }
        };
    }

    std::vector<BlockLabel> KnownBlocks()
    {
        std::vector<BlockLabel> blocks;
        for (const char* id : kBlocks) blocks.push_back({ id, {}, BlockApi::kPluginName, {} });
        const std::scoped_lock lock(g_registryLock);
        for (const auto& block : g_registry.Blocks()) blocks.push_back({ block.id, block.title, block.sender, block.legacyId });
        return blocks;
    }

    BlockRegistry::Outcome RegisterBlock(std::string_view a_sender, const void* a_data, std::uint32_t a_dataLen)
    {
        BlockRegistry::Outcome outcome;
        double height = 0.0;
        bool clickIgnored = false;
        {
            const std::scoped_lock lock(g_registryLock);
            outcome = g_registry.Register(a_sender, a_data, a_dataLen);
            if (const auto* block = outcome.accepted ? g_registry.Find(outcome.id) : nullptr) {
                height = block->height;
                clickIgnored = block->click && (block->flags & BlockApi::kFlagOwnMouse) != 0;
            }
        }
        const std::string id = outcome.id.empty() ? std::string("(no id)") : outcome.id;
        if (outcome.accepted) {
            logger::info("Widget: add-on block {} registered by {} ({:.0f} high)", id, a_sender, height);
            if (clickIgnored) logger::warn("Widget: add-on block {} sets both click and kFlagOwnMouse - its click is never called (BlockApi.h \"flags\")", id);
            SelfTest(std::format("L6 addon {} registered - from {}, {:.0f} high", id, a_sender, height));
        } else {
            logger::warn("Widget: add-on block {} from {} refused - {}", id, a_sender, outcome.reason);
            SelfTest(std::format("L6 addon {} refused {} (from {})", id, outcome.reason, a_sender));
        }
        FlushLog();
        return outcome;
    }

    void RequestAddonRedraw(std::string_view a_sender, const void* a_data, std::uint32_t a_dataLen)
    {
        constexpr int kMaxBadRedrawLogs = 5;
        constexpr std::size_t kMaxQueuedRedraws = 64;  // distinct ids waiting for one frame; more are dropped (and logged)
        static std::atomic<int> s_badLogs{ 0 };
        if (g_closing || !g_attachedMovie.load()) return;  // the menu is closed: it draws every block at its next open
        const auto id = BlockRules::RedrawId(a_data, a_dataLen);
        const auto sender = a_sender.empty() ? std::string_view("(no sender)") : a_sender;
        if (!id) {
            if (s_badLogs.fetch_add(1) < kMaxBadRedrawLogs) {
                logger::warn("Widget: a redraw request ('SLMD') from {} without a block id of 1..{} characters (data {}, {} byte(s)) - ignored", sender,
                    BlockRules::kMaxFinalIdLength, a_data ? "set" : "null", a_dataLen);
            }
            return;
        }
        const std::scoped_lock lock(g_redrawLock);
        if (std::ranges::find(g_redrawQueue, *id) != g_redrawQueue.end()) return;  // queued already: one redraw draws it
        if (g_redrawQueue.size() >= kMaxQueuedRedraws) {
            if (s_badLogs.fetch_add(1) < kMaxBadRedrawLogs) logger::warn("Widget: redraw request for {} from {} dropped - {} ids queued already", *id, sender, kMaxQueuedRedraws);
            return;
        }
        g_redrawQueue.push_back(*id);
    }

    void RequestAddonHeight(std::string_view a_sender, const void* a_data, std::uint32_t a_dataLen)
    {
        static std::atomic<int> s_badLogs{ 0 };
        QueueRequest(
            a_sender, "height request ('SLMH')", s_badLogs, g_heightLock, [&](std::string& a_why) { return BlockRules::ReadHeight(a_data, a_dataLen, a_why); },
            [](BlockRules::HeightRead a_request) { return BlockRules::Enqueue(g_heightQueue, a_request.id, a_request.height, kMaxQueuedRequests); });
    }

    void RequestAddonShowSpell(std::string_view a_sender, const void* a_data, std::uint32_t a_dataLen)
    {
        static std::atomic<int> s_badLogs{ 0 };
        QueueRequest(
            a_sender, "show request ('SLMS')", s_badLogs, g_v3Lock, [&](std::string& a_why) { return BlockRules::ReadShowSpell(a_data, a_dataLen, a_why); },
            [](BlockRules::ShowSpellRead a_request) {
                return BlockRules::EnqueueShowSpell(g_showSpellQueue, { std::move(a_request), g_selSeqShared.load() }, kMaxQueuedRequests);
            });
    }

    void RequestAddonVisible(std::string_view a_sender, const void* a_data, std::uint32_t a_dataLen)
    {
        static std::atomic<int> s_badLogs{ 0 };
        QueueRequest(
            a_sender, "visible request ('SLMV')", s_badLogs, g_v3Lock, [&](std::string& a_why) { return BlockRules::ReadVisible(a_data, a_dataLen, a_why); },
            [](BlockRules::VisibleRead a_request) { return BlockRules::Enqueue(g_visibleQueue, a_request.id, a_request.shown, kMaxQueuedRequests); });
    }

    void RequestAddonKeyHint(std::string_view a_sender, const void* a_data, std::uint32_t a_dataLen)
    {
        constexpr int kMaxBadHintLogs = 5;
        static std::atomic<int> s_badLogs{ 0 };
        const auto sender = a_sender.empty() ? std::string_view("(no sender)") : a_sender;
        std::string why;
        auto hint = BlockRules::ReadKeyHint(a_data, a_dataLen, why);
        if (hint) {
            const std::scoped_lock lock(g_registryLock);
            if (!g_registry.Find(hint->id)) {
                why = "\"" + hint->id + "\" is not a registered block";
                hint.reset();
            }
        }
        if (!hint) {
            if (s_badLogs.fetch_add(1) < kMaxBadHintLogs) logger::warn("Widget: a key hint ('SLMK') from {} refused - {}", sender, why);
            return;
        }
        KeyHints::Set(sender, *hint);
    }

    void QueueAddonKey(std::uint32_t a_scanCode)
    {
        static std::atomic<bool> s_fullLogged{ false };
        if (g_closing || !g_attachedMovie.load()) return;
        const std::scoped_lock lock(g_v3Lock);
        if (!BlockRules::QueueKey(g_keyQueue, a_scanCode, kMaxQueuedKeys) && !s_fullLogged.exchange(true)) {
            logger::warn("Widget: key {} for the add-on blocks dropped - {} keys wait for one frame already (logged once)", a_scanCode, kMaxQueuedKeys);
        }
    }

    void RegisterDebugFiller()
    {
        constexpr float kFillerHeight = 300.0f;
        const BlockApi::Registration reg{ .id = Layout::kDebugFillerId, .title = "FILLER (TEST)", .height = kFillerHeight, .draw = DrawFiller };
        RegisterBlock(BlockApi::kPluginName, &reg, sizeof(reg));
    }

    void Install()
    {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) {
            logger::error("Widget: no UI singleton - the magic menu widget is off");
            return;
        }
        ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuSink::GetSingleton());
        logger::info("Widget: magic menu sink installed");
        // review v13 round 2 #1: hooked whatever bDrawPreviewOver/bMovePreview say - the Powers tab hides its 3D with both
        // off too; with both off the hook calls Preview only for that hide (PostDisplayHook::thunk)
        InstallPostDisplayHook();
        if (!Settings::Get().drawPreviewOver && !Settings::Get().movePreview) {
            logger::info("Widget: bDrawPreviewOver=0 and bMovePreview=0 - MagicMenu PostDisplay hooked for the Powers tab's 3D hide only");
        }
    }

    RE::GFxMovieView* OpenMagicMenuMovie()
    {
        auto* ui = RE::UI::GetSingleton();
        auto menu = ui ? ui->GetMenu<RE::MagicMenu>() : nullptr;
        return menu ? menu->uiMovie.get() : nullptr;
    }

    bool LayoutModeOn()
    {
        return g_layoutMode;
    }

    bool ToggleLayoutMode(const char* a_source, bool a_heldOther)
    {
        {
            const std::scoped_lock lock(g_layoutRequestLock);
            if (g_modeLedger.on) {
                QueueModeLocked(LayoutRequestKind::kSave, a_source);
                return true;
            }
        }
        if (const auto bits = LiveBlock(a_heldOther); bits != 0) {
            LogRefusal(bits, a_source);
            return false;
        }
        const std::scoped_lock lock(g_layoutRequestLock);
        if (!g_modeLedger.on) QueueModeLocked(LayoutRequestKind::kEnter, a_source);
        return true;
    }

    bool CancelLayoutMode(const char* a_why)
    {
        const std::scoped_lock lock(g_layoutRequestLock);
        if (!g_modeLedger.on) return false;
        QueueModeLocked(LayoutRequestKind::kCancel, a_why);
        return true;
    }

    void LayoutKey(Layout::Nav a_nav, const char* a_source)
    {
        if (g_layoutMode) QueueLayout(LayoutRequestKind::kNav, a_source, a_nav);
    }

    void LayoutStick(float a_y)
    {
        g_layoutStickY = a_y;
    }

    bool FocusLost()
    {
        return FocusGone();
    }
}

#include "Common.h"

#include "Widget.h"

#include "BlockApi.h"
#include "BlockRegistry.h"

#include "Draw.h"
#include "Hom.h"
#include "HomStyle.h"
#include "Layout.h"
#include "LayoutMath.h"
#include "Preview.h"
#include "SelectionRule.h"
#include "Settings.h"
#include "Spells.h"
#include "TreeData.h"

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
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// =============================================================================
// WIDGET - see Widget.h. Draws from Spells::Current() only: the frame handler
// runs on several threads (game run 2026-10-25) and never calls HoM.
// =============================================================================

namespace Widget
{
    namespace
    {
        using Spells::State;

        // ---------------------------------------------------------------------
        // Constants
        // ---------------------------------------------------------------------

        constexpr const char* kRootName = "mmx_root";
        constexpr const char* kIndexMember = "mmxIndex";  // a node clip's tree index, read by its onRelease

        // SkyUI's item card, then vanilla's (the bookmark branch, game runs 2026-10-08/09); its _parent is the fade holder
        constexpr std::array<const char*, 2> kCardPaths{ "_root.Menu_mc.itemCardFadeHolder.ItemCard_mc",
                                                         "_root.Menu_mc.ItemCardFadeHolder_mc.ItemCard_mc" };
        // Inventory Reforged's panel script on the menu's _root: present only when IR runs its panel in this menu
        constexpr const char* kIrPanelPath = "_root.equipmentPanelScript";
        // SkyUI's category list (SkyUI-Community b7a24a93: MagicMenu.inventoryLists.categoryList, an entry's "flag")
        constexpr const char* kCategoryFlagPath = "_root.Menu_mc.inventoryLists.categoryList.selectedEntry.flag";

        // SkyUI's magic category flags (skyui/defines/Inventory.as FILTERFLAG_MAGIC_*)
        constexpr int kFlagAlteration = 2, kFlagIllusion = 4, kFlagDestruction = 8, kFlagConjuration = 16, kFlagRestoration = 32,
                      kFlagShouts = 64, kFlagPowers = 128, kFlagActiveEffects = 256;

        constexpr int kPollFrames = 15;  // HoM state is asked for again this often
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
        constexpr double kSecondsPerMinute = 60.0, kSecondsPerHour = 3600.0;
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
        constexpr double kListRowFallback = 14.0, kListRowMin = 10.0, kListRowMax = 20.0;
        constexpr int kListMeasureFrames = 600;  // frames the list row is looked for after the menu opens (it needs a list selection)
        constexpr double kLineFactor = 1.25;     // a text line's height per font size unit until the font is measured (MeasureTextMetrics)
        constexpr double kProbeSize = 20.0, kMetricMin = 0.8, kMetricMax = 2.5;  // the font probe's size; accepted line shares of a size
        // A text line's ascent (field text top -> first baseline) per size unit, for the row-2 baseline (spec v6 L2): the probe's
        // getLineMetrics(0).ascent (a Scaleform TextField extension, CLIK TextField.as) when the movie gives it, accepted in
        // kAscentMin..kAscentMax; else kAscentFallback, measured on the build-3 frame base_v3_poison.png (size 19 -> 17 units,
        // tools/menu-render textengine.py BASELINE) - an approximation, logged as such
        constexpr double kAscentFallback = 0.90, kAscentMin = 0.5, kAscentMax = 1.5;
        constexpr double kMinFitSize = 8.0;      // a text shrunk to its room goes no smaller (review round 1 #4)
        constexpr double kFitStep = 0.5;         // and is shrunk in these steps
        constexpr int kMaxCutLogs = 40;
        // the item card's description fields by frame (SkyUI ItemCard.as): the first one shown is measured for the log (round 2 #5)
        constexpr auto kCardTextFields = std::to_array<const char*>({ "MagicEffectsLabel", "ShoutEffectsLabel", "BookDescriptionLabel",
            "PotionsLabel", "ApparelEnchantedLabel", "WeaponEnchantedLabel", "EnchantmentLabel", "MessageText" });          // "Q4 text ... cut to" lines per menu
        constexpr int kNoDataFrames = 600;       // frames without HoM's state for the school before the card stops saying "loading"
        constexpr double kGutter = 4.0;          // AS2 text fields keep 2 units free on each side of their text (textWidth leaves it out)
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

        // Tree window. Its marks, links and label colours are Heart of Magic's own (spec v11 T12-T15: HomStyle, read at menu
        // open); sizes are HoM world units x fTreeScale x fTreeMarkScale, line widths HoM world units x fTreeScale (review v11 #5:
        // HoM strokes under its zoom, canvasRendererSpell.js:38,263)
        constexpr double kLabelW = 104.0, kLabelSize = 10.0, kLabelGap = 1.0;
        constexpr int kLabelLines = 2;  // a name wraps to this many lines, then ends in "..."
        constexpr double kLinkBoxStep = 4.0;  // a link's boxes for the labels to keep off (LayoutMath::LinkBoxes)
        constexpr double kNoteSize = 10.0, kLegendSize = 10.0, kLegendIconR = 5.0, kLegendTextGap = 4.0;
        // tree badges (spec v10 T7): arrow half size, its distance from the edge, the "N개 더" text's width
        constexpr double kExitArrow = 5.0, kExitInset = 7.0, kExitTextW = 56.0;
        constexpr std::uint32_t kBlue = 0x4A90D9, kWhite = 0xF0F0F0, kLockedEdge = 0x6A6A6A, kLabelColor = 0xD8D8D8, kDimText = 0x8A8A8A;
        // the fill colour of a node's invisible hit area (alpha 0); HoM's opaque _backdrop under dimmed spells is not drawn (see DrawHomMark)
        constexpr std::uint32_t kBackdrop = 0x000000;
        // HoM's config and design presets (in game through MO2's VFS; relative to the game folder like Spells.cpp's kTreePath)
        constexpr const char* kHomConfigPath = "Data/SKSE/Plugins/SpellLearning/config.json";
        constexpr const char* kHomPresetDir = "Data/SKSE/Plugins/SpellLearning/presets/design";
        constexpr double kMinLabelW = 20.0;  // a label with less room than this to the window edge is left out
        constexpr int kKeptFarSteps = 2;      // a kept label tries its sides this many mark-reach steps further out
        constexpr int kKeptRank = 2;          // LabelRank of the kept labels and below: the centre, the selected node, the target
        constexpr double kDimTreeAlpha = 35.0;  // the previous tree under a selection that is not in the tree (T7.1)
        // The hover tooltip (spec v10 T6): its width, inner padding, gap from the node's mark and fill strength
        constexpr double kTooltipW = 150.0, kTooltipPad = 4.0, kTooltipGap = 4.0, kTooltipAlpha = 90.0;
        constexpr double kBaselineTolerance = 0.5;  // L2 self-test: the two row-2 baselines read back this close
        constexpr int kMaxL2Logs = 6;               // "SelfTest: L2 ..." lines per menu (each new reading once)

        // Progress card (laid out top-down by line heights of the sizes in use)
        // The XP numbers sit right of the bar in a row as high as their field, the bar centred in it (review round 2 #2: the
        // field centred on the 9-high bar ran into the target line above and the hint below); they keep kXpSize, not the reading
        // size, so the card's last line keeps room at large list rows
        constexpr double kCardTop = 4.0, kCardBottom = 2.0, kSchoolSize = 11.0, kTargetSize = 19.0, kBarH = 9.0, kXpW = 120.0,
                         kXpSize = 12.0, kHintSize = 13.0;
        constexpr int kMessageLines = 2;
        constexpr std::uint32_t kBarBack = 0x202830;

        // Node card (the mockup's layout: name, school and level, state with its mark, requirements, XP, button)
        // The bottom block hangs from the button: its bottom at the card bottom minus kPad (spec v6 L2: 615 with the E2 rects, the
        // item card slot's inner bottom), the XP line and the bottom rule above it as in build 5 (build 5: 38 / 70 / 76)
        constexpr double kNameSize = 19.0, kSubSize = 12.0, kStateSize = 14.0, kStateIconR = 6.0, kStateTextX = 22.0, kRuleGap = 4.0,
                         kRowSize = 13.0, kMarkShare = 0.8, kMarkGap = 4.0, kButtonH = 28.0, kButtonSize = 14.0,
                         kButtonFromBottom = kPad + kButtonH, kXpFromBottom = kButtonFromBottom + 32.0,
                         kBottomRuleFromBottom = kButtonFromBottom + 38.0;
        constexpr int kNameLines = 2, kHintLines = 2;
        constexpr std::uint32_t kOk = 0x6CC070, kNotYet = 0xE05050, kButtonFill = 0x1E3A5A, kButtonOff = 0x262626;
        // No check/cross characters: the game font has neither U+2713 nor U+2717 (build 3 frame f_008: a missing-glyph box at the
        // start of each requirement line); Draw::Check / Draw::Cross draw them as lines. U+00B7 "·" is in the font (the same
        // frame draws "Restoration · Novice"), and "..." is ASCII.

        // ---------------------------------------------------------------------
        // On-screen text: one table (spec v5 Q2), Korean (the user plays in Korean). Placeholders in braces are filled by
        // Fill(); {key} already carries its brackets ("[U]"). Wording: Gemini Argon's draft 2026-10-25 with two lines tightened
        // by Claude (spec v5 table); the rows after "build 5" were written in the same short label style for states and lines
        // the spec's table does not have. Not passed to std::format: the braces are names, not format fields.
        // ---------------------------------------------------------------------

        enum class Str : std::size_t
        {
            kProgressTitle,
            kProgressTarget,
            kProgressNoneCandidates,
            kProgressNoneNothing,
            kProgressAllMastered,
            kProgressNotReady,
            kProgressNotInTree,
            kNodeHint,
            kNodeSetTarget,
            kNodeIsTarget,
            kNodeLocked,
            kNodeMastered,
            kLegendMastered,
            kLegendLearning,
            kLegendCanLearn,
            kTreeNotInTree,
            kPreviewTitle,
            kTreeTitle,
            // build 5
            kProgressCandidates,
            kProgressCandidatesNoXY,
            kProgressReady,
            kProgressLearningEarly,
            kProgressUnknown,
            kProgressNoOther,
            kProgressNoTree,
            kProgressNoSchoolTree,
            kXp,
            kTreeNoTree,
            kTreeNoCentre,
            kTreeNoLinks,
            kTreeExit,
            kNodeCanLearn,
            kNodeTargetPct,
            kNodeLearningPct,
            kNodeReady,
            kNodeUnknown,
            kNodeSub,
            kReqAll,
            kReqSome,
            kReqNone,
            kReqMastered,
            kReqNotMastered,
            kReqChecking,
            kReqNotInGame,
            kReqMore,
            kSchoolAlteration,
            kSchoolConjuration,
            kSchoolDestruction,
            kSchoolIllusion,
            kSchoolRestoration,
            kProgressNoHom,
            kProgressNoData,
            // build 6
            kTreeLocked,
            // build 7 (C1, drafts for Argon's review)
            kCardMagicka,
            kCardMagickaPerSec,
            kCardCastFireForget,
            kCardCastConcentration,
            kCardCastConstant,
            kCardCastScroll,
            kCardShout,
            kCardShoutRecharge,
            kCardPower,
            kCardLesserPower,
            kCardAbility,
            kCardPowerAny,
            kCardActiveEffect,
            kCardTimeLeft,
            kCardHours,
            kCardMinutes,
            kCardSeconds,
            // blocks plan Task 4 (D5): the layout buttons
            kLayoutEdit,
            kLayoutLock,
            kLayoutReset,
            kCount
        };

        struct StrEntry
        {
            Str id;
            const char* key;   // the name in logs ("SelfTest: Q2 state <key>") and in the spec's table
            const char* text;
        };

        constexpr auto kStrings = std::to_array<StrEntry>({
            { Str::kProgressTitle, "progress_title", "{school} 학습" },
            { Str::kProgressTarget, "progress_target", "학습 목표: {spell} ({pct}%)" },
            { Str::kProgressNoneCandidates, "progress_none_candidates", "학습 목표 없음 · 배울 수 있는 주문 {n}개 · {key} 다음 주문" },
            { Str::kProgressNoneNothing, "progress_none_nothing", "배울 수 있는 주문 없음 (선행 조건 미충족)" },
            { Str::kProgressAllMastered, "progress_all_mastered", "모든 주문 숙달 완료" },
            { Str::kProgressNotReady, "progress_not_ready", "주문 데이터 불러오는 중..." },
            { Str::kProgressNotInTree, "progress_not_in_tree", "선택한 주문은 주문 트리에 없음" },
            { Str::kNodeHint, "node_hint", "트리에서 주문 클릭 · {key} 다음 주문" },
            { Str::kNodeSetTarget, "node_set_target", "학습 목표 지정 {key}" },
            { Str::kNodeIsTarget, "node_is_target", "현재 학습 목표" },
            { Str::kNodeLocked, "node_locked", "선행 조건 필요" },
            { Str::kNodeMastered, "node_mastered", "숙달 완료" },
            { Str::kLegendMastered, "legend_mastered", "숙달" },
            { Str::kLegendLearning, "legend_learning", "학습 중" },
            { Str::kLegendCanLearn, "legend_can_learn", "배울 수 있음" },
            { Str::kTreeNotInTree, "tree_not_in_tree", "주문 트리에 없는 주문" },
            { Str::kPreviewTitle, "preview_title", "미리보기" },
            { Str::kTreeTitle, "tree_title", "주문 트리" },
            // build 5
            { Str::kProgressCandidates, "progress_candidates", "배울 수 있는 주문 {n}개 · {key} 다음 주문" },
            { Str::kProgressCandidatesNoXY, "progress_candidates_no_xy", "배울 수 있는 주문 {n}개 · 트리 창에 표시 안 됨" },
            { Str::kProgressReady, "progress_ready", "경험치 충족된 주문 {n}개 · 습득 대기" },
            { Str::kProgressLearningEarly, "progress_learning_early", "미리 배운 주문 {n}개 · 숙달 진행 중" },
            { Str::kProgressUnknown, "progress_unknown", "상태를 알 수 없는 주문 {n}개" },
            { Str::kProgressNoOther, "progress_no_other", "배울 수 있는 다른 주문 없음" },
            { Str::kProgressNoTree, "progress_no_tree", "주문 트리 파일 없음" },
            { Str::kProgressNoSchoolTree, "progress_no_school_tree", "{school}: 주문 트리에 없는 학파" },
            { Str::kXp, "xp", "{cur} / {need} 경험치" },
            { Str::kTreeNoTree, "tree_no_tree", "주문 트리 파일 없음 (spell_tree.json)" },
            { Str::kTreeNoCentre, "tree_no_centre", "선택한 학파 주문 없음" },
            { Str::kTreeNoLinks, "tree_no_links", "연결된 주문 없음" },
            // spec v10 T7: an off-window badge (a drawn arrow, then this text)
            { Str::kTreeExit, "tree_exit", "{n}개 더" },
            { Str::kNodeCanLearn, "node_can_learn", "배울 수 있음" },
            { Str::kNodeTargetPct, "node_target_pct", "현재 학습 목표 · {pct}%" },
            { Str::kNodeLearningPct, "node_learning_pct", "미리 배움 · 숙달 {pct}%" },
            { Str::kNodeReady, "node_ready", "경험치 충족 · 습득 대기" },
            { Str::kNodeUnknown, "node_unknown", "상태 확인 중" },
            { Str::kNodeSub, "node_sub", "{school} · {level}" },
            { Str::kReqAll, "req_all", "선행 조건 모두 숙달 필요" },
            { Str::kReqSome, "req_some", "선행 조건 중 {n}개 숙달 필요" },
            { Str::kReqNone, "req_none", "선행 조건 없음" },
            { Str::kReqMastered, "req_mastered", "{name} · 숙달" },
            { Str::kReqNotMastered, "req_not_mastered", "{name} · 미숙달" },
            { Str::kReqChecking, "req_checking", "{name} · 확인 중" },
            { Str::kReqNotInGame, "req_not_in_game", "{name} · 게임에 없음" },
            { Str::kReqMore, "req_more", "외 {n}개" },
            { Str::kSchoolAlteration, "school_alteration", "변화마법" },
            { Str::kSchoolConjuration, "school_conjuration", "소환마법" },
            { Str::kSchoolDestruction, "school_destruction", "파괴마법" },
            { Str::kSchoolIllusion, "school_illusion", "환영마법" },
            { Str::kSchoolRestoration, "school_restoration", "회복마법" },
            // review round 1 #3
            { Str::kProgressNoHom, "progress_no_hom", "Heart of Magic 연결 안 됨" },
            { Str::kProgressNoData, "progress_no_data", "주문 데이터를 받지 못함" },
            // build 6 (spec v6 L4); since build 11 the tree legend's locked item (spec v11 T18)
            { Str::kTreeLocked, "tree_locked", "잠김" },
            // build 7 (spec v7 C1): our info card. DRAFT wording by Claude in the short label style, for Argon's review
            { Str::kCardMagicka, "card_magicka", "매지카 {n}" },
            { Str::kCardMagickaPerSec, "card_magicka_per_sec", "매지카 {n}/초" },
            { Str::kCardCastFireForget, "card_cast_fire_forget", "단발" },
            { Str::kCardCastConcentration, "card_cast_concentration", "집중" },
            { Str::kCardCastConstant, "card_cast_constant", "지속 효과" },
            { Str::kCardCastScroll, "card_cast_scroll", "마법 주문서" },
            { Str::kCardShout, "card_shout", "함성" },
            { Str::kCardShoutRecharge, "card_shout_recharge", "충전 {t}" },
            { Str::kCardPower, "card_power", "특수능력 · 하루 1회" },
            { Str::kCardLesserPower, "card_lesser_power", "하급 특수능력" },
            { Str::kCardAbility, "card_ability", "능력" },
            { Str::kCardPowerAny, "card_power_any", "특수능력" },
            { Str::kCardActiveEffect, "card_active_effect", "적용중인 효과" },
            { Str::kCardTimeLeft, "card_time_left", "남은 시간 {t}" },
            { Str::kCardHours, "card_hours", "{n}시간" },
            { Str::kCardMinutes, "card_minutes", "{n}분" },
            { Str::kCardSeconds, "card_seconds", "{n}초" },
            // blocks plan Task 4 (D5): the lock button by the mode's state (off: "change the layout", on: "lock the layout"), the
            // reset button; the labels the plan gives, {key} the toggle key as the hints show keys
            { Str::kLayoutEdit, "layout_edit", "배치 바꾸기 {key}" },
            { Str::kLayoutLock, "layout_lock", "배치 잠금 {key}" },
            { Str::kLayoutReset, "layout_reset", "처음 배치로" },
        });

        consteval bool StringsInOrder()
        {
            for (std::size_t i = 0; i < kStrings.size(); ++i) {
                if (static_cast<std::size_t>(kStrings[i].id) != i) return false;
            }
            return kStrings.size() == static_cast<std::size_t>(Str::kCount);
        }
        static_assert(StringsInOrder(), "kStrings must list every Str once, in enum order");

        const char* Text(Str a_id)
        {
            return kStrings[static_cast<std::size_t>(a_id)].text;
        }

        const char* Key(Str a_id)
        {
            return kStrings[static_cast<std::size_t>(a_id)].key;
        }

        // The table's text with each {placeholder} replaced (every occurrence)
        std::string Fill(Str a_id, std::initializer_list<std::pair<std::string_view, std::string>> a_values)
        {
            std::string text = Text(a_id);
            for (const auto& [name, value] : a_values) {
                for (auto at = text.find(name); at != std::string::npos; at = text.find(name, at + value.size())) {
                    text.replace(at, name.size(), value);
                }
            }
            return text;
        }

        // A tree school key ("Destruction") as the list's school column names it
        std::string SchoolText(const std::string& a_school)
        {
            constexpr std::array<std::pair<std::string_view, Str>, 5> kSchools{ { { "Alteration", Str::kSchoolAlteration },
                { "Conjuration", Str::kSchoolConjuration }, { "Destruction", Str::kSchoolDestruction }, { "Illusion", Str::kSchoolIllusion },
                { "Restoration", Str::kSchoolRestoration } } };
            for (const auto& [name, id] : kSchools) {
                if (name == a_school) return Text(id);
            }
            return a_school;
        }

        // A string game setting, empty when there is none
        std::string GameString(const char* a_setting)
        {
            auto* settings = RE::GameSettingCollection::GetSingleton();
            auto* setting = settings ? settings->GetSetting(a_setting) : nullptr;
            const char* text = setting && setting->GetType() == RE::Setting::Type::kString ? setting->GetString() : nullptr;
            return text ? text : "";
        }

        // The tree's level word ("Novice" .. "Master", spell_tree.json) as the game's own level names (the vanilla GMSTs LevelText
        // reads; the Adept tier is sJourneymanDisplayName); the saved word when it is another one or the game has no string
        std::string LevelName(const std::string& a_level)
        {
            constexpr std::array<std::pair<std::string_view, const char*>, 5> kLevels{ { { "Novice", "sNoviceDisplayName" },
                { "Apprentice", "sApprenticeDisplayName" }, { "Adept", "sJourneymanDisplayName" }, { "Expert", "sExpertDisplayName" },
                { "Master", "sMasterDisplayName" } } };
            for (const auto& [name, setting] : kLevels) {
                if (name != a_level) continue;
                auto text = GameString(setting);
                return text.empty() ? a_level : text;
            }
            return a_level;
        }

        // ---------------------------------------------------------------------
        // Menu state
        // ---------------------------------------------------------------------

        // Kept across menu opens (spec: "keep the previous centre")
        struct Session
        {
            int centre = -1;
            std::string school;  // the list's active school
            // The centre was a fallback (DefaultCentre) picked from a snapshot that did not cover this school yet: the
            // school's root stood in for its learning target. Picked again once a snapshot covers the school; a centre
            // the user chose (list selection, tree click, the next-learnable key) is never provisional.
            std::string provisionalSchool;
        } g_session;

        struct CardFit
        {
            double x{}, y{}, scale{};  // the fade holder's _x, _y (in its parent), and _xscale/_yscale in percent
            std::array<double, 4> local{};  // the card's visible parts' union in the holder (xMin, xMax, yMin, yMax) it was fitted for
        };

        enum class Tab
        {
            kFollow,   // All, Favorites (and anything unknown): follow the selected spell's school
            kSchool,   // one school's tab
            kPowers,   // Powers, Shouts: no tree, no progress
            kEffects,  // Active Effects: none of our panels
        };

        // The active school's state counts over its non-dup nodes (Q2); available = Spells::Learnable, candidates = those of
        // them with a place in the tree
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
            bool notInTree = false;          // the list's spell has no tree node (the tree keeps its centre)
            int selected = -1;               // the node clicked in the tree (node card)
            RE::FormID selectedByUs = 0;     // a tree click selected this spell in the list: its change is ours
            int clicked = -1;                // a node click waiting for the frame handler
            bool dirty = true;
            bool refresh = true;             // ask Spells for HoM's state at the end of this frame
            int frame = 0;
            std::uint64_t epoch = 0;         // the snapshot drawn last
            std::vector<TreeData::PlacedNode> shown;  // clickable nodes drawn in the tree window
            std::vector<TreeData::PlacedNode> drawn;  // every node drawn there, faint ones too (the tooltip finds its node here)
            int hovered = -1;                // the node clip under the mouse (onRollOver / onRollOut record it; spec v10 T6)
            int tooltipFor = -1;             // the node the tooltip shows (-1: none)
            bool tooltipDue = false;         // the tree was drawn again: the tooltip is drawn again too (on top of it)
            bool spacingLogged = false;      // "SelfTest: T2 spacing PASS" logged this menu (FAIL lines always)
            std::optional<std::string> legendSchool;  // the school whose shape and colour the legend shows (T18; none drawn yet)
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
            std::set<std::string> availabilityLogged;
            SchoolInfo schoolInfo;
            double rowSize = 0.0;            // the list's row text size in stage units, 0 = not measured yet (Q4)
            int listMeasureLeft = kListMeasureFrames;
            std::set<std::string> q2Logged;  // Q2 states logged this menu
            bool selectedByKey = false;      // the node card shows a candidate the next-learnable key picked
            double lineFirst = kLineFactor;  // the game font's first line height per size unit (MeasureTextMetrics)
            double linePitch = kLineFactor;  // and its step per further line
            double ascent = kAscentFallback; // its ascent per size unit (MeasureTextMetrics; L2 row-2 baseline)
            bool ascentMeasured = false;     // ascent came from getLineMetrics, not kAscentFallback
            bool rowAligned = false;         // the progress card's first line was moved onto the node card name's baseline (L2)
            std::string rowAlignNote;        // why not, for the L2 self-test
            std::set<std::string> l2Logged;  // "SelfTest: L2 ..." readings logged this menu (up to kMaxL2Logs)
            bool buttonChecked = false;      // the node card button's bottom was read back (L2, once per menu)
            // our own info card (v7 C1)
            std::string ownCardSig;          // what it shows (tab, type, texts, sizes); "" = nothing shown
            bool ownCardShown = false;
            double ownCardBottom = 0.0;      // its panel's bottom in stage units as last drawn
            // the block layout (blocks plan Task 2): this tab's arranged rects, the same with nothing hidden (where a hidden
            // block sits), each panel's rect as last placed (a width change redraws it, D11) and SkyUI's card slot as last fitted
            // UNSCROLLED rects (Arrange's; BlockRect adds the scroll). placed: each panel's rect as last placed (BlockRect's)
            std::map<Layout::BlockId, Settings::Rect> blocks, blocksAll, placed;
            // blocks plan Task 6: add-on blocks to draw next frame (DrawAddons), drawn this menu (SelfTest L6), whose callback threw
            std::set<Layout::BlockId> addonDrawDue, addonDrawn, addonFailed;
            // blocks plan Task 3 (D4): each column's scroll offset (stage units, 0 = its top block at the column's top), applied in
            // one place only: the column clip's _y (PlaceColumns). Clamped by Layout::ClampScroll at every placing
            std::array<double, Layout::kColumns> scroll{};
            std::array<std::optional<Settings::Rect>, Layout::kColumns> masked;  // each column's area as its mask was last drawn
            int wheelScrolls = 0, wheelZoomsKept = 0;  // wheel notches that scrolled a column / zooms of SkyUI's kept from firing
            bool wheelInstallDone = false;
            int wheelInstallLeft = kSelectionInstallFrames;  // frames InstallWheel waits for Menu_mc's wheel handler
            int nodeClicks = 0, nodeClicksOutside = 0;  // node clicks this menu, and those outside the tree's visible column area
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
            std::set<std::string> cutLogged; // "Q4 text ... cut to" lines logged this menu (field and text)
            std::string uncoveredSchool;     // the school no snapshot has had since frame uncoveredFrom
            int uncoveredFrom = 0;
            bool noData = false;             // kNoDataFrames passed without HoM's state for uncoveredSchool
            LayoutEdit layout;               // blocks plan Task 4
        } g_menu;

        // Heart of Magic's tree look and settings (spec v11 T): read at menu open (AttachWidget, before the frame handler is
        // set). The handler runs on several threads, so the style is handed over as Spells::Current() hands over a snapshot:
        // a lock around a shared pointer, the style itself never changed once published
        std::mutex g_styleLock;
        std::shared_ptr<const HomStyle::Style> g_style = std::make_shared<const HomStyle::Style>();

        std::shared_ptr<const HomStyle::Style> CurrentStyle()
        {
            const std::scoped_lock lock(g_styleLock);
            return g_style;
        }

        std::atomic<bool> g_pendingNext{ false }, g_pendingSetTarget{ false }, g_pendingButton{ false };
        std::atomic<bool> g_closing{ false };  // the close event came: the frame handler does nothing until the next open
        // The over-pass (MagicMenu PostDisplay): the 3D preview drawn again after the menu movie, over our black box
        std::atomic<RE::GFxMovieView*> g_attachedMovie{ nullptr };  // the movie our widget is on
        std::atomic<bool> g_overPass{ false };       // this menu: widget attached, IR not active, bDrawPreviewOver=1
        std::atomic<bool> g_fitPass{ false };        // this menu: widget attached, IR not active, bMovePreview=1 (place/fit in PostDisplay)
        std::atomic<bool> g_previewShown{ true };    // the preview box is visible (not on Active Effects)
        // spec v13 B1: the Powers tab (flag 128) shows the box but no 3D (user 2026-10-26: like the Shouts tab); Preview treats it
        // as a tab without the box (every model hidden, no over-pass draw), the layout stays the Powers/Shouts one
        std::atomic<bool> g_hide3D{ false };
        // review v13 round 2 #1: with neither place/fit nor the over-pass, PostDisplay still calls Preview for the Powers hide
        // (this menu: widget attached, IR not active, not VR - where Preview touches nothing) and once more after it
        std::atomic<bool> g_powersHideCalls{ false };
        std::atomic<bool> g_powersHidePending{ false };
        std::atomic<bool> g_overPassLogged{ false };  // the first over-pass of this menu is logged
        bool g_postDisplayHooked = false;           // set once at kDataLoaded
        bool g_noApiLogged = false;

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

        std::string Upper(std::string a_text)
        {
            std::ranges::transform(a_text, a_text.begin(), [](char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; });
            return a_text;
        }

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

        std::optional<double> Number(const RE::GFxValue& a_object, const char* a_member)
        {
            RE::GFxValue value;
            if (!a_object.IsObject() || !a_object.GetMember(a_member, &value) || !value.IsNumber()) return std::nullopt;
            return value.GetNumber();
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

        // A reading size: at least the left list's row size (Q4)
        double Size(double a_base)
        {
            return std::max(a_base, g_menu.rowSize > 0 ? g_menu.rowSize : kListRowFallback);
        }

        std::string StringMember(const RE::GFxValue& a_object, const char* a_member);  // defined with the item card's helpers

        // The step from one text line to the next at a size: the game font's, measured at attach (MeasureTextMetrics)
        double LineH(double a_size)
        {
            return a_size * g_menu.linePitch;
        }

        // A field's height for a size and line count: the font's first line, a step per further line, and the gutter
        double FieldH(double a_size, int a_lines = 1)
        {
            return a_size * (g_menu.lineFirst + g_menu.linePitch * static_cast<double>(std::max(1, a_lines) - 1)) + kGutter;
        }

        // The same metrics for LayoutMath (its FieldH is this FieldH)
        LayoutMath::TextMetrics Metrics()
        {
            return { g_menu.lineFirst, g_menu.linePitch, kGutter };
        }

        std::size_t CodePoints(std::string_view a_text)
        {
            return static_cast<std::size_t>(std::ranges::count_if(a_text, [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
        }

        void SetText(RE::GFxMovieView* a_movie, RE::GFxValue& a_field, const std::string& a_text)
        {
            RE::GFxValue text;
            a_movie->CreateString(&text, a_text.c_str());
            a_field.SetMember("text", text);
        }

        double TextW(const RE::GFxValue& a_field)
        {
            return Number(a_field, "textWidth").value_or(0.0) + kGutter;
        }

        double TextH(const RE::GFxValue& a_field)
        {
            return Number(a_field, "textHeight").value_or(0.0) + kGutter;
        }

        // How many lines the field's text takes at a_size: its textHeight against the font's measured first line and step
        // (0 for no text)
        int TextLines(const RE::GFxValue& a_field, double a_size)
        {
            const double height = Number(a_field, "textHeight").value_or(0.0);
            if (height <= 0 || a_size <= 0) return 0;
            const double more = std::max(0.0, height - a_size * g_menu.lineFirst);
            return 1 + static_cast<int>(std::lround(more / (a_size * g_menu.linePitch)));
        }

        // No wider than a_w and no more than a_lines lines. Judged by the line count, not the height: cutting a one-line text
        // never lowers its height, so a font whose line is taller than our estimate cut every text to "..." (review round 1 #1)
        bool TextFits(const RE::GFxValue& a_field, double a_w, double a_size, int a_lines)
        {
            constexpr double kSlack = 0.5;
            return TextW(a_field) <= a_w + kSlack && TextLines(a_field, a_size) <= a_lines;
        }

        // A text that had to be cut, once per field and text per menu (up to kMaxCutLogs): what is shown now; WARN when only
        // "..." is left of the variable part
        void LogCut(const RE::GFxValue& a_field, double a_w, double a_size, int a_lines, bool a_dotsOnly)
        {
            auto where = StringMember(a_field, "_target");
            if (where.empty()) where = StringMember(a_field, "_name");
            const auto shown = StringMember(a_field, "text");
            if (g_menu.cutLogged.size() >= static_cast<std::size_t>(kMaxCutLogs) || !g_menu.cutLogged.insert(where + "|" + shown).second) return;
            if (a_dotsOnly) {
                logger::warn("Widget: Q4 text {} cut to '{}' - WARN only the ellipsis is left ({:.0f} wide, {} line(s) at size {:.1f}; "
                             "text {:.1f} x {:.1f})",
                    where, shown, a_w, a_lines, a_size, TextW(a_field), TextH(a_field));
            } else {
                logger::info("Widget: Q4 text {} cut to '{}' ({:.0f} wide, {} line(s) at size {:.1f})", where, shown, a_w, a_lines, a_size);
            }
        }

        // Puts a_compose(a_part) into the field when it fits (a_w wide, a_lines lines at a_size); else a_part cut to the most
        // code points that fit, with "..." (Draw::Cut), found by bisection on the text the field really lays out
        // (textWidth/textHeight read back), and logged (LogCut). False when even "..." alone does not fit (that text is kept).
        bool FitText(RE::GFxMovieView* a_movie, RE::GFxValue& a_field, double a_w, double a_size, int a_lines, const std::string& a_part,
            const std::function<std::string(const std::string&)>& a_compose)
        {
            SetText(a_movie, a_field, a_compose(a_part));
            if (TextFits(a_field, a_w, a_size, a_lines)) return true;
            std::size_t lo = 0, hi = CodePoints(a_part);  // fits(lo) unknown, fits(hi) false
            bool any = false;
            while (lo < hi) {
                const std::size_t mid = (lo + hi) / 2;
                SetText(a_movie, a_field, a_compose(Draw::Cut(a_part, mid)));
                if (TextFits(a_field, a_w, a_size, a_lines)) {
                    any = true;
                    lo = mid + 1;
                } else {
                    hi = mid;
                }
            }
            // lo is the first count that does not fit: lo - 1 is the longest that does (0 when none does)
            const std::size_t best = lo > 0 ? lo - 1 : 0;
            SetText(a_movie, a_field, a_compose(Draw::Cut(a_part, best)));
            const bool fits = any || TextFits(a_field, a_w, a_size, a_lines);
            LogCut(a_field, a_w, a_size, a_lines, best == 0);
            return fits;
        }

        bool FitWhole(RE::GFxMovieView* a_movie, RE::GFxValue& a_field, double a_w, double a_size, int a_lines, const std::string& a_text)
        {
            return FitText(a_movie, a_field, a_w, a_size, a_lines, a_text, [](const std::string& a_part) { return a_part; });
        }

        // A one-line text field whose text ends in "..." when it is wider than a_w
        RE::GFxValue FitLine(RE::GFxMovieView* a_movie, RE::GFxValue& a_parent, const char* a_name, double a_x, double a_y, double a_w,
            double a_size, const std::string& a_text, std::uint32_t a_rgb, const char* a_align = "left")
        {
            auto field = Draw::Text(a_movie, a_parent, a_name, a_x, a_y, a_w, FieldH(a_size), "", a_rgb, a_size, a_align);
            if (field.IsDisplayObject()) FitWhole(a_movie, field, a_w, a_size, 1, a_text);
            return field;
        }

        // A text field wrapping to at most a_lines lines, the last ending in "..." when the text needs more
        RE::GFxValue FitLines(RE::GFxMovieView* a_movie, RE::GFxValue& a_parent, const char* a_name, double a_x, double a_y, double a_w,
            double a_size, int a_lines, const std::string& a_text, std::uint32_t a_rgb, const char* a_align = "left")
        {
            auto field = Draw::Text(a_movie, a_parent, a_name, a_x, a_y, a_w, FieldH(a_size, a_lines), "", a_rgb, a_size, a_align);
            if (!field.IsDisplayObject()) return field;
            field.SetMember("wordWrap", true);
            FitWhole(a_movie, field, a_w, a_size, a_lines, a_text);
            return field;
        }

        // The largest size up to a_size (down to kMinFitSize) whose a_lines-line field fits a_room; logged once per menu when
        // even that runs over (review round 1 #4: at a list row size of 20 the progress card's last line ended at 104.75 of 102)
        double SizeFor(double a_size, int a_lines, double a_room, const char* a_what)
        {
            const double size = LayoutMath::SizeFor(Metrics(), a_size, a_lines, a_room, kMinFitSize, kFitStep);
            if (a_what && FieldH(size, a_lines) > a_room && g_menu.cutLogged.insert(std::string("room|") + a_what).second) {
                logger::info("Widget: Q4 {} needs {:.1f} of {:.1f} stage units even at size {:.1f} - it runs over", a_what, FieldH(size, a_lines),
                    a_room, size);
            }
            return size;
        }

        // "SelfTest: Q2 state <name>" the first time a state is shown in this menu, with the text the field shows (read back;
        // the intended text after it when the two differ)
        void Q2State(std::string_view a_name, const std::string& a_intended, const RE::GFxValue* a_field = nullptr)
        {
            if (!g_menu.q2Logged.emplace(a_name).second) return;
            const auto shown = !a_field ? a_intended : a_field->IsDisplayObject() ? StringMember(*a_field, "text") : std::string("(no field)");
            SelfTest(shown == a_intended ? std::format("Q2 state {} - \"{}\"", a_name, shown) :
                                           std::format("Q2 state {} - \"{}\" (intended \"{}\")", a_name, shown, a_intended));
        }

        void Q2State(Str a_id, const std::string& a_intended, const RE::GFxValue* a_field = nullptr)
        {
            Q2State(Key(a_id), a_intended, a_field);
        }

        const Spells::Data& Data()
        {
            return *Spells::Get();  // callers check Spells::Get() first
        }

        std::string CentreSchool()
        {
            return Spells::Get() && g_session.centre >= 0 ? Data().tree.nodes[g_session.centre].school : std::string{};
        }

        // A node can be a window's centre only with a place in the tree (TreeData::Query shows nothing around a node
        // without x/y): one without counts as "not in the tree" for the centre rule
        bool Centrable(int a_index)
        {
            return Spells::Get() && a_index >= 0 && Data().tree.nodes[a_index].hasXY;
        }

        int DefaultCentre(const std::string& a_school, const Spells::Snapshot& a_snapshot)
        {
            if (a_school.empty()) return -1;
            const int target = Spells::FindNode(a_snapshot.Target(a_school), a_school);
            if (Centrable(target)) return target;
            const auto* school = Spells::SchoolNamed(a_school);
            return school && Centrable(school->rootIndex) ? school->rootIndex : -1;
        }

        // The active school's numbers for this snapshot (worked out again when either changes). The candidates are what the
        // next-learnable key walks and what the cards count (Q2: one set): learnable, non-dup, with a place in the tree.
        const SchoolInfo& SchoolInfoOf(const Spells::Snapshot& a_snapshot)
        {
            auto& info = g_menu.schoolInfo;
            if (info.valid && info.school == g_session.school && info.epoch == a_snapshot.epoch) return info;
            info = SchoolInfo{ g_session.school, a_snapshot.epoch, {}, {}, true };
            const auto learnable = Spells::Learnable(a_snapshot, g_session.school);
            std::ranges::copy_if(learnable, std::back_inserter(info.candidates), Centrable);
            auto& t = info.tally;
            t.available = static_cast<int>(learnable.size());
            t.candidates = static_cast<int>(info.candidates.size());
            const auto* school = Spells::SchoolNamed(g_session.school);
            if (!school) return info;
            for (const int index : school->nodes) {
                if (Spells::IsDup(Data().tree.nodes[index])) continue;
                ++t.nodes;
                switch (Spells::LookOf(a_snapshot, index).state) {
                case State::kTarget: ++t.target; break;
                case State::kReady: ++t.ready; break;
                case State::kLearning: ++t.learning; break;
                case State::kMastered: ++t.mastered; break;
                case State::kLocked: ++t.locked; break;
                case State::kAvailable: break;  // counted by Spells::Learnable above
                default: ++t.unknown; break;
                }
            }
            return info;
        }

        // A learned spell (mastered, known, ready, the target) or one that can be learned: what the tree shows
        bool Shown(State a_state)
        {
            return a_state == State::kMastered || a_state == State::kReady || a_state == State::kTarget || a_state == State::kLearning ||
                   a_state == State::kAvailable;
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
                logger::warn("Widget: selection wrap NOT installed - no {}.{} after {} frames; the windows follow the list highlight (build 11, "
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
                                 "the list highlight (build 11, spec v12 G3)",
                        pressOn ? "on" : "failed", auxOn ? "on" : "failed", auxOn ? "failed" : "not tried");
                    return;
                }
                g_menu.selWrapped = true;
                logger::info("Widget: selection wrap installed - {}.{} wrapped ({}), itemList itemPress/itemPressAux observed (spec v12 G1/G4)",
                    kListsPath, kSelectionChangeName, skyuis ? "SkyUI's own" : "not the prototype's: another handler, wrapped as it is");
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
        ListPick SelectInList(RE::FormID a_id, SelectionRule::Source a_source = SelectionRule::Source::kTree)
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
        // Item card: moved under the progress card by its fade holder's _x/_y/_xscale/_yscale only
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

        std::optional<Bounds> BoundsIn(RE::GFxValue& a_clip, RE::GFxValue& a_space)
        {
            RE::GFxValue bounds;
            const std::array<RE::GFxValue, 1> args{ a_space };
            if (!a_clip.Invoke("getBounds", &bounds, args) || !bounds.IsObject()) return std::nullopt;
            const auto xMin = Number(bounds, "xMin"), xMax = Number(bounds, "xMax"), yMin = Number(bounds, "yMin"),
                       yMax = Number(bounds, "yMax");
            if (!xMin || !xMax || !yMin || !yMax || *xMax <= *xMin || *yMax <= *yMin) return std::nullopt;
            return Bounds{ *xMin, *xMax, *yMin, *yMax };
        }

        std::optional<std::pair<RE::GFxValue, RE::GFxValue>> CardAndHolder(RE::GFxMovieView* a_movie)
        {
            auto card = FindCard(a_movie);
            RE::GFxValue holder;
            if (!card || !card->GetMember("_parent", &holder) || !holder.IsDisplayObject()) return std::nullopt;
            return std::pair{ *card, holder };
        }

        // v7 C1: our own info card replaces SkyUI's (bOwnCard=1), not while Inventory Reforged runs its panel here
        bool OwnCard()
        {
            return Settings::Get().ownCard && !g_menu.irActive && !g_menu.ownCardFallback;
        }

        // ---------------------------------------------------------------------
        // Blocks (blocks plan Task 2): every panel's rect comes from the saved layout (Layout::Arrange). The ini rects are
        // the blocks' default sizes and places; at the default layout every rect is build 13's (SelfTest L1)
        // ---------------------------------------------------------------------

        using Layout::BlockId;
        // in build 13's panel creation order (the spell card's panel last, on top)
        constexpr std::array<const char*, 5> kBlocks{ Layout::kPreview, Layout::kTree, Layout::kProgress, Layout::kNodeCard, Layout::kSpellCard };
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
        // one without a saved place goes to the end of the right column)
        void CloseRegistry()
        {
            std::vector<BlockRegistry::Block> blocks;
            {
                const std::scoped_lock lock(g_registryLock);
                if (!g_registry.Close()) return;
                blocks = g_registry.Blocks();
            }
            g_addons = std::move(blocks);
            std::vector<BlockId> ids;
            std::string names;
            for (std::size_t i = 0; i < g_addons.size(); ++i) {
                ids.push_back(g_addons[i].id);
                g_addonClips.push_back(std::format("mmxAddon{}", i));
                g_allBlocks.push_back(g_addons[i].id);
                names += std::format("{}{} ({:.0f} high, from {})", i ? ", " : "", g_addons[i].id, g_addons[i].height, g_addons[i].sender);
            }
            logger::info("Widget: add-on block registration closed at the first magic menu open - {} block(s){}{}", g_addons.size(),
                g_addons.empty() ? "" : ": ", names);
            Settings::AddBlocks(ids);
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
                Draw::Rect(a_clip, 0, y, a_width * share, barH, kBlue, kBarAlpha);
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

        // A block's ini rect (its default size and place)
        Settings::Rect IniRect(const Settings::Values& a_values, const BlockId& a_id)
        {
            if (a_id == Layout::kPreview) return a_values.preview;
            if (a_id == Layout::kTree) return a_values.tree;
            if (a_id == Layout::kProgress) return a_values.progress;
            if (a_id == Layout::kNodeCard) return a_values.nodeCard;
            return a_values.itemCard;
        }

        // The blocks a tab hides (D6, build 13's tab rules): Powers/Shouts the tree, the progress card and the node card; Active
        // Effects the preview too. Hidden blocks close up (Arrange). The progress card's hide on a spell tab with no school is
        // not one of these: it is only made invisible (ApplyVisibility) and keeps its slot, as in build 13 (the card under it
        // stays where it is)
        std::set<BlockId> TabHidden(Tab a_tab)
        {
            switch (a_tab) {
            case Tab::kPowers: return { Layout::kTree, Layout::kProgress, Layout::kNodeCard };
            case Tab::kEffects: return { Layout::kPreview, Layout::kTree, Layout::kProgress, Layout::kNodeCard };
            default: return {};
            }
        }

        struct Arrangement
        {
            std::set<BlockId> hidden, fill;
        };

        // What Arrange is given on a tab (D6). Our card (a_own) fills the rest of its column: Powers/Shouts 317 and Active
        // Effects 556 high at the E2 rects (build 13's OwnCardRect). SkyUI's fitted card (bOwnCard=0) keeps its height, and on
        // Active Effects the spell tabs' slot (build 13's CardRect moved it on Powers/Shouts only; nothing of ours shows there)
        Arrangement ArrangementFor(Tab a_tab, bool a_own)
        {
            if (!a_own && a_tab == Tab::kEffects) return {};
            return { TabHidden(a_tab), a_own ? std::set<BlockId>{ Layout::kSpellCard } : std::set<BlockId>{} };
        }

        // The blocks' heights: the ini rects', the node card [Debug] fDebugExtraHeight taller (a scroll test, blocks plan Task 3),
        // the add-ons' registered ones (Task 6). A height that is not finite or negative leaves its add-on out (absent: Arrange
        // would skip it too) - the registry refuses such heights, so this is the last guard; logged once per block
        std::map<BlockId, double> BlockHeights()
        {
            const auto& settings = Settings::Get();
            auto heights = Layout::DefaultHeights(settings);
            heights[Layout::kNodeCard] += settings.debugExtraHeight;
            for (const auto& addon : g_addons) {
                if (std::isfinite(addon.height) && addon.height >= 0) {
                    heights[addon.id] = addon.height;
                    continue;
                }
                static std::set<BlockId> s_logged;
                if (s_logged.insert(addon.id).second) {
                    logger::warn("Widget: add-on block {} has the height {} - left out of the layout (logged once)", addon.id, addon.height);
                }
            }
            return heights;
        }

        // blocks spec "Calls made by Claude" (safety net only): Inventory Reforged's magic-menu panel on in its MCM, or our fallback
        // switches off (bOwnCard=0, bMovePreview=0, bDrawPreviewOver=0): the layout mode rests (lock greyed) and the default
        // layout is used; the saved layout is kept. Empty when none applies
        std::string SafetyNetReason()
        {
            const auto& s = Settings::Get();
            std::string why;
            const auto add = [&](const char* a_part) { why += (why.empty() ? "" : ", ") + std::string(a_part); };
            if (g_menu.irActive) add("Inventory Reforged runs its panel in this menu");
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

        // UNSCROLLED stage rects (Arrange's, plan D4): the scroll is the column clip's (PlaceColumns), BlockRect adds it
        std::map<BlockId, Settings::Rect> ArrangeBlocks(const Arrangement& a_arrangement)
        {
            const auto& settings = Settings::Get();
            return Layout::Arrange(CurrentLayout(), BlockHeights(), a_arrangement.hidden, a_arrangement.fill, Layout::DefaultFrames(settings));
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
            if (Addon(a_id)) return {};  // an add-on with no arranged place (no usable height): nothing to draw in
            return IniRect(Settings::Get(), a_id);  // before the menu's first Rearrange (Repair keeps every block in the layout)
        }

        // The block is laid out on this tab (D6); ApplyVisibility adds "something chosen" and the progress card's school
        bool BlockShown(const BlockId& a_id)
        {
            return !TabHidden(g_menu.tab).contains(a_id) && g_menu.blocks.contains(a_id);
        }

        // SkyUI's item card slot (bOwnCard=0): the spell card block without growing - under the progress card at the default
        // layout; on Powers/Shouts (no progress card) right under the preview, the same height (user 2026-10-25). Task 2 review:
        // the arranged block (Rearrange arranges with own = OwnCard(), again at each tab change and at the card fallback), so
        // SkyUI's card is fitted to it only while it is the card; on screen (scrolled), as the fit maps it from _root
        Settings::Rect CardRect()
        {
            return BlockRect(Layout::kSpellCard);
        }

        // Our card's box per tab (C2): the spell card block, grown to the rest of its column where the tab hides blocks (on
        // Active Effects the panel is then cut to its content). The same block as CardRect, arranged with our card
        Settings::Rect OwnCardRect()
        {
            return BlockRect(Layout::kSpellCard);
        }

        std::string StringMember(const RE::GFxValue& a_object, const char* a_member)
        {
            RE::GFxValue value;
            if (!a_object.IsObject() || !a_object.GetMember(a_member, &value) || !value.IsString()) return {};
            const char* text = value.GetString();
            return text ? text : "";
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
                    g_menu.tab == Tab::kPowers ? "under the preview" : "under the progress card", w, h, seen->parts, rect.left, rect.top,
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
                    name, text->size, text->yscale, text->toRoot, g_menu.cardFit->scale, text->stage, Size(0.0),
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
            if (!Settings::Get().moveItemCard || g_menu.irActive || OwnCard()) return;  // our card replaces SkyUI's (v7 C1)
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

        // ---------------------------------------------------------------------
        // Clicks (they only record; the frame handler acts, so no clip is removed inside its own handler)
        // ---------------------------------------------------------------------

        class NodeClick final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params& a_params) override
            {
                if (!a_params.thisPtr) return;
                const auto index = Number(*a_params.thisPtr, kIndexMember);
                if (!index || g_menu.layout.active) return;  // Task 4: no node clicks in the layout mode (the covers take them first)
                ++g_menu.nodeClicks;
                // blocks plan Task 3: whether setMask keeps clicks off a masked-off part is unverified in Skyrim's Scaleform, so a
                // click outside the tree column's viewport is ignored and counted (SelfTest L2 mask clicks at close: want 0)
                RE::GFxValue x, y;
                const auto column = ColumnOf(Layout::kTree);
                if (column && a_params.movie && a_params.movie->GetVariable(&x, "_root._xmouse") && x.IsNumber() &&
                    a_params.movie->GetVariable(&y, "_root._ymouse") && y.IsNumber() && !ColumnViewport(*column).Contains(x.GetNumber(), y.GetNumber())) {
                    ++g_menu.nodeClicksOutside;
                    return;
                }
                g_menu.clicked = static_cast<int>(*index);
            }
        };

        class ButtonClick final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params&) override { g_pendingButton = true; }
        };

        constexpr const char* kFocusChild = "focus";  // a node clip's hover outline (spec v11 R6)

        // A node clip's hover outline shown or hidden: the clip's own child, drawn with it, so a hover needs no redraw (R6)
        void ShowFocus(RE::GFxValue& a_clip, bool a_shown)
        {
            RE::GFxValue focus;
            if (a_clip.GetMember(kFocusChild, &focus) && focus.IsDisplayObject()) focus.SetMember("_visible", a_shown);
        }

        // The hover tooltip's node (spec v10 T6): recorded, drawn by the frame handler; the node's own outline is toggled here
        // (R6). A roll-out of another node (the events of two neighbours may come in either order) leaves the hovered one alone
        class NodeRollOver final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params& a_params) override
            {
                if (!a_params.thisPtr || g_menu.layout.active) return;  // Task 4: no tooltips in the layout mode
                const auto index = Number(*a_params.thisPtr, kIndexMember);
                if (!index) return;
                g_menu.hovered = static_cast<int>(*index);
                ShowFocus(*a_params.thisPtr, true);
            }
        };

        class NodeRollOut final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params& a_params) override
            {
                if (!a_params.thisPtr) return;
                const auto index = Number(*a_params.thisPtr, kIndexMember);
                if (!index) return;
                ShowFocus(*a_params.thisPtr, false);
                if (g_menu.hovered == static_cast<int>(*index)) g_menu.hovered = -1;
            }
        };

        RE::GPtr<NodeClick> g_nodeClick;
        RE::GPtr<NodeRollOver> g_nodeRollOver;
        RE::GPtr<NodeRollOut> g_nodeRollOut;
        RE::GPtr<ButtonClick> g_buttonClick;

        void SetHandler(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, const char* a_event, RE::GFxFunctionHandler* a_handler)
        {
            RE::GFxValue fn;
            a_movie->CreateFunction(&fn, a_handler);
            a_clip.SetMember(a_event, fn);
        }

        // ---------------------------------------------------------------------
        // Shapes shared by the tree, its legend and the node card
        // ---------------------------------------------------------------------

        // Heart of Magic's spell marks (spec v11 T12, T13): renderNode's looks (canvasRendererSpell.js:69-288) and the mystery
        // look (renderMysteryNode, :18-67). Not ported: the XP progress ring (:168-199; the percent stays in the tooltip), the
        // hard-prerequisite shell and ring (:207-255, R5), halos and the hand-drawn ink.

        HomStyle::Behaviour BehaviourOf(State a_state)
        {
            switch (a_state) {
            case State::kLocked: return HomStyle::Behaviour::kLocked;
            case State::kAvailable: return HomStyle::Behaviour::kAvailable;
            case State::kReady: return HomStyle::Behaviour::kReady;
            case State::kTarget: return HomStyle::Behaviour::kTarget;
            case State::kLearning: return HomStyle::Behaviour::kLearning;
            case State::kMastered: return HomStyle::Behaviour::kMastered;
            default: return HomStyle::Behaviour::kUnknown;
            }
        }

        // HoM's display state of a tree node (R1): its behaviour state, the learning target of its school
        HomStyle::Display DisplayOfNode(int a_index, const Spells::Snapshot& a_snapshot)
        {
            const auto& node = Data().tree.nodes[a_index];
            const bool isTarget = a_index == Spells::FindNode(a_snapshot.Target(node.school), node.school);
            return HomStyle::DisplayOf(BehaviourOf(Spells::LookOf(a_snapshot, a_index).state), isTarget);
        }

        // Whether HoM names this spell to the player (review v11 #1, T16): Reveal on its display state, each node by its own;
        // else "???" wherever the name would show (tooltip, node card title, requirement rows) and no item card or 3D on a click
        bool NameShown(int a_index, const Spells::Snapshot& a_snapshot)
        {
            const auto style = CurrentStyle();
            return HomStyle::Reveal(DisplayOfNode(a_index, a_snapshot), Spells::LookOf(a_snapshot, a_index).percent, style->revealName,
                       style->cheatMode) == HomStyle::Name::kShown;
        }

        std::string ShownName(int a_index, const Spells::Snapshot& a_snapshot)
        {
            return NameShown(a_index, a_snapshot) ? Spells::NodeName(a_index) : std::string(HomStyle::kHiddenName);
        }

        // A HoM colour's own alpha times a_alpha, as the drawing API's percent: the one place the dim factor and the state's
        // alpha go in (R10: never also into the clip's _alpha)
        double Percent(const HomStyle::Color& a_color, double a_alpha)
        {
            return std::clamp(a_color.alpha * a_alpha, 0.0, 1.0) * Draw::kAlphaFull;
        }

        struct MarkLook
        {
            HomStyle::Display display{};
            bool mystery{};    // a locked spell drawn as undiscovered (HomStyle::Style::Mystery)
            bool focus{};      // the selected spell: FOCUS_GROW larger, focusStroke; its alpha its full/dim class's (review v11 #2)
            bool onPath{};     // a learned spell on the learning target's path: the hub ring for its outline (:103)
            double dim = 1.0;  // the off-path factor (T11; _contextFactor)
        };

        struct MarkPlace
        {
            std::string_view school, themeColor, level;
            int tier{};
            double worldX{}, worldY{};  // the node's saved place: HoM turns its shape toward the tree origin (NodeBatch.turnAt)
            double x{}, y{};            // the mark's centre in its clip
            double scale{};             // stage units per HoM world unit
            double lineScale = 1.0;     // line widths: HoM world units x this (the tree's fTreeScale; 1 in the legend and card)
        };

        // What a mark drew, for its node clip: the body's outline (the hover outline follows it), how far it reaches (hit area,
        // labels), and the mystery "?" with its colour
        struct MarkDrawn
        {
            std::vector<HomStyle::Point> body;
            double reach{};
            bool question{};
            HomStyle::Color questionColor;
            double questionAlpha{};
        };

        MarkDrawn DrawHomMark(RE::GFxValue& a_clip, const HomStyle::Style& a_style, const MarkPlace& a_place, const MarkLook& a_look)
        {
            using HomStyle::Display;
            const auto& t = a_style.tree;
            const auto school = a_style.SchoolColor(std::string(a_place.school), a_place.themeColor);
            // review v11 #7: the mystery outline and "?" take the inked school colour, never the node's themeColor
            // (renderMysteryNode, canvasRendererSpell.js:19)
            const auto mysteryColour = HomStyle::Dim(a_style.SchoolColor(std::string(a_place.school)), HomStyle::kMysteryDim);
            const auto size = HomStyle::SizeOf(a_look.display, a_look.mystery, a_place.level, a_place.tier, a_look.focus, t.availableRing);
            HomStyle::Color fill, stroke;
            double alpha = 1.0;
            bool dashed = false;
            switch (a_look.display) {
            case Display::kUnlocked:
                fill = t.unlockedFill.value_or(school);
                stroke = a_look.onPath ? a_style.hubRing : t.unlockedRim.value_or(school);
                break;
            case Display::kLearning:
                fill = a_style.learning;  // the learning colour, the hub ring round it (R4)
                stroke = a_style.hubRing;
                break;
            case Display::kAvailable:
                fill = t.nodeFill;
                stroke = school;
                alpha = t.availableAlpha;
                break;
            default:
                dashed = true;  // "state is encoded in the outline too: locked = dashed" (:265-266)
                if (a_look.mystery) {
                    fill = t.mysteryFill;
                    stroke = mysteryColour;
                    alpha = HomStyle::kMysteryAlpha;
                } else {
                    fill = t.nodeFill;
                    stroke = t.lockedStroke.value_or(school);
                    alpha = HomStyle::kLockedAlpha;
                }
                break;
            }
            alpha *= a_look.dim;
            if (a_look.focus) stroke = t.focusStroke;  // its alpha stays its class's (review v11 #2)
            const double s = a_place.scale, r = size.size * s, ls = a_place.lineScale;
            auto body = HomStyle::ShapePoints(a_place.school, a_place.worldX, a_place.worldY, a_place.x, a_place.y, r);
            if (size.ring) {
                // the learnable spell's own thin ring, a circle, see-through (no backdrop under it: NodeBatch "bare")
                Draw::LineStyle(a_clip, HomStyle::kRingWidth * ls, school.rgb, Percent(school, HomStyle::kRingAlpha * a_look.dim));
                Draw::Polygon(a_clip, HomStyle::ShapePoints({}, 0, 0, a_place.x, a_place.y, (size.size + HomStyle::kRingGap) * s));
            }
            Draw::NoLine(a_clip);
            // HoM's opaque _underlay under a see-through spell is left out: on our 70 % panel it would show as a black patch
            // darker than the panel (HoM's page is opaque, so there it matches); the links under a dimmed spell show through a little
            Draw::BeginFill(a_clip, fill.rgb, Percent(fill, alpha));
            Draw::Polygon(a_clip, body);
            Draw::EndFill(a_clip);
            // the outline after the fill, a dashed one in one phase round the whole shape (R10)
            Draw::LineStyle(a_clip, size.stroke * ls, stroke.rgb, Percent(stroke, alpha));
            if (dashed) {
                Draw::Segments(a_clip, HomStyle::DashOutline(body, HomStyle::kDashShare * r, HomStyle::kDashGapShare * r));
            } else {
                Draw::Polygon(a_clip, body);
            }
            const auto inner = [&](double a_share, const HomStyle::Color& a_colour) {
                Draw::NoLine(a_clip);
                Draw::BeginFill(a_clip, a_colour.rgb, Percent(a_colour, alpha));
                Draw::Polygon(a_clip, HomStyle::ShapePoints(a_place.school, a_place.worldX, a_place.worldY, a_place.x, a_place.y, r * a_share));
                Draw::EndFill(a_clip);
            };
            if (a_look.display == Display::kUnlocked) inner(HomStyle::kCoreShare, t.unlockedCore.value_or(HomStyle::InnerAccent(school)));
            if (a_look.display == Display::kLearning) inner(HomStyle::kLearningDotShare, { HomStyle::kLearningDot, 1.0 });
            const bool question = a_look.display == Display::kLocked && a_look.mystery;
            return { std::move(body), HomStyle::Reach(size, a_place.school, s, ls), question, mysteryColour, HomStyle::kMysteryMarkAlpha * a_look.dim };
        }

        // The undiscovered spell's "?" of a mark DrawHomMark drew at (a_cx, a_cy) in a_clip (NodeBatch.MARK_FONT 10 x a_scale,
        // upright), its colour's alpha as the field's _alpha; nothing when the mark has none. The tree and the legend (review v11 #8)
        void DrawQuestion(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, double a_cx, double a_cy, double a_scale, const MarkDrawn& a_drawn)
        {
            if (!a_drawn.question) return;
            const double size = HomStyle::kMarkFontSize * a_scale, h = FieldH(size);
            auto mark = Draw::Text(a_movie, a_clip, "mark", a_cx - h / 2, a_cy - h / 2, h, h, "?", a_drawn.questionColor.rgb, size, "center");
            if (mark.IsDisplayObject()) mark.SetMember("_alpha", Percent(a_drawn.questionColor, a_drawn.questionAlpha));
        }

        // ---------------------------------------------------------------------
        // Panels
        // ---------------------------------------------------------------------

        // The tree window's legend strip (spec v11 T18): HoM's four looks - unlocked, learning, available, locked (the mystery
        // look when HoM's discovery mode shows locked spells so) - in a_school's shape and colour, the known size kLegendIconR.
        // One line in the smaller kLegendSize (spec v10 T9, below the reading size: a key, not text to read), laid out by the
        // texts' measured widths.
        void DrawLegend(RE::GFxMovieView* a_movie, RE::GFxValue& a_panel, double a_w, double a_h, const std::string& a_school)
        {
            struct Item
            {
                HomStyle::Display display;
                Str text;
            };
            constexpr std::array<Item, 4> kItems{ { { HomStyle::Display::kUnlocked, Str::kLegendMastered },
                { HomStyle::Display::kLearning, Str::kLegendLearning }, { HomStyle::Display::kAvailable, Str::kLegendCanLearn },
                { HomStyle::Display::kLocked, Str::kTreeLocked } } };
            constexpr double kStep = 1.0, kIconScale = kLegendIconR / HomStyle::kKnownSize;
            constexpr double kTurnX = 1.0;  // a world place straight right of the origin: the school's shape at its base turn
            g_menu.legendSchool = a_school;
            const auto style = CurrentStyle();
            const double y = a_h - Settings::kTreeLegendH / 2 - kPad / 2;
            const double reach = HomStyle::MaxReach(kIconScale, style->tree.availableRing);
            const double room = a_w - 2 * kPad, icon = 2 * reach;
            for (double size = kLegendSize;; size -= kStep) {
                Draw::Remove(a_panel, "legend");
                auto legend = Draw::Clip(a_panel, "legend");
                if (!legend.IsDisplayObject()) return;
                const double h = FieldH(size);
                std::array<RE::GFxValue, kItems.size()> fields;
                double used = 0;
                for (std::size_t i = 0; i < kItems.size(); ++i) {
                    const auto name = std::format("t{}", i);
                    fields[i] = Draw::Text(a_movie, legend, name.c_str(), 0, y - h / 2, room, h, Text(kItems[i].text), kDimText, size);
                    used += icon + kLegendTextGap + TextW(fields[i]);
                }
                if (used > room && size - kStep >= kLegendSize) continue;
                if (used > room) {
                    logger::info("Widget: the tree legend needs {:.0f} of {:.0f} stage units at size {:.0f} - what runs past the border is cut",
                        used, room, size);
                }
                const double gap = std::max(0.0, (room - used) / static_cast<double>(kItems.size()));
                const double right = kPad + room;
                double x = kPad + gap / 2;
                for (std::size_t i = 0; i < kItems.size(); ++i) {
                    if (x + icon > right) {  // not even the mark fits: neither is drawn (review round 1 #6)
                        if (fields[i].IsDisplayObject()) fields[i].Invoke("removeTextField");
                        continue;
                    }
                    const auto drawn = DrawHomMark(legend, *style, { a_school, {}, {}, 0, kTurnX, 0.0, x + reach, y, kIconScale },
                        { kItems[i].display, style->Mystery(), false, false, 1.0 });
                    if (drawn.question) {
                        // the locked icon's "?" as on the tree's mystery marks (review v11 #8), in a clip of its own
                        const auto clipName = std::format("q{}", i);
                        auto mark = Draw::Clip(legend, clipName.c_str());
                        if (mark.IsDisplayObject()) DrawQuestion(a_movie, mark, x + reach, y, kIconScale, drawn);
                    }
                    x += icon + kLegendTextGap;
                    if (!fields[i].IsDisplayObject()) continue;
                    if (x + TextW(fields[i]) > right) {
                        // the text ends in "..." at the border, or goes when not even that fits
                        fields[i].Invoke("removeTextField");
                        if (right - x >= size) {
                            const auto name = std::format("t{}", i);
                            fields[i] = FitLine(a_movie, legend, name.c_str(), x, y - h / 2, right - x, size, Text(kItems[i].text), kDimText);
                        }
                        x = right;
                        continue;
                    }
                    fields[i].SetMember("_x", x);
                    x += TextW(fields[i]) + gap;
                }
                return;
            }
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
                g_menu.scroll[column] = Layout::ClampScroll(g_menu.scroll[column], content, visible);
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
            if (a_id == Layout::kTree) return Text(Str::kTreeTitle);
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
            if (Addon(a_id) && !g_menu.blocksAll.contains(a_id)) return;  // an add-on with no usable height: no panel
            auto parent = BlockParent(a_widget, a_id);
            if (!parent.IsDisplayObject()) return;
            auto panel = Draw::Clip(parent, PanelName(a_id));
            if (!panel.IsDisplayObject()) return;
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

        // Task 6: the add-on blocks due (after the panels are made, a size change, a move to the other column) drawn by their
        // own callbacks, each into a fresh content clip below its title strip and inside the padding, lent for the call only
        // (BlockApi.h "When draw is called"). Called from the frame handler. An exception from a callback is logged and that
        // block is not drawn again this menu (a crash inside it cannot be caught)
        void DrawAddons(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            if (g_menu.addonDrawDue.empty()) return;
            const auto due = std::exchange(g_menu.addonDrawDue, {});
            for (const auto& id : due) {
                const auto* addon = Addon(id);
                if (!addon || g_menu.addonFailed.contains(id) || !g_menu.blocksAll.contains(id)) continue;
                auto content = Content(a_widget, id);
                if (!content.IsDisplayObject()) {
                    logger::warn("Widget: add-on block {} - its panel's content clip could not be made, not drawn", id);
                    continue;
                }
                const auto rect = BlockRect(id);
                const double top = addon->title.empty() ? kPad : kTitleH + kPad;
                const double width = std::max(0.0, rect.Width() - 2 * kPad), height = std::max(0.0, rect.Height() - top - kPad);
                content.SetMember("_x", kPad);
                content.SetMember("_y", top);
                std::string error;
                try {
                    addon->draw(a_movie, content, static_cast<float>(width), static_cast<float>(height), addon->user);
                } catch (const std::exception& e) {
                    error = e.what();
                } catch (...) {  // another plugin's callback: whatever it throws must not leave the frame handler
                    error = "an exception that is not a std::exception";
                }
                if (!error.empty()) {
                    g_menu.addonFailed.insert(id);
                    logger::error("Widget: add-on block {} (from {}) threw from its draw callback: {} - not drawn again this menu", id, addon->sender, error);
                    SelfTest(std::format("L6 addon {} drawn FAIL - its draw callback threw: {}", id, error));
                    continue;
                }
                if (g_menu.addonDrawn.insert(id).second) {
                    SelfTest(std::format("L6 addon {} drawn - {:.0f} x {:.0f} content in column {} (block {})", id, width, height,
                        ColumnOf(id).value_or(-1), RectText(rect)));
                }
            }
        }

        // blocks plan Task 2 / D4, D11: this tab's rects worked out again (Arrange) and every panel put at its place - a move
        // changes _x/_y only. A block whose width changed since it was last placed is drawn again once: its box, the tree's
        // legend, and the next full redraw (dirty) for the tree, progress and node cards; our card through its signature (it
        // holds the rect, WatchOwnCard), SkyUI's card through a new fit when its slot moved. The first placing of a menu only
        // places (the panels are drawn new after it)
        // SkyUI's card is fitted to its slot: a moved slot (Powers/Shouts and back, a new width, a scroll) is fitted again
        void CheckCardSlot()
        {
            const auto slot = CardRect();
            if (g_menu.cardSlot && !SameRect(*g_menu.cardSlot, slot)) g_menu.cardFit.reset();
            g_menu.cardSlot = slot;
        }

        void PlaceLayoutChrome(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget);  // the layout mode's buttons, handle, covers (Task 4)

        void Rearrange(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            g_menu.blocksAll = ArrangeBlocks({});
            g_menu.blocks = ArrangeBlocks(ArrangementFor(g_menu.tab, OwnCard()));
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
                // Task 2 review #1: a new height redraws the content too (the tree's legend sits at its bottom, the cards fill it)
                if (resized) {
                    logger::info("Widget: block {} is {:.0f} x {:.0f} now (was {:.0f} x {:.0f}) - drawn again", id, rect.Width(), rect.Height(),
                        last->second.Width(), last->second.Height());
                    if (id == Layout::kTree && panel.IsDisplayObject()) {
                        DrawLegend(a_movie, panel, rect.Width(), rect.Height(), g_menu.legendSchool.value_or(std::string{}));
                    }
                    if (id == Layout::kTree || id == Layout::kProgress || id == Layout::kNodeCard) g_menu.dirty = true;
                    if (Addon(id)) g_menu.addonDrawDue.insert(id);
                }
                g_menu.placed[id] = rect;
            }
            CheckCardSlot();
            PlaceLayoutChrome(a_movie, a_widget);  // Task 4: the buttons and the border handle follow the border, the covers the blocks
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
        // removed at close: RemoveWheelListener) and the zoom wrap. Either missing is logged; the other still works
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

        // At the menu's close (the close event, before the movie goes): our Mouse listener taken off again. A movie that is gone
        // already took its listeners with it
        void RemoveWheelListener()
        {
            auto* movie = OpenMagicMenuMovie();
            if (!movie || movie != g_attachedMovie.load()) {
                logger::info("Widget: wheel listener - the menu's movie is gone already (its listeners with it)");
                return;
            }
            RE::GFxValue mouse, listener;
            const auto path = std::format("_root.{}.{}", kRootName, kWheelListener);
            if (!movie->GetVariable(&listener, path.c_str()) || !listener.IsObject()) return;  // never added
            const std::array<RE::GFxValue, 1> args{ listener };
            const bool removed = MouseObject(movie, &mouse) && mouse.Invoke("removeListener", nullptr, args.data(), args.size());
            logger::info("Widget: wheel listener {}", removed ? "removed (Mouse.removeListener)" : "NOT removed - Mouse.removeListener failed");
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

        // Which panels the current tab shows (design 3.7, spec v7 C2): the spell tabs everything (the progress card only with a
        // school); Powers/Shouts the preview and our card; Active Effects our card alone. Without our card (bOwnCard=0) Active
        // Effects shows nothing, as in build 6. Our card shows only while SkyUI's would (WatchOwnCard).
        // What shows now (ApplyVisibility's rules): each block, and the column clips (any)
        struct Showing
        {
            bool preview{}, tree{}, nodeCard{}, progress{}, card{}, any{};
        };

        Showing Visibility()
        {
            const bool own = OwnCard();
            // Task 4: in the layout mode the blocks show with nothing chosen too (they are what is moved)
            const bool chosen = g_menu.chosen || g_menu.layout.active;
            Showing shown;
            // D6: the tab's hidden blocks (TabHidden) are the ones Arrange closes up; the progress card also waits for a school
            shown.preview = chosen && BlockShown(Layout::kPreview);
            shown.tree = chosen && BlockShown(Layout::kTree);
            shown.nodeCard = chosen && BlockShown(Layout::kNodeCard);
            shown.progress = chosen && BlockShown(Layout::kProgress) && !g_session.school.empty();
            shown.any = chosen && (shown.preview || own);
            shown.card = own && g_menu.ownCardShown && chosen;
            return shown;
        }

        // The blocks the player sees now (Task 4 review: the layout mode's covers and the Task 5 focus model take only these - an
        // invisible block, such as our card before its first item or the progress card with no school, cannot be grabbed or focused)
        std::set<BlockId> VisibleBlocks()
        {
            const auto shown = Visibility();
            std::set<BlockId> ids;
            if (!shown.any) return ids;  // the column clips are hidden
            if (shown.preview) ids.insert(Layout::kPreview);
            if (shown.tree) ids.insert(Layout::kTree);
            if (shown.nodeCard) ids.insert(Layout::kNodeCard);
            if (shown.progress) ids.insert(Layout::kProgress);
            if (shown.card) ids.insert(Layout::kSpellCard);
            for (const auto& addon : g_addons) {  // Task 6: an add-on shows on every tab while the columns do (if it has a place)
                if (g_menu.blocks.contains(addon.id)) ids.insert(addon.id);
            }
            return ids;
        }

        void ApplyVisibility(RE::GFxValue& a_widget)
        {
            const bool own = OwnCard();
            const bool effects = g_menu.tab == Tab::kEffects;
            const auto [preview, tree, nodeCard, progress, card, any] = Visibility();
            g_previewShown = preview;  // the over-pass draws the preview only while its box is shown
            // Task 4 (D5): the column clips are hidden, not mmx_root - the layout buttons below the left column stay up
            for (const char* column : kColumnClips) {
                if (auto clip = Child(a_widget, column); clip.IsDisplayObject()) clip.SetMember("_visible", any);
            }
            SetVisible(a_widget, Layout::kPreview, preview);
            SetVisible(a_widget, Layout::kTree, tree);
            SetVisible(a_widget, Layout::kNodeCard, nodeCard);
            SetVisible(a_widget, Layout::kProgress, progress);
            SetVisible(a_widget, Layout::kSpellCard, card);
            // with our card the layout is logged once its first item is drawn on the tab (review v7: not at the tab change)
            if (g_menu.chosen && (!own || card) && g_menu.c2Logged.insert(static_cast<int>(g_menu.tab)).second) {
                const auto rect = OwnCardRect();
                SelfTest(std::format("C2 tab {} layout - preview {}, tree {}, progress {}, node card {}, our card {} at {:.0f},{:.0f}..{:.0f},{:.0f}{}",
                    TabName(g_menu.tab), preview ? "on" : "off", tree ? "on" : "off", progress ? "on" : "off",
                    nodeCard ? "on" : "off", !own ? "off (bOwnCard=0, Inventory Reforged or no card info)" : "shown", rect.left, rect.top,
                    rect.right, own ? g_menu.ownCardBottom : rect.bottom, effects ? " (cut to its content)" : ""));
            }
        }

        // SelfTest L1 reference (blocks plan Task 2 / D6): build 13's panel rects per tab - its CardRect, OwnCardRect and
        // ApplyVisibility (Widget.cpp at 683a5e99 :1448, :1465, :2677) and the panels at the ini rects (:5111) - kept only as
        // this test's formulas over the ini rects. nullopt: the block is not shown on that tab
        std::optional<Settings::Rect> Build13Rect(Tab a_tab, bool a_own, const BlockId& a_id)
        {
            const auto& s = Settings::Get();
            const bool powers = a_tab == Tab::kPowers, effects = a_tab == Tab::kEffects;
            if (a_id == Layout::kSpellCard) {
                const auto& card = s.itemCard;
                if (a_own && powers) return Settings::Rect{ card.left, s.preview.bottom + s.panelGap, card.right, card.bottom };
                if (a_own && effects) return Settings::Rect{ card.left, s.preview.top, card.right, card.bottom };
                if (!a_own && powers) {
                    const double top = s.preview.bottom + s.panelGap;
                    return Settings::Rect{ card.left, top, card.right, top + card.Height() };
                }
                return card;
            }
            if (a_id == Layout::kPreview) {
                if (effects) return std::nullopt;
                return s.preview;
            }
            if (powers || effects) return std::nullopt;
            return IniRect(s, a_id);
        }

        // SelfTest L1 (blocks plan Task 2, D6): each tab's arranged rects (our card and SkyUI's) logged, PASS when they are build
        // 13's (Build13Rect), FAIL with the differences; SKIP when the layout file is not the default layout (its rects differ by
        // design). The ini and the layout file are read once per game session, so this runs at the first menu open only
        void CheckLayoutRects()
        {
            static bool s_checked = false;
            if (!Settings::Get().selfTest || std::exchange(s_checked, true)) return;
            // Task 3: [Debug] fDebugExtraHeight makes the node card taller on purpose - no build 13 reference either
            // Task 6: add-ons at the end of the right column (Settings::DefaultLayout) leave the built-in blocks' rects as they were
            const bool custom = !(CurrentLayout() == Settings::DefaultLayout()) || Settings::Get().debugExtraHeight > 0;
            constexpr std::array kTabs{ Tab::kFollow, Tab::kSchool, Tab::kPowers, Tab::kEffects };
            for (const Tab tab : kTabs) {
                for (const bool own : { true, false }) {
                    const auto arranged = ArrangeBlocks(ArrangementFor(tab, own));
                    const auto hidden = TabHidden(tab);
                    std::string rects, diffs;
                    for (const BlockId id : kBlocks) {
                        const auto it = arranged.find(id);
                        const bool shown = id == Layout::kSpellCard || !hidden.contains(id);  // the card's showing is WatchOwnCard's
                        const std::optional<Settings::Rect> now = shown && it != arranged.end() ? std::optional{ it->second } : std::nullopt;
                        const auto then = Build13Rect(tab, own, id);
                        rects += std::format(" {} {}", id, now ? RectText(*now) : std::string("hidden"));
                        if (now.has_value() != then.has_value() || (now && !SameRect(*now, *then))) {
                            diffs += std::format("; {} {} (build 13 {})", id, now ? RectText(*now) : std::string("hidden"),
                                then ? RectText(*then) : std::string("hidden"));
                        }
                    }
                    const char* verdict = custom ? "SKIP" : diffs.empty() ? "PASS" : "FAIL";
                    SelfTest(std::format("L1 layout {} {} ({}) -{}{}{}", TabName(tab), verdict, own ? "our card" : "SkyUI's card", rects,
                        custom ? " (the layout file is not the default layout, or [Debug] fDebugExtraHeight is on: build 13's rects are no reference)" : "",
                        custom ? "" : diffs));
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

        RE::GFxValue ContentChild(RE::GFxValue& a_widget, const BlockId& a_id, const char* a_name, RE::GFxValue& a_content);  // with the L2 read-backs

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

        std::string HtmlEscape(std::string_view a_text)
        {
            std::string out;
            for (const char c : a_text) {
                switch (c) {
                case '&': out += "&amp;"; break;
                case '<': out += "&lt;"; break;
                case '>': out += "&gt;"; break;
                default: out += c; break;
                }
            }
            return out;
        }

        std::string ColorHex(std::uint32_t a_rgb)
        {
            return std::format("#{:06X}", a_rgb);
        }

        // A time left in SkyUI's way (ItemCard.as:330-354: whole hours, else whole minutes, else seconds) in our words
        std::string TimeText(double a_seconds)
        {
            const double whole = std::floor(a_seconds);
            if (whole >= kSecondsPerHour) return Fill(Str::kCardHours, { { "{n}", std::format("{:.0f}", std::floor(whole / kSecondsPerHour)) } });
            if (whole >= kSecondsPerMinute) return Fill(Str::kCardMinutes, { { "{n}", std::format("{:.0f}", std::floor(whole / kSecondsPerMinute)) } });
            return Fill(Str::kCardSeconds, { { "{n}", std::format("{:.0f}", whole) } });
        }

        std::string CastTypeText(const RE::SpellItem* a_spell)
        {
            switch (a_spell->GetCastingType()) {
            case RE::MagicSystem::CastingType::kConcentration: return Text(Str::kCardCastConcentration);
            case RE::MagicSystem::CastingType::kConstantEffect: return Text(Str::kCardCastConstant);
            case RE::MagicSystem::CastingType::kScroll: return Text(Str::kCardCastScroll);
            default: return Text(Str::kCardCastFireForget);
            }
        }

        std::string PowerText(const RE::SpellItem* a_spell)
        {
            if (!a_spell) return Text(Str::kCardPowerAny);
            switch (a_spell->GetSpellType()) {
            case RE::MagicSystem::SpellType::kPower: return Text(Str::kCardPower);
            case RE::MagicSystem::SpellType::kLesserPower: return Text(Str::kCardLesserPower);
            case RE::MagicSystem::SpellType::kAbility: return Text(Str::kCardAbility);
            default: return Text(Str::kCardPowerAny);
            }
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
                if (school.empty() && spell) {
                    const int node = Spells::Get() ? Spells::PrimaryNode(spell->GetFormID()) : -1;
                    school = SchoolText(node >= 0 ? Data().tree.nodes[node].school : Spells::SchoolOf(spell));
                }
                auto level = StringMember(a_info, "castLevel");
                if (level.empty() && spell) level = LevelText(spell);
                view.sub = school.empty() ? level : level.empty() ? school : Fill(Str::kNodeSub, { { "{school}", school }, { "{level}", level } });
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

        // The colours an HTML text sets ("color=" values, upper case), for the colour check
        std::set<std::string> HtmlColours(const std::string& a_html)
        {
            std::string lower = a_html;
            std::ranges::transform(lower, lower.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
            std::set<std::string> colours;
            constexpr std::size_t kHexDigits = 6;
            for (auto at = lower.find("color="); at != std::string::npos; at = lower.find("color=", at + 1)) {
                const auto hash = lower.find('#', at);
                if (hash == std::string::npos || hash > at + 8 || hash + 1 + kHexDigits > lower.size()) continue;
                colours.insert(Upper(a_html.substr(hash + 1, kHexDigits)));
            }
            return colours;
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
        double DrawHtmlText(RE::GFxMovieView* a_movie, RE::GFxValue& a_content, const char* a_name, double a_y, double a_w, double a_room,
            const std::string& a_html, double a_size, int a_colourType = -1)
        {
            const int lines = std::max(1, 1 + static_cast<int>(std::floor((a_room - FieldH(a_size)) / LineH(a_size))));
            auto field = Draw::Text(a_movie, a_content, a_name, 0, a_y, a_w, FieldH(a_size, lines), "", kWhite, a_size);
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
            const int needed = TextLines(field, a_size);
            if (needed > lines) {
                const auto fitsAt = [&](int a_at) {
                    SetHtml(a_movie, field, a_html, a_size);
                    CutFrom(a_movie, field, a_at);
                    return TextLines(field, a_size) <= lines;
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
                if (g_menu.cutLogged.insert(std::string("c1|") + a_name).second) {
                    logger::info("Widget: C1 card {} cut to {} of its {} lines at size {:.1f} (character {}, then \"{}\")", a_name, lines, needed, a_size, lo,
                        kEllipsis);
                }
            }
            return std::min(TextH(field), FieldH(a_size, lines));
        }

        // Our card in a_rect (render layout F): name (size 19, two lines), sub line, the shout's words, a rule, the description,
        // a rule and the footer (cost left, cast type right); with a_fit the panel ends under its content
        void RedrawOwnCard(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget, const CardView& a_view, const Settings::Rect& a_rect, bool a_fit)
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
            const double w = a_rect.Width() - 2 * kPad, maxH = a_rect.Height();
            double y = kCardTop;
            auto name = FitLines(a_movie, content, "name", 0, y, w, kNameSize, kNameLines, a_view.name, a_view.negative ? kNotYet : kWhite);
            y += name.IsDisplayObject() ? std::clamp(TextH(name), FieldH(kNameSize), FieldH(kNameSize, kNameLines)) : FieldH(kNameSize);
            const double subSize = Size(kSubSize), bodySize = Size(kRowSize);
            if (!a_view.sub.empty()) {
                FitLine(a_movie, content, "sub", 0, y, w, subSize, a_view.sub, kTitleColor);
                y += FieldH(subSize);
            }
            if (!a_view.words.empty()) y += DrawHtmlText(a_movie, content, "words", y, w, FieldH(subSize), a_view.words, subSize);
            y += kCardRuleGap;
            Draw::LineStyle(content, kEdgeWidth, kPanelEdge);
            Draw::MoveTo(content, 0, y);
            Draw::LineTo(content, w, y);
            y += kCardRuleGap;
            const bool foot = !a_view.footLeft.empty() || !a_view.footRight.empty();
            const double footH = FieldH(subSize), footReserve = foot ? 2 * kCardRuleGap + footH + kPad : kPad;
            double used = y;
            if (!a_view.effects.empty() && maxH - footReserve - y >= FieldH(kMinFitSize)) {
                used = y + DrawHtmlText(a_movie, content, "desc", y, w, maxH - footReserve - y, a_view.effects, bodySize, a_view.type);
            }
            const double h = a_fit ? std::min(maxH, used + footReserve) : maxH;
            if (foot) {
                const double ruleY = h - footReserve + kCardRuleGap;
                Draw::LineStyle(content, kEdgeWidth, kPanelEdge);
                Draw::MoveTo(content, 0, ruleY);
                Draw::LineTo(content, w, ruleY);
                const double footY = h - kPad - footH;
                if (!a_view.footLeft.empty()) FitLine(a_movie, content, "cost", 0, footY, w * kCardFootShare, subSize, a_view.footLeft, kLabelColor);
                if (!a_view.footRight.empty()) {
                    FitLine(a_movie, content, "cast", w * kCardFootShare, footY, w * (1 - kCardFootShare), subSize, a_view.footRight, kDimText, "right");
                }
            }
            Draw::NoLine(panel);
            Draw::Rect(panel, 0, 0, a_rect.Width(), h, kPanelFill, kPanelAlpha);
            g_menu.ownCardBottom = a_rect.top + h;
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
            const auto cardRect = BlockRect(Layout::kSpellCard);
            const auto sig = show ? std::format("{}|{:.1f}x{:.1f}|{:.1f}|{}|{}|{}", static_cast<int>(g_menu.tab), cardRect.Width(), cardRect.Height(),
                                        g_menu.rowSize, infoSig, view.sub, view.footRight)
                                  : std::string{};

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
                    RedrawOwnCard(a_movie, a_widget, view, cardRect, g_menu.tab == Tab::kEffects);
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
        constexpr const char* kButtonsName = "layoutButtons";  // the lock / reset row (D5), outside the column masks
        constexpr const char* kBorderName = "layoutBorder";    // the border handle in the gap between the columns (the mode only)
        constexpr const char* kOverlayName = "layoutOverlay";  // the drag ghost, the insertion line, the border guide: on top
        constexpr const char* kCoverPrefix = "mmxCover_";      // a block's drag cover, in its column clip above the panels
        constexpr const char* kCoverBlock = "mmxBlock";        // a cover's block id
        constexpr std::string_view kGamepadSource = Widget::kGamepadToggleSource;
        constexpr const char* kHandleInputName = "handleInput";  // ItemMenu.as:205 / MagicMenu.as:80
        constexpr const char* kSavedHandleInput = "mmxSavedHandleInput";
        constexpr const char* kAttemptEquipName = "AttemptEquip";  // MagicMenu.as:111
        constexpr const char* kSavedAttemptEquip = "mmxSavedAttemptEquip";
        constexpr const char* kShowPanelPath = "_global.InventoryLists.SHOW_PANEL";  // InventoryLists.as:34 (static, 1)
        constexpr double kButtonRowGap = 4.0, kButtonRowH = 22.0;  // D5: the row's top 4 below the left column (627 at the E2 rects)
        constexpr double kLockW = 104.0, kResetW = 84.0, kButtonGap = 6.0, kButtonTextSize = 12.0, kButtonAlpha = 90.0;
        constexpr double kGreyAlpha = 40.0, kDraggedAlpha = 45.0, kGhostAlpha = 25.0, kLineW = 2.0, kHandleLineAlpha = 80.0;
        constexpr double kMinHandleW = 6.0;  // the border handle's hit strip at least this wide (fPanelGap may be 0)
        constexpr double kAutoScrollEdge = 28.0, kAutoScrollStep = 8.0;  // D8: within this of a column's top/bottom the column scrolls, per frame
        constexpr int kMaxDragLogs = 24;
        // Task 5: the focus look (on the block's cover), the pick's keep-in-view margin around its insertion line, the right stick
        constexpr double kFocusEdgeW = 2.0, kPickEdgeW = 3.0, kLiftShadow = 4.0, kLiftShadowAlpha = 45.0, kLiftFillAlpha = 12.0;
        constexpr double kLineViewMargin = 24.0;  // stage units kept in view above and below the pick's insertion line
        constexpr double kStickStep = 14.0;       // stage units per frame at full tilt (unverified feel: eyes-only)
        constexpr int kMaxPadLogs = 40;

        std::atomic<bool> g_layoutMode{ false };            // the input boundary's view: kept from the accepted toggle to the leave
        std::atomic<std::uint64_t> g_menuGeneration{ 0 };   // bumped at every attach (D8: a drag session is of one menu)

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
        };
        std::mutex g_layoutRequestLock;
        std::vector<LayoutRequest> g_layoutRequests;

        // Menus that pause the game, use the menu context or are modal, opened while the magic menu is open (the mode ends as
        // Cancel when one opens, and does not start while one is up)
        std::mutex g_blockingLock;
        std::set<std::string> g_blockingMenus;

        void QueueLayout(LayoutRequestKind a_kind, std::string a_why, Layout::Nav a_nav = {})
        {
            const std::scoped_lock lock(g_layoutRequestLock);
            g_layoutRequests.push_back({ a_kind, std::move(a_why), a_nav });
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

        bool FocusLost()
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

        class LockClick final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params&) override
            {
                if (!g_closing) g_menu.layout.lockDue = true;
            }
        };

        class ResetClick final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params&) override
            {
                if (!g_closing && g_menu.layout.active) g_menu.layout.resetDue = true;
            }
        };

        // Layer 3: the shield's press does nothing - it is there so Mouse.getTopMostEntity() is never one of SkyUI's clips
        class ShieldPress final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params&) override { ++g_menu.layout.shieldPresses; }
        };

        // A_this's saved member called with the this and the arguments it would have had; its return value handed back
        bool CallSavedMember(RE::GFxFunctionHandler::Params& a_params, const char* a_saved)
        {
            RE::GFxValue original;
            if (!a_params.thisPtr->GetMember(a_saved, &original) || !original.IsObject()) return false;
            std::vector<RE::GFxValue> argv;
            argv.reserve(a_params.argCount + 1);
            argv.push_back(*a_params.thisPtr);
            for (std::uint32_t i = 0; i < a_params.argCount; ++i) argv.push_back(a_params.args[i]);
            RE::GFxValue result;
            const bool called = original.Invoke("call", &result, argv.data(), argv.size());
            if (called && a_params.retVal) *a_params.retVal = result;
            return called;
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
                if (!CallSavedMember(a_params, kSavedHandleInput)) {
                    static std::atomic<bool> s_logged{ false };
                    if (!s_logged.exchange(true)) logger::warn("Layout: handleInput wrap without its saved original - the key is dropped (logged once)");
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
                if (!CallSavedMember(a_params, kSavedAttemptEquip)) {
                    static std::atomic<bool> s_logged{ false };
                    if (!s_logged.exchange(true)) logger::warn("Layout: AttemptEquip wrap without its saved original - the equip is dropped (logged once)");
                }
            }
        };

        RE::GPtr<CoverPress> g_coverPress;
        RE::GPtr<CoverRelease> g_coverRelease;
        RE::GPtr<BorderPress> g_borderPress;
        RE::GPtr<BorderRelease> g_borderRelease;
        RE::GPtr<LockClick> g_lockClick;
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
        // with the block). Picked: a thicker accent edge, a light fill and a shadow band along the inner bottom and right edges
        void DrawFocusLook(RE::GFxValue& a_cover, double a_w, double a_h, bool a_picked)
        {
            if (!a_picked) {
                Draw::LineStyle(a_cover, kFocusEdgeW, kBlue);
                Draw::Rect(a_cover, kFocusEdgeW / 2, kFocusEdgeW / 2, a_w - kFocusEdgeW, a_h - kFocusEdgeW, kPanelFill, 0.0);
                return;
            }
            Draw::NoLine(a_cover);
            Draw::Rect(a_cover, a_w - kLiftShadow, kLiftShadow, kLiftShadow, a_h - kLiftShadow, kPanelFill, kLiftShadowAlpha);
            Draw::Rect(a_cover, kLiftShadow, a_h - kLiftShadow, a_w - 2 * kLiftShadow, kLiftShadow, kPanelFill, kLiftShadowAlpha);
            Draw::LineStyle(a_cover, kPickEdgeW, kBlue);
            Draw::Rect(a_cover, kPickEdgeW / 2, kPickEdgeW / 2, a_w - kPickEdgeW, a_h - kPickEdgeW, kWhite, kLiftFillAlpha);
        }

        // In the mode: a transparent cover over each visible block (VisibleBlocks: ApplyVisibility's rules), above every panel of its
        // column (made again after any panel is made again), carrying the drag and the focus look (Task 5). It also takes the tree's
        // node clicks and roll-overs and the node card's button (off in the mode). Scrolls with its column clip
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

        // D5: the lock (and, in the mode, reset) button right-aligned at the left column's right edge (the border), just below it
        void DrawButtons(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            auto row = Child(a_widget, kButtonsName);
            if (!row.IsDisplayObject()) return;
            const auto& m = g_menu.layout;
            const double right = ColumnViewport(0).right;
            const double top = Layout::DefaultFrames(Settings::Get())[0].bottom + kButtonRowGap;
            const auto key = KeyLabel(Settings::Get().keyLayout);
            const bool grey = !SafetyNetReason().empty();
            const auto button = [&](const char* a_name, double a_x, double a_w, const std::string& a_text, RE::GFxFunctionHandler* a_handler) {
                Draw::Remove(row, a_name);
                auto clip = Draw::Clip(row, a_name);
                if (!clip.IsDisplayObject()) return;
                clip.SetMember("_x", a_x);
                clip.SetMember("_y", top);
                Draw::Box(clip, a_w, kButtonRowH, kButtonFill, kButtonAlpha, kPanelEdge, kEdgeWidth);
                const double size = kButtonTextSize;
                FitLine(a_movie, clip, "label", 0, (kButtonRowH - FieldH(size)) / 2, a_w, size, a_text, kWhite, "center");
                SetHandler(a_movie, clip, "onRelease", a_handler);
                if (grey) clip.SetMember("_alpha", kGreyAlpha);
            };
            button("lock", right - kLockW, kLockW, Fill(m.active ? Str::kLayoutLock : Str::kLayoutEdit, { { "{key}", key } }), g_lockClick.get());
            if (m.active) {
                button("reset", right - kLockW - kButtonGap - kResetW, kResetW, Text(Str::kLayoutReset), g_resetClick.get());
            } else {
                Draw::Remove(row, "reset");
            }
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

        // Rearrange's tail: the buttons and the handle follow the border; the covers follow the blocks
        void PlaceLayoutChrome(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            DrawButtons(a_movie, a_widget);
            DrawBorderHandle(a_widget);
            if (g_menu.layout.active) SetCovers(a_movie, a_widget);
        }

        // After the panels (AttachWidget): the button row, the border handle, then the overlay on top of everything
        void MakeLayoutChrome(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            auto row = Draw::Clip(a_widget, kButtonsName);
            auto handle = Draw::Clip(a_widget, kBorderName);
            auto overlay = Draw::Clip(a_widget, kOverlayName);
            if (handle.IsDisplayObject()) {
                SetHandler(a_movie, handle, "onPress", g_borderPress.get());
                SetHandler(a_movie, handle, "onRelease", g_borderRelease.get());
                SetHandler(a_movie, handle, "onReleaseOutside", g_borderRelease.get());
                handle.SetMember("useHandCursor", false);
            }
            if (!row.IsDisplayObject() || !handle.IsDisplayObject() || !overlay.IsDisplayObject()) {
                logger::warn("Layout: the button row, border handle or overlay could not be made - the layout mode will not start this menu");
            }
            PlaceLayoutChrome(a_movie, a_widget);
            if (const auto why = SafetyNetReason(); !why.empty() && !std::exchange(g_menu.layout.restLogged, true)) {
                logger::info("Layout: the layout mode rests this menu - {}; the lock button is greyed, the default layout is used (the saved one is "
                             "kept)",
                    why);
            }
        }

        bool ChromeReady(RE::GFxValue& a_widget)
        {
            return Child(a_widget, kShieldName).IsDisplayObject() && Child(a_widget, kButtonsName).IsDisplayObject() &&
                   Child(a_widget, kBorderName).IsDisplayObject() && Child(a_widget, kOverlayName).IsDisplayObject();
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
            if (a_id == Layout::kTree) {
                if (auto panel = BlockClip(a_widget, a_id); panel.IsDisplayObject()) {
                    DrawLegend(a_movie, panel, rect.Width(), rect.Height(), g_menu.legendSchool.value_or(std::string{}));
                }
                g_menu.tooltipDue = true;
            }
            if (a_id == Layout::kTree || a_id == Layout::kProgress || a_id == Layout::kNodeCard) g_menu.dirty = true;
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
            Draw::LineStyle(line, kLineW, kBlue);
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
                    Draw::Box(ghost, rect.Width(), rect.Height(), kPanelFill, kGhostAlpha, kBlue, kLineW);
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
            if (m.drag->generation != g_menuGeneration.load() || FocusLost()) {
                logger::info("Layout: drag of {} ended without a move - {}", m.drag->id, FocusLost() ? "the game window lost the focus" : "another menu's drag");
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
            if (FocusLost()) {
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
            Draw::LineStyle(guide, kLineW, kBlue);
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
                g_layoutMode = false;
                logger::warn("Layout: mode refused - the shield, button row, border handle or overlay is missing in this menu ({})", a_why);
                return;
            }
            m.active = true;
            m.entry = m.working = Settings::SavedLayout();
            ++m.entered;
            g_menu.hovered = -1;  // the tooltip goes (RedrawTooltip)
            g_menu.tooltipDue = true;
            // Task 5: entered with the gamepad, the first visible block has the focus at once (there is no pointer); otherwise the
            // first arrow / D-pad press gives it, so a mouse user sees no outline
            g_layoutFocus = {};
            g_layoutStickY = 0.0f;
            if (a_why == kGamepadSource) g_layoutFocus.focus = Layout::FirstFocus(m.working, VisibleBlocks());
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
            if (!m.active) {
                g_layoutMode = false;
                return;
            }
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
                        logger::error("Layout: save FAILED - {}. The layout mode stays on with the edited layout (the lock button or {} again to retry, "
                                      "Esc to leave without saving)",
                            detail, KeyLabel(Settings::Get().keyLayout));
                        SelfTest(std::format("L3 layout saved FAIL - not written: {}", detail));
                        g_layoutMode = true;
                        return;
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
            m.active = false;
            g_layoutMode = false;
            if (!a_save) {
                const bool same = CurrentLayout() == m.entry && Settings::SavedLayout() == m.entry;
                const auto arranged = ArrangeBlocks(ArrangementFor(g_menu.tab, OwnCard()));
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
            g_menu.tooltipDue = true;
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
            case Effect::kLeave:
                LeaveLayoutMode(a_movie, a_widget, false, a_source);
                return;
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
        void LayoutFrame(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            auto& m = g_menu.layout;
            InstallInputWraps(a_movie);
            PublishLayoutBlock(a_movie);
            std::vector<LayoutRequest> requests;
            {
                const std::scoped_lock lock(g_layoutRequestLock);
                requests.swap(g_layoutRequests);
            }
            for (const auto& request : requests) {
                switch (request.kind) {
                case LayoutRequestKind::kEnter: EnterLayoutMode(a_movie, a_widget, request.why); break;
                case LayoutRequestKind::kSave: LeaveLayoutMode(a_movie, a_widget, true, request.why); break;
                case LayoutRequestKind::kCancel: LeaveLayoutMode(a_movie, a_widget, false, request.why); break;
                case LayoutRequestKind::kNav: PadKey(a_movie, a_widget, request.nav, request.why); break;
                }
            }
            if (std::exchange(m.lockDue, false)) {
                if (m.active) {
                    g_layoutMode = false;
                    LeaveLayoutMode(a_movie, a_widget, true, "the lock button");
                } else if (const auto bits = LiveBlock(Input::HeldCount() > 0); bits != 0) {
                    LogRefusal(bits, "the lock button");
                } else if (!g_layoutMode.exchange(true)) {
                    EnterLayoutMode(a_movie, a_widget, "the lock button");
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
        void LayoutAtClose()
        {
            auto& m = g_menu.layout;
            const bool wasOn = m.active || g_layoutMode.exchange(false);
            g_layoutMode = false;
            {
                const std::scoped_lock lock(g_layoutRequestLock);
                g_layoutRequests.clear();
            }
            g_layoutBlock = kBlockNoWidget;
            if (m.active) {
                m.active = false;
                m.drag.reset();
                m.borderGrab.reset();
                const bool same = Settings::SavedLayout() == m.entry;
                logger::info("Layout: mode ended by the menu's close - cancelled, nothing written");
                SelfTest(std::format("L4 cancel {} - menu closed in the layout mode: the saved layout {} the entry layout ({})", same ? "PASS" : "FAIL",
                    same ? "is" : "is NOT", Describe(m.entry)));
            }
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
            g_layoutFocus = {};
            g_layoutStickY = 0.0f;
        }

        // ---------------------------------------------------------------------
        // Tree window
        // ---------------------------------------------------------------------

        using LabelBox = LayoutMath::Box;

        // HoM's sizes in the tree window (spec v11 T12): HoM world units x fTreeScale x fTreeMarkScale
        double MarkScale()
        {
            const auto& settings = Settings::Get();
            return settings.treeScale * settings.treeMarkScale;
        }

        // A drawn node (spec v10 T4, v11 T11-T13): clickable when linked to the centre (within iMaxSteps, either kind of link)
        // or learned / learnable - build 10's rule, unchanged (T11); full when it is the centre or one STRUCTURAL link from it,
        // else dimmed (R11, built apart from the clickability steps); drawn in HoM's look of its display state (R1)
        struct TreeNode
        {
            TreeData::PlacedNode placed;
            Spells::Look look;
            HomStyle::Display display{};
            bool clickable{};
            bool full{};
            bool focus{};    // the clicked (selected) node: HoM's focus look, FOCUS_GROW larger (R6)
            bool onPath{};   // on the learning target's path
            double reach{};  // how far its mark reaches, stage units (HomStyle::Reach)
            RE::GFxValue clip;
        };

        // A link as drawn, for the labels to keep off it (LayoutMath::LinkBoxes)
        struct DrawnLine
        {
            double ax{}, ay{}, bx{}, by{}, width{};
        };

        struct EdgeTally
        {
            int dim{}, locked{}, frontier{}, unlocked{}, selected{}, learning{}, chains{}, hidden{};
            std::vector<DrawnLine> lines;
        };

        // A chain from a hard prerequisite to the centre (canvasRendererEdges.js:354-434): links LINK_W x LINK_H every 1.15 x
        // LINK_W, every second one turned a quarter, at least 3. HoM draws a cached sprite in world units (a rounded link with a
        // dark hole); here a plain filled rectangle in its fill and outline, at the mark scale.
        void DrawChain(RE::GFxValue& a_content, double a_ax, double a_ay, double a_bx, double a_by, double a_scale, double a_alpha, EdgeTally& a_tally)
        {
            constexpr int kMinLinks = 3;
            const double dx = a_bx - a_ax, dy = a_by - a_ay, dist = std::hypot(dx, dy);
            if (dist < a_scale) return;  // HoM: dist < 1 world unit
            const double linkW = HomStyle::kChainLinkW * a_scale, linkH = HomStyle::kChainLinkH * a_scale;
            const int links = std::max(kMinLinks, static_cast<int>(std::lround(dist / (linkW * HomStyle::kChainSpacing))));
            const double angle = std::atan2(dy, dx);
            Draw::LineStyle(a_content, HomStyle::kChainStroke * a_scale, HomStyle::kChainEdge.rgb, Percent(HomStyle::kChainEdge, a_alpha));
            for (int i = 0; i < links; ++i) {
                const double t = (i + 0.5) / links, cx = a_ax + dx * t, cy = a_ay + dy * t;
                const double turn = angle + (i % 2 != 0 ? std::numbers::pi / 2 : 0.0);
                const double c = std::cos(turn), s = std::sin(turn), hw = linkW / 2, hh = linkH / 2;
                const std::array<HomStyle::Point, 4> corners{ { { cx - hw * c + hh * s, cy - hw * s - hh * c }, { cx + hw * c + hh * s, cy + hw * s - hh * c },
                    { cx + hw * c - hh * s, cy + hw * s + hh * c }, { cx - hw * c - hh * s, cy - hw * s + hh * c } } };
                Draw::BeginFill(a_content, HomStyle::kChainFill.rgb, Percent(HomStyle::kChainFill, a_alpha));
                Draw::Polygon(a_content, corners);
                Draw::EndFill(a_content);
            }
            ++a_tally.chains;
            a_tally.lines.push_back({ a_ax, a_ay, a_bx, a_by, linkW });
        }

        // The links between drawn nodes (spec v11 T14; canvasRendererEdges.js:157-434), bottom to top as HoM strokes them: its
        // dim batch, locked, frontier (learned -> learnable), learned; the centre's links to its direct neighbours
        // (showSelectionPath: HoM's selected path, here the centre's own links, user 2026-10-26); the learning target's path
        // (spec v10 T8; 3 wide into the target, R5); the chains of the centre's hard prerequisites (HoM's are the selected
        // spell's). With showSelectionPath every other link is HoM's dim batch (its own dimming: no x 0.6 on top); otherwise a
        // link with an end outside the full set gets the dim factor in its alpha (T11). showBaseConnections off: only learned
        // and frontier links and the paths (:207-208). Widths are HoM's world units x fTreeScale (review v11 #5).
        EdgeTally DrawEdges(RE::GFxValue& a_content, const TreeData::Window& a_window, const std::unordered_map<int, const TreeNode*>& a_nodes,
            const std::set<std::pair<int, int>>& a_path, int a_target, const HomStyle::Style& a_style)
        {
            using HomStyle::Display;
            const auto& tree = Data().tree;
            const auto& t = a_style.tree;
            const int centre = g_session.centre;
            EdgeTally tally;
            const double lineScale = Settings::Get().treeScale;  // review v11 #5: HoM's widths are world units
            struct Line
            {
                const TreeData::PlacedEdge* placed;
                double width;
                HomStyle::Color colour;
                double alpha;
            };
            enum Layer : std::size_t
            {
                kDim,
                kLocked,
                kFrontier,
                kUnlocked,
                kSelected,
                kLearning,
                kLayers
            };
            std::array<std::vector<Line>, kLayers> layers;
            const auto node = [&](int a_index) {
                const auto it = a_nodes.find(a_index);
                return it == a_nodes.end() ? nullptr : it->second;
            };
            // HoM dims only while the selected spell has a path to show (hasSelectedPath, :157)
            const bool dimAll = a_style.showSelectionPath && std::ranges::any_of(tree.adjacent[centre], [&](int a_edge) {
                return tree.edges[a_edge].kind == TreeData::EdgeKind::kStructural;
            });
            for (const auto& placed : a_window.edges) {
                const auto& edge = tree.edges[placed.edge];
                const auto* from = node(edge.from);
                const auto* to = node(edge.to);
                if (edge.kind != TreeData::EdgeKind::kStructural || !from || !to) continue;  // a lock link: a chain below or nothing
                const double factor = from->full && to->full ? 1.0 : a_style.DimFactor();
                if (a_path.contains(std::minmax(edge.from, edge.to))) {
                    // review v11 #6: 3 wide only when its end toward the target is the target in the learning look
                    // (canvasRendererEdges.js:343-345 toNode.state === 'learning'); the path ends at the target
                    const auto* targetNode = node(a_target);
                    const bool intoTarget =
                        (edge.from == a_target || edge.to == a_target) && targetNode && targetNode->display == Display::kLearning;
                    layers[kLearning].push_back({ &placed, intoTarget ? HomStyle::kLearningEdgeIntoTarget : HomStyle::kLearningEdgeWidth, a_style.learning,
                        HomStyle::kLearningEdgeAlpha * factor });
                    continue;
                }
                if (a_style.showSelectionPath && (edge.from == centre || edge.to == centre)) {
                    layers[kSelected].push_back({ &placed, t.selectedPathWidth, t.selectedPathColor, t.selectedPathAlpha });
                    continue;
                }
                const bool both = from->display == Display::kUnlocked && to->display == Display::kUnlocked;
                const bool frontier = t.frontierEdgeAlpha > 0 && !both && from->display == Display::kUnlocked &&
                                      (to->display == Display::kAvailable || to->display == Display::kLearning);
                if (!both && !frontier && !a_style.showBaseConnections) {
                    ++tally.hidden;
                    continue;
                }
                const auto school = a_style.SchoolColor(tree.nodes[edge.from].school);
                if (dimAll) {
                    layers[kDim].push_back({ &placed, HomStyle::kDimEdgeWidth, t.dimEdgeColor, HomStyle::kDimEdgeAlpha });
                } else if (both) {
                    layers[kUnlocked].push_back({ &placed, t.unlockedEdgeWidth, t.unlockedEdgeColor.value_or(school), t.unlockedEdgeAlpha * factor });
                } else if (frontier) {
                    layers[kFrontier].push_back({ &placed, HomStyle::kFrontierEdgeWidth, school, t.frontierEdgeAlpha * factor });
                } else {
                    layers[kLocked].push_back({ &placed, HomStyle::kLockedEdgeWidth, t.lockedEdgeColor, t.lockedEdgeAlpha * factor });
                }
            }
            const std::array<int*, kLayers> counts{ &tally.dim, &tally.locked, &tally.frontier, &tally.unlocked, &tally.selected, &tally.learning };
            for (std::size_t layer = 0; layer < kLayers; ++layer) {
                for (const auto& line : layers[layer]) {
                    // one alpha per drawing call: the line's own, with the dim factor in it (R10)
                    const double width = line.width * lineScale;
                    Draw::LineStyle(a_content, width, line.colour.rgb, Percent(line.colour, line.alpha));
                    Draw::MoveTo(a_content, line.placed->ax, line.placed->ay);
                    Draw::LineTo(a_content, line.placed->bx, line.placed->by);
                    ++*counts[layer];
                    tally.lines.push_back({ line.placed->ax, line.placed->ay, line.placed->bx, line.placed->by, width });
                }
            }
            // the chains: from each drawn hard prerequisite of the centre to the centre (HoM, :356-366)
            const auto* hub = node(centre);
            for (const auto prerequisite : tree.nodes[centre].hard) {
                for (const auto& [index, drawn] : a_nodes) {
                    if (index == centre || !hub || tree.nodes[index].formId != prerequisite) continue;
                    const double factor = drawn->full ? 1.0 : a_style.DimFactor();
                    DrawChain(a_content, drawn->placed.sx, drawn->placed.sy, hub->placed.sx, hub->placed.sy, MarkScale(), HomStyle::kChainAlpha * factor, tally);
                }
            }
            return tally;
        }

        LabelBox NodeBox(const TreeNode& a_node)
        {
            const double r = a_node.reach;
            return { a_node.placed.sx - r, a_node.placed.sy - r, a_node.placed.sx + r, a_node.placed.sy + r };
        }

        // A node's label (spec v11 T15): HoM's text and colour, beside the node (by default facing the centre: left of a node
        // right of the centre; the centre's to its right). The text wraps to kLabelLines lines at the reading size and ends in
        // "..." when it needs more (Q4).
        struct LabelText
        {
            std::string name;  // the name cut to labelMaxChars, or "???"
            bool left{};       // the label left of the node
            double gap{};      // from the node's centre to the label
            std::uint32_t color{};
            double alpha = 1.0;  // the colour's own alpha x the dim factor: the field's _alpha, its only alpha (R10)
        };

        // A node's label in its clip (a_clip sits at the node): the text wraps to kLabelLines lines at the reading size and ends
        // in "..." when it needs more (Q4). Kept inside the window: up and down by its padding, and across no wider than the room
        // to the window edge on its side (v6 design review #2: a far-side label of a node near the edge ran past the panel), left
        // out when that room is under kMinLabelW. Its box goes into a_drawn. True when drawn.
        bool PlaceLabel(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, const TreeData::PlacedNode& a_placed, const LabelText& a_text,
            const Settings::Area& a_frame, std::vector<LabelBox>& a_drawn)
        {
            const double size = Size(kLabelSize), boxH = FieldH(size, kLabelLines);
            const double labelW = LayoutMath::LabelWidth(a_placed.sx, a_text.gap, a_frame.halfW + kPad, a_text.left, kLabelW);
            if (labelW < kMinLabelW) return false;
            auto field = Draw::Text(a_movie, a_clip, "label", a_text.left ? -a_text.gap - labelW : a_text.gap, -boxH / 2, labelW, boxH, "",
                a_text.color, size, a_text.left ? "right" : "left");
            if (!field.IsDisplayObject()) return false;
            field.SetMember("wordWrap", true);
            field.SetMember("_alpha", a_text.alpha * Draw::kAlphaFull);
            FitWhole(a_movie, field, labelW, size, kLabelLines, a_text.name);
            const double width = std::min(TextW(field), labelW), height = std::min(TextH(field), boxH);
            // centred on the node, inside the window's padding
            const double limit = a_frame.halfH + kPad;
            const double top = std::clamp(a_placed.sy - height / 2, -limit, std::max(-limit, limit - height)) - a_placed.sy;
            field.SetMember("_y", top);
            const double from = a_text.left ? a_placed.sx - a_text.gap - width : a_placed.sx + a_text.gap;
            a_drawn.push_back({ from, a_placed.sy + top, from + width, a_placed.sy + top + height });
            return true;
        }

        // HoM's label of a drawn node (spec v11 T15; _labelTextOf / _labelCandidates, canvasRendererLabels.js:90-153): none for
        // a locked spell; "???" in labelHidden for an available one under revealName % (not cheatMode); the name cut to
        // labelMaxChars in labelUnlocked / the learning colour / labelAvailable. Its alpha: the colour's own x the dim factor
        // (HoM draws a label at _contextFactor). The percent lines of build 10 are gone (the tooltip has the percent).
        std::optional<LabelText> NodeLabelText(const TreeNode& a_node, bool a_left, const HomStyle::Style& a_style)
        {
            using HomStyle::Display;
            if (a_node.display == Display::kLocked) return std::nullopt;  // even under cheatMode (review v11 #9: Labels rule)
            const auto reveal = HomStyle::Reveal(a_node.display, a_node.look.percent, a_style.revealName, a_style.cheatMode);
            if (reveal == HomStyle::Name::kNone) return std::nullopt;
            const auto& t = a_style.tree;
            const bool hidden = reveal == HomStyle::Name::kHidden;
            const auto colour = a_node.display == Display::kUnlocked ? t.labelUnlocked :
                                a_node.display == Display::kLearning ? a_style.learning :
                                hidden                               ? t.labelHidden :
                                                                       t.labelAvailable;
            const double dim = a_node.full ? 1.0 : a_style.DimFactor();  // review v11 #2: the focus is dimmed as its class
            return LabelText{ hidden ? std::string(HomStyle::kHiddenName) : HomStyle::ClipLabel(Spells::NodeName(a_node.placed.index), t.labelMaxChars),
                a_left, a_node.reach + kLabelGap, colour.rgb, std::clamp(colour.alpha * dim, 0.0, 1.0) };
        }

        // The same label drawn into a_box (window coordinates; a place LayoutMath::KeptLabelPlaces chose), aligned to the side it
        // sits on (right-aligned on the left of the node, centred above or below). The box is the size the label measured at
        // its first place, so the text fits it again (one unit of slack).
        void PlaceLabelAt(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, const TreeData::PlacedNode& a_placed, const LabelText& a_text,
            const LabelBox& a_box, LayoutMath::Place a_place, std::vector<LabelBox>& a_drawn)
        {
            constexpr double kSlack = 1.0;
            const double size = Size(kLabelSize), boxH = FieldH(size, kLabelLines), w = a_box.right - a_box.left + kSlack;
            const bool left = a_place == LayoutMath::Place::kLeft || a_place == LayoutMath::Place::kLeftFar;
            const bool centred = a_place == LayoutMath::Place::kBelow || a_place == LayoutMath::Place::kAbove;
            const double x = (left ? a_box.right - w : centred ? (a_box.left + a_box.right - w) / 2 : a_box.left) - a_placed.sx;
            auto field = Draw::Text(a_movie, a_clip, "label", x, a_box.top - a_placed.sy, w, boxH, "", a_text.color, size,
                left ? "right" : centred ? "center" : "left");
            if (!field.IsDisplayObject()) return;
            field.SetMember("wordWrap", true);
            field.SetMember("_alpha", a_text.alpha * Draw::kAlphaFull);
            FitWhole(a_movie, field, w, size, kLabelLines, a_text.name);
            a_drawn.push_back(a_box);
        }

        const char* PlaceName(LayoutMath::Place a_place)
        {
            switch (a_place) {
            case LayoutMath::Place::kLeft: return "left";
            case LayoutMath::Place::kBelow: return "below";
            case LayoutMath::Place::kAbove: return "above";
            case LayoutMath::Place::kRightFar: return "right, further out";
            case LayoutMath::Place::kLeftFar: return "left, further out";
            default: return "right";
            }
        }

        // A drawn node's clip at its place, in a_layer (spec v10 T4, T6; v11 T12, T13, R6). HoM's mark of its state (the mystery
        // "?" in a text field of its own), a hidden focus outline child (shown on roll-over, so a hover needs no redraw), and a
        // hit area on the mark alone (a child clip set as hitArea, so the label later drawn in the same clip takes no click or
        // hover). A clickable node gets onRelease; every node the roll-over / roll-out handlers (the tooltip).
        RE::GFxValue DrawNode(RE::GFxMovieView* a_movie, RE::GFxValue& a_layer, const TreeNode& a_node, const HomStyle::Style& a_style)
        {
            const auto name = std::format("n{}", a_node.placed.index);
            auto clip = Draw::Clip(a_layer, name.c_str());
            if (!clip.IsDisplayObject()) return clip;
            clip.SetMember("_x", a_node.placed.sx);
            clip.SetMember("_y", a_node.placed.sy);
            clip.SetMember(kIndexMember, static_cast<double>(a_node.placed.index));
            const auto& data = Data().tree.nodes[a_node.placed.index];
            const double s = MarkScale();
            const double lineScale = Settings::Get().treeScale;  // review v11 #5
            // review v11 #2: dimmed by `full` alone - the focus keeps its outline and size, not a full alpha
            const auto drawn = DrawHomMark(clip, a_style,
                { data.school, data.themeColor, data.skillLevel, data.tier, data.x, data.y, 0.0, 0.0, s, lineScale },
                { a_node.display, a_style.Mystery(), a_node.focus, a_node.onPath, a_node.full ? 1.0 : a_style.DimFactor() });
            DrawQuestion(a_movie, clip, 0.0, 0.0, s, drawn);
            if (auto focus = Draw::Clip(clip, kFocusChild); focus.IsDisplayObject()) {
                const auto& stroke = a_style.tree.focusStroke;
                Draw::LineStyle(focus, HomStyle::kFocusStroke * lineScale, stroke.rgb, Percent(stroke, 1.0));
                Draw::Polygon(focus, drawn.body);
                focus.SetMember("_visible", a_node.placed.index == g_menu.hovered);
            }
            if (auto hit = Draw::Clip(clip, "hit"); hit.IsDisplayObject()) {
                Draw::NoLine(hit);
                Draw::BeginFill(hit, kBackdrop, 0.0);  // alpha 0 still takes the mouse
                Draw::Polygon(hit, HomStyle::ShapePoints({}, 0, 0, 0, 0, a_node.reach));
                Draw::EndFill(hit);
                clip.SetMember("hitArea", hit);
            }
            if (a_node.clickable) {
                SetHandler(a_movie, clip, "onRelease", g_nodeClick.get());
            } else {
                clip.SetMember("useHandCursor", false);
            }
            SetHandler(a_movie, clip, "onRollOver", g_nodeRollOver.get());
            SetHandler(a_movie, clip, "onRollOut", g_nodeRollOut.get());
            return clip;
        }

        // Where a label went (spec v10 T5; build 6 review round 3, build 7 T7.2): drawn at its default side first, which gives its
        // real size; then that side, the other side, below, above and both sides further out are measured against every box
        // reserved so far - drawn marks, badges, the legend, labels placed before it (LayoutMath::KeptLabelPlaces / ChoosePlace) -
        // and it takes the first clear place. With none clear it is dropped (no forced overlap: the tooltip names the spell);
        // place and overlap then name the least covered place, for the log. none: HoM gives the node no label (T15).
        struct KeptPlace
        {
            LayoutMath::Place place = LayoutMath::Place::kRight;
            double firstOverlap{}, overlap{};
            bool drawn{};
            bool none{};
        };

        // a_farSteps: how often both sides are tried further out (only for the centre's, the selected node's and the target's: marks
        // are close, so an ordinary node's label further out would sit by its neighbour and read as that one's)
        KeptPlace PlaceNodeLabel(RE::GFxMovieView* a_movie, TreeNode& a_node, bool a_defaultLeft, int a_farSteps, const Settings::Area& a_frame,
            const HomStyle::Style& a_style, std::vector<LabelBox>& a_labels)
        {
            auto text = NodeLabelText(a_node, a_defaultLeft, a_style);
            if (!text) return { .none = true };
            if (!PlaceLabel(a_movie, a_node.clip, a_node.placed, *text, a_frame, a_labels)) {
                text->left = !text->left;
                if (!PlaceLabel(a_movie, a_node.clip, a_node.placed, *text, a_frame, a_labels)) return {};
            }
            const LabelBox drawn = a_labels.back();
            a_labels.pop_back();
            auto places = LayoutMath::KeptLabelPlaces(a_node.placed.sx, a_node.placed.sy, drawn.right - drawn.left, drawn.bottom - drawn.top,
                text->gap, text->left, a_frame.halfW + kPad, a_frame.halfH + kPad, a_node.reach, a_farSteps);
            places.front().box = drawn;  // exactly where it was drawn
            const auto choice = LayoutMath::ChoosePlace(places, a_labels);
            const auto& chosen = places[choice.index];
            if (choice.clear && choice.index == 0) {
                a_labels.push_back(drawn);
            } else {
                RE::GFxValue field;
                if (a_node.clip.GetMember("label", &field) && field.IsDisplayObject()) field.Invoke("removeTextField");
                if (choice.clear) PlaceLabelAt(a_movie, a_node.clip, a_node.placed, *text, chosen.box, chosen.place, a_labels);
            }
            return { chosen.place, choice.firstOverlap, choice.overlap, choice.clear };
        }

        // The label order (spec v10 T5): the centre, the selected node, the learning target, then the centre's direct neighbours,
        // then every other node; nearest first within each (the window's order; review v10: clickable before faint broke that)
        int LabelRank(const TreeNode& a_node, int a_target)
        {
            const int index = a_node.placed.index;
            if (index == g_session.centre) return 0;
            if (index == g_menu.selected) return 1;
            if (index == a_target) return 2;
            return a_node.placed.steps == 1 ? 3 : 4;
        }

        // An arrow at the window edge in each sector's direction (from the window centre) and its "N개 더" text just inside it
        // (spec v10 T7; the arrow is drawn, the font's arrow characters are not relied on). False, drawing nothing, when the
        // window has no room for an arrow and its text (review round 1 #2: a 32-wide tree rect passes the settings check and
        // turned the text's clamp range around)
        bool DrawBadges(RE::GFxMovieView* a_movie, RE::GFxValue& a_content, const LayoutMath::Exits& a_exits, const Settings::Area& a_frame,
            std::vector<LabelBox>& a_boxes)
        {
            constexpr double kTiny = 1e-9, kFar = 1e9;
            const double size = Size(kNoteSize), h = FieldH(size), textW = kExitTextW;
            const double hw = a_frame.halfW - kExitInset, hh = a_frame.halfH - kExitInset;
            if (hw < textW / 2 + kExitArrow || hh < kExitArrow || a_frame.halfH + kPad < h / 2 + kExitArrow) return false;
            for (std::size_t i = 0; i < a_exits.sectors.size(); ++i) {
                if (a_exits.sectors[i] == 0) continue;
                const double angle = static_cast<double>(i) * 2 * std::numbers::pi / static_cast<double>(a_exits.sectors.size());
                const double ux = std::cos(angle), uy = std::sin(angle);
                const double t = std::min(std::abs(ux) > kTiny ? hw / std::abs(ux) : kFar, std::abs(uy) > kTiny ? hh / std::abs(uy) : kFar);
                const double px = ux * t, py = uy * t, a = kExitArrow;
                Draw::NoLine(a_content);
                Draw::BeginFill(a_content, kLabelColor, Draw::kAlphaFull);
                Draw::MoveTo(a_content, px + ux * a, py + uy * a);
                Draw::LineTo(a_content, px - ux * a - uy * a, py - uy * a + ux * a);
                Draw::LineTo(a_content, px - ux * a + uy * a, py - uy * a - ux * a);
                Draw::LineTo(a_content, px + ux * a, py + uy * a);
                Draw::EndFill(a_content);
                const double cx = std::clamp(px - ux * (a + textW / 2), -a_frame.halfW + textW / 2, a_frame.halfW - textW / 2);
                const double cy = std::clamp(py - uy * (a + h / 2), -a_frame.halfH - kPad + h / 2, a_frame.halfH + kPad - h / 2);
                const auto name = std::format("exit{}", i);
                FitLine(a_movie, a_content, name.c_str(), cx - textW / 2, cy - h / 2, textW, size,
                    Fill(Str::kTreeExit, { { "{n}", std::to_string(a_exits.sectors[i]) } }), kLabelColor, "center");
                // what the labels keep off: the arrow and its text
                a_boxes.push_back({ px - 2 * a, py - 2 * a, px + 2 * a, py + 2 * a });
                a_boxes.push_back({ cx - textW / 2, cy - h / 2, cx + textW / 2, cy + h / 2 });
            }
            return true;
        }

        // In-game spacing check (spec v11 R5): the largest reach of the drawn marks against half the distance of the closest two
        // drawn marks (a mark reaching past it could touch its neighbour's). PASS once per menu, every FAIL. O(n^2) for n drawn
        // nodes (iMaxNodes at most 200).
        void CheckSpacing(const std::vector<TreeNode>& a_nodes)
        {
            double closest = std::numeric_limits<double>::infinity(), reach = 0.0;
            int a = -1, b = -1, widest = -1;
            for (std::size_t i = 0; i < a_nodes.size(); ++i) {
                if (a_nodes[i].reach > reach) {
                    reach = a_nodes[i].reach;
                    widest = a_nodes[i].placed.index;
                }
                for (std::size_t j = i + 1; j < a_nodes.size(); ++j) {
                    const auto& p = a_nodes[i].placed;
                    const auto& q = a_nodes[j].placed;
                    const double d = std::hypot(p.sx - q.sx, p.sy - q.sy);
                    if (d < closest) {
                        closest = d;
                        a = p.index;
                        b = q.index;
                    }
                }
            }
            if (a < 0) return;
            const bool pass = reach <= closest / 2;
            if (pass && std::exchange(g_menu.spacingLogged, true)) return;
            SelfTest(std::format("T2 spacing {} - closest drawn marks {:.1f} apart ({} / {}), half {:.2f}; the largest drawn reach {:.2f} ({}) "
                                 "(HoM sizes x fTreeScale {} x fTreeMarkScale {})",
                pass ? "PASS" : "FAIL", closest, Spells::NodeName(a), Spells::NodeName(b), closest / 2, reach, Spells::NodeName(widest),
                Settings::Get().treeScale, Settings::Get().treeMarkScale));
        }

        // After a redraw the tooltip's node must still be drawn and where it was (a click recentres the tree; the node under the
        // mouse is then another one until the mouse moves): else nothing is hovered (spec v10 T6)
        void RecheckHover(const std::optional<std::pair<double, double>>& a_before)
        {
            constexpr double kSamePlace = 0.5;
            const auto it = std::ranges::find_if(g_menu.drawn, [](const TreeData::PlacedNode& a_placed) { return a_placed.index == g_menu.hovered; });
            if (!a_before || it == g_menu.drawn.end() || std::abs(it->sx - a_before->first) > kSamePlace ||
                std::abs(it->sy - a_before->second) > kSamePlace) {
                g_menu.hovered = -1;
            }
            g_menu.tooltipDue = true;
        }

        // review v11 #10: a redraw can put another mark (or none) under a still mouse without a roll-out, so each frame while a
        // node is hovered its hit area is tested at the mouse (one hitTest, only while hovering); off it, the hover, its outline
        // and (through RedrawTooltip) the tooltip go
        void DropStaleHover(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget)
        {
            if (g_menu.hovered < 0) return;
            auto panel = BlockClip(a_widget, Layout::kTree);
            auto content = panel.IsDisplayObject() ? Child(panel, "content") : RE::GFxValue{};
            const auto name = std::format("n{}", g_menu.hovered);
            auto clip = content.IsDisplayObject() ? Child(content, name.c_str()) : RE::GFxValue{};
            auto hit = clip.IsDisplayObject() ? Child(clip, "hit") : RE::GFxValue{};
            RE::GFxValue x, y, result;
            if (!hit.IsDisplayObject() || !a_movie->GetVariable(&x, "_root._xmouse") || !x.IsNumber() ||
                !a_movie->GetVariable(&y, "_root._ymouse") || !y.IsNumber()) {
                return;
            }
            std::array<RE::GFxValue, 3> args{ x, y, RE::GFxValue(true) };  // shapeFlag: the mark's polygon, not its box
            if (!hit.Invoke("hitTest", &result, args.data(), args.size()) || !result.IsBool() || result.GetBool()) return;
            ShowFocus(clip, false);
            g_menu.hovered = -1;
        }

        void RedrawTree(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget, const Spells::Snapshot& a_snapshot)
        {
            const auto started = std::chrono::steady_clock::now();  // the redraw's time goes in its log line (GPT design review #7)
            std::optional<std::pair<double, double>> hoveredAt;  // the hovered node's place before this redraw
            for (const auto& placed : g_menu.drawn) {
                if (placed.index == g_menu.hovered) hoveredAt = { placed.sx, placed.sy };
            }
            g_menu.shown.clear();
            g_menu.drawn.clear();
            auto content = Content(a_widget, Layout::kTree);
            if (!content.IsDisplayObject()) {
                RecheckHover(hoveredAt);
                return;
            }
            const auto& settings = Settings::Get();
            const auto treeRect = BlockRect(Layout::kTree);
            const auto frame = Settings::TreeContent(treeRect);
            content.SetMember("_x", frame.cx);
            content.SetMember("_y", frame.cy);
            // T7.1: a list selection without a tree node keeps the previous centre (design), drawn dimmed so it is not taken for
            // the selection's tree; the note goes in a clip of its own over the content, at full strength
            content.SetMember("_alpha", g_menu.notInTree ? kDimTreeAlpha : Draw::kAlphaFull);
            auto treePanel = BlockClip(a_widget, Layout::kTree);
            if (treePanel.IsDisplayObject()) Draw::Remove(treePanel, "overlay");
            if (g_menu.notInTree && treePanel.IsDisplayObject()) {
                auto overlay = Draw::Clip(treePanel, "overlay");
                // in the title strip, right of the title: the reading size as far as the strip's height allows
                const auto text = Text(Str::kTreeNotInTree);
                const auto note = overlay.IsDisplayObject() ?
                                      FitLine(a_movie, overlay, "note", kPad, kPad / 2, treeRect.Width() - 2 * kPad,
                                          std::min(Size(kNoteSize), (kTitleH - kGutter) / g_menu.lineFirst), text, kDimText, "right") :
                                      RE::GFxValue{};
                Q2State(Str::kTreeNotInTree, text, &note);
            }
            if (!Spells::Get() || g_session.centre < 0) {
                const Str id = Spells::Get() ? Str::kTreeNoCentre : Str::kTreeNoTree;
                const double size = Size(kRowSize);
                const auto empty = FitLines(a_movie, content, "empty", -frame.halfW, -FieldH(size), std::max(0.0, 2 * frame.halfW), size, 2,
                    Text(id), kDimText, "center");
                Q2State(id, Text(id), &empty);
                RecheckHover(hoveredAt);
                return;
            }
            const auto& centre = Data().tree.nodes[g_session.centre];
            const auto style = CurrentStyle();
            // the legend in the centre's school shape and colour (T18), drawn again when the school changes
            if (treePanel.IsDisplayObject() && g_menu.legendSchool != centre.school) {
                DrawLegend(a_movie, treePanel, treeRect.Width(), treeRect.Height(), centre.school);
            }
            if (!style->SchoolShown(centre.school)) {
                // T17: HoM's schoolVisibility hides this school - nothing of it is drawn
                const double size = Size(kRowSize);
                const auto empty = FitLines(a_movie, content, "empty", -frame.halfW, -FieldH(size), std::max(0.0, 2 * frame.halfW), size, 2,
                    Text(Str::kTreeNoCentre), kDimText, "center");
                Q2State("tree_school_hidden", std::format("{} - \"{}\"", centre.school, Text(Str::kTreeNoCentre)), &empty);
                if (g_menu.cutLogged.insert("hidden|" + centre.school).second) {
                    logger::info("Widget: tree - school {} is hidden by Heart of Magic's schoolVisibility: nothing of it drawn", centre.school);
                }
                RecheckHover(hoveredAt);
                return;
            }
            const double scale = MarkScale();
            const double maxReach = HomStyle::MaxReach(scale, style->tree.availableRing, settings.treeScale);
            // the learning target of the centre's school (kept under the cap, its path drawn: spec v10 T3, T8)
            const int target = Spells::FindNode(a_snapshot.Target(centre.school), centre.school);
            TreeData::View view;
            view.scale = settings.treeScale;  // HoM's saved x/y, not turned (spec v11 T10 / R12)
            view.halfW = frame.halfW;
            view.halfH = frame.halfH;
            view.margin = maxReach;  // a node's whole mark inside the window
            view.maxSteps = settings.maxSteps;
            view.maxNodes = settings.maxNodes;
            for (const int keep : { g_menu.selected, target }) {
                if (keep >= 0 && keep != g_session.centre) view.keep.push_back(keep);
            }
            const auto window = TreeData::Query(Data().tree, g_session.centre, view);
            const auto fullList = TreeData::StructuralNeighbours(Data().tree, g_session.centre);
            const std::unordered_set<int> full(fullList.begin(), fullList.end());
            const auto path = target >= 0 && target != g_session.centre ? TreeData::StructuralPath(Data().tree, g_session.centre, target) :
                                                                         std::vector<int>{};
            std::set<std::pair<int, int>> pathLinks;
            for (std::size_t i = 1; i < path.size(); ++i) pathLinks.insert(std::minmax(path[i - 1], path[i]));
            const std::unordered_set<int> pathNodes(path.begin(), path.end());

            std::vector<TreeNode> nodes;
            for (const auto& placed : window.nodes) {
                const auto look = Spells::LookOf(a_snapshot, placed.index);
                const bool clickable = placed.steps >= 0 || Shown(look.state);
                TreeNode node{ placed, look, HomStyle::DisplayOf(BehaviourOf(look.state), placed.index == target), clickable, full.contains(placed.index) };
                node.onPath = pathNodes.contains(placed.index);
                nodes.push_back(node);
                g_menu.drawn.push_back(placed);
                if (clickable) g_menu.shown.push_back(placed);
            }
            // keep the clicked node only while it is drawn and clickable
            if (g_menu.selected >= 0 &&
                std::ranges::none_of(g_menu.shown, [](const TreeData::PlacedNode& a_placed) { return a_placed.index == g_menu.selected; })) {
                g_menu.selected = -1;
            }
            // before the marks are drawn: a hovered node that moved is no longer hovered, so its outline is drawn hidden (R6)
            RecheckHover(hoveredAt);
            int fullCount = 0;
            for (auto& node : nodes) {
                const auto& data = Data().tree.nodes[node.placed.index];
                // the focus look on the clicked spell, else on the centre (then the spell chosen in the list is the selection)
                node.focus = node.placed.index == (g_menu.selected >= 0 ? g_menu.selected : g_session.centre);
                node.reach = HomStyle::Reach(HomStyle::SizeOf(node.display, style->Mystery(), data.skillLevel, data.tier, node.focus, style->tree.availableRing),
                    data.school, scale, settings.treeScale);
                fullCount += node.full ? 1 : 0;
            }

            std::unordered_map<int, const TreeNode*> byIndex;
            for (const auto& node : nodes) byIndex.emplace(node.placed.index, &node);
            const auto edges = DrawEdges(content, window, byIndex, pathLinks, target, *style);
            // the marks over the links: the dimmed ones first, then the full ones, the selected one last (HoM draws the selected
            // and hovered spells after the rest)
            std::vector<std::size_t> drawOrder(nodes.size());
            for (std::size_t i = 0; i < drawOrder.size(); ++i) drawOrder[i] = i;
            std::ranges::stable_sort(drawOrder, {}, [&](std::size_t a_i) { return (nodes[a_i].full ? 1 : 0) + (nodes[a_i].focus ? 2 : 0); });
            for (const auto i : drawOrder) nodes[i].clip = DrawNode(a_movie, content, nodes[i], *style);
            CheckSpacing(nodes);

            std::vector<LabelBox> labels;  // every drawn mark first: no label over a node
            for (const auto& node : nodes) labels.push_back(NodeBox(node));
            // then every drawn link, so a label keeps off a line too (review v10), half its width each side
            for (const auto& line : edges.lines) {
                const auto link = LayoutMath::LinkBoxes(line.ax, line.ay, line.bx, line.by, maxReach, kLinkBoxStep, line.width / 2);
                labels.insert(labels.end(), link.begin(), link.end());
            }
            {
                // and no label on the legend strip (DrawLegend's row at its size), which the window's clamp alone can graze
                // (review round 1 #5)
                const double legendMid = treeRect.Height() - Settings::kTreeLegendH / 2 - kPad / 2 - frame.cy;
                const double legendHalf = FieldH(kLegendSize) / 2;
                labels.push_back({ -frame.halfW - kPad, legendMid - legendHalf, frame.halfW + kPad, legendMid + legendHalf });
            }
            // The badges first (build 6 review round 2 #1): their arrows and texts go into `labels` before any node label, so the
            // labels keep off them. The centre's direct neighbours outside the window (spec v10 T7); the ones inside it over the
            // cap, without x/y or of another school are only logged
            const auto badges = LayoutMath::Badges(window.outside);
            const bool noLinks = Data().tree.adjacent[g_session.centre].empty();
            bool badgesDrawn = false;
            if (badges.total > 0) {
                badgesDrawn = DrawBadges(a_movie, content, badges, frame, labels);
                if (badgesDrawn) {
                    Q2State(Str::kTreeExit, Fill(Str::kTreeExit, { { "{n}", std::to_string(badges.total) } }));
                } else if (g_menu.cutLogged.insert("exits|no room").second) {
                    logger::info("Widget: {} tree badges not drawn - the tree window ({:.0f} x {:.0f} inside) has no room for them", badges.total,
                        2 * frame.halfW, 2 * frame.halfH);
                }
            } else if (noLinks) {
                const double size = Size(kNoteSize), top = frame.halfH - FieldH(size);
                const auto note = FitLine(a_movie, content, "nolinks", -frame.halfW, top, std::max(0.0, 2 * frame.halfW), size,
                    Text(Str::kTreeNoLinks), kDimText, "center");
                labels.push_back({ -frame.halfW, top, frame.halfW, top + FieldH(size) });
                Q2State(Str::kTreeNoLinks, Text(Str::kTreeNoLinks), &note);
            }

            // Labels in rank order, each only at a clear place (spec v10 T5); none at all when HoM's showNodeNames is off (T15)
            std::vector<std::size_t> order(nodes.size());
            for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
            std::ranges::stable_sort(order, [&](std::size_t a_lhs, std::size_t a_rhs) {
                return LabelRank(nodes[a_lhs], target) < LabelRank(nodes[a_rhs], target);
            });
            int labelsDrawn = 0, labelsDropped = 0, labelsNone = 0;
            KeptPlace centreSide, selectedSide, targetSide;
            for (const auto i : style->showNodeNames ? order : std::vector<std::size_t>{}) {
                auto& node = nodes[i];
                if (!node.clip.IsDisplayObject()) continue;
                const int index = node.placed.index;
                const bool defaultLeft = index != g_session.centre && node.placed.sx > 0;
                const auto side = PlaceNodeLabel(a_movie, node, defaultLeft, LabelRank(node, target) <= kKeptRank ? kKeptFarSteps : 0, frame,
                    *style, labels);
                ++(side.none ? labelsNone : side.drawn ? labelsDrawn : labelsDropped);
                if (index == g_session.centre) {
                    centreSide = side;
                } else if (index == g_menu.selected) {
                    selectedSide = side;
                } else if (index == target) {
                    targetSide = side;
                }
            }
            if (const auto hidden = std::ranges::find_if(nodes, [&](const TreeNode& a_node) {
                    return HomStyle::Reveal(a_node.display, a_node.look.percent, style->revealName, style->cheatMode) == HomStyle::Name::kHidden;
                });
                hidden != nodes.end() && style->showNodeNames) {
                Q2State("tree_name_hidden", std::format("{} - \"{}\" ({:.0f}% under revealName {})", Spells::NodeName(hidden->placed.index),
                    HomStyle::kHiddenName, hidden->look.percent, style->revealName));
            }
            const auto labelNote = [](const char* a_what, const KeptPlace& a_side) {
                return a_side.none  ? std::format(", {} label none (HoM shows no name)", a_what) :
                       a_side.drawn ? std::format(", {} label {} (overlap at its side {:.1f})", a_what, PlaceName(a_side.place), a_side.firstOverlap) :
                                      std::format(", {} label dropped (least overlap {:.1f})", a_what, a_side.overlap);
            };
            const bool selectedApart = g_menu.selected >= 0 && g_menu.selected != g_session.centre;
            const bool targetApart = target >= 0 && target != g_session.centre && target != g_menu.selected;
            const auto clickable = std::ranges::count_if(nodes, [](const TreeNode& a_node) { return a_node.clickable; });
            logger::info("Widget: tree redraw centre {} {:08X} ({}), scale {} marks x{} (HoM sizes x {:.3f}, preset {}): in window {} -> drawn {} "
                         "({} clickable; full {} / dim {} at x{}), capped {}; links dim {} locked {} frontier {} learned {} centre's {} path {} "
                         "chains {} (not drawn: {}); outside {} -> badges {} (centre's neighbours apart: {} capped, {} without x/y, {} other "
                         "school){}; labels {} drawn / {} dropped / {} none{}{}{}{}; target {}, path {} node(s), snapshot {}, {:.1f} ms",
                Spells::NodeName(g_session.centre), Data().gameIds[g_session.centre], centre.school, view.scale, settings.treeMarkScale, scale,
                style->presetFile.empty() ? "DEFAULTS" : style->presetId, window.candidatesBeforeCap, nodes.size(), clickable, fullCount,
                nodes.size() - static_cast<std::size_t>(fullCount), style->DimFactor(), window.candidatesBeforeCap - static_cast<int>(nodes.size()),
                edges.dim, edges.locked, edges.frontier, edges.unlocked, edges.selected, edges.learning, edges.chains, edges.hidden,
                window.outside.size(), badgesDrawn ? "drawn" : badges.total > 0 ? "no room" : "none", window.neighboursCapped, window.neighboursNoXY,
                window.neighboursOtherSchool, noLinks ? ", no links" : "", labelsDrawn, labelsDropped, labelsNone,
                style->showNodeNames ? "" : " (showNodeNames off)", labelNote("centre", centreSide),
                selectedApart ? labelNote("selected", selectedSide) : std::string{}, targetApart ? labelNote("target", targetSide) : std::string{},
                target >= 0 ? Spells::NodeName(target) : std::string("none"), path.size(), a_snapshot.epoch,
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
            if (g_menu.notInTree) logger::info("Widget: tree drawn dimmed to {:.0f}% (the selection is not in the tree)", kDimTreeAlpha);
            RecheckHover(hoveredAt);
        }

        // ---------------------------------------------------------------------
        // Progress card
        // ---------------------------------------------------------------------

        // A line of text with the table entry it came from (its key names the Q2 state in the log)
        struct Line
        {
            Str id;
            std::string text;
        };

        // What to do next in the active school (Q2), from its numbers: the line in place of the learning target when there is
        // none, and the line under the target when there is one. Precedence: candidates (counted exactly as the next-learnable
        // key walks them), learnable spells without a tree position, spells at full XP not learned yet, spells learned early
        // and not mastered, locked spells (prerequisites missing), spells of unknown state, every spell mastered.
        Line NextStepLine(const SchoolInfo& a_info, bool a_hasTarget)
        {
            const auto& t = a_info.tally;
            const auto count = [](int a_n) { return std::to_string(a_n); };
            if (t.candidates > 0) {
                const Str id = a_hasTarget ? Str::kProgressCandidates : Str::kProgressNoneCandidates;
                return { id, Fill(id, { { "{n}", count(t.candidates) }, { "{key}", KeyLabel(Settings::Get().keyNext) } }) };
            }
            if (t.available > 0) return { Str::kProgressCandidatesNoXY, Fill(Str::kProgressCandidatesNoXY, { { "{n}", count(t.available) } }) };
            if (t.ready > 0) return { Str::kProgressReady, Fill(Str::kProgressReady, { { "{n}", count(t.ready) } }) };
            if (t.learning > 0) return { Str::kProgressLearningEarly, Fill(Str::kProgressLearningEarly, { { "{n}", count(t.learning) } }) };
            if (t.locked > 0) return { Str::kProgressNoneNothing, Text(Str::kProgressNoneNothing) };
            if (t.unknown > 0) return { Str::kProgressUnknown, Fill(Str::kProgressUnknown, { { "{n}", count(t.unknown) } }) };
            if (a_hasTarget) return { Str::kProgressNoOther, Text(Str::kProgressNoOther) };
            if (t.mastered > 0) return { Str::kProgressAllMastered, Text(Str::kProgressAllMastered) };
            return { Str::kProgressNoSchoolTree, Fill(Str::kProgressNoSchoolTree, { { "{school}", SchoolText(a_info.school) } }) };
        }

        void RedrawProgress(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget, const Spells::Snapshot& a_snapshot)
        {
            auto content = Content(a_widget, Layout::kProgress);
            g_menu.rowAligned = false;
            g_menu.rowAlignNote = "no progress card";
            if (g_session.school.empty() || !content.IsDisplayObject()) return;
            const auto rect = BlockRect(Layout::kProgress);
            const double w = rect.Width() - 2 * kPad;
            content.SetMember("_x", kPad);

            const double titleSize = Size(kSchoolSize), hintSize = Size(kHintSize), bottom = rect.Height() - kCardBottom;
            // Row 2 (spec v6 L2): the school title's baseline on the node card name's. Both start kCardTop below their card's top
            // in the same font, so the title moves down by the tops' difference plus the ascent's difference of the two sizes
            // (the size-14 title under a size-19 name: about 4.5). Applied only when that lands within one title size; a card
            // layout whose tops are far apart is not aligned.
            const auto nodeCard = BlockRect(Layout::kNodeCard);
            const double shift = nodeCard.top - rect.top + g_menu.ascent * (kNameSize - titleSize);
            double offset = shift >= 0 && shift <= titleSize ? shift : 0.0;
            if (g_menu.cutLogged.insert(std::format("l2|{:.2f}", shift)).second) {
                logger::info("Widget: L2 progress card first line (size {:.1f}) {} - node card name size {:.0f}, ascent {:.3f} of the size ({}), card "
                             "tops {:.0f} / {:.0f}",
                    titleSize, offset > 0 || shift == 0 ? std::format("moved down {:.2f} to the name's baseline", offset) :
                                                          std::format("not aligned (it would move {:.2f})", shift),
                    kNameSize, g_menu.ascent, g_menu.ascentMeasured ? "measured" : "NOT measured, frame value", rect.top, nodeCard.top);
            }
            g_menu.rowAligned = offset > 0 || shift == 0;
            g_menu.rowAlignNote = g_menu.rowAligned ? "" : "the card tops are too far apart";
            auto title = FitLine(a_movie, content, "school", 0, kCardTop + offset, w, titleSize,
                Fill(Str::kProgressTitle, { { "{school}", SchoolText(g_session.school) } }), kTitleColor);
            double y = kCardTop + offset + FieldH(titleSize);
            // The alignment gives way when what follows would not fit even at kMinFitSize (review round 1 #2: the last line
            // shrinks first): the title goes back up (logged once per menu)
            const auto giveUpOffset = [&](double a_left, const char* a_what) {
                title.SetMember("_y", kCardTop);
                g_menu.rowAligned = false;
                g_menu.rowAlignNote = "the offset gave way (no room)";
                if (g_menu.cutLogged.insert("progress|l2").second) {
                    logger::info("Widget: L2 progress card drops its baseline offset {:.2f} - {} does not fit even at size {:.0f} ({:.1f} left "
                                 "under the title)",
                        offset, a_what, kMinFitSize, a_left);
                }
                offset = 0.0;
            };
            const auto dropOffset = [&](double a_need, const char* a_what) {
                if (offset <= 0 || y + a_need <= bottom || !title.IsDisplayObject()) return;
                const double left = bottom - y;
                y -= offset;
                giveUpOffset(left, a_what);
            };
            const auto dropTitle = [&](double a_need, const char* a_what) {
                title.Invoke("removeTextField");
                title = RE::GFxValue{};
                g_menu.rowAligned = false;
                g_menu.rowAlignNote = "the school title gave way";
                if (g_menu.cutLogged.insert("progress|title").second) {
                    logger::info("Widget: Q4 progress card drops its school title - {} needs {:.1f} of the card's {:.1f}", a_what, a_need, bottom - kCardTop);
                }
            };
            // Nothing is drawn past the card (review round 2 #1: with a tall font and large list rows even size 8 ended at 106 of
            // 102): when what has to follow does not fit under the school title, the title goes; an optional last line that does
            // not fit at kMinFitSize is left out. Both logged once per menu.
            const auto makeRoom = [&](double a_need, const char* a_what) {
                dropOffset(a_need, a_what);
                if (y + a_need <= bottom || !title.IsDisplayObject()) return;
                dropTitle(a_need, a_what);
                y = kCardTop;
            };
            // no learning target to show: one line (up to two rows) that says what to do, in place of the target and its bar,
            // smaller (one row when two do not fit) when the card has no room for it at the reading size (review round 1 #4);
            // the baseline offset gives way only when the message does not fit even at kMinFitSize (makeRoom)
            const auto message = [&](const Line& a_line) {
                makeRoom(FieldH(kMinFitSize), "the message");
                int lines = kMessageLines;
                double size = SizeFor(hintSize, lines, bottom - y, nullptr);
                if (FieldH(size, lines) > bottom - y) {
                    lines = 1;
                    size = SizeFor(hintSize, lines, bottom - y, "progress message");
                }
                const auto field = FitLines(a_movie, content, "message", 0, y, w, size, lines, a_line.text, kWhite);
                Q2State(a_line.id, a_line.text, &field);
            };
            if (!Spells::Get()) return message({ Str::kProgressNoTree, Text(Str::kProgressNoTree) });
            // the widget is attached only once HoM's API is there, so this is a guard (review round 1 #3)
            if (!Hom::Ready()) return message({ Str::kProgressNoHom, Text(Str::kProgressNoHom) });
            if (!Spells::SchoolNamed(g_session.school)) {
                return message({ Str::kProgressNoSchoolTree, Fill(Str::kProgressNoSchoolTree, { { "{school}", SchoolText(g_session.school) } }) });
            }
            // "loading" only while no snapshot has had the school for less than kNoDataFrames frames (a refresh for it is asked
            // for every kPollFrames frames); after that the card says the data did not arrive (WatchSnapshotCover)
            const auto it = a_snapshot.schools.find(g_session.school);
            if (it == a_snapshot.schools.end()) {
                return message(g_menu.noData ? Line{ Str::kProgressNoData, Text(Str::kProgressNoData) } :
                                               Line{ Str::kProgressNotReady, Text(Str::kProgressNotReady) });
            }
            const auto& info = SchoolInfoOf(a_snapshot);
            const auto& state = it->second;
            if (!state.target) return message(NextStepLine(info, false));

            // the learning target: name and share, the bar with its XP, and the next step under it
            const int targetNode = Spells::FindNode(state.target, g_session.school);
            const auto* form = RE::TESForm::LookupByID(state.target);
            const std::string name = targetNode >= 0           ? Spells::NodeName(targetNode) :
                                     form && form->GetName()   ? std::string(form->GetName()) :
                                                                 std::format("{:08X}", state.target);
            const auto pct = std::format("{:.0f}", state.percent);
            // where each line goes and what gives way (LayoutMath::PlanProgress: the hint shrinks, then the offset goes, then the
            // hint, then the title; the same function tools/magic-menu-preview-check runs)
            const auto plan = LayoutMath::PlanProgress(Metrics(),
                { rect.Height(), kCardTop, kCardBottom, titleSize, kTargetSize, kXpSize, kBarH, hintSize, kMinFitSize, kFitStep, offset });
            if (plan.offsetDropped && title.IsDisplayObject()) giveUpOffset(bottom - (kCardTop + offset + FieldH(titleSize)), "the next step");
            if (!plan.title && title.IsDisplayObject()) dropTitle(plan.targetH + plan.barRowH, "the learning target and its bar");
            const double targetH = plan.targetH, barRowH = plan.barRowH, xpH = FieldH(kXpSize);
            y = plan.targetY;
            auto target = Draw::Text(a_movie, content, "target", 0, y, w, targetH, "", kWhite, kTargetSize);
            const auto compose = [&](const std::string& a_name) { return Fill(Str::kProgressTarget, { { "{spell}", a_name }, { "{pct}", pct } }); };
            if (target.IsDisplayObject()) FitText(a_movie, target, w, kTargetSize, 1, name, compose);
            Q2State(Str::kProgressTarget, compose(name), &target);
            y = plan.barRowY;

            const double barW = std::max(0.0, w - kXpW - kPad), barY = y + (barRowH - kBarH) / 2;
            Draw::NoLine(content);
            Draw::Rect(content, 0, barY, barW, kBarH, kBarBack, Draw::kAlphaFull);
            if (state.percent > 0) {
                Draw::Rect(content, 0, barY, barW * std::clamp(state.percent / 100.0, 0.0, 1.0), kBarH, kBlue, Draw::kAlphaFull);
            }
            const auto xp = Fill(Str::kXp, { { "{cur}", std::format("{:.0f}", state.required * state.percent / 100.0f) },
                                               { "{need}", std::format("{:.0f}", state.required) } });
            FitLine(a_movie, content, "xp", barW + kPad, y + (barRowH - xpH) / 2, kXpW, kXpSize, xp, kWhite, "right");
            y = plan.hintY;
            // the list's spell has no tree node: said here instead of the next step (the tree window says it too)
            const Line next = g_menu.notInTree ? Line{ Str::kProgressNotInTree, Text(Str::kProgressNotInTree) } : NextStepLine(info, true);
            if (!plan.hint) {
                if (g_menu.cutLogged.insert("progress|hint").second) {
                    logger::info("Widget: Q4 progress card leaves out its last line '{}' - {:.1f} left, it needs {:.1f} even at size {:.0f}",
                        next.text, bottom - y, FieldH(kMinFitSize), kMinFitSize);
                }
                const RE::GFxValue none;
                Q2State(next.id, next.text, &none);  // logged as "(no field)": the state was reached, its line not shown
                return;
            }
            if (plan.hintSize < hintSize && g_menu.cutLogged.insert(std::format("progress|hint {:.1f}", plan.hintSize)).second) {
                logger::info("Widget: Q4 progress card hint at size {:.1f} (reading size {:.1f}) - {:.1f} left under the bar{}", plan.hintSize, hintSize,
                    bottom - y, offset > 0 ? std::format(", the L2 offset {:.2f} kept", offset) : std::string{});
            }
            const auto hint = FitLine(a_movie, content, "hint", 0, y, w, plan.hintSize, next.text, kLabelColor);
            Q2State(next.id, next.text, &hint);
        }

        // ---------------------------------------------------------------------
        // Node card (always shown; 2026-10-24 user layout, mockup v8)
        // ---------------------------------------------------------------------

        // The node card's state words; on the button a target says only that it is the target
        Line StateLine(State a_state, float a_percent, bool a_button)
        {
            const auto pct = std::format("{:.0f}", a_percent);
            switch (a_state) {
            case State::kTarget:
                return a_button ? Line{ Str::kNodeIsTarget, Text(Str::kNodeIsTarget) } :
                                  Line{ Str::kNodeTargetPct, Fill(Str::kNodeTargetPct, { { "{pct}", pct } }) };
            case State::kLearning: return { Str::kNodeLearningPct, Fill(Str::kNodeLearningPct, { { "{pct}", pct } }) };
            case State::kAvailable: return { Str::kNodeCanLearn, Text(Str::kNodeCanLearn) };
            case State::kReady: return { Str::kNodeReady, Text(Str::kNodeReady) };
            case State::kMastered: return { Str::kNodeMastered, Text(Str::kNodeMastered) };
            case State::kLocked: return { Str::kNodeLocked, Text(Str::kNodeLocked) };
            default: return { Str::kNodeUnknown, Text(Str::kNodeUnknown) };
            }
        }

        // ---------------------------------------------------------------------
        // Tree tooltip (spec v10 T6)
        // ---------------------------------------------------------------------

        // The hovered node's name, school and level, and state (the node card's words) in a box beside its mark, inside the tree
        // panel. Its own clip on the tree panel, over the content: made again after each tree redraw, so it stays on top, and not
        // dimmed with a tree that is not the selection's (T7.1); hidden with the panel. Drawn only when the hovered node changes
        // or the tree was drawn again; it never marks the tree dirty.
        void RedrawTooltip(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget, const Spells::Snapshot& a_snapshot)
        {
            const int hovered = g_menu.layout.active ? -1 : g_menu.hovered;  // Task 4: no tooltip in the layout mode
            if (hovered == g_menu.tooltipFor && !g_menu.tooltipDue) return;
            g_menu.tooltipDue = false;
            g_menu.tooltipFor = -1;
            auto panel = BlockClip(a_widget, Layout::kTree);
            if (!panel.IsDisplayObject()) return;
            Draw::Remove(panel, "tooltip");
            const auto it = std::ranges::find_if(g_menu.drawn, [&](const TreeData::PlacedNode& a_placed) { return a_placed.index == hovered; });
            if (hovered < 0 || !Spells::Get() || it == g_menu.drawn.end()) return;
            g_menu.tooltipFor = hovered;
            const auto rect = BlockRect(Layout::kTree);
            const auto frame = Settings::TreeContent(rect);
            const auto& node = Data().tree.nodes[hovered];
            const auto look = Spells::LookOf(a_snapshot, hovered);
            const auto school = SchoolText(node.school);
            const auto level = LevelName(node.skillLevel);
            // the name by HoM's reveal rule (spec v11 T16): "???" for a locked spell and for one HoM shows as "???"; the school,
            // level and state lines stay
            const auto style = CurrentStyle();
            const std::array<std::pair<std::string, std::uint32_t>, 3> lines{ {
                { ShownName(hovered, a_snapshot), kWhite },
                { level.empty() ? school : Fill(Str::kNodeSub, { { "{school}", school }, { "{level}", level } }), kTitleColor },
                { StateLine(look.state, look.percent, false).text, kLabelColor },
            } };
            const double size = Size(kLabelSize), lineH = FieldH(size);
            const double w = std::min(kTooltipW, rect.Width() - 2 * kPad), h = static_cast<double>(lines.size()) * lineH + 2 * kTooltipPad;
            // right of the mark, else left of it, centred on its height; with room on neither side (review v10: the centre node of a
            // 260-wide tree, whose clamped tooltip covered it) above it, else below it, centred on it. Inside the panel (below the
            // title, within the padding)
            const double nx = frame.cx + it->sx, ny = frame.cy + it->sy, off = HomStyle::MaxReach(MarkScale(), style->tree.availableRing, Settings::Get().treeScale) + kTooltipGap;
            double x = nx + off, y = ny - h / 2;
            if (x + w > rect.Width() - kPad) x = nx - off - w;
            if (x < kPad) {
                x = nx - w / 2;
                y = ny - off - h >= kTitleH ? ny - off - h : ny + off;
            }
            x = std::clamp(x, kPad, std::max(kPad, rect.Width() - kPad - w));
            y = std::clamp(y, kTitleH, std::max(kTitleH, rect.Height() - kPad - h));
            auto tip = Draw::Clip(panel, "tooltip");
            if (!tip.IsDisplayObject()) return;
            tip.SetMember("_x", x);
            tip.SetMember("_y", y);
            Draw::Box(tip, w, h, kPanelFill, kTooltipAlpha, kPanelEdge, kEdgeWidth);
            for (std::size_t i = 0; i < lines.size(); ++i) {
                const auto name = std::format("t{}", i);
                FitLine(a_movie, tip, name.c_str(), kTooltipPad, kTooltipPad + static_cast<double>(i) * lineH, w - 2 * kTooltipPad, size,
                    lines[i].first, lines[i].second);
            }
            Q2State("tree_tooltip", std::format("{} - {}", lines[0].first, lines[2].first));
        }

        enum class Mark
        {
            kNone,   // a heading: from the left edge
            kCheck,  // a drawn check, then the text
            kCross,  // a drawn cross, then the text
            kSpace   // no mark, the text lined up with the marked rows
        };

        struct Row
        {
            std::string name;           // the whole text, or with a format the {name} it fills (cut first when the row is too long)
            std::optional<Str> format;
            std::uint32_t color;
            Mark mark;
        };

        // One text field per line (two fields placed by an estimated width overlapped on a Korean name, game run 2026-10-25)
        std::vector<Row> RequirementRows(const TreeData::Node& a_node, const Spells::Snapshot& a_snapshot)
        {
            std::vector<Row> rows;
            // a state the snapshot does not have yet is "checking", never a cross (game run 2: a cross from missing data)
            const auto add = [&](std::uint32_t a_saved) {
                const auto index = Spells::FindSaved(a_saved, a_node.school).first;
                // each prerequisite named by its own state (review v11 #1; spellCardNeeds.js:85)
                std::string name = index >= 0 ? ShownName(index, a_snapshot) : std::format("{:08X}", a_saved);
                switch (Spells::PrereqOf(a_snapshot, a_saved, a_node.school)) {
                case Spells::Prereq::kMastered: rows.push_back({ std::move(name), Str::kReqMastered, kOk, Mark::kCheck }); break;
                case Spells::Prereq::kNotMastered: rows.push_back({ std::move(name), Str::kReqNotMastered, kNotYet, Mark::kCross }); break;
                case Spells::Prereq::kUnknown: rows.push_back({ std::move(name), Str::kReqChecking, kDimText, Mark::kSpace }); break;
                case Spells::Prereq::kNotInGame: rows.push_back({ std::move(name), Str::kReqNotInGame, kDimText, Mark::kSpace }); break;
                }
            };
            if (!a_node.hard.empty()) {
                rows.push_back({ Text(Str::kReqAll), std::nullopt, kTitleColor, Mark::kNone });
                for (const auto saved : a_node.hard) add(saved);
            }
            if (!a_node.soft.empty() && a_node.softNeeded > 0) {
                rows.push_back({ Fill(Str::kReqSome, { { "{n}", std::to_string(a_node.softNeeded) } }), std::nullopt, kTitleColor, Mark::kNone });
                for (const auto saved : a_node.soft) add(saved);
            }
            if (rows.empty()) rows.push_back({ Text(Str::kReqNone), std::nullopt, kTitleColor, Mark::kNone });
            return rows;
        }

        // The rows from a_top down to a_bottom at the reading size; the last row that fits says how many more there are
        void DrawRows(RE::GFxMovieView* a_movie, RE::GFxValue& a_content, std::vector<Row> a_rows, double a_w, double a_top, double a_bottom)
        {
            const double size = Size(kRowSize), h = FieldH(size), pitch = LineH(size) + kGutter / 2;
            const auto fit = static_cast<std::size_t>(std::max(0.0, std::floor((a_bottom - a_top - (h - pitch)) / pitch)));
            if (fit == 0) {
                if (!a_rows.empty() && g_menu.cutLogged.insert("rows|no room").second) {
                    logger::info("Widget: Q4 node card has no room for its {} requirement line(s) ({:.0f} high, one needs {:.0f})", a_rows.size(),
                        a_bottom - a_top, h);
                }
                return;
            }
            if (a_rows.size() > fit) {
                // a heading left as the last shown line, with none of its spells under it, goes too (review round 2 #3); "N more"
                // counts the spells left out, not their heading lines (review round 1 #5)
                auto keep = fit - 1;
                while (keep > 0 && !a_rows[keep - 1].format) --keep;
                const auto hidden = std::count_if(a_rows.begin() + static_cast<std::ptrdiff_t>(keep), a_rows.end(),
                    [](const Row& a_row) { return a_row.format.has_value(); });
                a_rows.resize(keep);
                if (hidden > 0) {
                    a_rows.push_back({ Fill(Str::kReqMore, { { "{n}", std::to_string(hidden) } }), std::nullopt, kDimText, Mark::kNone });
                }
            }
            const double markSize = size * kMarkShare, textX = markSize + kMarkGap;
            for (std::size_t i = 0; i < a_rows.size() && i < fit; ++i) {
                const auto& row = a_rows[i];
                const double y = a_top + pitch * static_cast<double>(i);
                const double x = row.mark == Mark::kNone ? 0.0 : textX;
                const double markY = y + h / 2 - markSize / 2;
                if (row.mark == Mark::kCheck) Draw::Check(a_content, 0, markY, markSize, row.color);
                if (row.mark == Mark::kCross) Draw::Cross(a_content, 0, markY, markSize, row.color);
                const auto name = std::format("row{}", i);
                auto field = Draw::Text(a_movie, a_content, name.c_str(), x, y, a_w - x, h, "", row.color, size);
                if (!field.IsDisplayObject()) continue;
                if (row.format) {
                    FitText(a_movie, field, a_w - x, size, 1, row.name,
                        [&](const std::string& a_name) { return Fill(*row.format, { { "{name}", a_name } }); });
                } else {
                    FitWhole(a_movie, field, a_w - x, size, 1, row.name);
                }
            }
        }

        // a_canSet false: the card shows a node nobody selected (L3's default view), which the button cannot act on
        void DrawButton(RE::GFxMovieView* a_movie, RE::GFxValue& a_content, double a_w, double a_y, const Spells::Look& a_look, bool a_canSet)
        {
            auto button = Draw::Clip(a_content, "button");
            if (!button.IsDisplayObject()) return;
            button.SetMember("_y", a_y);
            const bool enabled = a_canSet && a_look.state == State::kAvailable;
            Draw::LineStyle(button, kEdgeWidth, enabled ? kWhite : kLockedEdge);
            Draw::Rect(button, 0, 0, a_w, kButtonH, enabled ? kButtonFill : kButtonOff, Draw::kAlphaFull);
            const std::string label = enabled ? Fill(Str::kNodeSetTarget, { { "{key}", KeyLabel(Settings::Get().keySetTarget) } }) :
                                                StateLine(a_look.state, a_look.percent, true).text;
            const double size = Size(kButtonSize);
            FitLine(a_movie, button, "label", 0, (kButtonH - FieldH(size)) / 2, a_w, size, label, enabled ? kWhite : kDimText, "center");
            if (enabled) SetHandler(a_movie, button, "onRelease", g_buttonClick.get());
        }

        void RedrawNodeCard(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget, const Spells::Snapshot& a_snapshot)
        {
            auto content = Content(a_widget, Layout::kNodeCard);
            if (!content.IsDisplayObject()) return;
            const auto& settings = Settings::Get();
            const auto rect = BlockRect(Layout::kNodeCard);
            const double w = rect.Width() - 2 * kPad, h = rect.Height();
            content.SetMember("_x", kPad);
            // What the card shows: the clicked (or next-candidate) node; with none, the school's learning target as if it had
            // been clicked (spec v6 L3), only on this card - g_menu.selected stays -1, so the tree neither recentres nor draws a
            // selection for it and no click path (list selection, P2 card, P3 preview) runs
            int shown = Spells::Get() ? g_menu.selected : -1;
            const bool byDefault = shown < 0;
            if (Spells::Get() && byDefault && !g_session.school.empty()) {
                const RE::FormID target = a_snapshot.Target(g_session.school);
                shown = Spells::FindNode(target, g_session.school);
                if (target && shown < 0) {
                    // a target outside the tree: the hint stays (the progress card names it)
                    const auto* form = RE::TESForm::LookupByID(target);
                    Q2State("node_default_target_not_in_tree", std::format("{:08X} {} - the hint is shown", target,
                        form && form->GetName() ? form->GetName() : "(no form)"));
                }
            }
            if (shown < 0) {
                // a short hint at the top (Q2), not a big centred line
                const auto text = Fill(Str::kNodeHint, { { "{key}", KeyLabel(settings.keyNext) } });
                const auto hint = FitLines(a_movie, content, "hint", 0, kCardTop, w, Size(kRowSize), kHintLines, text, kDimText);
                Q2State(Str::kNodeHint, text, &hint);
                return;
            }
            const auto& node = Data().tree.nodes[shown];
            const auto look = Spells::LookOf(a_snapshot, shown);
            double y = kCardTop;
            // the name wraps to two lines, then ends in "..." (Q4); what follows moves down by the lines it took
            // "???" while HoM keeps the name hidden (review v11 #1; spellCardView.js:213-218)
            auto name = FitLines(a_movie, content, "name", 0, y, w, kNameSize, kNameLines, ShownName(shown, a_snapshot), kWhite);
            y += name.IsDisplayObject() ? std::clamp(TextH(name), FieldH(kNameSize), FieldH(kNameSize, kNameLines)) : FieldH(kNameSize);
            const double subSize = Size(kSubSize);
            const auto school = SchoolText(node.school);
            const auto level = LevelName(node.skillLevel);
            FitLine(a_movie, content, "sub", 0, y, w, subSize, level.empty() ? school : Fill(Str::kNodeSub, { { "{school}", school }, { "{level}", level } }),
                kTitleColor);
            y += FieldH(subSize);
            const double stateSize = Size(kStateSize), stateH = FieldH(stateSize);
            {
                // the state in HoM's look (spec v11 T13) at the known size kStateIconR, the node's school shape and colour
                constexpr double kIconScale = kStateIconR / HomStyle::kKnownSize;
                const auto style = CurrentStyle();
                const bool isTarget = shown == Spells::FindNode(a_snapshot.Target(node.school), node.school);
                const double reach = HomStyle::MaxReach(kIconScale, style->tree.availableRing);
                DrawHomMark(content, *style, { node.school, node.themeColor, node.skillLevel, node.tier, node.x, node.y, reach, y + stateH / 2, kIconScale },
                    { HomStyle::DisplayOf(BehaviourOf(look.state), isTarget), style->Mystery(), false, false, 1.0 });
            }
            const auto state = StateLine(look.state, look.percent, false);
            const auto stateField = FitLine(a_movie, content, "state", kStateTextX, y, w - kStateTextX, stateSize, state.text, kWhite);
            Q2State(state.id, state.text, &stateField);
            if (byDefault) Q2State("node_default_target", std::format("{} - {}", Spells::NodeName(shown), state.text));
            if (!byDefault && g_menu.selectedByKey) Q2State("node_next_candidate", std::format("{} - {}", Spells::NodeName(shown), state.text));
            y += stateH + kRuleGap;
            Draw::LineStyle(content, kEdgeWidth, kPanelEdge);
            Draw::MoveTo(content, 0, y);
            Draw::LineTo(content, w, y);

            const double bottomRule = h - kBottomRuleFromBottom;
            DrawRows(a_movie, content, RequirementRows(node, a_snapshot), w, y + kRuleGap, bottomRule - kRuleGap);
            Draw::LineStyle(content, kEdgeWidth, kPanelEdge);
            Draw::MoveTo(content, 0, bottomRule);
            Draw::LineTo(content, w, bottomRule);
            const auto* spell = a_snapshot.Spell(Data().gameIds[shown]);
            const float required = spell ? spell->required : 0.0f;
            FitLine(a_movie, content, "xp", 0, h - kXpFromBottom, w, stateSize,
                Fill(Str::kXp, { { "{cur}", std::format("{:.0f}", required * look.percent / 100.0f) }, { "{need}", std::format("{:.0f}", required) } }),
                kLabelColor, "right");
            DrawButton(a_movie, content, w, h - kButtonFromBottom, look, !byDefault);
        }

        // ---------------------------------------------------------------------
        // Per-frame work
        // ---------------------------------------------------------------------

        // A centre the user chose (list selection, tree click, the next-learnable key)
        void SetCentre(int a_index)
        {
            g_session.provisionalSchool.clear();
            if (a_index == g_session.centre) return;
            g_session.centre = a_index;
            g_menu.refresh = true;
            g_menu.dirty = true;
        }

        // The school's default centre; provisional while the snapshot does not cover the school yet
        void SetDefaultCentre(const std::string& a_school, const Spells::Snapshot& a_snapshot)
        {
            SetCentre(DefaultCentre(a_school, a_snapshot));
            if (!a_snapshot.Covers(a_school)) g_session.provisionalSchool = a_school;
        }

        // A snapshot that covers the provisional centre's school arrived: pick that school's default centre again
        void SettleProvisionalCentre(const Spells::Snapshot& a_snapshot)
        {
            const auto school = g_session.provisionalSchool;
            if (school.empty() || !a_snapshot.Covers(school)) return;
            SetCentre(DefaultCentre(school, a_snapshot));  // clears the provisional mark
            logger::info("Widget: provisional centre of {} settled on {} (snapshot {})", school,
                g_session.centre >= 0 ? Spells::NodeName(g_session.centre) : std::string("(none)"), a_snapshot.epoch);
        }

        void SetSchool(const std::string& a_school)
        {
            if (a_school.empty() || a_school == g_session.school) return;
            g_session.school = a_school;
            g_menu.refresh = true;
            g_menu.dirty = true;
        }

        // A category tab change (design 3.7) is acted on at once
        void WatchTab(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget, const Spells::Snapshot& a_snapshot)
        {
            const auto tab = ReadTab(a_movie);
            if (g_menu.tabFlag && *g_menu.tabFlag == tab.flag) return;
            const bool first = !g_menu.tabFlag.has_value();
            g_menu.tabFlag = tab.flag;
            g_menu.tab = tab.kind;
            g_hide3D = tab.flag == kFlagPowers;  // spec v13 B1: Powers only, not Shouts (64)
            if (g_hide3D && !std::exchange(g_menu.powersHideLogged, true)) logger::info("Widget: Powers tab: 3D hidden (user 2026-10-26)");
            logger::info("Widget: category flag {} ({})", tab.flag,
                tab.kind == Tab::kSchool ? tab.school : tab.kind == Tab::kPowers ? "powers/shouts: no tree" :
                                                tab.kind == Tab::kEffects        ? "active effects: nothing drawn" :
                                                                                   "all/favorites: follows the spell");
            g_menu.tabSchool = tab.kind == Tab::kSchool ? tab.school : std::string{};
            // review v12 #S3: a category change keeps the selection - the school and the centre come from it, and the list is
            // put back on it once it is filled again (WatchReselect). Without the wrap (G3) or a selection: as before
            const bool keep = !first && g_menu.selWrapped && HasSelection();
            if (keep) {
                g_menu.reselectLeft = kReselectWaitFrames + kReselectFrames;
                g_menu.reselectSeq = g_menu.selSeq;
                logger::info("Widget: category change keeps the selection {:08X}{} (school {}, centre kept; the list is put back on it once filled)",
                    g_menu.sel.id, g_menu.cardFromTree ? std::format(", tree card {:08X}", g_menu.cardFromTree) : std::string{},
                    g_session.school.empty() ? "-" : g_session.school);
            } else if (tab.kind == Tab::kSchool) {
                SetSchool(tab.school);
                if (CentreSchool() != tab.school) SetDefaultCentre(tab.school, a_snapshot);
            }
            // blocks plan Task 4: a tab change ends the layout mode as Cancel (the hook and the shield keep the tabs from changing
            // in the mode; whatever got through, the entry layout is put back first)
            if (!first && g_menu.layout.active) {
                g_layoutMode = false;
                LeaveLayoutMode(a_movie, a_widget, false, "the tab change");
            }
            // D6: the tab's hidden blocks close up; the item card's slot moves between the Powers/Shouts tabs and the others, so
            // its transform is worked out again (Rearrange)
            Rearrange(a_movie, a_widget);
            ApplyVisibility(a_widget);
            g_menu.dirty = true;
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
                logger::info("Widget: {} - the right side {}", chosen ? "something chosen" : "nothing chosen (no list row, no tree card)", chosen ? "shows" : "is hidden");
            }
        }

        // spec v12 G2: follows the committed selection (a click, a key, a tree click, the menu's opening), not the list's
        // highlight; a change of it is a new selection even for the same spell (another row, or a press replacing a tree card)
        void WatchSelection(const Spells::Snapshot& a_snapshot)
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
            // review v11 #2: a list choice that is not our click drops the clicked node, so the focus follows the list
            if (!ours) g_menu.selected = -1;
            // a tree click centres on the clicked node itself (a "_dup_" node's twin could push it out of the window);
            // any other tree spell on its non-dup node
            const int node = ours && g_menu.selected >= 0 && Spells::Get() && Data().gameIds[g_menu.selected] == id ? g_menu.selected :
                                                                                                                    Spells::PrimaryNode(id);
            // the school: a school tab's own, else a tree spell's node's, else the spell's magic skill. review v12 #S3: set on a
            // school tab too (WatchTab keeps the earlier selection's school across a category change)
            if (g_menu.tab == Tab::kSchool && !g_menu.tabSchool.empty()) {
                SetSchool(g_menu.tabSchool);
            } else if (g_menu.tab != Tab::kSchool) {
                SetSchool(node >= 0 ? Data().tree.nodes[node].school : Spells::SchoolOf(spell));
            }
            g_menu.notInTree = id && node < 0;
            if (Centrable(node)) {
                SetCentre(node);
            } else if (g_session.centre < 0 || (g_menu.tab == Tab::kSchool && !g_session.school.empty() && CentreSchool() != g_session.school)) {
                // (a school tab's centre is of its school, as WatchTab made it before review v12 #S3 kept it across the change)
                SetDefaultCentre(g_session.school, a_snapshot);
            }
            logger::info("Widget: list selection {:08X} {} (school {}, tree node {}{}{})", id, spell ? spell->GetName() : "(none)",
                g_session.school.empty() ? "-" : g_session.school, node, node >= 0 && !Centrable(node) ? " without x/y" : "",
                ours ? ", from a tree click" : "");
            g_menu.dirty = true;
        }

        void OnNodeClicked(RE::GFxMovieView* a_movie, int a_index, const Spells::Snapshot& a_snapshot)
        {
            if (!Spells::Get() || a_index < 0 || a_index >= static_cast<int>(Data().tree.nodes.size())) return;
            g_menu.selected = a_index;
            g_menu.selectedByKey = false;
            g_menu.notInTree = false;  // the tree shows what was clicked: no longer dimmed (review v7 round 1)
            g_menu.dirty = true;
            // the tree follows what is clicked (user, game run 2): centre on it, learned or not; the list change our
            // selection causes is ours (selectedByUs) and centres on this same node, so nothing moves twice
            if (Centrable(a_index)) SetCentre(a_index);
            if (g_menu.tab != Tab::kSchool) SetSchool(Data().tree.nodes[a_index].school);
            const auto id = Data().gameIds[a_index];
            const auto pick = id ? SelectInList(id) : ListPick{};
            if (pick.took) {
                g_menu.selectedByUs = id;
                // spec v12 S3/G1: a tree click stays a selection; committed here too, since a set to the row the list already
                // had sends no selectionChange for the wrap to see (BasicList.as:341 / ScrollingList.as:328)
                RE::GFxValue itemList;
                if (a_movie->GetVariable(&itemList, kItemListPath)) Commit(EntryAt(itemList, pick.index), pick.index, SelectionRule::Source::kTree);
            } else if (id) {
                // not in the list (or filtered out by the tab): the tree card (P2) shows it; the committed list selection stays
                logger::info("Widget: selection {:08X} by tree (not in the list: the tree card shows it)", id);
            }
            logger::info("Widget: tree click {:08X} {}: in list {}, index {}, member selectedIndex, took {}", id, Spells::NodeName(a_index),
                pick.found ? "yes" : "no", pick.index, pick.took ? "yes" : pick.found ? "no (filtered out by the tab)" : "-");
            FollowClick(a_movie, id, pick, NameShown(a_index, a_snapshot));
        }

        void DoNext(const Spells::Snapshot& a_snapshot)
        {
            if (!Spells::Get() || g_session.school.empty()) {
                logger::info("Widget: next learnable - no school yet");
                return;
            }
            // the same candidates the cards count (Q2): learnable, with a place in the tree (only those can be centred on)
            const auto& info = SchoolInfoOf(a_snapshot);
            const auto& nodes = info.candidates;
            if (nodes.empty()) {
                logger::info("Widget: next learnable - none in {} (snapshot {}): {}", g_session.school, a_snapshot.epoch,
                    Key(NextStepLine(info, false).id));
                return;
            }
            auto next = nodes.front();
            const auto here = std::ranges::find(nodes, g_session.centre);
            if (here != nodes.end() && std::next(here) != nodes.end()) next = *std::next(here);
            SetCentre(next);
            g_menu.selected = next;
            g_menu.selectedByKey = true;
            g_menu.notInTree = false;  // the tree shows the next candidate: no longer dimmed (review v7 round 1)
            g_menu.dirty = true;
            logger::info("Widget: next learnable in {} ({} of them): {} {:08X}", g_session.school, nodes.size(), Spells::NodeName(next),
                Data().gameIds[next]);
        }

        void DoSetTarget(const Spells::Snapshot& a_snapshot)
        {
            if (!Spells::Get() || g_menu.selected < 0) {
                logger::info("Widget: set target - no node selected");
                return;
            }
            const auto& node = Data().tree.nodes[g_menu.selected];
            const auto look = Spells::LookOf(a_snapshot, g_menu.selected);
            if (look.state != State::kAvailable) {
                logger::info("Widget: set target - {} is {}, not learnable", Spells::NodeName(g_menu.selected), Spells::StateText(look.state));
                return;
            }
            Hom::RequestTarget(Data().gameIds[g_menu.selected], node.school, Spells::NodeName(g_menu.selected));
        }

        // Once per menu open and school: how many nodes are available now (the tree-side availability rule)
        void LogAvailability(const Spells::Snapshot& a_snapshot)
        {
            for (const auto& [name, school] : a_snapshot.schools) {
                if (!g_menu.availabilityLogged.insert(name).second) continue;
                const auto* tree = Spells::SchoolNamed(name);
                logger::info("Widget: {} - {} nodes, {} available to learn now, target {:08X} (snapshot {})", name,
                    tree ? tree->nodes.size() : 0, Spells::Learnable(a_snapshot, name).size(), school.target, a_snapshot.epoch);
            }
        }

        // Q4: the left SkyUI list's row text size in _root stage units, where our widget draws: the selected row's clip
        // (BasicList.as:123 selectedClip) -> its text field that shows the entry's text (else its largest) -> the field's
        // getTextFormat().size, times the field's _yscale and the row clip's scale to _root (a 100-unit segment mapped through
        // the stage, as the item card's fit does). Needs a list selection: tried every frame for kListMeasureFrames frames.
        // Once read, the cards and the tree redraw and the legend is laid out again at the new size.
        void MeasureListRow(RE::GFxMovieView* a_movie, RE::GFxValue& a_root, RE::GFxValue& a_widget)
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
            g_menu.dirty = true;
            if (auto tree = BlockClip(a_widget, Layout::kTree); tree.IsDisplayObject()) {
                const auto rect = BlockRect(Layout::kTree);
                DrawLegend(a_movie, tree, rect.Width(), rect.Height(), g_menu.legendSchool.value_or(std::string{}));
            }
        }

        // Q2 (review round 1 #3): "loading" only while HoM's state for the school can still come. When no snapshot has had the
        // school for kNoDataFrames frames (a refresh is asked for every kPollFrames frames), the progress card says the data did
        // not arrive; logged once per school and menu.
        void WatchSnapshotCover(const Spells::Snapshot& a_snapshot)
        {
            const auto& school = g_session.school;
            if (school.empty() || a_snapshot.Covers(school) || !Spells::SchoolNamed(school)) {
                g_menu.uncoveredSchool.clear();
                g_menu.noData = false;
                return;
            }
            if (g_menu.uncoveredSchool != school) {
                g_menu.uncoveredSchool = school;
                g_menu.uncoveredFrom = g_menu.frame;
                g_menu.noData = false;
                return;
            }
            if (g_menu.noData || g_menu.frame - g_menu.uncoveredFrom < kNoDataFrames) return;
            g_menu.noData = true;
            g_menu.dirty = true;
            logger::warn("Widget: no HoM state for {} after {} frames (HoM API {}, a refresh asked every {} frames) - the progress card "
                         "says the data did not arrive",
                school, kNoDataFrames, Hom::Ready() ? "ready" : "NOT ready", kPollFrames);
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
            // the first line's ascent (spec v6 L2): Scaleform's getLineMetrics, present only with the movie's GFx extensions on
            RE::GFxValue extensions, metrics;
            const bool hasExtensions = a_movie->GetVariable(&extensions, "_global.gfxExtensions") && !extensions.IsUndefined();
            if (one.IsDisplayObject()) {
                const RE::GFxValue line0(0.0);
                one.Invoke("getLineMetrics", &metrics, &line0, 1);
            }
            const auto ascent = Number(metrics, "ascent");
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
            const double share = ascent ? *ascent / kProbeSize : 0.0;
            g_menu.ascentMeasured = share >= kAscentMin && share <= kAscentMax;
            g_menu.ascent = g_menu.ascentMeasured ? share : kAscentFallback;
            const auto extensionsText = !hasExtensions          ? std::string("undefined") :
                                        extensions.IsBool()    ? std::string(extensions.GetBool() ? "true" : "false") :
                                        extensions.IsNumber()  ? std::format("{}", extensions.GetNumber()) :
                                                                 std::string("set");
            logger::info("Widget: L2 font ascent at size {:.0f}: getLineMetrics(0) {} (keys {}; _global.gfxExtensions {}) -> {:.3f} of the size{}",
                kProbeSize, metrics.IsObject() ? "returned an object" : "gave nothing", metrics.IsObject() ? KeysOf(metrics) : std::string("-"),
                extensionsText, g_menu.ascent,
                g_menu.ascentMeasured ? std::string(" (measured)") :
                                        std::format(" - NOT measured (ascent {}), the build-3 frame's {:.2f} is used",
                                            ascent ? std::format("{:.1f}", *ascent) : std::string("none"), kAscentFallback));
        }

        // ---------------------------------------------------------------------
        // L2 read-backs (review round 1 #3, #6): what was drawn, not what was asked for
        // ---------------------------------------------------------------------

        // A new "SelfTest: L2 ..." reading, each once per menu, up to kMaxL2Logs
        void L2SelfTest(const std::string& a_line)
        {
            if (g_menu.l2Logged.size() >= static_cast<std::size_t>(kMaxL2Logs) || !g_menu.l2Logged.insert(a_line).second) return;
            SelfTest(a_line);
        }

        // A text field's first baseline in a_root's units: its _y, the 2-unit gutter and its line 0 ascent (getLineMetrics, the
        // Scaleform extension MeasureTextMetrics probes), mapped from a_parent through the stage. nullopt when the movie does not
        // give the line metrics.
        std::optional<double> BaselineIn(RE::GFxMovieView* a_movie, RE::GFxValue& a_parent, RE::GFxValue& a_field, RE::GFxValue& a_root)
        {
            RE::GFxValue metrics;
            const RE::GFxValue line0(0.0);
            if (!a_field.Invoke("getLineMetrics", &metrics, &line0, 1)) return std::nullopt;
            const auto ascent = Number(metrics, "ascent"), x = Number(a_field, "_x"), y = Number(a_field, "_y");
            if (!ascent || !x || !y) return std::nullopt;
            const auto at = MapPoint(a_movie, a_parent, a_root, *x, *y + kGutter / 2 + *ascent);
            return at ? std::optional<double>(at->second) : std::nullopt;
        }

        // The bottom of a text field's first character's glyph box in a_root's units (getExactCharBoundaries(0), a Scaleform extension;
        // its rectangle is taken in the field's own coordinates). A second reading of the baseline that does not use the ascent:
        // the glyphs differ, so it is shown beside the L2 verdict, not judged. nullopt when the movie does not give it.
        std::optional<double> GlyphBottomIn(RE::GFxMovieView* a_movie, RE::GFxValue& a_parent, RE::GFxValue& a_field, RE::GFxValue& a_root)
        {
            RE::GFxValue box;
            const RE::GFxValue char0(0.0);
            if (!a_field.Invoke("getExactCharBoundaries", &box, &char0, 1)) return std::nullopt;
            const auto top = Number(box, "y"), height = Number(box, "height"), x = Number(a_field, "_x"), y = Number(a_field, "_y");
            if (!top || !height || !x || !y || *height <= 0) return std::nullopt;
            const auto at = MapPoint(a_movie, a_parent, a_root, *x, *y + *top + *height);
            return at ? std::optional<double>(at->second) : std::nullopt;
        }

        // A panel's "content" child's child, not a display object when any step is missing
        RE::GFxValue ContentChild(RE::GFxValue& a_widget, const BlockId& a_id, const char* a_name, RE::GFxValue& a_content)
        {
            auto panel = BlockClip(a_widget, a_id);
            a_content = panel.IsDisplayObject() ? Child(panel, "content") : RE::GFxValue{};
            return a_content.IsDisplayObject() ? Child(a_content, a_name) : RE::GFxValue{};
        }

        // The progress card's first line and the node card's spell name share a baseline (spec v6 L2): both read back after a
        // redraw. SKIP when the alignment was not asked for (or gave way), a field is missing, or the movie has no line metrics.
        void CheckRowBaselines(RE::GFxMovieView* a_movie, RE::GFxValue& a_root, RE::GFxValue& a_widget)
        {
            if (!Settings::Get().selfTest || g_menu.tab == Tab::kPowers || g_menu.tab == Tab::kEffects) return;  // the cards are hidden
            RE::GFxValue progress, card;
            auto school = ContentChild(a_widget, Layout::kProgress, "school", progress);
            auto name = ContentChild(a_widget, Layout::kNodeCard, "name", card);
            if (!g_menu.rowAligned) return L2SelfTest(std::format("L2 baseline SKIP - not aligned: {}", g_menu.rowAlignNote));
            if (!school.IsDisplayObject() || !name.IsDisplayObject()) {
                return L2SelfTest(std::format("L2 baseline SKIP - no {} to compare", !school.IsDisplayObject() ? "school title" : "spell name on the node card"));
            }
            const auto first = BaselineIn(a_movie, progress, school, a_root), named = BaselineIn(a_movie, card, name, a_root);
            if (!first || !named) {
                return L2SelfTest(std::format("L2 baseline SKIP - getLineMetrics not available on the drawn fields (the offset used the {})",
                    g_menu.ascentMeasured ? "probe's measured ascent" : "build-3 frame's ascent, not measured"));
            }
            const double diff = *first - *named;
            const auto glyphFirst = GlyphBottomIn(a_movie, progress, school, a_root), glyphNamed = GlyphBottomIn(a_movie, card, name, a_root);
            const auto glyphs = glyphFirst && glyphNamed ?
                                    std::format("{:.2f} / {:.2f} (diff {:.2f})", *glyphFirst, *glyphNamed, *glyphFirst - *glyphNamed) :
                                    std::string("SKIP (getExactCharBoundaries not available)");
            L2SelfTest(std::format("L2 baseline {} - progress first line {:.2f}, node card name {:.2f} in _root (diff {:.2f}, tolerance {:.1f}; ascent "
                                   "{:.3f} {}); first glyph bottoms, not judged: {}",
                std::abs(diff) <= kBaselineTolerance ? "PASS" : "FAIL", *first, *named, diff, kBaselineTolerance, g_menu.ascent,
                g_menu.ascentMeasured ? "measured" : "NOT measured", glyphs));
        }

        // The node card's button ends at the item card slot's bottom minus the padding (spec v6 L2: 615 with the E2 rects), read
        // back once per menu as drawn (the rect's bottom mapped to _root), when the item card is in place (fitted by us, or left
        // alone), with the item card's visible bottom beside it.
        void CheckButtonBottom(RE::GFxMovieView* a_movie, RE::GFxValue& a_root, RE::GFxValue& a_widget)
        {
            if (g_menu.buttonChecked || !Settings::Get().selfTest || g_menu.tab == Tab::kPowers || g_menu.tab == Tab::kEffects) return;
            const bool cardPlaced = g_menu.cardFit.has_value() || g_menu.irActive || !Settings::Get().moveItemCard || (OwnCard() && g_menu.ownCardShown);
            RE::GFxValue content;
            auto button = ContentChild(a_widget, Layout::kNodeCard, "button", content);
            if (!button.IsDisplayObject() || !cardPlaced) return;
            g_menu.buttonChecked = true;
            // the E2 rects' row 2 (blocks plan Task 2): the node card and the card slot end at one bottom; a layout that puts
            // them apart has nothing to line up
            const auto slot = BlockRect(Layout::kSpellCard), nodeCard = BlockRect(Layout::kNodeCard);
            if (std::abs(slot.bottom - nodeCard.bottom) > kRectTolerance) {
                SelfTest(std::format("L2 node button bottom SKIP - the layout ends the node card at {:.0f} and the card slot at {:.0f}", nodeCard.bottom,
                    slot.bottom));
                return;
            }
            const double want = slot.bottom - kPad;
            // the rect's own bottom (its _y + kButtonH), not the clip's bounds: its centred label can hang below a 28-high rect at
            // large reading sizes (build 6 review round 2 #2)
            const auto top = Number(button, "_y");
            const auto edge = top ? MapPoint(a_movie, content, a_root, 0.0, *top + kButtonH) : std::nullopt;
            std::string cardBottom = "not read";
            if (OwnCard()) {
                cardBottom = std::format("{:.2f} (our card)", g_menu.ownCardBottom);
            } else if (auto card = FindCard(a_movie)) {
                if (const auto seen = VisibleUnion(a_movie, *card, a_root)) cardBottom = std::format("{:.2f}", seen->bounds.yMax);
            }
            if (!edge) {
                SelfTest(std::format("L2 node button bottom FAIL - the button rect's bottom could not be mapped to _root (want {:.0f}); item card "
                                     "visible bottom {}",
                    want, cardBottom));
                return;
            }
            SelfTest(std::format("L2 node button bottom {} - rect bottom {:.2f} in _root, want {:.0f} (item card slot bottom {:.0f} - padding {:.0f}, "
                                 "tolerance {:.1f}); item card visible bottom {}",
                std::abs(edge->second - want) <= kBaselineTolerance ? "PASS" : "FAIL", edge->second, want, slot.bottom, kPad,
                kBaselineTolerance, cardBottom));
        }

        void RequestState()
        {
            Spells::RequestRefresh({ g_session.school, CentreSchool() });
            g_menu.refresh = false;
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
                    logger::info("Widget: first frame on thread {} (main thread {}); HoM is read by main-thread tasks",
                        REX::W32::GetCurrentThreadId(), Hom::GameThreadId());
                }
                // our own SetLearningTarget ran: ask for HoM's state now, not at the next poll
                if (Hom::TakeStateChanged()) g_menu.refresh = true;
                const auto snapshot = Spells::Current();
                // spec v12 G1/G3: the selection wrap goes on in the first frames; without it the highlight is followed (build 11)
                InstallSelectionWrap(movie);
                InstallWheel(movie, widget);  // blocks plan Task 3 (D7)
                if (!g_menu.selWrapped) FollowHighlight(movie);
                WatchTab(movie, widget, *snapshot);  // first: the item card's slot depends on the tab
                WatchChosen(movie, widget);
                FitItemCard(movie, root);
                WatchOwnCard(movie, widget);  // v7 C1 (after WatchTab: its box follows the tab)
                MeasureListRow(movie, root, widget);  // Q4: our reading sizes follow the list's rows
                LayoutFrame(movie, widget);  // blocks plan Task 4: the layout mode (after WatchTab: a tab change cancels it first)
                DrawAddons(movie, widget);   // blocks plan Task 6: after the layout mode (it may make a panel again in its new column)
                // the click first: its list selection is noticed in this same frame
                if (const int clicked = std::exchange(g_menu.clicked, -1); clicked >= 0) OnNodeClicked(movie, clicked, *snapshot);
                WatchSelection(*snapshot);
                WatchReselect();        // review v12 #S3: after a category change, the list back on the selection
                WatchTreeCard(movie);   // P2: our tree card leaves when the menu has no selection to show
                WatchChosen(movie, widget);  // again: the tree card may have left this frame (GPT review: one frame late)
                HoldCardCheck(movie);   // P2: the tree card read back again
                CheckUnlearned();       // P3: one model check per frame
                g_menu.firstFrame = false;
                if (g_pendingNext.exchange(false)) DoNext(*snapshot);
                // both read every frame: the button and the key in the same frame are one request
                const bool key = g_pendingSetTarget.exchange(false);
                const bool button = g_pendingButton.exchange(false);
                if (key || button) DoSetTarget(*snapshot);
                PreviewFrame(movie);
                if (++g_menu.frame % kPollFrames == 0) g_menu.refresh = true;
                if (g_menu.refresh) RequestState();
                if (snapshot->epoch != g_menu.epoch) {
                    g_menu.epoch = snapshot->epoch;
                    g_menu.dirty = true;
                    SettleProvisionalCentre(*snapshot);
                    LogAvailability(*snapshot);
                }
                WatchSnapshotCover(*snapshot);
                CheckButtonBottom(movie, root, widget);  // once per menu, when the button and the item card are both up
                DropStaleHover(movie, widget);  // review v11 #10
                RedrawTooltip(movie, widget, *snapshot);  // the hover changes no tree: outside the dirty gate (spec v10 T6)
                if (!g_menu.dirty) return;
                g_menu.dirty = false;
                RedrawTree(movie, widget, *snapshot);  // first: it drops a selection that is no longer drawn
                RedrawTooltip(movie, widget, *snapshot);  // again over the new tree (or gone with its node)
                RedrawProgress(movie, widget, *snapshot);
                RedrawNodeCard(movie, widget, *snapshot);
                ApplyVisibility(widget);
                CheckRowBaselines(movie, root, widget);
            }

        private:
            static void PreviewFrame(RE::GFxMovieView* a_movie)
            {
                Preview::FrameInfo info;
                info.visible = a_movie->GetVisibleFrameRect();
                RE::GViewport viewport;
                a_movie->GetViewport(&viewport);
                const double visibleH = info.visible.bottom - info.visible.top;
                info.pixelsPerUnit = visibleH > 0 ? viewport.height / visibleH : 0.0;
                info.box = Settings::PanelContent(BlockRect(Layout::kPreview));  // below the "PREVIEW" title band (R9); scrolled (on screen)
                // blocks plan Task 3 (D2): the preview column's viewport - the scissor and the alpha restore take box cut to it
                if (const auto column = ColumnOf(Layout::kPreview)) info.columnClip = ColumnViewport(*column);
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
        // Not active (the preview box hidden on Active Effects, the menu closing, another movie): Preview keeps what it hid
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

        // Heart of Magic's style read again at each menu open (spec v11 T: the player may change HoM's settings or design between
        // menus), published for the frame handler. The line (T19) is logged when it differs from the last one; each note (a
        // default taken and why) once per game session
        void ReadHomStyle()
        {
            static std::string s_lastSummary;
            static std::set<std::string> s_notesLogged;
            auto style = std::make_shared<const HomStyle::Style>(HomStyle::Read(kHomConfigPath, kHomPresetDir));
            if (auto summary = style->Summary(); summary != s_lastSummary) {
                logger::info("HomStyle: {} ({}, {})", summary, kHomConfigPath, kHomPresetDir);
                s_lastSummary = std::move(summary);
            }
            for (const auto& note : style->notes) {
                if (s_notesLogged.insert(note).second) logger::info("HomStyle: {} (logged once)", note);
            }
            const std::scoped_lock lock(g_styleLock);
            g_style = std::move(style);
        }

        void AttachWidget()
        {
            auto* movie = OpenMagicMenuMovie();
            if (!movie) return;  // closed meanwhile
            if (!Hom::Ready()) {
                if (!g_noApiLogged) logger::warn("Widget: Heart of Magic's API has not arrived - nothing drawn (logged once)");
                g_noApiLogged = true;
                return;
            }
            RE::GFxValue root, existing;
            if (!movie->GetVariable(&root, "_root") || !root.IsDisplayObject()) return;
            if (root.GetMember(kRootName, &existing) && existing.IsDisplayObject()) return;  // already on this movie

            g_menu = MenuState{};
            g_menu.irActive = IrActive(movie);
            // blocks plan Task 4: a new menu - no mode, no queued request, the input tests counted afresh; the wraps are not on yet
            ++g_menuGeneration;
            g_layoutMode = false;
            g_layoutFocus = {};
            g_layoutStickY = 0.0f;
            {
                const std::scoped_lock lock(g_layoutRequestLock);
                g_layoutRequests.clear();
            }
            g_layoutBlock = kBlockNoWidget | kBlockNoInputWrap | kBlockNoEquipWrap;  // the widget clears its bit once it is up
            g_refusalLogs = 0;
            Input::ResetCounters();
            logger::info("Widget: Inventory Reforged {} in this magic menu{}", g_menu.irActive ? "IS active" : "is not active",
                g_menu.irActive ? " - the item card and the preview are not moved" : "");
            g_closing = false;
            g_pendingNext = g_pendingSetTarget = g_pendingButton = false;
            ResetUnlearnedPreview(g_menu.irActive);
            Preview::OnMenuOpen(g_menu.irActive);
            ReadHomStyle();
            if (!g_frame) {
                g_frame = RE::make_gptr<Frame>();
                g_nodeClick = RE::make_gptr<NodeClick>();
                g_nodeRollOver = RE::make_gptr<NodeRollOver>();
                g_nodeRollOut = RE::make_gptr<NodeRollOut>();
                g_buttonClick = RE::make_gptr<ButtonClick>();
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
                g_lockClick = RE::make_gptr<LockClick>();
                g_resetClick = RE::make_gptr<ResetClick>();
                g_shieldPress = RE::make_gptr<ShieldPress>();
                g_handleInputWrap = RE::make_gptr<HandleInputWrap>();
                g_attemptEquipWrap = RE::make_gptr<AttemptEquipWrap>();
            }
            auto widget = Draw::Clip(root, kRootName);
            if (!widget.IsDisplayObject()) {
                logger::error("Widget: could not create _root.{}", kRootName);
                return;
            }
            MakeShield(movie, widget);  // blocks plan Task 4: first, so it is the lowest clip of mmx_root
            MeasureTextMetrics(movie, widget);  // before anything is fitted (review round 1 #1)
            const auto& settings = Settings::Get();
            // the over-pass: the magic menu draws its 3D before the movie, under our black preview box
            g_overPassLogged = false;
            g_previewShown = true;
            g_hide3D = false;
            g_powersHidePending = false;
            g_attachedMovie = movie;
            g_overPass = settings.drawPreviewOver && g_postDisplayHooked && !g_menu.irActive;
            logger::info("Widget: preview over-pass {} ({})", g_overPass ? "ON" : "off",
                !settings.drawPreviewOver ? "bDrawPreviewOver=0" :
                !g_postDisplayHooked      ? "PostDisplay not hooked" :
                g_menu.irActive           ? "Inventory Reforged draws the magic menu's 3D itself" :
                                            "the 3D preview is drawn again after the menu movie");
            const bool vr = REL::Module::IsVR();
            g_fitPass = settings.movePreview && g_postDisplayHooked && !g_menu.irActive && !vr;
            g_powersHideCalls = g_postDisplayHooked && !g_menu.irActive && !vr;
            logger::info("Widget: preview place/fit {} ({})", g_fitPass ? "ON" : "off",
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
                                                                    "SkyUI's, moved and fitted (bOwnCard=0)");
            if (auto tree = BlockClip(widget, Layout::kTree); tree.IsDisplayObject()) {
                const auto rect = BlockRect(Layout::kTree);
                DrawLegend(movie, tree, rect.Width(), rect.Height(), {});
            }
            MakeLayoutChrome(movie, widget);  // blocks plan Task 4 (D5): the lock / reset row, the border handle, the drag overlay
            CalibrateFocusCheck();
            g_layoutBlock = g_layoutBlock.load() & ~kBlockNoWidget;
            RE::GFxValue fn;
            movie->CreateFunction(&fn, g_frame.get());
            widget.SetMember("onEnterFrame", fn);
            RequestState();
            // the keys the hints name (Q1: chosen by the key scan, v5-key-scan.md)
            logger::info("Widget: keys - next learnable {} {}, set target {} {} (the on-screen hints use these names)", settings.keyNext,
                KeyLabel(settings.keyNext), settings.keySetTarget, KeyLabel(settings.keySetTarget));
            const auto area = movie->GetVisibleFrameRect();
            logger::info("Widget: attached on thread {} (visible stage x {:.0f}..{:.0f} y {:.0f}..{:.0f}, tree {})",
                REX::W32::GetCurrentThreadId(), area.left, area.right, area.top, area.bottom, Spells::Get() ? "loaded" : "missing");
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
                    CloseRegistry();  // blocks plan Task 6: the first open ends add-on registration (before the widget is made)
                    SKSE::GetTaskInterface()->AddUITask([] { AttachWidget(); });
                } else {
                    g_closing = true;  // first: no frame work from here on
                    LayoutAtClose();   // blocks plan Task 4: a mode still on is cancelled; the input tests
                    RemoveWheelListener();  // before g_attachedMovie goes (it checks the movie is still ours)
                    if (g_menu.nodeClicks > 0) {
                        SelfTest(std::format("L2 mask clicks {} - {} node click(s) this menu, {} outside the tree column's visible area (ignored; "
                                             "want 0: setMask keeps the masked-off part from taking clicks)",
                            g_menu.nodeClicksOutside == 0 ? "PASS" : "FAIL", g_menu.nodeClicks, g_menu.nodeClicksOutside));
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

    BlockRegistry::Outcome RegisterBlock(std::string_view a_sender, const void* a_data, std::uint32_t a_dataLen)
    {
        BlockRegistry::Outcome outcome;
        double height = 0.0;
        {
            const std::scoped_lock lock(g_registryLock);
            outcome = g_registry.Register(a_sender, a_data, a_dataLen);
            if (const auto* block = outcome.accepted ? g_registry.Find(outcome.id) : nullptr) height = block->height;
        }
        const std::string id = outcome.id.empty() ? std::string("(no id)") : outcome.id;
        if (outcome.accepted) {
            logger::info("Widget: add-on block {} registered by {} ({:.0f} high)", id, a_sender, height);
            SelfTest(std::format("L6 addon {} registered - from {}, {:.0f} high", id, a_sender, height));
        } else {
            logger::warn("Widget: add-on block {} from {} refused - {}", id, a_sender, outcome.reason);
            SelfTest(std::format("L6 addon {} refused {} (from {})", id, outcome.reason, a_sender));
        }
        FlushLog();
        return outcome;
    }

    void RegisterDebugFiller()
    {
        constexpr float kFillerHeight = 300.0f;
        const BlockApi::Registration reg{ .id = "SL_MagicMenu.filler", .title = "FILLER (TEST)", .height = kFillerHeight, .draw = DrawFiller };
        RegisterBlock(BlockApi::kPluginName, &reg, sizeof(reg));
    }

    bool LoadTree()
    {
        return Spells::Load();
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

    void RequestNext()
    {
        g_pendingNext = true;
    }

    void RequestSetTarget()
    {
        g_pendingSetTarget = true;
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
        if (g_layoutMode.exchange(false)) {
            QueueLayout(LayoutRequestKind::kSave, a_source);
            return true;
        }
        if (const auto bits = LiveBlock(a_heldOther); bits != 0) {
            LogRefusal(bits, a_source);
            return false;
        }
        if (!g_layoutMode.exchange(true)) QueueLayout(LayoutRequestKind::kEnter, a_source);
        return true;
    }

    bool CancelLayoutMode(const char* a_why)
    {
        if (!g_layoutMode.exchange(false)) return false;
        QueueLayout(LayoutRequestKind::kCancel, a_why);
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
}

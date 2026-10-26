#pragma once

#include <cstdint>
#include <optional>

// =============================================================================
// SELECTION RULE - spec v12 S1 / G1: which list event of SkyUI's magic menu is a
// selection (our windows and the game's card/3D follow it), which is a mouse
// hover (swallowed: nothing follows it) and which changes nothing. No game
// types; tools/magic-menu-tree-check --selftest runs it.
//
// SkyUI (schlangster/skyui master, fetched 2026-10-26; the installed
// SkyUI-Community build may differ by a few lines):
//   BasicList.as:23-24      SELECT_MOUSE = 0, SELECT_KEYBOARD = 1
//   ScrollingList.as:372    selectionChange {index, keyboardOrMouse} (its doSetSelectedIndex override, :326)
//   BasicList.as:318-324    a roll-over is doSetSelectedIndex(index, SELECT_MOUSE): a hover
//   ScrollingList.as:247/268 an up/down move is SELECT_KEYBOARD (also :291/:295 selectDefaultIndex)
//   ScrollingList.as:256/277 PageUp/PageDown are doSetSelectedIndex(-1, SELECT_MOUSE); BasicList.as:335 roll-out -1
//   BasicList.as:106-108    the selectedIndex setter is SELECT_MOUSE too (ours are marked before it runs)
//   BasicList.as:296-316    itemPress / itemPressAux {index: the highlighted row, entry, keyboardOrMouse}
//                           (undefined -> SELECT_KEYBOARD, :301-302 / :312-313; Aux only for button 1, :309)
// =============================================================================

namespace SelectionRule
{
    inline constexpr int kSelectMouse = 0;     // BasicList.as:23
    inline constexpr int kSelectKeyboard = 1;  // BasicList.as:24

    enum class Event
    {
        kSelectionChange,  // the list's selectionChange, as InventoryLists.onItemsListSelectionChange receives it
        kPress,            // itemPress (a left click, or Enter on a highlighted row)
        kPressAux,         // itemPressAux (a right click)
    };

    enum class Source
    {
        kOpen,       // the list's highlight when the menu opened (S3)
        kClick,      // a mouse press on a row
        kKey,        // a keyboard/gamepad move or press
        kTree,       // a tree node click selected it in the list (marked by us before the setter)
        kHighlight,  // the wrap is not installed: build 11's follow-the-highlight (G3)
        kOther,      // a selectionChange without SkyUI's keyboardOrMouse (another mod's dispatch): passed on
    };

    enum class Verdict
    {
        kSelect,  // commit it and call the original handler for it
        kHover,   // a mouse hover: swallowed (the original is not called), logged
        kKeep,    // nothing changes: swallowed, not logged
    };

    // Our own selectedIndex set: its source and the row it sets. Only that row's change is ours: the setter may scroll
    // first (ScrollingList.as:351-357), and the scroll's UpdateList re-selects the row under the cursor with SELECT_MOUSE
    // (:190-193) - that nested change is judged as any other
    struct Mark
    {
        Source source = Source::kTree;
        int index = -1;
    };

    struct Input
    {
        Event event = Event::kSelectionChange;
        int index = -1;                        // event.index (an entryList index; -1 = none)
        std::optional<int> keyboardOrMouse;    // event.keyboardOrMouse, nullopt when the event has none
        std::optional<Mark> ours;              // our own programmatic change in progress (marked before the setter)
        bool sameEntry = false;                // the event's row is the committed entry (same FormID; same index when neither has one)
        bool cardElsewhere = false;            // our windows show something else than the committed entry (a tree card)
    };

    struct Result
    {
        Verdict verdict = Verdict::kKeep;
        Source source = Source::kOther;  // meaningful for kSelect only
    };

    inline const char* Name(Source a_source)
    {
        switch (a_source) {
        case Source::kOpen: return "open";
        case Source::kClick: return "click";
        case Source::kKey: return "key";
        case Source::kTree: return "tree";
        case Source::kHighlight: return "highlight";
        default: return "other";
        }
    }

    // An event's row is the committed entry: the same FormID; the same index only when neither has a FormID (an active
    // effect row); nothing committed (-1) or no row (-1) is never the same
    inline bool SameEntry(std::uint32_t a_id, int a_index, std::uint32_t a_committedId, int a_committedIndex)
    {
        if (a_index < 0 || a_committedIndex < 0) return false;
        if (a_id || a_committedId) return a_id == a_committedId;
        return a_index == a_committedIndex;
    }

    // review v12 #S2: a commit is a change of the committed selection (WatchSelection acts on it) when the row or form
    // differs, when forced, or when our windows show something else (a tree card): coming back from it to the same row
    // must still reach WatchSelection, which drops the tree card
    inline bool CommitChanges(bool a_sameId, bool a_sameIndex, bool a_force, bool a_cardElsewhere)
    {
        return a_force || a_cardElsewhere || !(a_sameId && a_sameIndex);
    }

    // review v12 #S1: ItemMenu.onItemHighlightChange, reached by every itemHighlightChange (also the ones InventoryLists
    // dispatches itself, InventoryLists.as:327/397, and onShowItemsList's, ItemMenu.as:370-373)
    enum class Highlight
    {
        kPass,     // the original runs: the game's card / 3D / bottom bar follow
        kSwallow,  // not the selection: nothing follows
    };

    struct HighlightInput
    {
        int index = -1;              // event.index
        bool ours = false;           // our own token: inside the saved selection-change original, or our own dispatch
        bool committed = false;      // a selection exists (a committed row, or a tree card)
        bool sameEntry = false;      // the event's row is the committed entry
        bool cardElsewhere = false;  // our windows show a tree card, not the committed row
    };

    inline Highlight DecideHighlight(const HighlightInput& a_in)
    {
        if (a_in.ours) return Highlight::kPass;
        // -1 (a category change's empty list, InventoryLists.as:315/327; showItemsList -1, :395): with a selection it stays up
        // (S3 puts the list back on it); with none, vanilla's no-selection path runs (resetMenu / onHideItemsList)
        if (a_in.index < 0) return a_in.committed ? Highlight::kSwallow : Highlight::kPass;
        if (a_in.cardElsewhere) return Highlight::kSwallow;  // the tree card is the selection: the list row would replace it
        return a_in.sameEntry ? Highlight::kPass : Highlight::kSwallow;
    }

    inline Result Decide(const Input& a_in)
    {
        // -1: no row (PageUp/PageDown, roll-out, a category's fresh list) - the selection stays (G1); passing it on would
        // fade the game's card and drop its 3D (ItemMenu.as:359-365) while ours keep showing the selection
        if (a_in.index < 0) return { Verdict::kKeep };
        if (a_in.ours && a_in.ours->index == a_in.index) return { Verdict::kSelect, a_in.ours->source };
        if (a_in.event != Event::kSelectionChange) {
            // a press commits the pressed entry; the committed one again (nothing else shown) changes nothing. By entry, not
            // index: a sort or InvalidateData (ScrollingList.as:208-213) puts another entry at the committed index
            if (a_in.sameEntry && !a_in.cardElsewhere) return { Verdict::kKeep };
            const bool key = a_in.keyboardOrMouse && *a_in.keyboardOrMouse == kSelectKeyboard;
            return { Verdict::kSelect, key ? Source::kKey : Source::kClick };
        }
        if (!a_in.keyboardOrMouse) return { Verdict::kSelect, Source::kOther };  // not SkyUI's: build 11 behaviour
        if (*a_in.keyboardOrMouse == kSelectKeyboard) return { Verdict::kSelect, Source::kKey };
        if (*a_in.keyboardOrMouse == kSelectMouse) {
            // the mouse onto the committed entry (InvalidateData's restore after an equip, ScrollingList.as:229-235): no hover
            return { a_in.sameEntry ? Verdict::kKeep : Verdict::kHover };
        }
        return { Verdict::kSelect, Source::kOther };
    }
}

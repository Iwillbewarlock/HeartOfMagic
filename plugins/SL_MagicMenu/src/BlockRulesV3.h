#pragma once

#include "BlockApi.h"
#include "BlockRules.h"
#include "Layout.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// =============================================================================
// BLOCK RULES v3 - the pure parts of the Block API v3 host side (HoM add-on
// design 2026-10-30, "Block API v3" and "Changes after the GPT design review"):
// the retired ids a v3 block may take over (legacyId), reading the 'SLMS' and
// 'SLMV' messages, whether a queued 'SLMS' is still due, the key queue, the
// content clip's pad, the block's visible part and its overlay's clears; the
// 'SLMK' key hints (2026-10-31): reading, the per-block list, the strip's hint
// buttons of a frame and the panel's growth (KeyHints.cpp). In namespace
// BlockRules beside BlockRules.h (which is at the repo's 600-line limit). No
// game types: tools/magic-menu-block-api-check runs these on made-up cases.
// =============================================================================

namespace BlockRules
{
    /// The built-in block ids a v3 block may take over with Registration::legacyId (their saved places stay under these ids)
    inline constexpr std::array<std::string_view, 3> kRetiredIds{ Layout::kTree, Layout::kProgress, Layout::kNodeCard };

    /// Whether a block may take over a_legacyId (null / "" is no claim: nullopt), or the reason it may not: the id is not one
    /// of kRetiredIds (exact, case-sensitive, as the layout keys it), or a_alreadyTaken (another block claimed it first)
    inline std::optional<std::string> JudgeLegacy(std::string_view a_legacyId, bool a_alreadyTaken)
    {
        if (a_legacyId.empty()) return std::nullopt;
        if (std::find(kRetiredIds.begin(), kRetiredIds.end(), a_legacyId) == kRetiredIds.end()) {
            std::string known;
            for (const auto id : kRetiredIds) known += (known.empty() ? "" : ", ") + std::string(id);
            return "legacyId \"" + std::string(a_legacyId) + "\" is not a retired id (" + known + ")";
        }
        if (a_alreadyTaken) return "legacyId \"" + std::string(a_legacyId) + "\" is claimed already by another block";
        return std::nullopt;
    }

    // ---------------------------------------------------------------------
    // kMessageShowSpell ('SLMS') and kMessageVisible ('SLMV'): v3's add-on -> host messages (BlockApi.h)
    // ---------------------------------------------------------------------

    struct ShowSpellRead
    {
        std::string id;
        std::uint32_t formId = 0;
        std::uint32_t requestId = 0;
        bool nameShown = true;
    };

    struct VisibleRead
    {
        std::string id;
        bool shown = true;
    };

    inline constexpr std::size_t kShowSpellNeeded = offsetof(BlockApi::ShowSpellRequest, nameShown) + sizeof(std::uint32_t);  // the fields read
    inline constexpr std::size_t kVisibleNeeded = offsetof(BlockApi::VisibleRequest, shown) + sizeof(std::uint32_t);

    /// A v3 message's struct copied from a_data, or nullopt with the reason in a_why: the message must cover a_needed bytes (by
    /// dataLen and structSize), have apiVersion kVersion (3: these messages are v3's) and an id of 1..kMaxFinalIdLength
    /// characters, which a_id gets
    template <class T>
    std::optional<T> ReadV3Message(const void* a_data, std::uint32_t a_dataLen, std::size_t a_needed, const char* a_name, std::string& a_id,
        std::string& a_why)
    {
        if (!a_data || a_dataLen < a_needed) {
            a_why = "the message is " + std::to_string(a_data ? a_dataLen : 0) + " bytes, a " + a_name + " needs " + std::to_string(a_needed);
            return std::nullopt;
        }
        T request{};
        std::memcpy(&request, a_data, std::min<std::size_t>(a_dataLen, sizeof(request)));
        if (request.apiVersion != BlockApi::kVersion) {
            a_why = "apiVersion " + std::to_string(request.apiVersion) + " (this MagicMenuRewrite reads " + std::to_string(BlockApi::kVersion) + ")";
            return std::nullopt;
        }
        if (request.structSize < a_needed) {
            a_why = "structSize " + std::to_string(request.structSize) + " does not cover the fields (" + std::to_string(a_needed) + ")";
            return std::nullopt;
        }
        const std::size_t length = request.id ? strnlen(request.id, kMaxFinalIdLength + 1) : 0;
        if (length == 0 || length > kMaxFinalIdLength) {
            a_why = "the id is not 1.." + std::to_string(kMaxFinalIdLength) + " characters";
            return std::nullopt;
        }
        a_id.assign(request.id, length);
        return request;
    }

    /// The id, spell, request number and name rule of a kMessageShowSpell message, or nullopt with the reason (ReadV3Message's,
    /// or formId 0). Whether the id names a block is the host's to say
    inline std::optional<ShowSpellRead> ReadShowSpell(const void* a_data, std::uint32_t a_dataLen, std::string& a_why)
    {
        std::string id;
        const auto request = ReadV3Message<BlockApi::ShowSpellRequest>(a_data, a_dataLen, kShowSpellNeeded, "ShowSpellRequest", id, a_why);
        if (!request) return std::nullopt;
        if (request->formId == 0) {
            a_why = "formId 0 (no spell)";
            return std::nullopt;
        }
        return ShowSpellRead{ std::move(id), request->formId, request->requestId, request->nameShown != 0 };
    }

    /// The id and shown state of a kMessageVisible message (any shown value but 0 is shown), or nullopt with the reason
    inline std::optional<VisibleRead> ReadVisible(const void* a_data, std::uint32_t a_dataLen, std::string& a_why)
    {
        std::string id;
        const auto request = ReadV3Message<BlockApi::VisibleRequest>(a_data, a_dataLen, kVisibleNeeded, "VisibleRequest", id, a_why);
        if (!request) return std::nullopt;
        return VisibleRead{ std::move(id), request->shown != 0 };
    }

    /// A queued kMessageShowSpell is served only when the list selection did not change after it was queued (the host's selection
    /// sequence then and now): a newer player choice beats an add-on's older request
    constexpr bool ShowSpellDue(std::uint64_t a_seqAtQueue, std::uint64_t a_seqNow)
    {
        return a_seqAtQueue == a_seqNow;
    }

    /// A queued kMessageShowSpell: the request and the host's selection sequence when it was queued
    struct QueuedShowSpell
    {
        ShowSpellRead request;
        std::uint64_t seqAtQueue = 0;
    };
    using ShowSpellQueue = std::vector<QueuedShowSpell>;  // in arrival order, the newest last

    /// a_queued queued as the newest request (an older one of the same block id goes); kFull (dropped) when a_cap other ids
    /// wait already
    inline Queued EnqueueShowSpell(ShowSpellQueue& a_queue, QueuedShowSpell a_queued, std::size_t a_cap)
    {
        const auto erased = std::erase_if(a_queue, [&](const QueuedShowSpell& a_old) { return a_old.request.id == a_queued.request.id; });
        if (erased == 0 && a_queue.size() >= a_cap) return Queued::kFull;
        a_queue.push_back(std::move(a_queued));
        return erased ? Queued::kReplaced : Queued::kAdded;
    }

    /// One frame's pick: at most one request is served - the newest, when it is still due (ShowSpellDue against a_seqNow); every
    /// other one is dropped (an older one is the add-ons' earlier wish, and when the newest is stale every older one is too:
    /// the sequence only grows)
    struct ShowSpellPick
    {
        std::optional<QueuedShowSpell> serve;
        std::vector<QueuedShowSpell> dropped;
    };

    inline ShowSpellPick PickShowSpell(const ShowSpellQueue& a_queue, std::uint64_t a_seqNow)
    {
        ShowSpellPick pick;
        if (a_queue.empty()) return pick;
        pick.dropped.assign(a_queue.begin(), a_queue.end() - 1);
        if (ShowSpellDue(a_queue.back().seqAtQueue, a_seqNow)) {
            pick.serve = a_queue.back();
        } else {
            pick.dropped.push_back(a_queue.back());
        }
        return pick;
    }

    /// kMessageVisible applied to the host's hidden set: true when a_id's state changed
    inline bool SetShown(std::set<std::string>& a_hidden, const std::string& a_id, bool a_shown)
    {
        return a_shown ? a_hidden.erase(a_id) > 0 : a_hidden.insert(a_id).second;
    }

    /// v3 key: a keyboard down queued for the next frame's key calls; false (dropped) when a_cap keys wait already
    inline bool QueueKey(std::vector<std::uint32_t>& a_queue, std::uint32_t a_scanCode, std::size_t a_cap)
    {
        if (a_queue.size() >= a_cap) return false;
        a_queue.push_back(a_scanCode);
        return true;
    }

    /// v3 key (fix round 1 of HoM add-on Task 7): a frame's keys go to a block only when it has a key callback, shows in that frame
    /// (a place on the tab, not hidden by kMessageVisible, not failed, the columns up) and the layout mode is off - right before
    /// its frame call. A block that does not show never gets them (a key for another tab is not kept for later)
    constexpr bool DeliversKey(bool a_hasKey, bool a_shown, bool a_layoutMode)
    {
        return a_hasKey && a_shown && !a_layoutMode;
    }

    // ---------------------------------------------------------------------
    // The v3 block's box, visible part and overlay (Widget.cpp's add-on drawing)
    // ---------------------------------------------------------------------

    /// An add-on block that closes up while the menu shows no spell: one drawn per selection, without kFlagKeepWhenEmpty
    constexpr bool ClosesWhenEmpty(bool a_drawSelection, std::uint32_t a_flags)
    {
        return a_drawSelection && (a_flags & BlockApi::kFlagKeepWhenEmpty) == 0;
    }

    /// The content clip in the block's box (box coordinates): below the title strip when it has one and inside the pad on
    /// every side - a_pad when >= 0 (Registration::pad), else a_hostPad. Width and height are never negative
    struct ContentBox
    {
        double left{}, top{}, width{}, height{};
    };

    inline ContentBox ContentBoxOf(double a_boxW, double a_boxH, bool a_titled, double a_titleH, double a_pad, double a_hostPad)
    {
        const double pad = a_pad >= 0 ? a_pad : a_hostPad;
        const double top = a_titled ? a_titleH + pad : pad;
        return { pad, top, std::max(0.0, a_boxW - 2 * pad), std::max(0.0, a_boxH - top - pad) };
    }

    /// Context::viewLeft .. viewBottom: the part of a_content (the content clip on screen) inside a_viewport (its column's
    /// visible area), in content clip coordinates (0,0 = the clip's top-left); all 0 when none of it shows
    inline Settings::Rect ViewInContent(const Settings::Rect& a_content, const Settings::Rect& a_viewport)
    {
        const double left = std::max(a_content.left, a_viewport.left), top = std::max(a_content.top, a_viewport.top);
        const double right = std::min(a_content.right, a_viewport.right), bottom = std::min(a_content.bottom, a_viewport.bottom);
        if (!(right > left) || !(bottom > top)) return {};
        return { left - a_content.left, top - a_content.top, right - a_content.left, bottom - a_content.top };
    }

    /// What a block's overlay clip was made for, and the same now (StepOverlay)
    struct OverlaySnapshot
    {
        bool shown = false;               // the block has an overlay now: it shows (a place on this tab, not hidden by
                                          // kMessageVisible, the columns up) and the layout mode is off (its covers stay on top)
        int column = -1;
        Settings::Rect onScreen;          // the block's box on screen (scrolled)
        std::uint64_t contentDraws = 0;   // Context::contentGeneration: draws into a new content clip
        std::uint64_t modeChanges = 0;    // the layout mode entered or left
        std::uint64_t panelsMade = 0;     // panels made in its column (one made after the overlay lies above it)
    };

    enum class OverlayEvent
    {
        kNone,
        kHidden,      // the block stopped showing
        kMoved,       // another column or another place on screen: a move, a resize or a scroll
        kDrawn,       // drawn again into a new content clip
        kLayoutMode,  // the layout mode started or ended
        kPanelAbove,  // a panel was made in its column after it
    };

    inline const char* OverlayEventText(OverlayEvent a_event)
    {
        switch (a_event) {
        case OverlayEvent::kHidden: return "the block is hidden";
        case OverlayEvent::kMoved: return "the block moved or scrolled";
        case OverlayEvent::kDrawn: return "the block was drawn again";
        case OverlayEvent::kLayoutMode: return "the layout mode started or ended";
        case OverlayEvent::kPanelAbove: return "a panel was made above it";
        default: return "nothing";
        }
    }

    /// Why the overlay made at a_made must go now (a layout mode change first, then the enum's order), or kNone. Rects within
    /// a_tolerance are the same
    inline OverlayEvent OverlayEventOf(const OverlaySnapshot& a_made, const OverlaySnapshot& a_now, double a_tolerance)
    {
        const auto same = [&](double a_a, double a_b) { return std::abs(a_a - a_b) <= a_tolerance; };
        if (a_now.modeChanges != a_made.modeChanges) return OverlayEvent::kLayoutMode;
        if (!a_now.shown) return OverlayEvent::kHidden;
        if (a_now.column != a_made.column || !same(a_now.onScreen.left, a_made.onScreen.left) || !same(a_now.onScreen.top, a_made.onScreen.top) ||
            !same(a_now.onScreen.right, a_made.onScreen.right) || !same(a_now.onScreen.bottom, a_made.onScreen.bottom)) {
            return OverlayEvent::kMoved;
        }
        if (a_now.contentDraws != a_made.contentDraws) return OverlayEvent::kDrawn;
        if (a_now.panelsMade != a_made.panelsMade) return OverlayEvent::kPanelAbove;
        return OverlayEvent::kNone;
    }

    /// One block's overlay across frames: the overlay up now (as made) and Context::overlayGeneration, bumped with each new
    /// overlay clip - so it changes whenever the overlay the add-on drew into was cleared. Kept across menus (a menu's first
    /// overlay is a new one too)
    struct OverlayTrack
    {
        std::optional<OverlaySnapshot> made;
        std::uint32_t generation = 0;
    };

    struct OverlayStep
    {
        bool remove = false;  // the old overlay clip goes
        bool make = false;    // a new one is made (generation bumped)
        OverlayEvent why = OverlayEvent::kNone;
    };

    /// The frame's step for a block's overlay: the old one removed on an OverlayEventOf event, a new one made while the block
    /// shows and has none. a_track is updated as if the host's make succeeds (a host whose make failed resets a_track.made, and
    /// the next frame tries again)
    inline OverlayStep StepOverlay(OverlayTrack& a_track, const OverlaySnapshot& a_now, double a_tolerance)
    {
        OverlayStep step;
        if (a_track.made) {
            step.why = OverlayEventOf(*a_track.made, a_now, a_tolerance);
            if (step.why == OverlayEvent::kNone) return step;
            step.remove = true;
            a_track.made.reset();
        }
        if (!a_now.shown) return step;
        step.make = true;
        a_track.made = a_now;
        ++a_track.generation;
        return step;
    }

    // ---------------------------------------------------------------------
    // kMessageKeyHint ('SLMK', 2026-10-31): the add-ons' key hints in SkyUI's key-hint strip (KeyHints.cpp)
    // ---------------------------------------------------------------------

    struct KeyHintRead
    {
        std::string id;
        std::uint32_t scanCode = 0;
        std::string label;  // "" = remove the block's hint for scanCode
    };

    inline constexpr std::size_t kKeyHintNeeded = offsetof(BlockApi::KeyHint, label) + sizeof(const char*);

    /// a_text cut to at most a_bytes bytes, never inside a UTF-8 sequence (the continuation bytes at the cut go with it)
    inline std::string CutUtf8(std::string_view a_text, std::size_t a_bytes)
    {
        constexpr unsigned char kContMask = 0xC0, kCont = 0x80;
        if (a_text.size() <= a_bytes) return std::string(a_text);
        std::size_t end = a_bytes;
        while (end > 0 && (static_cast<unsigned char>(a_text[end]) & kContMask) == kCont) --end;
        return std::string(a_text.substr(0, end));
    }

    /// The id, key and label of a kMessageKeyHint message, or nullopt with the reason (ReadV3Message's, a scanCode of 0 or more
    /// than kMaxHintScanCode). The label is read up to kMaxHintLabelBytes + 1 bytes and cut there (CutUtf8). Whether the id names
    /// a block is the host's to say
    inline std::optional<KeyHintRead> ReadKeyHint(const void* a_data, std::uint32_t a_dataLen, std::string& a_why)
    {
        std::string id;
        const auto request = ReadV3Message<BlockApi::KeyHint>(a_data, a_dataLen, kKeyHintNeeded, "KeyHint", id, a_why);
        if (!request) return std::nullopt;
        if (request->scanCode == 0 || request->scanCode > BlockApi::kMaxHintScanCode) {
            a_why = "scanCode " + std::to_string(request->scanCode) + " is not a keyboard scan code (1.." + std::to_string(BlockApi::kMaxHintScanCode) + ")";
            return std::nullopt;
        }
        const std::size_t length = request->label ? strnlen(request->label, BlockApi::kMaxHintLabelBytes + 1) : 0;
        const std::string_view label(request->label ? request->label : "", length);
        return KeyHintRead{ std::move(id), request->scanCode, CutUtf8(label, BlockApi::kMaxHintLabelBytes) };
    }

    struct KeyHintEntry
    {
        std::string id;  // the block's final id
        std::uint32_t scanCode = 0;
        std::string label;
    };
    using KeyHintList = std::vector<KeyHintEntry>;  // every block's hints, each block's in the order first sent

    enum class HintSet
    {
        kAdded,
        kReplaced,  // the same block and key: the new label, its place kept
        kRemoved,   // an empty label
        kNotThere,  // an empty label for a key the block has no hint for
        kFull,      // a new key of a block that has a_cap already: refused
    };

    inline HintSet SetKeyHint(KeyHintList& a_list, const KeyHintRead& a_read, std::size_t a_cap)
    {
        const auto same = [&](const KeyHintEntry& a_entry) { return a_entry.id == a_read.id && a_entry.scanCode == a_read.scanCode; };
        const auto found = std::ranges::find_if(a_list, same);
        if (a_read.label.empty()) {
            if (found == a_list.end()) return HintSet::kNotThere;
            a_list.erase(found);
            return HintSet::kRemoved;
        }
        if (found != a_list.end()) {
            found->label = a_read.label;
            return HintSet::kReplaced;
        }
        const auto ofBlock = std::ranges::count_if(a_list, [&](const KeyHintEntry& a_entry) { return a_entry.id == a_read.id; });
        if (static_cast<std::size_t>(ofBlock) >= a_cap) return HintSet::kFull;
        a_list.push_back({ a_read.id, a_read.scanCode, a_read.label });
        return HintSet::kAdded;
    }

    /// One of our buttons in the strip: its key, label and whether it shows this frame
    struct HintButton
    {
        std::uint32_t scanCode = 0;
        std::string label;
        bool shown = false;
        bool operator==(const HintButton&) const = default;
    };

    /// A block as the frame sees it: its final id and whether it shows now (a place on this tab, not hidden, the columns up)
    struct HintBlock
    {
        std::string id;
        bool shown = false;
    };

    /// The frame's hint buttons in strip order: the host's own (a_host; none when its scanCode is 0 or its label empty) first,
    /// shown on the keyboard in every mode; then every hint of a_blocks (registration order) in the block's own order, shown while
    /// its block shows and the layout mode is off. None shows while SkyUI shows gamepad buttons (a_gamepad). A hint of a block not
    /// in a_blocks has no button. Every hint gets a button, shown or not, so a change of tab or mode only shows or hides buttons
    inline std::vector<HintButton> HintButtons(const HintButton& a_host, const std::vector<HintBlock>& a_blocks, const KeyHintList& a_hints, bool a_layoutMode,
        bool a_gamepad)
    {
        std::vector<HintButton> buttons;
        if (a_host.scanCode != 0 && !a_host.label.empty()) buttons.push_back({ a_host.scanCode, a_host.label, !a_gamepad });
        for (const auto& block : a_blocks) {
            for (const auto& hint : a_hints) {
                if (hint.id == block.id) buttons.push_back({ hint.scanCode, hint.label, block.shown && !a_layoutMode && !a_gamepad });
            }
        }
        return buttons;
    }

    /// The buttons' keys and labels alike, their shown flags aside: when they differ, our buttons are made again (a shown flag
    /// only toggles _visible)
    inline bool SameHintSet(const std::vector<HintButton>& a_a, const std::vector<HintButton>& a_b)
    {
        return std::ranges::equal(a_a, a_b, [](const HintButton& a_x, const HintButton& a_y) { return a_x.scanCode == a_y.scanCode && a_x.label == a_y.label; });
    }

    /// Buttons to add to SkyUI's panel before a_needed more are taken from it: what a_length buttons with a_used in use lack,
    /// within a_growLeft (the cap on the buttons we add to one panel)
    constexpr std::size_t ButtonsToGrow(std::size_t a_length, std::size_t a_used, std::size_t a_needed, std::size_t a_growLeft)
    {
        const std::size_t free = a_length > a_used ? a_length - a_used : 0;
        return a_needed > free ? std::min(a_needed - free, a_growLeft) : 0;
    }

    /// Our a_count buttons from index a_first are the panel's last ones in use (a_used): only then can they be taken off its end
    /// without SkyUI's buttons made again
    constexpr bool OursAtTail(std::size_t a_first, std::size_t a_count, std::size_t a_used)
    {
        return a_first + a_count == a_used;
    }

    /// A shown button's place in the panel (_x, _y in the panel's own space)
    struct ButtonSpot
    {
        double x = 0.0;
        double y = 0.0;
    };

    inline constexpr double kRowTolerance = 0.5;  // panel units: two buttons this close in _y are on one row

    /// SelfTest L10: a_now is laid out after the shown button before it (a_last; none: the first shown) in the panel's reading
    /// order - right of it on its row, or on a later row. SkyUI's ButtonPanel.doUpdateButtons (ButtonPanel.as:106-128) lays
    /// every button out on one row; Untarnished UI's bottombar.swf (the one installed in TAKEALOOK, disassembled 2026-10-31)
    /// starts a new row at every 4th button index that shows (x 0, y + 35, spacing 2), so x alone goes back to 0 there
    constexpr bool LaidOutAfter(const std::optional<ButtonSpot>& a_last, const ButtonSpot& a_now)
    {
        if (!a_last) return true;
        if (a_now.y > a_last->y + kRowTolerance) return true;
        return a_now.y >= a_last->y - kRowTolerance && a_now.x > a_last->x;
    }
}

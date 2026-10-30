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
// content clip's pad, the block's visible part and its overlay's clears. In namespace
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
            a_why = "apiVersion " + std::to_string(request.apiVersion) + " (this SL_MagicMenu reads " + std::to_string(BlockApi::kVersion) + ")";
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
}

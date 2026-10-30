#pragma once

#include "BlockRules.h"
#include "Layout.h"
#include "Settings.h"

#include <cmath>
#include <cstddef>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// =============================================================================
// BLOCK RULES - the key-hint strip, the gear, the reset button and the board
// after the review of 2026-10-30 (Task 8): reading the strip when one shown
// button's bounds never read (R1), the gear's row while this menu's strip size
// is unknown (R2), leaving SkyUI's strip where it is when moving it would pull
// it up into the board (R3), and the self-test verdicts read back in game (L8
// last, L9 board / gear / reset, L1's per-block SKIP). In namespace BlockRules
// beside BlockRules.h (which is at the repo's 600-line limit). No game types:
// tools/magic-menu-layout-check runs these on made-up cases. Stage units, y down.
// =============================================================================

namespace BlockRules
{
    /// The union of the shown children whose bounds read (an unread one left out); nullopt when none of them read
    inline std::optional<Settings::Rect> ReadUnion(const std::vector<ChildBox>& a_children)
    {
        std::vector<ChildBox> read;
        for (const auto& child : a_children) {
            if (child.visible && child.read) read.push_back(child);
        }
        return VisibleUnion(read);
    }

    /// Two strip signatures alike, a value that did not read (NaN) alike to another NaN - for counting failed reads only: the
    /// content cache keeps comparing with == (a NaN never matches, so the strip is read again)
    inline bool SameSignature(const std::vector<double>& a_a, const std::vector<double>& a_b)
    {
        if (a_a.size() != a_b.size()) return false;
        for (std::size_t i = 0; i < a_a.size(); ++i) {
            if (!(a_a[i] == a_b[i]) && !(std::isnan(a_a[i]) && std::isnan(a_b[i]))) return false;
        }
        return true;
    }

    /// R1 fix round: the strip signature's look - without its first two entries (the panel's origin on the stage), so a move of
    /// the panel (ours, or SkyUI's positionElements) does not restart the fail count while the same child stays unread
    inline constexpr std::size_t kSignatureOriginEntries = 2;

    inline std::vector<double> StripLook(const std::vector<double>& a_signature)
    {
        if (a_signature.size() <= kSignatureOriginEntries) return {};
        return { a_signature.begin() + kSignatureOriginEntries, a_signature.end() };
    }

    /// What one read of the strip's children gives (R1)
    struct StripRead
    {
        std::optional<Settings::Rect> content;  // the visible content to place by (nullopt: none this frame)
        bool cache = false;                     // kept with the signature (read again only when it changes)
        bool partial = false;                   // built without the shown child(ren) whose bounds did not read
    };

    /// R1: every shown child read - the union, cached, the fail count back to 0. A shown child unread - nothing this frame (read
    /// again next), a_fails counted up while the signature stays the same (a_sameSignature; restarted at 1 when it changed);
    /// once it reaches a_pollFrames, the union of the children that read, cached with the signature (partial). Nothing read at
    /// all: no content even then (cached, so the strip is not read again every frame until something changes)
    inline StripRead ReadStrip(const std::vector<ChildBox>& a_children, bool a_sameSignature, int& a_fails, int a_pollFrames)
    {
        if (AllShownRead(a_children)) {
            a_fails = 0;
            return { VisibleUnion(a_children), true, false };
        }
        a_fails = a_sameSignature ? a_fails + 1 : 1;
        if (a_fails < a_pollFrames) return {};
        return { ReadUnion(a_children), true, true };
    }

    /// R2: the strip size the gear's row is laid out with - this menu's when known, else the last one seen (an earlier menu's),
    /// else none (0 x 0: the gear alone under the board)
    inline std::pair<double, double> RowStripSize(const std::optional<std::pair<double, double>>& a_thisMenu, const std::optional<std::pair<double, double>>& a_last)
    {
        if (a_thisMenu) return *a_thisMenu;
        if (a_last) return *a_last;
        return { 0.0, 0.0 };
    }

    /// R3: the strip's row (HintRowRects) pulled up into the board - its top above a_boardBottom + a_gap by more than a_tolerance
    /// (the columns reach the stage's bottom). The strip is then left where SkyUI put it: our board and panels in mmx_root are
    /// drawn over SkyUI's Menu_mc and would cover it. Only for a strip of known size (no strip: nothing to pull up)
    inline bool StripCoversBoard(const HintRow& a_row, double a_boardBottom, double a_gap, double a_tolerance)
    {
        if (!(a_row.strip.Width() > 0.0 && a_row.strip.Height() > 0.0)) return false;
        return a_row.strip.top < a_boardBottom + a_gap - a_tolerance;
    }

    enum class Verdict3
    {
        kPass,
        kFail,
        kSkip,
    };

    constexpr const char* VerdictText(Verdict3 a_verdict)
    {
        return a_verdict == Verdict3::kPass ? "PASS" : a_verdict == Verdict3::kFail ? "FAIL" : "SKIP";
    }

    /// L8 last (T1): the strip's last target (where it was last placed) against where its visible content was read on a frame
    /// after that move (a_readAfterMove: a read on a later frame; the move's own frame read before it). SKIP with nothing placed
    /// or no read after the last move
    inline Verdict3 StripLastCheck(const std::optional<Settings::Rect>& a_target, const std::optional<Point>& a_read, bool a_readAfterMove, double a_tolerance)
    {
        if (!a_target || !a_read || !a_readAfterMove) return Verdict3::kSkip;
        return NearPoint(*a_read, Point{ a_target->left, a_target->top }, a_tolerance) ? Verdict3::kPass : Verdict3::kFail;
    }

    inline std::string RectString(const Settings::Rect& a_rect)
    {
        return std::format("{:.0f},{:.0f}..{:.0f},{:.0f}", a_rect.left, a_rect.top, a_rect.right, a_rect.bottom);  // a NaN prints "nan"
    }

    inline bool NearRect(const Settings::Rect& a_a, const Settings::Rect& a_b, double a_tolerance)
    {
        return std::abs(a_a.left - a_b.left) <= a_tolerance && std::abs(a_a.top - a_b.top) <= a_tolerance && std::abs(a_a.right - a_b.right) <= a_tolerance &&
               std::abs(a_a.bottom - a_b.bottom) <= a_tolerance;
    }

    /// L9 board (T2): what is wrong with the board ("" when nothing) - the rect kept for the menu against BoardRect over the
    /// columns now (a_recomputed), and the drawn clip's bounds (a_drawn; nullopt: did not read) against it, a_edge larger (its
    /// line's half width on each side, Flash's getBounds counts the stroke)
    inline std::string BoardProblem(const Settings::Rect& a_kept, const Settings::Rect& a_recomputed, const std::optional<Settings::Rect>& a_drawn, double a_edge,
        double a_tolerance)
    {
        std::string why;
        if (!NearRect(a_kept, a_recomputed, a_tolerance)) why += "kept " + RectString(a_kept) + " but BoardRect now " + RectString(a_recomputed) + "; ";
        if (!a_drawn) {
            why += "the board clip's bounds did not read; ";
        } else if (!NearRect(*a_drawn, a_kept, a_edge + a_tolerance)) {
            why += "drawn at " + RectString(*a_drawn) + " (want " + RectString(a_kept) + " within " + std::format("{:.1f}", a_edge) + "); ";
        }
        return why;
    }

    /// L9 gear (T2): the gear's read-back (_x/_y, a_read; nullopt: did not read) against where it was put (a_put), and a_put
    /// itself: inside a_visible, its top a_gap or more under the board's bottom unless the stage's bottom held it up, its left
    /// edge on a_left (the strip's start: the left column's left edge, moved left only to stay on the stage)
    inline std::string GearProblem(const std::optional<Point>& a_read, const Settings::Rect& a_put, double a_boardBottom, double a_gap, double a_left,
        const Settings::Rect& a_visible, double a_tolerance)
    {
        std::string why;
        if (!a_read) {
            why += "its _x/_y did not read; ";
        } else if (!NearPoint(*a_read, Point{ a_put.left, a_put.top }, a_tolerance)) {
            why += std::format("at {:.1f},{:.1f}", a_read->x, a_read->y) + " but put at " + RectString(a_put) + "; ";
        }
        if (a_put.left < a_visible.left - a_tolerance || a_put.right > a_visible.right + a_tolerance || a_put.top < a_visible.top - a_tolerance ||
            a_put.bottom > a_visible.bottom + a_tolerance) {
            why += "off the stage " + RectString(a_visible) + "; ";
        }
        const bool heldUp = a_put.bottom >= a_visible.bottom - a_tolerance;
        if (!heldUp && a_put.top < a_boardBottom + a_gap - a_tolerance) why += "over the board (its bottom " + std::format("{:.1f}", a_boardBottom) + "); ";
        if (a_put.left > a_left + a_tolerance) why += "right of the strip's start " + std::format("{:.1f}", a_left) + "; ";
        return why;
    }

    /// L9 reset (T2): the reset button's read-back - its place (_x/_y, a_read) against ResetButtonRect (a_want), shown exactly
    /// while any of it is out (a_shownW > 0), its mask as wide as the part out (a_maskW, a_edge wider: the mask's rect is). Its
    /// clicks (taken only fully out) are counted at the click and judged at the close
    inline std::string ResetProblem(const std::optional<Point>& a_read, const Settings::Rect& a_want, std::optional<bool> a_shown, double a_shownW,
        std::optional<double> a_maskW, double a_edge, double a_tolerance)
    {
        std::string why;
        if (!a_read) {
            why += "its _x/_y did not read; ";
        } else if (!NearPoint(*a_read, Point{ a_want.left, a_want.top }, a_tolerance)) {
            why += std::format("at {:.1f},{:.1f}", a_read->x, a_read->y) + " but ResetButtonRect " + RectString(a_want) + "; ";
        }
        if (!a_shown || *a_shown != (a_shownW > 0.0)) why += "_visible does not match the width out (" + std::format("{:.1f}", a_shownW) + "); ";
        if (a_shownW > 0.0 && (!a_maskW || std::abs(*a_maskW - (a_shownW + a_edge)) > a_tolerance)) {
            why += "its mask " + (a_maskW ? std::format("{:.1f}", *a_maskW) : std::string("unread")) + " wide, want " + std::format("{:.1f}", a_shownW + a_edge) + "; ";
        }
        return why;
    }

    /// L1 (Task 5 review): whether the core's reference rect (the default layout with nothing ahead) holds for a_id in its column
    /// a_column (the layout's order): the preview while no arranged add-on is ahead of it; the spell card while none
    /// is ahead of it, and - our card (a_fills: it fills the rest of the column, Layout::Arrange) - none after it either (each
    /// shortens the fill). a_arrangedAddon: an add-on arranged on this tab (hidden ones close up and do not count)
    inline bool CoreCompared(const std::vector<Layout::BlockId>& a_column, const Layout::BlockId& a_id, bool a_fills,
        const std::function<bool(const Layout::BlockId&)>& a_arrangedAddon)
    {
        bool ahead = true;
        for (const auto& id : a_column) {
            if (id == a_id) {
                ahead = false;
                continue;
            }
            if (!a_arrangedAddon(id)) continue;
            if (ahead || (a_fills && a_id == Layout::kSpellCard)) return false;
        }
        return !ahead;  // not in the column: nothing to compare
    }
}

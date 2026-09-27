#pragma once

#include "Common.h"

#include <atomic>
#include <exception>

#include <nlohmann/json.hpp>

// =============================================================================
// LayoutDeclutter - no spell hidden under another spell, a line or the heart
// =============================================================================
//
// The native twin of the panel's modules/layoutDeclutter.js (with
// layoutLineClear.js and layoutLineGrid.js): the same pass, the same
// constants, the same order of work and the same sums, so it moves every spell
// to the same place. The panel sends a tree about to be saved (DeclutterTree),
// this runs it on a worker thread and sends the positions back
// (onDeclutterResult); the panel's browser has no JIT and took seconds of
// frames for a big tree. See docs/TREE_BUILDING_SYSTEM.md, "Decluttering
// before save".
//
// No RE:: or SKSE use: safe on any thread. Each call has its own state, so two
// calls may run at once.

namespace LayoutDeclutter
{
    // Thrown by Run when its cancel flag is set (a newer request, the plugin unloading)
    struct Cancelled : std::exception
    {
        const char* what() const noexcept override { return "declutter cancelled"; }
    };

    /**
     * Declutter one tree.
     *
     * request: { id, schools, globe: {x, y, radius}, layoutMode, noRotate }
     *   schools is either an array [{ name, startAngle, endAngle, nodes }] taken
     *   in that order, or the saved tree's object { name: { startAngle,
     *   endAngle, nodes } } taken by name; nodes are [{ formId, x, y, isRoot,
     *   children }].
     *
     * reply: { id, positions: [[formId, x, y], ...] in the order the spells
     *   were taken (schools in order, nodes in array order, spells without a
     *   position skipped), moved, rounds, overlapsLeft, linesLeft, linesMoved,
     *   passes, ms }. Fewer than two positioned spells: positions is empty and
     *   nothing moves (skipped: true). A spell whose x or y is not a finite
     *   number within +-1e6 (kMaxCoord) counts as one without a position.
     *
     * cancel (may be null) is checked once per spell searched and once per
     * push-apart round; once it is set, Run throws Cancelled.
     *
     * Throws on a request it cannot read.
     */
    nlohmann::json Run(const nlohmann::json& request, const std::atomic<bool>* cancel = nullptr);
}

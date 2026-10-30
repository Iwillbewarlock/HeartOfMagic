#pragma once

#include "BlockApi.h"

#include <cstdint>

// =============================================================================
// SELECTION - the Heart of Magic block's pick rules (HoM add-on plan Task 6),
// no game types: tools/magic-menu-hom-block-check runs them. Bug G1 (design
// "Key-picked node"): a node the U key picked counts as chosen for the add-on's
// own tree and node card, so the node card shows the pick and G acts on it; the
// 3D preview and the core's spell card keep following the list (a key pick sends
// no 'SLMS'). A list change drops the pick, unless it was the add-on's own
// served show request.
// =============================================================================

namespace Selection
{
    /// The add-on's blocks have something to show: the menu's card shows a form (a_shownForm, the host's Context::shown or
    /// drawSelection's a_shown) or the U key picked a node (a_keyPick, its node index; -1 = none)
    constexpr bool Chosen(std::uint32_t a_shownForm, int a_keyPick)
    {
        return a_shownForm != 0 || a_keyPick >= 0;
    }

    /// The set-target key (G) and the node card's button act only when something is chosen (review I8: with nothing chosen the
    /// node card shows its hint, nothing a press could act on)
    constexpr bool SetTargetActs(std::uint32_t a_shownForm, int a_keyPick)
    {
        return Chosen(a_shownForm, a_keyPick);
    }

    /// The list selection changed since the add-on last looked (Context::selectionSeq; A -> C -> A still moves it): a key pick
    /// or clicked node is dropped
    constexpr bool DropPick(std::uint32_t a_seqSeen, std::uint32_t a_seqNow)
    {
        return a_seqSeen != a_seqNow;
    }

    /// The selection came from this add-on's own show request (Context::source kAddon, servedRequest its last requestId; 0 =
    /// it sent none): the change is its own, not the player's
    constexpr bool OwnRequest(BlockApi::SelectionSource a_source, std::uint32_t a_servedRequest, std::uint32_t a_lastRequest)
    {
        return a_lastRequest != 0 && a_source == BlockApi::SelectionSource::kAddon && a_servedRequest == a_lastRequest;
    }

    /// DropPick, but a change made by the add-on's own served request keeps the pick
    constexpr bool DropPick(std::uint32_t a_seqSeen, std::uint32_t a_seqNow, BlockApi::SelectionSource a_source, std::uint32_t a_servedRequest,
        std::uint32_t a_lastRequest)
    {
        return DropPick(a_seqSeen, a_seqNow) && !OwnRequest(a_source, a_servedRequest, a_lastRequest);
    }
}

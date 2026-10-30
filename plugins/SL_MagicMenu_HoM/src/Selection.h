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

    /// The node the add-on's tree and node card show as picked: a clicked node, or the U key's (byKey)
    struct Pick
    {
        int node = -1;
        bool byKey = false;

        constexpr bool operator==(const Pick&) const = default;
    };

    /// The pick after one step of the tree's frame: the list change first (a_drop: DropPick said so), then the U key's pick
    /// (a_next >= 0: the node it picked; -1 = no U, or nothing to pick). f83dc6f3's frame order (WatchSelection, then DoNext,
    /// W:8174-8185): a U pressed in the frame of a list change survives it
    constexpr Pick PickAfterFrame(Pick a_pick, bool a_drop, int a_next)
    {
        if (a_drop) a_pick = {};
        if (a_next >= 0) a_pick = { a_next, true };
        return a_pick;
    }

    /// A key pick made while the card showed a_pickShown is stale once the card shows another form (a_shownNow): the host draws
    /// the node card for a new form before the tree's frame drops the pick, and that draw treats it as dropped
    constexpr bool StaleKeyPick(bool a_byKey, std::uint32_t a_pickShown, std::uint32_t a_shownNow)
    {
        return a_byKey && a_pickShown != a_shownNow;
    }

    /// The set-target requests of one frame: the G key and the node card's button in the same frame are one request (f83dc6f3
    /// W:8186-8189), none when nothing is chosen (a_acts: SetTargetActs)
    constexpr int SetTargetRequests(bool a_key, bool a_button, bool a_acts)
    {
        return (a_key || a_button) && a_acts ? 1 : 0;
    }
}

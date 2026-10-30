#pragma once

// =============================================================================
// BLOCK REGISTRY - the add-on blocks other plugins registered (blocks plan Task 6,
// D9; API v2 2026-10-29, v3 2026-10-30): the checks a Registration must pass, the id prefix,
// the registration order and the window that closes at the first magic menu
// open. Pure: no GFx, no game calls, no logger, no lock (the caller holds one),
// so the offline check tools tools/magic-menu-layout-check and
// tools/magic-menu-block-api-check link it too.
// =============================================================================

#include "BlockApi.h"

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace BlockRegistry
{
    /// One accepted block, with its strings copied
    struct Block
    {
        std::string id;      // the final id (prefixed): the layout's key
        std::string title;   // "" = no title strip
        double height{};
        BlockApi::DrawFn draw{};
        void* user{};
        std::string sender;  // the plugin that registered it
        // API v2 (null / kTabsAll for a v1 block)
        std::uint32_t apiVersion = BlockApi::kVersionV1;
        BlockApi::DrawSelectionFn drawSelection{};
        BlockApi::ClickFn click{};
        std::uint32_t tabs = BlockApi::kTabsAll;
        // API v3 (null / kPadDefault / 0 / "" for a v1 or v2 block)
        BlockApi::FrameFn frame{};
        BlockApi::KeyFn key{};
        double pad = BlockApi::kPadDefault;  // < 0: the host's pad
        std::uint32_t flags = 0;
        std::string legacyId;                // the retired built-in id this block takes over ("" = none)
    };
    using Entry = Block;  // the name the HoM add-on plan (2026-10-30) uses

    struct Outcome
    {
        bool accepted = false;
        std::string id;      // the final id; the id as sent (or "") when refused before it was read
        std::string reason;  // "" when accepted
        std::uint32_t apiVersion = BlockApi::kVersion;  // the version the registration was read as (BlockApi::Result::apiVersion)
    };

    /// The Registration fields a message carries: apiVersion 1 reads the frozen v1 fields only, 2 the frozen v2 ones, 3 (and any
    /// other) the v3 ones; of those, only the fields that min(structSize, a_dataLen) covers whole are copied, the rest stay as a
    /// value-initialised Registration has them (null / 0, pad kPadDefault). a_data must hold at least the two header fields
    BlockApi::Registration ReadRegistration(const void* a_data, std::uint32_t a_dataLen);

    /// The id as the layout keys it: an id with no '.' gets "<sender>." in front (an empty sender: "addon.")
    std::string QualifiedId(std::string_view a_sender, std::string_view a_id);

    class Registry
    {
    public:
        /// a_reserved: ids that are taken already (the built-in blocks)
        explicit Registry(std::set<std::string> a_reserved = {}) : _reserved(std::move(a_reserved)) {}

        /// One registration message: a_data / a_dataLen as the SKSE message carries them. Checked in this order - the
        /// window, the data's size, apiVersion (1, 2 or 3), structSize (at least v1's), then id, height, draw (v1) or draw /
        /// drawSelection (v2, v3), tabs (known bits only), v3's flags (known bits only) and pad (finite), the id's uniqueness,
        /// v3's legacyId (BlockRules::JudgeLegacy). Accepted ones are kept in registration order. O(blocks) per call
        Outcome Register(std::string_view a_sender, const void* a_data, std::uint32_t a_dataLen);

        /// Each claimed retired id -> the final id of the block that took it over (legacy -> final)
        [[nodiscard]] std::map<std::string, std::string> LegacyAliases() const;

        /// Closes the window (the first magic menu open): every later Register is refused. True the first time
        bool Close();
        [[nodiscard]] bool Closed() const { return _closed; }

        [[nodiscard]] const std::vector<Block>& Blocks() const { return _blocks; }
        [[nodiscard]] const Block* Find(std::string_view a_id) const;

    private:
        std::set<std::string> _reserved;
        std::vector<Block> _blocks;
        bool _closed = false;
    };
}

#pragma once

// =============================================================================
// BLOCK REGISTRY - the add-on blocks other plugins registered (blocks plan Task 6,
// D9; API v2 2026-10-29): the checks a Registration must pass, the id prefix,
// the registration order and the window that closes at the first magic menu
// open. Pure: no GFx, no game calls, no logger, no lock (the caller holds one),
// so the offline check tools tools/magic-menu-layout-check and
// tools/magic-menu-block-api-check link it too.
// =============================================================================

#include "BlockApi.h"

#include <cstdint>
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
    };

    struct Outcome
    {
        bool accepted = false;
        std::string id;      // the final id; the id as sent (or "") when refused before it was read
        std::string reason;  // "" when accepted
        std::uint32_t apiVersion = BlockApi::kVersion;  // the version the registration was read as (BlockApi::Result::apiVersion)
    };

    /// The Registration fields a message carries: apiVersion 1 reads the frozen v1 fields only, 2 the v2 ones; of those, only
    /// the fields that min(structSize, a_dataLen) covers whole are copied, the rest stay as a zero-initialised Registration has
    /// them. a_data must hold at least the two header fields
    BlockApi::Registration ReadRegistration(const void* a_data, std::uint32_t a_dataLen);

    /// The id as the layout keys it: an id with no '.' gets "<sender>." in front (an empty sender: "addon.")
    std::string QualifiedId(std::string_view a_sender, std::string_view a_id);

    class Registry
    {
    public:
        /// a_reserved: ids that are taken already (the built-in blocks)
        explicit Registry(std::set<std::string> a_reserved = {}) : _reserved(std::move(a_reserved)) {}

        /// One registration message: a_data / a_dataLen as the SKSE message carries them. Checked in this order - the
        /// window, the data's size, apiVersion (1 or 2), structSize (at least v1's), then id, height, draw (v1) or draw /
        /// drawSelection (v2), tabs (known bits only), the id's uniqueness. Accepted ones are kept in registration order.
        /// O(blocks) per call
        Outcome Register(std::string_view a_sender, const void* a_data, std::uint32_t a_dataLen);

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

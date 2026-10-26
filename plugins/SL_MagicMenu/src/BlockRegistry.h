#pragma once

// =============================================================================
// BLOCK REGISTRY - the add-on blocks other plugins registered (blocks plan Task 6,
// D9): the checks a Registration must pass, the id prefix, the registration
// order and the window that closes at the first magic menu open. Pure: no GFx,
// no game calls, no logger, no lock (the caller holds one), so the offline
// check tool tools/magic-menu-layout-check links it too.
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
    };

    struct Outcome
    {
        bool accepted = false;
        std::string id;      // the final id; the id as sent (or "") when refused before it was read
        std::string reason;  // "" when accepted
    };

    /// The id as the layout keys it: an id with no '.' gets "<sender>." in front (an empty sender: "addon.")
    std::string QualifiedId(std::string_view a_sender, std::string_view a_id);

    class Registry
    {
    public:
        /// a_reserved: ids that are taken already (the built-in blocks)
        explicit Registry(std::set<std::string> a_reserved = {}) : _reserved(std::move(a_reserved)) {}

        /// One registration message: a_data / a_dataLen as the SKSE message carries them. Checked in this order - the
        /// window, the data's size, apiVersion, structSize, then id, height, draw, the id's uniqueness. Accepted ones are kept
        /// in registration order. O(blocks) per call
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

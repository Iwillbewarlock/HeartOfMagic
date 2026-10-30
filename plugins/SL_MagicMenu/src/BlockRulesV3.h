#pragma once

#include "BlockApi.h"
#include "BlockRules.h"
#include "Layout.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

// =============================================================================
// BLOCK RULES v3 - the pure parts of the Block API v3 host side (HoM add-on
// design 2026-10-30, "Block API v3" and "Changes after the GPT design review"):
// the retired ids a v3 block may take over (legacyId), reading the 'SLMS' and
// 'SLMV' messages, and whether a queued 'SLMS' is still due. In namespace
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
}

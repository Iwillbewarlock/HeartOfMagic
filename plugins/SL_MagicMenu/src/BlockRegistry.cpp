#include "BlockRegistry.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <format>
#include <utility>

namespace BlockRegistry
{
    namespace
    {
        // The size of the two header fields every version starts with
        constexpr std::uint32_t kHeaderSize = 2 * sizeof(std::uint32_t);

        // A C string from another plugin read with a bound: nullopt when it runs past a_max characters
        std::optional<std::string> Bounded(const char* a_text, std::size_t a_max)
        {
            if (!a_text) return std::string{};
            const std::size_t length = strnlen(a_text, a_max + 1);
            if (length > a_max) return std::nullopt;
            return std::string(a_text, length);
        }

        Outcome Refused(std::string a_id, std::string a_reason, std::uint32_t a_version = BlockApi::kVersion)
        {
            return { false, std::move(a_id), std::move(a_reason), a_version };
        }

        // a_field copied from a_data when the first a_covered bytes hold it whole (a field cut short stays as it is)
        template <class T>
        void CopyField(const void* a_data, std::size_t a_covered, std::size_t a_offset, T& a_field)
        {
            if (a_offset + sizeof(T) <= a_covered) std::memcpy(&a_field, static_cast<const std::byte*>(a_data) + a_offset, sizeof(T));
        }
    }

    BlockApi::Registration ReadRegistration(const void* a_data, std::uint32_t a_dataLen)
    {
        using BlockApi::Registration;
        Registration reg{};
        std::uint32_t header[2]{};
        std::memcpy(header, a_data, kHeaderSize);
        reg.apiVersion = header[0];
        reg.structSize = header[1];
        // v1 is frozen: the bytes of a v1 sender past the v1 fields (its own struct grew) are never read as v2 fields
        const std::size_t layout = reg.apiVersion == BlockApi::kVersionV1 ? BlockApi::kRegistrationV1Size : sizeof(Registration);
        const std::size_t covered = std::min<std::size_t>({ reg.structSize, a_dataLen, layout });
        CopyField(a_data, covered, offsetof(Registration, id), reg.id);
        CopyField(a_data, covered, offsetof(Registration, title), reg.title);
        CopyField(a_data, covered, offsetof(Registration, height), reg.height);
        CopyField(a_data, covered, offsetof(Registration, draw), reg.draw);
        CopyField(a_data, covered, offsetof(Registration, user), reg.user);
        CopyField(a_data, covered, offsetof(Registration, drawSelection), reg.drawSelection);
        CopyField(a_data, covered, offsetof(Registration, click), reg.click);
        CopyField(a_data, covered, offsetof(Registration, tabs), reg.tabs);
        return reg;
    }

    std::string QualifiedId(std::string_view a_sender, std::string_view a_id)
    {
        if (a_id.find('.') != std::string_view::npos) return std::string(a_id);
        return std::format("{}.{}", a_sender.empty() ? std::string_view("addon") : a_sender, a_id);
    }

    Outcome Registry::Register(std::string_view a_sender, const void* a_data, std::uint32_t a_dataLen)
    {
        if (_closed) {
            return Refused({}, "registration is closed: the magic menu was opened already (register at kPostPostLoad .. kDataLoaded)");
        }
        if (!a_data || a_dataLen < kHeaderSize) {
            return Refused({}, std::format("no Registration in the message (data {}, {} byte(s))", a_data ? "set" : "null", a_dataLen));
        }
        std::uint32_t header[2]{};
        std::memcpy(header, a_data, kHeaderSize);
        const std::uint32_t version = header[0], size = header[1];
        if (version != BlockApi::kVersionV1 && version != BlockApi::kVersion) {
            return Refused({}, std::format("apiVersion {} is not one this SL_MagicMenu reads ({} or {}){}", version, BlockApi::kVersionV1,
                                   BlockApi::kVersion, version > BlockApi::kVersion ? ": update SL_MagicMenu" : ""));
        }
        if (size < BlockApi::kRegistrationV1Size || a_dataLen < BlockApi::kRegistrationV1Size) {
            return Refused({}, std::format("structSize {} / message size {} is smaller than the v1 Registration ({})", size, a_dataLen,
                                   BlockApi::kRegistrationV1Size), version);
        }
        const BlockApi::Registration reg = ReadRegistration(a_data, a_dataLen);

        const auto rawId = Bounded(reg.id, BlockApi::kMaxIdLength);
        if (!rawId) return Refused({}, std::format("the id is longer than {} characters", BlockApi::kMaxIdLength), version);
        if (rawId->empty()) return Refused({}, "the id is empty", version);
        const std::string id = QualifiedId(a_sender, *rawId);
        if (!std::isfinite(reg.height) || reg.height <= 0.0f || reg.height > BlockApi::kMaxHeight) {
            return Refused(id, std::format("the height {} is not a finite number in 0 < height <= {}", reg.height, BlockApi::kMaxHeight), version);
        }
        if (version == BlockApi::kVersionV1 && !reg.draw) return Refused(id, "no draw callback", version);
        if (!reg.draw && !reg.drawSelection) return Refused(id, "no draw callback (v2: neither draw nor drawSelection)", version);
        if ((reg.tabs & ~BlockApi::kTabsKnown) != 0) {
            return Refused(id, std::format("tabs 0x{:X} has bits this SL_MagicMenu does not know (known: 0x{:X}): update SL_MagicMenu", reg.tabs,
                                   BlockApi::kTabsKnown), version);
        }
        if (_reserved.contains(id)) return Refused(id, "the id is a built-in block's", version);
        if (const Block* taken = Find(id)) return Refused(id, std::format("the id is registered already (by {})", taken->sender), version);

        std::string title = reg.title ? std::string(reg.title, strnlen(reg.title, BlockApi::kMaxTitleLength)) : std::string{};
        _blocks.push_back({ id, std::move(title), static_cast<double>(reg.height), reg.draw, reg.user, std::string(a_sender), version,
            reg.drawSelection, reg.click, reg.tabs });
        return { true, id, {}, version };
    }

    bool Registry::Close()
    {
        return !std::exchange(_closed, true);
    }

    const Block* Registry::Find(std::string_view a_id) const
    {
        for (const auto& block : _blocks) {
            if (block.id == a_id) return &block;
        }
        return nullptr;
    }
}

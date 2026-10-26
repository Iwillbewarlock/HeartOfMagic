#include "BlockRegistry.h"

#include <cmath>
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

        Outcome Refused(std::string a_id, std::string a_reason)
        {
            return { false, std::move(a_id), std::move(a_reason) };
        }
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
        if (version != BlockApi::kVersion) {
            return Refused({}, std::format("apiVersion {} is not one this SL_MagicMenu reads ({}){}", version, BlockApi::kVersion,
                                   version > BlockApi::kVersion ? ": update SL_MagicMenu" : ""));
        }
        if (size < BlockApi::kRegistrationV1Size || a_dataLen < BlockApi::kRegistrationV1Size) {
            return Refused({}, std::format("structSize {} / message size {} is smaller than the v1 Registration ({})", size, a_dataLen,
                                   BlockApi::kRegistrationV1Size));
        }
        BlockApi::Registration reg;
        std::memcpy(&reg, a_data, BlockApi::kRegistrationV1Size);

        const auto rawId = Bounded(reg.id, BlockApi::kMaxIdLength);
        if (!rawId) return Refused({}, std::format("the id is longer than {} characters", BlockApi::kMaxIdLength));
        if (rawId->empty()) return Refused({}, "the id is empty");
        const std::string id = QualifiedId(a_sender, *rawId);
        if (!std::isfinite(reg.height) || reg.height <= 0.0f || reg.height > BlockApi::kMaxHeight) {
            return Refused(id, std::format("the height {} is not a finite number in 0 < height <= {}", reg.height, BlockApi::kMaxHeight));
        }
        if (!reg.draw) return Refused(id, "no draw callback");
        if (_reserved.contains(id)) return Refused(id, "the id is a built-in block's");
        if (const Block* taken = Find(id)) return Refused(id, std::format("the id is registered already (by {})", taken->sender));

        std::string title = reg.title ? std::string(reg.title, strnlen(reg.title, BlockApi::kMaxTitleLength)) : std::string{};
        _blocks.push_back({ id, std::move(title), static_cast<double>(reg.height), reg.draw, reg.user, std::string(a_sender) });
        return { true, id, {} };
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

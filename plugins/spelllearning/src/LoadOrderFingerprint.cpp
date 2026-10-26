#include "Common.h"
#include "LoadOrderFingerprint.h"

#include <atomic>
#include <cstdint>
#include <format>
#include <string_view>

namespace
{
    constexpr std::uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ULL;
    constexpr std::uint64_t kFnvPrime = 0x100000001b3ULL;
    constexpr std::string_view kNameEnd = "\n";
    constexpr std::string_view kLightPluginsMark = "|light|\n";

    void HashBytes(std::uint64_t& hash, std::string_view bytes)
    {
        for (const unsigned char c : bytes) {
            hash ^= c;
            hash *= kFnvPrime;
        }
    }

    // Set once by Current() on the game thread; read from any thread through Known()
    LoadOrderFingerprint::Value s_value;
    std::atomic<const LoadOrderFingerprint::Value*> s_published{ nullptr };
}

namespace LoadOrderFingerprint
{
    const Value& Current()
    {
        static const Value s_unknown{};
        if (const auto* known = s_published.load(std::memory_order_acquire)) return *known;

        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) return s_unknown;

        Value result;
        std::uint64_t hash = kFnvOffsetBasis;
        auto addFile = [&](const RE::TESFile* file) {
            if (!file) return;
            HashBytes(hash, file->GetFilename());
            HashBytes(hash, kNameEnd);
            ++result.plugins;
        };
        for (std::uint32_t i = 0; i < dh->GetLoadedModCount(); ++i) addFile(dh->GetLoadedMods()[i]);
        HashBytes(hash, kLightPluginsMark);
        if (const auto* light = dh->GetLoadedLightMods()) {
            for (std::uint32_t i = 0; i < dh->GetLoadedLightModCount(); ++i) addFile(light[i]);
        }
        result.spells = dh->GetFormArray<RE::SpellItem>().size();
        HashBytes(hash, std::to_string(result.spells));
        result.hash = std::format("{:016x}", hash);

        s_value = std::move(result);
        s_published.store(&s_value, std::memory_order_release);
        return s_value;
    }

    const Value* Known() noexcept
    {
        return s_published.load(std::memory_order_acquire);
    }
}

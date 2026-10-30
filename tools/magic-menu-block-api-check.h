#pragma once

// magic-menu-block-api-check's shared helpers (the PASS/FAIL counters, made-up callbacks, the registration builders) and
// the Spell Research add-on's own v2 header, for the check's two sources: magic-menu-block-api-check.cpp (v1, v2, the
// rules shared with v2) and magic-menu-block-api-check-v3.cpp (v3 registrations, messages and the v3 host rules)

#include "BlockApi.h"
#include "BlockRegistry.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

// The Spell Research add-on's own v2 header (std includes already in): an unmodified v2 add-on's bytes; its RE types are SrV2::RE
namespace SrV2
{
#include "../plugins/SL_MagicMenu_SpellResearch/src/BlockApi.h"
}

namespace BlockApiCheck
{
    inline int g_pass = 0;
    inline int g_fail = 0;

    inline constexpr unsigned char kJunkByte = 0xAB;  // bytes past a struct's covered part: never read as a field

    inline void Expect(bool a_ok, const std::string& a_what)
    {
        std::printf("  %s  %s\n", a_ok ? "PASS" : "FAIL", a_what.c_str());
        (a_ok ? g_pass : g_fail) += 1;
    }

    inline std::string Text(const char* a_format, auto... a_values)
    {
        char buffer[512];
        std::snprintf(buffer, sizeof(buffer), a_format, a_values...);
        return buffer;
    }

    inline void DrawNothing(RE::GFxMovieView*, RE::GFxValue&, float, float, void*) {}
    inline void DrawSelectionNothing(RE::GFxMovieView*, RE::GFxValue&, float, float, std::uint32_t, void*) {}
    inline void ClickNothing(float, float, float, float, void*) {}
    inline void FrameNothing(RE::GFxMovieView*, RE::GFxValue&, RE::GFxValue*, const BlockApi::Context&, void*) {}
    inline bool KeyNothing(std::uint32_t, void*) { return false; }
    inline void SrDrawSelection(SrV2::RE::GFxMovieView*, SrV2::RE::GFxValue&, float, float, std::uint32_t, void*) {}

    /// A function pointer's address, to compare pointers of the two headers' (distinct) callback types
    inline std::uintptr_t Address(auto a_fn) { return std::bit_cast<std::uintptr_t>(a_fn); }

    inline int g_user = 0;  // the user pointer handed back

    /// a_bytes of a_value, then a_extra junk bytes: what a sender with a bigger struct (or garbage after it) puts in the message
    template <class T>
    std::vector<unsigned char> Bytes(const T& a_value, std::size_t a_extra = 0)
    {
        std::vector<unsigned char> bytes(sizeof(T) + a_extra, kJunkByte);
        std::memcpy(bytes.data(), &a_value, sizeof(T));
        return bytes;
    }

    inline BlockRegistry::Outcome Send(BlockRegistry::Registry& a_registry, const std::vector<unsigned char>& a_bytes, std::uint32_t a_dataLen)
    {
        return a_registry.Register("AddonA", a_bytes.data(), a_dataLen);
    }

    inline BlockRegistry::Outcome Send(BlockRegistry::Registry& a_registry, const std::vector<unsigned char>& a_bytes)
    {
        return Send(a_registry, a_bytes, static_cast<std::uint32_t>(a_bytes.size()));
    }

    inline void ExpectRefused(const BlockRegistry::Outcome& a_outcome, std::string_view a_reasonPart, const std::string& a_case)
    {
        Expect(!a_outcome.accepted && a_outcome.reason.find(a_reasonPart) != std::string::npos,
            Text("%s: refused, reason has \"%s\" (got %s \"%s\")", a_case.c_str(), std::string(a_reasonPart).c_str(),
                a_outcome.accepted ? "accepted" : "refused", a_outcome.reason.c_str()));
    }

    inline BlockApi::RegistrationV1 V1(const char* a_id)
    {
        BlockApi::RegistrationV1 reg;
        reg.id = a_id;
        reg.title = "T";
        reg.height = 100.0f;
        reg.draw = DrawNothing;
        reg.user = &g_user;
        return reg;
    }

    inline BlockApi::RegistrationV2 V2(const char* a_id)
    {
        return { .id = a_id, .title = "T", .height = 100.0f, .user = &g_user, .drawSelection = DrawSelectionNothing, .click = ClickNothing,
            .tabs = BlockApi::kTabSpells };
    }

    inline BlockApi::Registration V3(const char* a_id, const char* a_legacyId = nullptr)
    {
        return { .id = a_id, .title = "T", .height = 100.0f, .user = &g_user, .drawSelection = DrawSelectionNothing, .click = ClickNothing,
            .tabs = BlockApi::kTabSpells, .frame = FrameNothing, .key = KeyNothing, .pad = 4.0f,
            .flags = BlockApi::kFlagOwnMouse | BlockApi::kFlagKeepWhenEmpty, .legacyId = a_legacyId };
    }

    /// No v3 field read: what a v1 / v2 block (or a v3 one cut at v2's size) is stored with
    inline bool NoV3Fields(const BlockRegistry::Block* a_block)
    {
        return a_block && !a_block->frame && !a_block->key && a_block->pad == BlockApi::kPadDefault && a_block->flags == 0 && a_block->legacyId.empty();
    }

    // magic-menu-block-api-check-v3.cpp
    void V3Registrations();
    void V3Messages();
    void V3Host();
    void V3KeyHints();
}

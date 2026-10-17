#pragma once

// =============================================================================
// FILE INDEX ENGINE - the state FileIndexInit.cpp finds and FileIndex.cpp uses (internal to FileIndex)
// =============================================================================
// Written once by Init on the main thread at kDataLoaded, before any Find or Register reads it; read-only afterwards.

#include "Common.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace BlackFlame::FileIndex::Engine
{
    using ID = RE::BSResource::ID;
    using Stream = RE::BSResource::Stream;
    using StreamPtr = RE::BSTSmartPointer<Stream>;
    using PathToIdFn = ID* (*)(ID*, const char*);
    using GuardCtorFn = void* (*)(void*);
    using GuardDtorFn = void (*)(void*);
    using FindRecordFn = bool (*)(void*, const ID*, std::byte**);
    using RegisterLooseFn = void (*)(void*, const ID*, StreamPtr*);

    inline constexpr std::size_t kGuardSize = 0x200;       // the engine's guard fits in this (Temp File Framework's buffer)
    inline constexpr std::size_t kSearchOffset = 0x08;     // the search state inside the guard
    inline constexpr std::size_t kRecordSizeFlags = 0x0C;  // int32: negative = archive record
    inline constexpr std::uint32_t kRefOne = 0x1000;        // StreamBase::kRefCountBeg
    inline constexpr std::uint32_t kRefMask = 0xFFFFF000u;  // StreamBase::kRefCountMask

    struct State
    {
        PathToIdFn pathToId = nullptr;
        GuardCtorFn guardCtor = nullptr;
        GuardDtorFn guardDtor = nullptr;
        FindRecordFn find = nullptr;
        RegisterLooseFn registerLoose = nullptr;
        void** manager = nullptr;
        RE::BSResource::GlobalLocations* globals = nullptr;
        std::string why = "not initialised";  // "" once every check passed
        bool initialised = false;
    };

    /// The one state (an inline function's static: one instance across the FileIndex translation units)
    [[nodiscard]] inline State& TheState()
    {
        static State state;
        return state;
    }

    /// The engine's Stream count offset (0x0C or 0x10); 0 while unknown. Read by our streams on IO threads
    [[nodiscard]] inline std::atomic<std::uint32_t>& CountOffset()
    {
        static std::atomic<std::uint32_t> offset{ 0 };
        return offset;
    }

    /// FindRecord for a_dataPath under its own guard, before Init's gate is set (the shipped-path confirmation)
    [[nodiscard]] bool RawFind(const std::string& a_dataPath, bool& a_found, bool& a_archive);

    /// "ids of '...': PathToID file=... | GenerateFromPath ... (the same | DIFFERENT)"
    [[nodiscard]] std::string DescribeIds(const std::string& a_dataPath);
}

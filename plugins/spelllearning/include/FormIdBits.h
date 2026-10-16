#pragma once

#include <cstdint>

// =============================================================================
// FORM ID BITS - the parts of a runtime form id (no game types)
// =============================================================================
//
// The high byte picks the plugin: a full plugin's load index, or 0xFE for a light
// plugin, whose number is the next 12 bits. The rest is the record's local id
// inside its plugin: 24 bits in a full plugin, 12 in a light one.
namespace FormIdBits
{
    inline constexpr unsigned kFileIndexShift = 24;
    inline constexpr std::uint32_t kLightIndex = 0xFE;
    /// Every light plugin's form ids start with this (kLightIndex in the high byte)
    inline constexpr std::uint32_t kLightPrefix = kLightIndex << kFileIndexShift;
    inline constexpr unsigned kLightFileShift = 12;
    inline constexpr std::uint32_t kLightFileMask = 0x00000FFF;
    inline constexpr std::uint32_t kFullLocalMask = 0x00FFFFFF;
    inline constexpr std::uint32_t kLightLocalMask = 0x00000FFF;
}

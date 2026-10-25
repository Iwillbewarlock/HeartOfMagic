#pragma once

// =============================================================================
// GreySelfTestKeyed.h - the key mode self-test's fixture, shared by its two parts (GreySelfTest.h CheckKeyed)
// =============================================================================
// GreySelfTestKeyed.cpp: the fixture, the request checks and the first runs; GreySelfTestKeyedRuns.cpp: the outcomes, the
// CLI, the dev mode and the throttle. Self-test only.

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif

#include "GreyKeyed.h"
#include "GreySelfTest.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace GreyBuild::SelfTest::Keyed
{
    inline constexpr std::uint64_t kModel = 0x1111111111111111ull;   // forms[0]: the source NIF as a PROJ model
    inline constexpr std::uint64_t kPlain = 0x2222222222222222ull;   // forms[1]: the AddOnNode-only NIF (values 9 and 40012)
    inline constexpr std::uint64_t kTxst = 0x3333333333333333ull;    // forms[2] and txst[0]
    inline constexpr std::uint64_t kMuzzle = 0x4444444444444444ull;  // muzzles[0]: the AddOnNode-only NIF
    inline constexpr std::uint64_t kMps = 0x5555555555555555ull;     // addnMps[0]: the source NIF as ADDN 9's MPS
    inline constexpr std::uint32_t kSlot = 0xF0C;
    inline constexpr int kSlotIndex = 40012;
    inline constexpr std::uint64_t kTinyCap = 64 * 1024;             // under the 64 x 64 test texture's estimate (109220)
    inline constexpr DWORD kChildTimeoutMs = 120000;                 // a CLI child run: 2 minutes at most
    inline constexpr const char* kFxFire = "textures\\effects\\fxfire01.dds";
    inline constexpr const char* kSourcePalette = "textures\\effects\\gradients\\gradflame01.dds";
    inline constexpr const char* kDecal = "textures\\effects\\decal.dds";

    enum class Variant
    {
        kNormal,
        kMissingPalette,  // gradflame01.dds not in the dump, decal.dds a BC5: fallbacks
        kNotDumped,       // fxfire01.dds outside the dump caps (state notdumped)
        kUnread,          // fxfire01.dds opened by the game, its read failed (state unread)
        kBc7              // fxfire01.dds a BC7 (the GPU encoder's target), otherwise kNormal
    };

    struct Fixture
    {
        std::filesystem::path folder;
        std::filesystem::path dump;
        std::filesystem::path staging;
    };

    std::filesystem::path Write(const std::filesystem::path& a_file, const std::string& a_text);
    [[nodiscard]] std::string ReadAll(const std::filesystem::path& a_file);
    [[nodiscard]] std::uint64_t Fnv(const std::filesystem::path& a_file);

    /// The game folder of a_variant, request.json written with a_tint in options (the prefix keeps 0.6), a_version and the
    /// prefix's output identity (empty: this build's own)
    Fixture MakeFixture(const std::filesystem::path& a_dir, const std::string& a_name, const std::filesystem::path& a_sourceNif,
        Variant a_variant, double a_tint = 0.6, int a_version = kGameRequestVersionKeyed, const std::string& a_identity = {});

    [[nodiscard]] int Run(const Fixture& a_fixture, std::uint64_t a_cap = kDefaultJobMemoryMiB * kBytesPerMiB);
    [[nodiscard]] BlackFlame::CopyContract::ProgressFile Progress(const Fixture& a_fixture);
    /// The last line for a_key, or nullptr
    [[nodiscard]] const BlackFlame::CopyContract::ProgressLine* Find(const BlackFlame::CopyContract::ProgressFile& a_file, std::uint64_t a_key);
    /// The first line for a_key's index, or the line count
    [[nodiscard]] std::size_t Position(const BlackFlame::CopyContract::ProgressFile& a_file, std::uint64_t a_key);
    [[nodiscard]] bool HasAddnDep(const BlackFlame::CopyContract::ProgressLine* a_line, std::uint32_t a_index);

    // GreySelfTestKeyedRuns.cpp
    void CheckOutcomes(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_sourceNif);
    void CheckCommandLine(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_sourceNif);
    void CheckDevMode(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_sourceNif);
    void CheckThrottleRun(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_sourceNif);
    void CheckPublishedRun(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_sourceNif);
}

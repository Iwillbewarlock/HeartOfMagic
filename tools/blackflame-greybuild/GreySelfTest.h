#pragma once

// =============================================================================
// GreySelfTest - shared by the --self-test files
// =============================================================================

#include "GreyBuild.h"

#include "../../plugins/BlackFlame/src/CopyContractKeys.h"

#include <nlohmann/json.hpp>

#include <iostream>
#include <string>

namespace GreyBuild::SelfTest
{
    struct Checker
    {
        int pass = 0;
        int fail = 0;

        void Check(bool a_ok, const std::string& a_what)
        {
            std::cout << (a_ok ? "PASS " : "FAIL ") << a_what << "\n";
            (a_ok ? pass : fail) += 1;
        }
    };

    /// Request planning (index reuse, collisions, unknown values) and the generated ESL,
    /// on a_sourceNif (AddOnNodes 9 and 24), in a_dir.
    void CheckPlanAndEsl(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_sourceNif);

    /// The v2 copies (--forms): ESL layout, stable ids, refusals, and a whole run with a_sourceNif as a model.
    void CheckForms(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_sourceNif);

    /// The game's request (v3), the copies map v2, the id budget, warn-and-continue and the per-file commit.
    void CheckGameRequest(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_sourceNif);

    /// The texture copies (GreyTexture.h): recolour, skips, failures and the NIF slot rule, on made-up files in a_dir.
    void CheckTextures(Checker& a_check, const std::filesystem::path& a_dir);

    /// The texture statistics (GreyTexStats.h, GreySelfTestStats.cpp): the integer ComputeStats against the double one it
    /// replaced (seeded and edge images, the two thresholds), the risk check's image from RecolourDds's own pixels, and
    /// LookStats across cache clears (hit, miss, a missing texture, our copies, a gated read); files in a_dir
    void CheckTexStats(Checker& a_check, const std::filesystem::path& a_dir);

    /// The path rule the DLL and the builder share (DataPathRule.h): normalising and the texture scan, one table (T7).
    void CheckPathRule(Checker& a_check);

    /// The decal texture set copies (GreyFormsTxst.h): types, record, map, ids, code page, texture step, old requests (a_dir)
    void CheckDecals(Checker& a_check, const std::filesystem::path& a_dir);

    /// The visual-risk check (GreyRisk.h), the glow rules (GreyGlow.h) and the derived palettes (GreyPalette.h, written to
    /// a_dir) on made-up NIFs and 4 x 4 textures.
    void CheckRisk(Checker& a_check, const std::filesystem::path& a_dir);

    /// A 64 x 64 DDS with a full mip chain (7 mips) in a_dxgiFormat (a DXGI_FORMAT value): orange with an alpha ramp, or
    /// mid grey (GreySelfTestTexture.cpp)
    [[nodiscard]] bool WriteTestTexture(const std::filesystem::path& a_file, unsigned a_dxgiFormat, bool a_orange);

    /// The single launch's parts on their own (GreySelfTestSlots.cpp): the slot pool ESL, --collisions, progress.jsonl,
    /// the memory budget, the parent watch, the object guard and outcome, the child key strings
    void CheckSlotsAndProgress(Checker& a_check, const std::filesystem::path& a_dir);

    /// The key mode end to end (GreySelfTestKeyed.cpp): request v5, a run into staging, the cache, terminal and fallback
    /// outcomes, the CLI refusals - on made-up NIFs (a_sourceNif) and DDS files in a_dir
    void CheckKeyed(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_sourceNif);
    void CheckHands(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_sourceNif);
    void CheckMarble(Checker& a_check, const std::filesystem::path& a_dir);

    /// The effect shader palettes of the key mode (GreySelfTestEfsh.cpp, contract 6): the request's efshPalettes written by
    /// the DLL's own writer (CopyContractEfsh.h), a fire palette greyed, a non-fire one and a missing one skipped, the key
    /// check, the cache, the refusals
    void CheckEfsh(Checker& a_check, const std::filesystem::path& a_dir);

    /// The golden request's efshPalettes (GreySelfTestEfsh.cpp): its gradflame01 (fire), its grey.dds (not fire) and a
    /// missing palette, through the DLL's writer with hashes of the files in a_data
    [[nodiscard]] nlohmann::json GoldenEfshPalettes(const std::filesystem::path& a_data, const BlackFlame::CopyContract::KeyPrefix& a_prefix);

    /// The self-test's made-up source NIF (GreySelfTest.cpp: AddOnNodes 9 and 24, effect shaders on and off the palette,
    /// normal and additive palette shapes, a lighting shader, a colour modifier and two colour controllers)
    void MakeSourceNif(const std::filesystem::path& a_file);

    /// The output identity and the golden-output test (GreySelfTestGolden.cpp): this exe's identity resource read the way
    /// the DLL reads it; the committed fixture in a_goldenDir run through the key mode (CPU encoder, the release identity)
    /// and compared with the table of kBuilderOutputVersion; the comparison's own failure cases. A missing a_goldenDir
    /// fails when a_required, else that part is skipped (a tester package has no fixtures)
    void CheckGolden(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_goldenDir, bool a_required);

    /// The GPU BC7 encoder's guards (GreySelfTestGpu.cpp): the crash marker, the output check's limits, the over-cap
    /// retry, the throttle event, and guard 5 (the BC7 fixtures of a_goldenDir, else stand-ins, on the GPU against the
    /// CPU's result; INFO, not FAIL, without a hardware device)
    void CheckGpu(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_goldenDir);

    /// This Windows has WARP for the encoder (d3d10warp.dll in System32 and DXGI 1.4); a_what says which part is there
    /// or missing. The key-mode throttle test uses it to tell "no WARP here" from a broken run
    [[nodiscard]] bool WarpAvailable(std::string& a_what);

    /// --self-test --print-golden: the fixture's run, printed as the table source for kBuilderOutputVersion. Returns 0/1.
    [[nodiscard]] int PrintGolden(const std::filesystem::path& a_dir, const std::filesystem::path& a_goldenDir);
}

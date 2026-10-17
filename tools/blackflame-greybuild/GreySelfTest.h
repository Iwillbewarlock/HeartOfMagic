#pragma once

// =============================================================================
// GreySelfTest - shared by the --self-test files
// =============================================================================

#include "GreyBuild.h"

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

    /// The path rule the DLL and the builder share (DataPathRule.h): normalising and the texture scan, one table (T7).
    void CheckPathRule(Checker& a_check);

    /// The decal texture set copies (GreyFormsTxst.h): types, record, map, ids, code page, texture step, old requests (a_dir)
    void CheckDecals(Checker& a_check, const std::filesystem::path& a_dir);

    /// The visual-risk check (GreyRisk.h), the glow rules (GreyGlow.h) and the derived palettes (GreyPalette.h, written to
    /// a_dir) on made-up NIFs and 4 x 4 textures.
    void CheckRisk(Checker& a_check, const std::filesystem::path& a_dir);
}

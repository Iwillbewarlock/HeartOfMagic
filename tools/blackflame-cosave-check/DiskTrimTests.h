#pragma once

// =============================================================================
// DISK TRIM TESTS - which work files the disk trim keeps (plugins/BlackFlame/src/DiskTrimRules.h)
// =============================================================================
// "Done" against the builder's reuse rule (a published or staged own file and deps, not_dumped done, too_large and failed
// not), the needed dump files (a pending object's files, our palettes always, a failed line keeps everything, a not-fire
// base with no line done), the not-fire keys reader, and which staging files may go. Included by main.cpp.

#include "../../plugins/BlackFlame/src/DiskTrimRules.h"

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace DiskTrimTests
{
    namespace Rules = BlackFlame::DiskTrimRules;
    namespace CC = BlackFlame::CopyContract;

    inline constexpr std::uint64_t kNif = 0x1111111111111111ull;
    inline constexpr std::uint64_t kTex = 0x2222222222222222ull;
    inline constexpr std::uint64_t kBase = 0x3333333333333333ull;
    inline constexpr std::uint64_t kNew = 0x4444444444444444ull;

    inline std::string Mesh(std::uint64_t a_key) { return "meshes\\heartofmagic\\blackflame\\copies\\" + CC::Hex16(a_key) + "\\copy.nif"; }
    inline std::string Tex(std::uint64_t a_key) { return "textures\\heartofmagic\\blackflame\\copies\\" + CC::Hex16(a_key) + "\\copy.dds"; }

    inline CC::ProgressLine NifLine(CC::Outcome a_outcome = CC::Outcome::kOk)
    {
        CC::ProgressLine line;
        line.kind = CC::ProgressKind::kNif;
        line.key = kNif;
        line.outcome = a_outcome;
        line.path = Mesh(kNif);
        line.bytes = 100;
        line.fnv64 = 0xAAull;
        line.deps.push_back({ false, 0, Tex(kTex), 0xBBull });
        line.deps.push_back({ true, 40012, {}, 0 });  // an ADDN dep is no file
        return line;
    }

    template <class Check>
    void Run(Check&& a_check)
    {
        const CC::PublishedList published{ { Mesh(kNif), { 100, 0xAAull } }, { Tex(kTex), { 50, 0xBBull } } };
        std::map<std::string, CC::PublishedFile> stagedFiles;
        const Rules::StagedSize staged = [&](const std::string& a_path) -> std::optional<CC::PublishedFile> {
            const auto it = stagedFiles.find(a_path);
            return it == stagedFiles.end() ? std::nullopt : std::optional(it->second);
        };
        const auto line = NifLine();
        a_check(Rules::LineDone(line, published, staged), "disk trim: an ok line whose own file and copy dep are listed is done (ADDN deps ignored)");
        a_check(!Rules::LineDone(line, { { Mesh(kNif), { 100, 0xAAull } } }, staged),
            "disk trim: a copy dep neither listed nor staged: not done (the builder rebuilds it)");
        stagedFiles[Tex(kTex)] = { 7, 0xBCull };
        a_check(!Rules::LineDone(line, { { Mesh(kNif), { 100, 0xAAull } } }, staged),
            "disk trim: a dep staged with other bytes (another build): not done (review Codex: the builder compares fnv64)");
        stagedFiles[Tex(kTex)] = { 7, 0xBBull };
        a_check(Rules::LineDone(line, { { Mesh(kNif), { 100, 0xAAull } } }, staged), "disk trim: a dep in staging with its fnv64 counts");
        a_check(!Rules::LineDone(line, { { Mesh(kNif), { 101, 0xAAull } }, { Tex(kTex), { 50, 0xBBull } } }, staged) &&
                    !Rules::LineDone(line, { { Mesh(kNif), { 100, 0xACull } }, { Tex(kTex), { 50, 0xBBull } } }, staged),
            "disk trim: the own file listed with another size or fnv64 is not the line's (the keep case): not done");
        stagedFiles[Mesh(kNif)] = { 99, 0xAAull };
        a_check(!Rules::LineDone(line, {}, staged), "disk trim: the own file staged with another size: not done");
        stagedFiles[Mesh(kNif)] = { 100, 0xABull };
        a_check(!Rules::LineDone(line, {}, staged), "disk trim: the own file staged with the line's size and other bytes: not done");
        stagedFiles[Mesh(kNif)] = { 100, 0xAAull };
        a_check(Rules::LineDone(line, {}, staged), "disk trim: own file and dep in staging with the line's bytes: done (no list)");
        a_check(!Rules::LineDone(NifLine(CC::Outcome::kFailed), published, staged) &&
                    !Rules::LineDone(NifLine(CC::Outcome::kTooLarge), published, staged) &&
                    Rules::LineDone(NifLine(CC::Outcome::kNotDumped), {}, [](const std::string&) { return std::nullopt; }) &&
                    Rules::LineDone(NifLine(CC::Outcome::kFallback), published, staged),
            "disk trim: failed and too_large are never done (a larger cap tries again), not_dumped always, fallback like ok");
        CC::ProgressLine txst;
        txst.kind = CC::ProgressKind::kTxst;
        txst.key = kTex;
        txst.outcome = CC::Outcome::kFallback;
        a_check(Rules::LineDone(txst, {}, [](const std::string&) { return std::nullopt; }),
            "disk trim: a TXST fallback line with no file and no dep is done");

        // the needed dump files
        stagedFiles.clear();
        std::map<std::uint64_t, CC::ProgressLine> lines{ { kNif, NifLine() } };
        const auto last = [&](std::uint64_t a_key) -> std::optional<CC::ProgressLine> {
            const auto it = lines.find(a_key);
            return it == lines.end() ? std::nullopt : std::optional(it->second);
        };
        const std::vector<std::string> always{ "textures\\heartofmagic\\blackflame\\grad.dds" };
        const std::vector<Rules::NeedObject> objects{ { kNif, false, { "meshes\\magic\\a.nif", "textures\\a.dds" } },
            { kNew, false, { "meshes\\magic\\b.nif", "textures\\a.dds", "textures\\b.dds" } },
            { kBase, true, { "textures\\base.dds" } } };
        auto needed = Rules::NeededFiles(objects, last, { kBase }, published, staged, always);
        a_check(!needed.all && needed.pending == 1 && needed.failed == 0 &&
                    needed.files == std::set<std::string>{ "textures\\heartofmagic\\blackflame\\grad.dds", "meshes\\magic\\b.nif",
                                                           "textures\\a.dds", "textures\\b.dds" },
            "disk trim: only a pending object's files are needed (a texture it shares with a done object too), our palettes "
            "always, a not-fire base with no line is done");
        needed = Rules::NeededFiles(objects, last, {}, published, staged, always);
        a_check(needed.pending == 2 && needed.files.count("textures\\base.dds") != 0,
            "disk trim: a base with no line that the not-fire file does not name is pending");
        a_check(Rules::NeededFiles({ { kNew, true, { "x" } } }, last, { kNew }, published, staged, {}).pending == 0 &&
                    Rules::NeededFiles({ { kNew, false, { "x" } } }, last, { kNew }, published, staged, {}).pending == 1,
            "disk trim: the not-fire file answers for effect shader bases only");
        lines[kNew] = NifLine(CC::Outcome::kFailed);
        lines[kNew].key = kNew;
        lines[kNew].reason = "texture textures\\b2.dds is listed as dumped but not in the dump";
        needed = Rules::NeededFiles(objects, last, { kBase }, published, staged, always);
        a_check(needed.all && needed.failed == 1 && needed.dumpMissing == 1,
            "disk trim: a line that failed for a source not in a limited dump brings the whole dump back");
        needed = Rules::NeededFiles(objects, last, { kBase }, published, staged, always, true);
        a_check(!needed.all && needed.pending == 1 && needed.files.count("meshes\\magic\\b.nif") != 0,
            "disk trim: the same failure after a whole dump: only the object's files (a whole dump cannot do more)");
        lines[kNew].reason = "meshes\\magic\\b.nif: nifly could not read it";
        needed = Rules::NeededFiles(objects, last, { kBase }, published, staged, always);
        a_check(!needed.all && needed.failed == 1 && needed.dumpMissing == 0 && needed.pending == 1 &&
                    needed.files.count("meshes\\magic\\b.nif") != 0 && needed.files.count("textures\\base.dds") == 0,
            "disk trim: a failure of the object's own (an unreadable NIF) keeps only its files, start after start (review Codex: "
            "no whole dump every other start)");
        lines.clear();
        needed = Rules::NeededFiles({}, last, {}, {}, staged, always);
        a_check(!needed.all && needed.pending == 0 && needed.files.size() == 1, "disk trim: no objects: our palettes alone");

        // the not-fire file and the staging trim
        const auto keys = Rules::NotFireKeys("1111111111111111 fire1:share=0.05\nnot a line\n2222222222222222 any\r\nABCDEF \n"
                                             "3333333333333333 cut");
        a_check(keys == std::set<std::uint64_t>{ kNif, kTex }, "disk trim: not-fire keys whatever the tag; bad, upper-case and cut lines skipped");
        a_check(Rules::NotFireKeys("").empty(), "disk trim: an empty not-fire file names nothing");
        a_check(Rules::MayTrim(Mesh(kNif), published, {}) && !Rules::MayTrim(Mesh(kNew), published, {}) &&
                    !Rules::MayTrim(Tex(kTex), published, { Tex(kTex) }),
            "disk trim: a staging file goes only when listed, and never one of our copies a source names");
    }
}

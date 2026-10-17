// =============================================================================
// GreyKeyedRun - key-directed outputs into staging (the run of GreyKeyed.h)
// =============================================================================
// Order: the request (v5) and its checks - the keys were made for this exe (options.keyPrefix's exe = this file's fnv64)
// and for the palettes the dump holds (grad / gradadd) -, the staging folder (leftover *.tmp removed, progress.jsonl
// compacted), every object whose line is reusable counted as a cache hit, the rest scheduled (GreyKeyed.h) and built one
// at a time under the memory budget, each ending in one progress line. A parent that dies stops the run after the object
// in hand.

#include "GreyKeyed.h"

#include "GreyGlow.h"
#include "GreyFormsTxst.h"

#include "../../plugins/BlackFlame/src/DataPathRule.h"

#include <NifFile.hpp>

#include <algorithm>
#include <chrono>
#include <fmt/format.h>
#include <iostream>
#include <new>
#include <set>
#include <tuple>

namespace GreyBuild
{
    namespace
    {
        namespace CC = BlackFlame::CopyContract;
        namespace Rule = BlackFlame::DataPathRule;

        constexpr int kOk = 0;
        constexpr int kFail = 1;
        constexpr int kUsage = 2;
        constexpr const char* kRequestFile = "request.json";
        constexpr const char* kDumpFolder = "data";

        /// Everything one run's objects share
        struct Run
        {
            const KeyedRequest& request;
            std::filesystem::path dump;
            std::filesystem::path staging;
            ProgressLog& log;
            KeyedOutputs& outputs;
            TextureJob& textures;
            GlowContext& glow;
            GreyMap map;
            std::set<int> slotIndices;  // the grey values the map hands out (40000 + place)
        };

        [[nodiscard]] CC::ProgressLine Line(const KeyedSpec& a_spec, CC::Outcome a_outcome, const std::string& a_reason)
        {
            CC::ProgressLine line;
            line.kind = a_spec.kind;
            line.key = a_spec.key;
            line.outcome = a_outcome;
            line.reason = a_reason;
            return line;
        }

        void Remove(const std::filesystem::path& a_file)
        {
            std::error_code ec;
            std::filesystem::remove(a_file, ec);
        }

        // The slot indices a written NIF holds (its {"addn": n} deps)
        [[nodiscard]] std::vector<std::uint32_t> SlotValues(const std::filesystem::path& a_nif, const std::set<int>& a_slots)
        {
            nifly::NifFile nif;
            LoadNif(nif, a_nif);
            std::set<std::uint32_t> values;
            const auto& hdr = nif.GetHeader();
            for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
                if (const auto* node = hdr.GetBlock<nifly::BSValueNode>(id); node && a_slots.count(node->value) != 0) {
                    values.insert(static_cast<std::uint32_t>(node->value));
                }
            }
            return { values.begin(), values.end() };
        }

        // A NIF object (form model, muzzle, MPS): BuildNifWith into <copy>.tmp, then renamed with its line's file fields
        [[nodiscard]] CC::ProgressLine BuildNifObject(const KeyedSpec& a_spec, KeyedObject& a_object, Run& a_run)
        {
            const auto copy = Rule::CopyPath(Rule::kMeshesRoot, CC::Hex16(a_spec.key), a_spec.source);
            if (!copy || !IsAscii(*copy)) {  // a progress line is UTF-8 (P0: copy basenames become ASCII)
                return Line(a_spec, CC::Outcome::kFailed, fmt::format("{}: no ASCII copy path for it", a_spec.source));
            }
            const auto source = FindSource({ a_run.dump }, a_spec.source);
            if (!source) {
                return Line(a_spec, CC::Outcome::kFailed, fmt::format("{} is not in the dump", a_spec.source));
            }
            const auto listed = a_run.request.nifHashes.find(a_spec.source);
            if (listed == a_run.request.nifHashes.end()) {
                return Line(a_spec, CC::Outcome::kFailed, fmt::format("{}: the request's nifs[] gives no fnv64 for it (its key cannot be "
                    "checked against the dump)", a_spec.source));
            }
            {
                std::string why;
                const auto digest = DigestFile(*source, why);
                if (!digest || digest->fnv64 != listed->second) {
                    return Line(a_spec, CC::Outcome::kFailed, digest ? fmt::format("{}: the dump's bytes are not the ones the request "
                        "keyed ({} != {})", a_spec.source, CC::Hex16(digest->fnv64), CC::Hex16(listed->second)) : why);
                }
            }
            NifOptions options{ true, true, true, Judging{ &a_run.textures, &a_run.glow, nullptr } };
            options.keyedPalettes = &a_run.outputs.Palettes();
            Totals totals;
            const std::string tempRel = *copy + kTempFileSuffix;
            const auto temp = StagedFile(a_run.staging, tempRel);
            const auto result = BuildNifWith(a_run.map, { a_run.dump }, a_run.staging, a_spec.source, tempRel, options, totals);
            if (!result.ok) {
                a_object.failed = result.reason.empty() ? std::string("the NIF was not built") : result.reason;
            } else if (!result.keptValues.empty()) {
                a_object.fallbacks.push_back(fmt::format("{} AddOnNode value(s) bound to no slot kept (first: {})", result.keptValues.size(),
                    result.keptValues.front()));
            }
            auto [outcome, reason] = ClassifyObject(a_object);
            if (!CC::IsPublishable(outcome)) {
                Remove(temp);
                return Line(a_spec, outcome, reason);
            }
            std::string why;
            std::vector<std::uint32_t> addn;
            try {
                addn = SlotValues(temp, a_run.slotIndices);
            } catch (const std::exception& e) {
                Remove(temp);
                return Line(a_spec, CC::Outcome::kFailed, fmt::format("the written copy does not read back: {}", e.what()));
            }
            const auto digest = DigestFile(temp, why);
            if (!digest || !CommitTemp(temp, StagedFile(a_run.staging, *copy), why)) {
                Remove(temp);
                return Line(a_spec, CC::Outcome::kFailed, why);
            }
            a_run.log.NoteWritten(*copy, *digest);
            auto line = Line(a_spec, outcome, reason);
            line.path = *copy;
            line.bytes = digest->bytes;
            line.fnv64 = digest->fnv64;
            line.deps = a_object.deps;
            for (const auto index : addn) {
                CC::ProgressDep dep;
                dep.isAddn = true;
                dep.addn = index;
                line.deps.push_back(dep);
            }
            return line;
        }

        // A TXST object: its TX00 / TX03 copies (the deps) through the same texture rule; no file of its own
        [[nodiscard]] CC::ProgressLine BuildTxstObject(const KeyedSpec& a_spec, KeyedObject& a_object, Run& a_run)
        {
            if (a_spec.form == kNoForm) {
                return Line(a_spec, CC::Outcome::kFailed, "no TXST form of the request has this key");
            }
            const auto& form = a_run.request.base.forms[a_spec.form];
            for (const auto& colour : kTxstColourSlots) {
                std::string slot = form.textures[colour.slot];
                if (slot.empty()) {
                    continue;
                }
                if (!IsAscii(slot)) {  // AB1: its dump file's name depends on a code page conversion of these bytes
                    a_object.fallbacks.push_back(fmt::format("{} '{}' is not plain ASCII: left", colour.name, form.textureNames[colour.slot]));
                    continue;
                }
                ++a_run.textures.totals.decalSlots;
                RetargetTextureSlot(slot, colour.role, a_run.textures, fmt::format("TXST {} {}", form.source, colour.name));
            }
            auto [outcome, reason] = ClassifyObject(a_object);
            auto line = Line(a_spec, outcome, reason);
            if (CC::IsPublishable(outcome)) {
                line.deps = a_object.deps;
            }
            return line;
        }

        // One object, bad_alloc and every other exception caught for its key only
        [[nodiscard]] CC::ProgressLine BuildObject(const KeyedSpec& a_spec, Run& a_run)
        {
            KeyedObject object;
            a_run.outputs.Begin(&object);
            const auto line = GuardObject(a_spec, [&]() {
                return a_spec.kind == CC::ProgressKind::kTxst ? BuildTxstObject(a_spec, object, a_run) : BuildNifObject(a_spec, object, a_run);
            });
            a_run.outputs.Begin(nullptr);
            return line;
        }

        // The scheduler's estimate: the textures the rule would recolour that the dump has, by their header estimates
        void EstimateCost(KeyedSpec& a_spec, const Run& a_run)
        {
            std::set<std::string> paths;
            const auto add = [&](const std::string& a_slot) {
                const auto path = TextureDataPath(a_slot);
                if (path && !StartsWith(*path, kOwnTextureRoot)) {
                    paths.insert(*path);
                }
            };
            if (a_spec.kind == CC::ProgressKind::kTxst) {
                if (a_spec.form != kNoForm) {
                    for (const auto& colour : kTxstColourSlots) {
                        add(a_run.request.base.forms[a_spec.form].textures[colour.slot]);
                    }
                }
            } else if (const auto source = FindSource({ a_run.dump }, a_spec.source)) {
                try {
                    nifly::NifFile nif;
                    LoadNif(nif, *source);
                    ForEachTextureSlot(nif, [&](std::string& a_slot, TextureRole) { add(a_slot); });
                } catch (const std::exception&) {
                    return;  // it fails fast when built
                }
            }
            for (const auto& path : paths) {
                std::string why;
                const auto file = FindSource({ a_run.dump }, path);
                const auto estimate = file ? EstimateTextureBytes(*file, why) : std::nullopt;
                if (estimate) {
                    a_spec.textured = true;
                    a_spec.cost += *estimate;
                }
            }
        }

        // The keys were made for this exe and for the palettes in the dump; false (printed) when not
        [[nodiscard]] bool CheckPrefixFiles(const KeyedRequest& a_request, const std::filesystem::path& a_dump)
        {
            std::string why;
            const auto exe = ThisExecutable();
            const auto own = exe.empty() ? std::nullopt : DigestFile(exe, why);
            if (!own || own->fnv64 != a_request.exeFnv64) {
                std::cerr << fmt::format("the request's keys were made for the builder exe {}, this one is {} ({}): its outputs would "
                                         "carry keys that are not theirs. Run the exe the DLL hashed "
                                         "(<DLL folder>\\HeartOfMagic_BlackFlame\\blackflame-greybuild.exe).\n",
                    CC::Hex16(a_request.exeFnv64), own ? CC::Hex16(own->fnv64) : std::string("unreadable"), PathText(exe));
                return false;
            }
            const std::pair<std::string_view, const std::string*> palettes[] = { { Rule::kOurPalette, &a_request.grad },
                { Rule::kOurPaletteAdd, &a_request.gradAdd } };
            for (const auto& [path, expected] : palettes) {
                const auto file = FindSource({ a_dump }, std::string(path));
                const auto digest = file ? DigestFile(*file, why) : std::nullopt;
                const std::string actual = digest ? CC::Hex16(digest->fnv64) : std::string(CC::kSentinelMissing);
                const bool notDumped = *expected == CC::kSentinelNotDumped && !file;
                if (actual != *expected && !notDumped) {
                    std::cerr << fmt::format("the request keyed {} as {}, the dump holds {}: start the game again so the DLL writes "
                                             "a new request and dump.\n", path, *expected, actual);
                    return false;
                }
            }
            return true;
        }

        void Tally(std::map<CC::Outcome, int>& a_counts, const CC::ProgressLine& a_line)
        {
            ++a_counts[a_line.outcome];
            const std::string what = a_line.path.empty() ? CC::Hex16(a_line.key) : a_line.path;
            std::cout << fmt::format("{}  {} {}{}\n", a_line.outcome == CC::Outcome::kFallback ? std::string("FALLBACK") :
                                     std::string(CC::OutcomeText(a_line.outcome)), CC::ProgressKindText(a_line.kind), what,
                a_line.reason.empty() ? std::string() : " - " + a_line.reason);
        }
    }

    CC::ProgressLine GuardObject(const KeyedSpec& a_spec, const std::function<CC::ProgressLine()>& a_build)
    {
        try {
            return a_build();
        } catch (const std::bad_alloc&) {
            return Line(a_spec, CC::Outcome::kFailed, "out of memory (bad_alloc)");
        } catch (const std::exception& e) {
            return Line(a_spec, CC::Outcome::kFailed, e.what());
        } catch (...) {
            return Line(a_spec, CC::Outcome::kFailed, "an exception that is no std::exception");
        }
    }

    int RunKeyed(const std::filesystem::path& a_folder, const KeyedOptions& a_options)
    {
        const auto started = std::chrono::steady_clock::now();
        std::filesystem::path folder = a_folder;
        if (folder.empty()) {
            std::string why;
            const auto found = FindRequestFolder(why);
            if (!found) {
                std::cerr << why << "\n";
                return kFail;
            }
            folder = *found;
        }
        const auto dump = folder / kDumpFolder;
        if (a_options.staging.empty() || IsInside(a_options.staging, dump) || IsInside(dump, a_options.staging)) {
            std::cerr << fmt::format("--staging must be a folder apart from the dump {}\n", PathText(dump));
            return kUsage;
        }
        LowerOwnPriority();
        ParentWatch watch;
        if (std::string why; a_options.parentPid != 0 && !watch.Start(a_options.parentPid, kParentGraceMs, why)) {
            std::cerr << why << "\n";
            return watch.NoProcess() ? kExitParentGone : kFail;
        }
        KeyedRequest request;
        try {
            request = LoadKeyedRequest(folder / kRequestFile);
        } catch (const std::exception& e) {
            std::cerr << e.what() << "\n";
            return kUsage;
        }
        for (const auto& line : request.warnings) {
            std::cout << "WARN  " << line << "\n";
        }
        if (!CheckPrefixFiles(request, dump)) {
            return kFail;
        }
        const int temps = RemoveTempFiles(a_options.staging);
        ProgressLog log;
        if (std::string why; !log.Open(a_options.staging, why)) {
            std::cerr << why << "\n";
            return kFail;
        }
        const auto& compaction = log.LastCompaction();
        std::cout << fmt::format("staging {}: {} temp file(s) removed; progress.jsonl {} line(s) read, {} kept, {} bad{}\n",
            PathText(a_options.staging), temps, compaction.read, compaction.kept, compaction.badLines,
            compaction.cutLastLine ? ", a cut last line dropped" : "");

        // The dump only: every byte an output depends on is one the keys name (our palettes too: grad / gradadd)
        TextureSource textureSource({ dump }, request.base.texturesNotDumped, a_options.staging);
        Budget budget(a_options.jobMemory, a_options.commitReserve, &textureSource, watch.Flag());
        TextureJob textures;
        textures.inputs = { dump };
        textures.out = a_options.staging;
        textures.diffuseTint = static_cast<float>(request.tint);
        textures.notDumped = request.base.texturesNotDumped;
        KeyedOutputs outputs(log, budget, request.keyPrefix, textures);
        textures.keyed = &outputs;
        // The glow rules' and palettes' reads decode too: admitted like a recolour, without clearing the cache (a TextureLook
        // may be alive); too large or over the cap makes the object too_large or failed (Codex review 1)
        textureSource.SetReadGate([&](const std::filesystem::path& a_file, std::string& a_why) {
            std::string why;
            const auto estimate = EstimateReadBytes(a_file, why);
            if (!estimate) {
                return true;  // ReadRgba says what is wrong with it
            }
            switch (budget.Acquire(*estimate, why, false)) {
            case Admission::kAdmit:
                return true;
            case Admission::kTooLarge:
                outputs.Terminal(CC::Outcome::kTooLarge, fmt::format("{}: {}", PathText(a_file.filename()), why));
                break;
            default:
                outputs.Failed(fmt::format("{}: {}", PathText(a_file.filename()), why));
                break;
            }
            a_why = why;
            return false;
        });
        textureSource.SetTransientSink([&](const std::string& a_why) { outputs.Failed(a_why); });
        PaletteJob palettes;
        palettes.mode = request.paletteMode;
        palettes.out = a_options.staging;
        palettes.textures = &textureSource;
        palettes.keyed = &outputs;
        GlowContext glow;
        glow.textures = &textureSource;
        glow.palettes = &palettes;
        glow.tint = static_cast<float>(request.tint);
        glow.tintAdd = static_cast<float>(request.tintAdd);
        Run run{ request, dump, a_options.staging, log, outputs, textures, glow };
        run.map.tint = glow.tint;
        run.map.tintAdd = glow.tintAdd;
        for (const auto& [index, grey] : request.addnSlots) {
            run.map.addons[index] = AddonEntry{ index, grey };
            run.slotIndices.insert(grey);
        }

        std::vector<KeyedSpec> pending;
        int hits = 0;
        for (const auto& spec : request.objects) {
            if (log.Reusable(spec.key, budget.Cap())) {
                ++hits;
                continue;
            }
            pending.push_back(spec);
            EstimateCost(pending.back(), run);
        }
        std::stable_sort(pending.begin(), pending.end(), [](const KeyedSpec& a, const KeyedSpec& b) {
            return std::make_tuple(a.textured, a.kind != CC::ProgressKind::kMps, a.cost) <
                   std::make_tuple(b.textured, b.kind != CC::ProgressKind::kMps, b.cost);
        });
        std::cout << fmt::format("keyed  {} object(s): {} reusable from staging, {} to build ({} with textures to recolour); job "
                                 "memory cap {} MiB, commit reserve {} MiB\n", request.objects.size(), hits, pending.size(),
            std::count_if(pending.begin(), pending.end(), [](const KeyedSpec& a_spec) { return a_spec.textured; }),
            a_options.jobMemory / kBytesPerMiB, a_options.commitReserve / kBytesPerMiB);

        std::map<CC::Outcome, int> counts;
        bool stopped = false;
        for (const auto& spec : pending) {
            if (watch.Gone()) {
                stopped = true;
                break;
            }
            budget.BetweenObjects();
            const auto line = BuildObject(spec, run);
            if (std::string why; !log.Append(line, why)) {
                // a full disk or a refused line: no later line is trusted on this handle either (Codex review 1)
                std::cerr << fmt::format("the line of {} {} was not written: {}. Stopping; the next run builds it again.\n",
                    CC::ProgressKindText(line.kind), CC::Hex16(line.key), why);
                return kFail;
            }
            Tally(counts, line);
        }
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started).count();
        std::cout << fmt::format("keyed  done in {} s: ok {}, fallback {}, failed {}, too_large {}, not_dumped {}, reused {}; memory "
                                 "pauses {}, cache clears {}{}\n", seconds, counts[CC::Outcome::kOk], counts[CC::Outcome::kFallback],
            counts[CC::Outcome::kFailed], counts[CC::Outcome::kTooLarge], counts[CC::Outcome::kNotDumped], hits, budget.Pauses(),
            budget.Evictions(), stopped ? "; stopped early: the parent process is gone" : "");
        std::cout << TextureTotalsLine(textures.totals) << "\n" << PaletteTotalsLine(palettes) << "\n";
        return stopped ? kExitParentGone : kOk;
    }
}

// =============================================================================
// GreyKeyedRun - key-directed outputs into staging (the run of GreyKeyed.h)
// =============================================================================
// Order: the request (v5) and the key prefix check (VerifyKeyPrefix: its options, this build's output identity and the
// dump's palettes give options.keyPrefix exactly), the staging folder (leftover *.tmp removed, progress.jsonl
// compacted), every object whose line is reusable counted as a cache hit, the rest scheduled (GreyKeyed.h) and built one
// at a time under the memory budget, each ending in one progress line. A parent that dies stops the run after the object
// in hand.

#include "GreyKeyed.h"

#include "GreyGlow.h"
#include "GreyFormsTxst.h"

#include "../../plugins/BlackFlame/src/DataPathRule.h"

#include "GreyIdentityText.h"  // written at build time (GreyIdentity.cmake)

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

        constexpr int kOk = kExitOk;
        constexpr int kFail = kExitFail;
        constexpr int kUsage = kExitUsage;
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
            std::uint64_t cap = 0;      // the job memory cap (too_large lines name it)
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


        // The slot indices the remap put into the copy (its {"addn": n} deps), read off the source's own AddOnNode values the
        // way RemapAddonValues binds them: a value the map binds gives its slot index. Not a set difference of copy and source
        // values: a source holding 9 and 40012 with 9 -> 40012 still depends on 40012 (Codex review). A source value already in
        // the slot range is no binding of ours and stays out, with a warning: the copy names a slot it did not ask for.
        [[nodiscard]] std::vector<std::uint32_t> BoundSlotValues(const std::filesystem::path& a_source, const GreyMap& a_map,
            const std::string& a_rel)
        {
            nifly::NifFile nif;
            LoadNif(nif, a_source);
            std::set<std::uint32_t> values;
            const auto& hdr = nif.GetHeader();
            for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
                const auto* node = hdr.GetBlock<nifly::BSValueNode>(id);
                if (!node) {
                    continue;
                }
                if (const auto it = a_map.addons.find(node->value); it != a_map.addons.end()) {
                    values.insert(static_cast<std::uint32_t>(it->second.grey));
                }
                if (node->value >= 0 && CC::SlotOfAddnIndex(static_cast<std::uint32_t>(node->value))) {
                    std::cout << fmt::format("WARN  {}: AddOnNode value {} (block {}) is already a slot index; no dep for it\n", a_rel,
                        node->value, id);
                }
            }
            return { values.begin(), values.end() };
        }

        // <copy>.tmp removed when the object leaves before its rename (an exception included)
        struct TempGuard
        {
            std::filesystem::path file;
            bool released = false;
            ~TempGuard()
            {
                if (!released) {
                    RemoveQuietly(file);
                }
            }
        };

        // A NIF object (form model, muzzle, MPS): BuildNifWith into <copy>.tmp, then renamed with its line's file fields
        [[nodiscard]] CC::ProgressLine BuildNifObject(const KeyedSpec& a_spec, KeyedObject& a_object, Run& a_run)
        {
            const auto copy = Rule::CopyPath(Rule::kMeshesRoot, CC::Hex16(a_spec.key), a_spec.source);
            if (!copy) {
                return Line(a_spec, CC::Outcome::kFailed, fmt::format("{}: no copy path for it", a_spec.source));
            }
            const auto listed = a_run.request.nifHashes.find(a_spec.source);
            if (listed == a_run.request.nifHashes.end()) {
                // nifs[] ok false (the game could not read it; its key says missing), or not listed at all (the DLL's slip, warned)
                return Line(a_spec, CC::Outcome::kFailed, a_run.request.unreadNifs.count(a_spec.source) != 0 ?
                    fmt::format("{}: the game could not read it (nifs[] ok false)", a_spec.source) :
                    fmt::format("{}: nifs[] does not list it (no fnv64, not ok false)", a_spec.source));
            }
            const auto source = FindSource({ a_run.dump }, a_spec.source);
            if (!source) {
                return Line(a_spec, CC::Outcome::kFailed, fmt::format("{} is not in the dump", a_spec.source));
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
            TempGuard guard{ temp };
            const auto result = BuildNifWith(a_run.map, { a_run.dump }, a_run.staging, a_spec.source, tempRel, options, totals);
            if (!result.ok) {
                a_object.failed = result.reason.empty() ? std::string("the NIF was not built") : result.reason;
            } else if (!result.keptValues.empty()) {
                a_object.fallbacks.push_back(fmt::format("{} AddOnNode value(s) bound to no slot kept (first: {})", result.keptValues.size(),
                    result.keptValues.front()));
            }
            auto [outcome, reason] = ClassifyObject(a_object);
            if (!CC::IsPublishable(outcome)) {
                RemoveQuietly(temp);
                return Line(a_spec, outcome, reason);
            }
            std::string why;
            std::vector<std::uint32_t> addn;
            try {
                addn = BoundSlotValues(*source, a_run.map, a_spec.source);
            } catch (const std::exception& e) {
                RemoveQuietly(temp);
                return Line(a_spec, CC::Outcome::kFailed, fmt::format("the source does not read back: {}", e.what()));
            }
            const auto digest = DigestFile(temp, why);
            if (!digest || !CommitTemp(temp, StagedFile(a_run.staging, *copy), why)) {
                RemoveQuietly(temp);
                return Line(a_spec, CC::Outcome::kFailed, why);
            }
            guard.released = true;
            a_run.log.NoteWritten(*copy, *digest);
            auto line = Line(a_spec, outcome, reason);
            line.path = *copy;
            line.bytes = digest->bytes;
            line.fnv64 = digest->fnv64;
            line.deps = a_object.deps;
            for (const auto index : addn) {
                CC::ProgressDep dep;  // contract 4: on nif and mps lines
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
            if (CC::IsPublishable(outcome) && a_object.deps.empty()) {
                // Contract 3: a TXST with no colour slot copied (both left or empty) is a fallback with no dep, never ok
                outcome = CC::Outcome::kFallback;
                reason = "no colour slot got a copy" + (reason.empty() ? std::string() : " (" + reason + ")");
            }
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
            auto line = GuardObject(a_spec, [&]() {
                return a_spec.kind == CC::ProgressKind::kTxst ? BuildTxstObject(a_spec, object, a_run) : BuildNifObject(a_spec, object, a_run);
            });
            a_run.outputs.Begin(nullptr);
            if (line.outcome == CC::Outcome::kTooLarge) {
                line.cap = a_run.cap;  // contract 4: the job memory cap it was over, as a field
            }
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
            } else if (const auto source = a_run.request.nifHashes.count(a_spec.source) != 0 ? FindSource({ a_run.dump }, a_spec.source) :
                                                                                             std::nullopt) {
                try {
                    nifly::NifFile nif;
                    LoadNif(nif, *source);
                    ForEachTextureSlot(nif, [&](std::string& a_slot, TextureRole) { add(a_slot); });
                } catch (const std::exception& e) {
                    std::cout << fmt::format("NOTE  {} does not load for the scheduler's estimate ({}): it fails when built\n", a_spec.source,
                        e.what());
                    return;
                }
            }
            for (const auto& path : paths) {
                std::string why;
                const auto file = a_run.request.base.texturesDumped.count(path) != 0 ? FindSource({ a_run.dump }, path) : std::nullopt;
                const auto estimate = file ? EstimateTextureBytes(*file, why) : std::nullopt;
                if (estimate) {
                    a_spec.textured = true;
                    a_spec.cost = *estimate > UINT64_MAX - a_spec.cost ? UINT64_MAX : a_spec.cost + *estimate;
                }
            }
        }

        // The prefix rebuilt from the options, the output identity (this build's, or the self-test's override) and the dump's
        // palettes must be the DLL's keyPrefix exactly (VerifyKeyPrefix); the verified prefix, or nullopt (printed)
        [[nodiscard]] std::optional<CC::KeyPrefix> VerifiedPrefix(const KeyedRequest& a_request, const std::filesystem::path& a_dump,
            const std::string& a_identity)
        {
            CC::KeyPrefix prefix;
            prefix.outputIdentity = a_identity;
            prefix.tint = a_request.tint;
            prefix.tintAdd = a_request.tintAdd;
            prefix.paletteMode = a_request.paletteMode == PaletteMode::kGodskin ? CC::PaletteMode::kGodskin : CC::PaletteMode::kDerived;
            prefix.gradBlackFlame = PaletteState(a_dump, Rule::kOurPalette, a_request.keyPrefix, "grad", a_request.base.texturesDumped);
            prefix.gradBlackFlameAdd = PaletteState(a_dump, Rule::kOurPaletteAdd, a_request.keyPrefix, "gradadd",
                a_request.base.texturesDumped);
            if (!CC::VerifyKeyPrefix(a_request.keyPrefix, a_request.tint, a_request.tintAdd, a_request.paletteModeText,
                    prefix.outputIdentity, prefix.gradBlackFlame, prefix.gradBlackFlameAdd)) {
                std::cerr << fmt::format("the request's keyPrefix '{}' is not what this builder rebuilds ('{}': its options, this "
                                         "builder's output identity {} and the dump's palettes): its outputs would carry keys that are "
                                         "not theirs. Run the builder the DLL read its identity from (<DLL folder>\\HeartOfMagic_BlackFlame"
                                         "\\blackflame-greybuild.exe) on the request and dump of one start.\n", a_request.keyPrefix,
                    CC::KeyPrefixString(prefix).value_or("refused"), prefix.outputIdentity);
                return std::nullopt;
            }
            return prefix;
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

    const char* OutputIdentity() noexcept
    {
        return BLACKFLAME_BUILDER_OUTPUT_IDENTITY;
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
        const auto prefix = VerifiedPrefix(request, dump,
            a_options.identityOverride.empty() ? std::string(OutputIdentity()) : a_options.identityOverride);
        if (!prefix) {
            return kFail;
        }
        const int temps = RemoveTempFiles(a_options.staging);
        ProgressLog log;
        if (std::string why; !log.Open(a_options.staging, why)) {
            std::cerr << why << "\n";
            return kFail;
        }
        const auto& compaction = log.LastCompaction();
        std::cout << fmt::format("staging {}: {} temp file(s) removed; progress.jsonl {} line(s) read, {} kept, {} bad{}; fnv64 checks {} ms\n",
            PathText(a_options.staging), temps, compaction.read, compaction.kept, compaction.badLines,
            compaction.cutLastLine ? ", a cut last line dropped" : "", compaction.hashMs);

        // The dump only: every byte an output depends on is one the keys name (our palettes too: grad / gradadd)
        TextureSource textureSource({ dump }, request.base.texturesNotDumped, a_options.staging);
        textureSource.SetListed(request.base.texturesDumped);
        Budget budget(a_options.jobMemory, a_options.commitReserve, &textureSource, watch.Flag(), a_options.memoryProbe);
        // BC7 targets on the GPU when its guards allow (GreyGpuEncoder.h); its device goes first when a texture is over the cap
        GpuEncoder gpu(a_options.gpu, a_options.staging, prefix->outputIdentity, a_options.jobMemory, a_options.commitReserve,
            a_options.memoryProbe);
        gpu.Start();
        budget.SetDeviceRelease([&gpu]() { return gpu.ReleaseForMemory(); });
        // The DLL's throttle event (BuilderSignal.h): absent (a run by hand) means never throttled. Read before each object
        // and by the GPU encoder at each texture (review round 1)
        ThrottleSignal throttle;
        if (a_options.parentPid != 0 && !throttle.Open(a_options.parentPid)) {
            std::cout << "THROTTLE no throttle event from the parent: this run is never throttled\n";
        }
        gpu.SetThrottleCheck([&throttle]() { return throttle.IsSet(); });
        bool throttled = false;
        TextureJob textures;
        textures.inputs = { dump };
        textures.out = a_options.staging;
        textures.diffuseTint = static_cast<float>(request.tint);
        textures.notDumped = request.base.texturesNotDumped;
        KeyedOutputs outputs(log, budget, *prefix, textures, request.base.texturesDumped, request.base.textureStates);
        textures.keyed = &outputs;
        textures.gpu = &gpu;
        // The glow rules' and palettes' reads decode too: admitted like a recolour, without clearing the cache (a TextureLook
        // may be alive); too large or over the cap makes the object too_large or failed (Codex review 1)
        textureSource.SetReadGate([&](const std::filesystem::path& a_file, std::string& a_why) {
            if (outputs.ObjectFailed()) {
                a_why = "the object has failed already";
                return false;  // no estimate or budget wait for an object that publishes nothing
            }
            std::string why;
            bool transient = false;
            const auto estimate = EstimateReadBytes(a_file, why, &transient);
            if (!estimate && transient) {
                outputs.Failed(fmt::format("{}: {}", PathText(a_file.filename()), why));
                a_why = why;
                return false;
            }
            if (!estimate) {
                return true;  // a header that does not read: ReadRgba refuses the same bytes at once (no decode)
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
        run.cap = budget.Cap();
        run.map.tint = glow.tint;
        run.map.tintAdd = glow.tintAdd;
        for (const auto& [index, grey] : request.addnSlots) {
            run.map.addons[index] = AddonEntry{ index, grey };
        }

        // contract 6: the effect shader palettes first (small, no scheduler)
        const auto efsh = BuildEfshPalettes(request, dump, log, budget, *prefix, [&watch]() { return watch.Gone(); });
        if (log.Broken()) {
            std::cerr << "writing progress.jsonl failed at an effect shader palette. Stopping; the next run builds it again.\n";
            return kFail;
        }
        std::vector<KeyedSpec> pending;
        int hits = 0;
        for (const auto& spec : request.objects) {
            if (watch.Gone()) {
                std::cerr << "the parent process is gone: stopping before the first object\n";
                return kExitParentGone;
            }
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
                                 "memory cap {} MiB, commit reserve {} MiB; output identity {}\n", request.objects.size(), hits,
            pending.size(), std::count_if(pending.begin(), pending.end(), [](const KeyedSpec& a_spec) { return a_spec.textured; }),
            a_options.jobMemory / kBytesPerMiB, a_options.commitReserve / kBytesPerMiB, prefix->outputIdentity);

        std::map<CC::Outcome, int> counts;
        bool stopped = false;
        for (const auto& spec : pending) {
            if (watch.Gone()) {
                stopped = true;
                break;
            }
            if (!throttled && throttle.IsSet()) {
                throttled = true;
                gpu.Throttle();
                OneOpenMpThread();
                std::cout << "THROTTLE the publish window closed (the parent's event is set): the GPU device is released for the "
                             "rest of the run and OpenMP runs on one thread\n";
            }
            budget.BetweenObjects(spec.cost);
            auto line = BuildObject(spec, run);
            if (std::string why; !log.Broken() && !log.Append(line, why) && !log.Broken()) {
                // the contract's writer refused it (deterministic): a failed line in its place, the run goes on (OCR review 4)
                std::cout << fmt::format("FAIL  the line of {} {} is refused by the contract's writer ({}): written as failed\n",
                    CC::ProgressKindText(line.kind), CC::Hex16(line.key), why);
                line = CC::ProgressLine{ line.kind, line.key };
                line.outcome = CC::Outcome::kFailed;
                line.reason = "the contract's writer refused this object's line";
                (void)log.Append(line, why);
            }
            if (log.Broken()) {
                // a write failed (a full disk): no later line is trusted on this handle, a child's included (OCR review 2)
                std::cerr << fmt::format("writing progress.jsonl failed at {} {}. Stopping; the next run builds it again.\n",
                    CC::ProgressKindText(line.kind), CC::Hex16(line.key));
                return kFail;
            }
            Tally(counts, line);
        }
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started).count();
        std::cout << fmt::format("keyed  done in {} s: ok {}, fallback {}, failed {}, too_large {}, not_dumped {}, reused {}; memory "
                                 "pauses {}, cache clears {}{}\n", seconds, counts[CC::Outcome::kOk], counts[CC::Outcome::kFallback],
            counts[CC::Outcome::kFailed], counts[CC::Outcome::kTooLarge], counts[CC::Outcome::kNotDumped], hits, budget.Pauses(),
            budget.Evictions(), stopped ? "; stopped early: the parent process is gone" : "");
        const auto reads = textureSource.Counts();
        std::cout << fmt::format("reads  texture decodes {} of {} path(s); statistics answered without a decode {}\n", reads.decodes,
            reads.paths, reads.statsAnswers);
        std::cout << TextureTotalsLine(textures.totals) << "\n" << PaletteTotalsLine(palettes) << "\n" << EfshTotalsLine(efsh) << "\n"
                  << gpu.SummaryLine() << "\n";
        return stopped ? kExitParentGone : kOk;
    }
}

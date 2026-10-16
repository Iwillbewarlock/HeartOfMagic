// =============================================================================
// SELF-TEST v2 STEPS - the copy framework spike (README "Black Flame v2")
// =============================================================================
// T0-hooks, T0-pred and T0-v2 (the synthetic GetImpactData call) need no gameplay;
// E4 judges T5's cast; T-threads reports the thread trace. E1/E2 are in
// SelfTestStepsV2Fire.cpp, E3 (save and reload) in SelfTestReload.cpp.

#include "SelfTestCtx.h"

#include "MainThreadQueue.h"

#include "Config.h"
#include "CopyRequest.h"
#include "CopySwap.h"
#include "FireSet.h"
#include "FormCopies.h"
#include "Forms.h"
#include "GameThread.h"
#include "HitEffectHook.h"
#include "ImpactSwap.h"
#include "SelfTestLog.h"
#include "SelfTestReload.h"
#include "SelfTestText.h"
#include "ThreadTrace.h"

#include <algorithm>
#include <array>
#include <format>
#include <string_view>

namespace BlackFlame::SelfTest
{
    namespace
    {
        using namespace SelfTestText;

        std::string DescribeDetour(const Patch::Detour& a_detour)
        {
            if (!a_detour.hooked) {
                return std::format("NOT hooked: {}; bytes {}", a_detour.note.empty() ? "off" : a_detour.note,
                    a_detour.bytes.empty() ? "-" : a_detour.bytes);
            }
            return std::format("{} ({}); bytes before {}", a_detour.chained ? "chained" : "detoured", a_detour.note,
                a_detour.bytes);
        }

        // A device that should be there: hooked, else FAIL; INFO when its config key is off or there are no copies
        void CheckDevice(const char* a_name, bool a_wanted, const Patch::Detour& a_detour)
        {
            if (!a_wanted) {
                SelfTestLog::Info("T0-hooks", std::format("{}: not wanted (config off or no copies)", a_name));
                return;
            }
            SelfTestLog::Check("T0-hooks", a_detour.hooked, std::format("{}: {}", a_name, DescribeDetour(a_detour)));
        }

        const RE::BGSImpactDataSet* FirstImpactSet(const RE::SpellItem* a_spell)
        {
            if (!a_spell) {
                return nullptr;
            }
            for (const auto* effect : a_spell->effects) {
                if (effect && effect->baseEffect && effect->baseEffect->data.impactDataSet) {
                    return effect->baseEffect->data.impactDataSet;
                }
            }
            return nullptr;
        }

        // V12: the prologue replay decoder on fixed byte cases (Patch::ReplayLength)
        void CheckReplayCases()
        {
            struct Case
            {
                const char* name;
                std::array<std::uint8_t, 8> bytes;
                std::size_t want;
            };
            constexpr std::array kCases{
                Case{ "mov [rsp+8], rcx (Launch, 1.7.104)", { 0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x53, 0x56 }, 5 },
                Case{ "push rbx (REX), rbp, rsi, rdi (creation, 1.7.104)", { 0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x90 }, 5 },
                Case{ "sub rsp, imm8; push rbx", { 0x48, 0x83, 0xEC, 0x28, 0x53, 0x90, 0x90, 0x90 }, 5 },
                Case{ "sub rsp, imm32", { 0x48, 0x81, 0xEC, 0x00, 0x01, 0x00, 0x00, 0x90 }, 7 },
                Case{ "mov rax, rsp; push rbp; push rsi", { 0x48, 0x8B, 0xC4, 0x55, 0x56, 0x90, 0x90, 0x90 }, 5 },
                Case{ "mov r11, rsp; push rbp; push rsi", { 0x4C, 0x8B, 0xDC, 0x55, 0x56, 0x90, 0x90, 0x90 }, 5 },
                Case{ "mov rax, rsp; mov [rax+8], rbx (refused: not rsp-based)", { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x90 }, 0 },
                Case{ "jmp rel32 (refused: not replayed)", { 0xE9, 0x00, 0x00, 0x00, 0x00, 0x90, 0x90, 0x90 }, 0 },
            };
            std::string bad;
            for (const auto& c : kCases) {
                const auto got = Patch::ReplayLength(c.bytes.data(), c.bytes.size());
                if (got != c.want) {
                    bad += std::format("{}{}: {} (want {})", bad.empty() ? "" : "; ", c.name, got, c.want);
                }
            }
            SelfTestLog::Check("T0-hooks", bad.empty(), bad.empty() ? std::format("replay decoder: all {} byte cases right",
                kCases.size()) : "replay decoder: " + bad);
        }

        void CheckPredicate(const SelfTestWorld::VanillaForms& a_forms, const SelfTestWorld::V2Forms& a_v2)
        {
            const bool fireOk = IsFireLaunch(a_forms.firebolt, nullptr, nullptr);
            const bool nothingOk = !IsFireLaunch(nullptr, nullptr, nullptr);
            SelfTestLog::Check("T0-pred", fireOk && nothingOk, std::format("IsFireLaunch(Firebolt) {} (want true); "
                "IsFireLaunch(no spell, no enchantment, no weapon) {} (want false)", fireOk, !nothingOk));
            if (!a_v2.sparks) {
                SelfTestLog::Info("T0-pred", "Sparks not found: the non-fire case is not checked");
            } else if (IsFireSpell(a_v2.sparks)) {
                SelfTestLog::Info("T0-pred", std::format("Sparks {:08X} counts as fire in this load order (catalog or a fire "
                    "MGEF): not a non-fire prop here", a_v2.sparks->GetFormID()));
            } else {
                const bool sparksOk = !IsFireLaunch(a_v2.sparks, nullptr, nullptr);
                SelfTestLog::Check("T0-pred", sparksOk, std::format("IsFireLaunch(Sparks {:08X}) {} (want false: a non-fire "
                    "spell is never swapped, even sharing a copied PROJ)", a_v2.sparks->GetFormID(), !sparksOk));
            }
        }
    }

    // T0-hooks, T0-pred, T0-v2 synthetic impact: the v2 devices as installed, the launch predicate on vanilla props, and
    // the GetImpactData thunk called directly with the window closed and open
    bool StepV2Static(Ctx& a_c, bool)
    {
        const auto& cfg = GetConfig();
        const auto& status = GetCopySwapStatus();
        const auto counts = GetCopyCounts();
        const bool copies = status.copies;
        CheckDevice("Projectile::Launch (copyLaunch / copyLaunchExplosion)", copies && (cfg.copyLaunch || cfg.copyLaunchExplosion),
            status.launch);
        // Multi-runtime: wanted on every runtime; a prologue the replay decoder does not take is the logged fallback (the
        // Launch post-write still swaps the MGEF explosions), so it is INFO, not FAIL
        if (!status.explosionCreate.hooked && status.explosionCreate.note.find("replay decoder") != std::string::npos) {
            SelfTestLog::Info("T0-hooks", "explosion creation: not detoured on this build (" + status.explosionCreate.note +
                "; bytes " + status.explosionCreate.bytes + "): the creation swap is off, the Launch post-write still works");
        } else {
            CheckDevice("explosion creation (copyExplosionCreate)", copies && cfg.copyExplosionCreate && counts.explosions > 0,
                status.explosionCreate);
        }
        CheckReplayCases();
        // C4: the casting art call site and the hazard creation detours (INFO when the decoder or the scan left them alone)
        const auto& art = status.castingArt;
        if (copies && cfg.copyCastingArt && counts.arts > 0) {
            SelfTestLog::Check("T0-hooks", art.hooked, "SetCastingArt call: " + art.note);
        } else {
            SelfTestLog::Info("T0-hooks", "SetCastingArt call: not wanted (" + (art.note.empty() ? std::string("config off or no art copies") : art.note) + ")");
        }
        if (!status.hazardCreate.hooked && status.hazardCreate.note.find("replay decoder") != std::string::npos) {
            SelfTestLog::Info("T0-hooks", "hazard creation: not detoured on this build (" + status.hazardCreate.note + "; bytes " +
                status.hazardCreate.bytes + ")");
        } else {
            CheckDevice("hazard creation (copyHazardCreate)", copies && cfg.copyHazardCreate && counts.hazards > 0, status.hazardCreate);
        }
        if (status.hazardCreate.hooked) {
            SelfTestLog::Info("T0-hooks", std::format("spawn-hazard window: {} ({}; bytes {})", status.spawnHazard.hooked ? "detoured" :
                "NOT detoured (spawn-hazard hazards stay orange)", status.spawnHazard.note, status.spawnHazard.bytes));
        }
        if (copies && cfg.copyImpacts && counts.impacts > 0) {
            SelfTestLog::Check("T0-hooks", status.impactSites == 2, std::format("GetImpactData call sites: {} of 2 hooked ({} "
                "chained): {}", status.impactSites, status.impactChained, status.impactNote));
        } else {
            SelfTestLog::Info("T0-hooks", std::format("GetImpactData call sites: not wanted ({})", status.impactNote));
        }
        const auto impact = GetImpactSwapStatus();
        SelfTestLog::Info("T0-hooks", std::format("impact window call sites (round 10, shared with the copy window): {} of 3 "
            "hooked, {} chained; GetModel {}", impact.sitesHooked, impact.sitesChained, impact.modelHooked ? "hooked" : "not hooked"));

        // C1: the copy request's state (judged against the census by hand from request.json; here only that it ran)
        const auto& request = GetCopyRequestState();
        const bool dumpOk = request.dumpState == "unchanged" || request.dumpState.starts_with("written (");
        if (!cfg.writeRequest) {
            SelfTestLog::Info("T0-request", std::format("copy request off (writeRequest false): {} closure forms, {} excluded",
                request.forms, request.excluded));
        } else {
            // J2: a NIF the game could not read or a dump that did not complete is a FAIL (the builder would miss it)
            // Decision 4: a form's or muzzle's NIF that failed is a FAIL (no copy of it can be made); an ADDN-only one is INFO below
            SelfTestLog::Check("T0-request", request.computed && request.forms > 0 && request.requestWritten &&
                request.nifsFailedForms == 0 && dumpOk,
                std::format("copy request: {} closure forms, {} excluded, {} NIFs ({} form or muzzle NIFs failed) read in {} ms, "
                    "fingerprint {}, request {}, dump {}", request.forms, request.excluded, request.nifs, request.nifsFailedForms,
                    request.readMs,
                    request.computed ? request.fingerprint : std::string("NOT computed"),
                    request.requestWritten ? "written" : "NOT written", request.dumpState));
        }
        if (request.nifsFailedAddons > 0) {
            std::string paths;
            for (const auto& path : request.failedAddonSample) {
                paths += (paths.empty() ? "" : ", ") + path;
            }
            SelfTestLog::Info("T0-request", std::format("{} ADDN-only NIF(s) did not open (often another mod's missing mesh; their "
                "grey particles are not made): {}{}", request.nifsFailedAddons, paths,
                request.nifsFailedAddons > request.failedAddonSample.size() ? ", ..." : ""));
        }

        std::string detail;
        const auto v2 = SelfTestWorld::LookupV2(detail);
        SelfTestLog::Info("T0-pred", "props: " + detail);
        if (a_c.haveForms) {
            CheckPredicate(a_c.forms, v2);
        }
        if (copies && cfg.copyImpacts && counts.impacts > 0 && a_c.haveForms) {
            const auto result = RunSyntheticImpactCheck(FirstImpactSet(a_c.forms.firebolt));
            if (result.ran) {
                SelfTestLog::Check("T0-v2", result.closedOriginal && result.openCopy, "synthetic GetImpactData: " + result.detail);
            } else {
                SelfTestLog::Fail("T0-v2", "synthetic GetImpactData not run: " + result.detail);
            }
        }
        return true;
    }

    // E4: the player's Firebolt (T3 and T5's casts) left our EFSH alight on the target. PASS only on that sighting;
    // the lines tell whether the Firebolt hit at all and how the 0x0D calls on the player's effects ended
    bool StepHitShader(Ctx& a_c, bool)
    {
        const auto target = RefOf(a_c.dummy);
        const auto targetId = target ? target->GetFormID() : 0;
        const auto spellId = a_c.forms.firebolt ? a_c.forms.firebolt->GetFormID() : 0;
        int hits = 0;
        std::string projectiles;
        for (const auto& hit : SelfTestProbe::HitsSince(a_c.t3Mark)) {
            const auto* player = RE::PlayerCharacter::GetSingleton();
            if (player && hit.cause == player->GetFormID() && hit.target == targetId && hit.source == spellId) {
                ++hits;
                projectiles += std::format("{}{:08X}", projectiles.empty() ? "" : ",", hit.projectile);
            }
        }
        const auto shader = GetHitSlotStats(true);
        const auto art = GetHitSlotStats(false);
        SelfTestLog::Info("E4", std::format("the player's Firebolt hit the target (TESHitEvent): {} time(s){}", hits,
            projectiles.empty() ? "" : " (projectiles " + projectiles + ")"));
        SelfTestLog::Info("E4", std::format("GetHitEffectShader on the player's effects: {} calls, {} swapped, {} engine answered "
            "null ({} with flags bit 1), {} not stoked, {} not a fire effect, {} other; GetHitEffectArt: {} calls, {} null, {} "
            "copy returned", shader.playerCalls, shader.swapped, shader.engineNull, shader.engineNullFlag, shader.notStoked,
            shader.notFire, shader.otherRejected, art.playerCalls, art.engineNull, art.copied));
        // C3/D3: judged only after a confirmed hit (T5's aim) or our shader seen on the target
        if (!a_c.aim.hit && !a_c.greySeen) {
            SelfTestLog::Fail("E4", std::format("aim: no hit after {} casts (T5) - nothing to judge | {}", a_c.aim.casts,
                AimGeometry(a_c)));
            return true;
        }
        const bool alight = a_c.greySeen.has_value();
        SelfTestLog::Check("E4", alight, alight ? "our EFSH was alight on the target after the player's Firebolt (T5)" :
            std::format("our EFSH never showed on the target (T5); hit {}, swapped {} - {}", hits, shader.swapped,
                hits == 0 ? "the Firebolt did not hit, so nothing was asked" :
                shader.playerCalls == 0 ? "the thunk was never reached for the player's effect" :
                shader.engineNull > 0 && shader.swapped == 0 ? "the engine answered null" :
                "our own check kept the engine's shader (see the GetHitEffectShader lines)"));
        return true;
    }

    bool CopyLaunchExpected(const RE::SpellItem* a_spell)
    {
        const auto* effect = a_spell ? a_spell->GetAVEffect() : nullptr;
        return GetConfig().copyLaunch && GetCopySwapStatus().launch.hooked && effect &&
               CopyOf(effect->data.projectileBase) != nullptr;
    }

    void ShiftV2Clocks(Clock::duration a_paused)
    {
        ShiftFireClocks(a_paused);
        ShiftFlameClocks(a_paused);
        ShiftArtClocks(a_paused);
        Reload::ShiftClocks(a_paused);
    }

    // T-threads (S4): which threads ran our task bodies and v2 hooks, and whether bodies on different
    // threads overlapped. Measurement only; no gate depends on it. Meant to observe: no recorded body is a FAIL.
    bool StepThreads(Ctx&, bool)
    {
        auto entries = ThreadTrace::Snapshot();
        std::erase_if(entries, [](const auto& a_e) { return a_e.end == 0; });  // START entries (C6c)
        const auto game = g_gameThreadNativeId.load();
        for (const auto& line : ThreadTrace::Report(game)) {
            SelfTestLog::Info("T-threads", line);
        }
        std::size_t onOther = 0;
        for (const auto& e : entries) {
            onOther += e.thread != game ? 1 : 0;
        }
        // PASS needs every v2 hook kind measured at least once; otherwise INFO naming the kinds with no entry
        // Only the kinds whose hook is installed are expected (the creation detour is off on unverified versions)
        const auto& status = GetCopySwapStatus();
        std::vector<std::string_view> kinds;
        if (status.launch.hooked) {
            kinds.emplace_back("Launch (v2 detour)");
        }
        if (status.explosionCreate.hooked) {
            kinds.emplace_back("explosion create (v2 detour)");
        }
        if (status.impactSites > 0) {
            kinds.emplace_back("GetImpactData in a copy window");
        }
        std::string missing = kinds.empty() ? std::string("(no v2 hook installed)") : std::string();
        for (const auto kind : kinds) {
            const bool seen = std::ranges::any_of(entries, [&](const auto& a_e) { return kind == a_e.kind; });
            if (!seen) {
                missing += std::format("{}'{}'", missing.empty() ? "" : ", ", kind);
            }
        }
        SelfTestLog::Info("T-threads", std::format("{} traced bodies (tasks and hooks), {} not on the main thread, {} overlapping "
            "pairs on different threads; v2 hook kinds {}", entries.size(), onOther, ThreadTrace::CountCrossThreadOverlaps(entries),
            missing.empty() ? std::string("all measured") : "not measured (no entry): " + missing));
        // D1: every queued body ran on the main thread, one drain at a time; hooks stay measured above
        const auto q = MainQueue::GetStats();
        // E7/G1: job-side hook bodies (Launch, explosion creation, GetImpactData and Explosion::Update in a copy window) entered
        // on a worker thread while a drain was running, counted online at each hook's entry over the whole session
        const bool ok = MainQueue::Installed() && q.bodies > 0 && q.bodiesOffMain == 0 && q.overlaps == 0 && q.fallbacks == 0 &&
                        q.thread != 0 && q.thread == q.mainThreadId && q.jobHooksDuringDrain == 0;
        SelfTestLog::Check("T-threads", ok, std::format("main-thread queue {}: {} frames ({} paused), {} task bodies, {} off "
            "Main::threadID, {} overlapping drains, {} posts sent to SKSE AddTask; drain thread {}, Main::threadID {}; {} job-side "
            "hook bodies entered on a worker thread during a drain (whole session)", MainQueue::Installed() ? "installed" :
            "NOT installed", q.frames, q.pausedFrames, q.bodies, q.bodiesOffMain, q.overlaps, q.fallbacks, q.thread, q.mainThreadId,
            q.jobHooksDuringDrain));
        return true;
    }
}

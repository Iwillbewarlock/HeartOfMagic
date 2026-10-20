// =============================================================================
// COPY SWAP - the copy window and the GetImpactData call sites (CopySwap.h, device 3), with
// the rune decal window (AE34452's AE44186 call); the install entry point and the status
// =============================================================================

#include "CopySwap.h"

#include "Config.h"
#include "FormCopies.h"
#include "LogLimit.h"
#include "MainThreadQueue.h"
#include "SehGuard.h"
#include "ThreadTrace.h"

#include <array>
#include <atomic>
#include <cstring>
#include <format>
#include <optional>

namespace BlackFlame
{
    namespace
    {
        // BGSImpactDataSet's lookup: (BGSImpactDataSet*, BGSMaterialType*) -> BGSImpactData* (1.7.104 code at
        // 0x31e680: CRC32 of the key, then the impactMap hash walk; same pdata size and unwind codes on 1.6.1170)
        constexpr std::uint64_t kGetImpactDataSe = 20408, kGetImpactDataAe = 20860;

        struct Caller
        {
            const char* name;
            std::uint64_t se;
            std::uint64_t ae;
            // The call's offset in 1.7.104 code, for a call another plugin rewrote; only where the function has the
            // same pdata size and unwind codes on 1.6.1170; 0 none
            std::uint32_t knownOffsetAe;
        };
        constexpr std::array kCallers{
            Caller{ "Projectile::SpawnCollisionEffects", 43022, 44213, 0xBC },
            // 0x7FC on 1.7.104, but 43851 differs on 1.6.1170 (pdata size 0x135D vs 0x139D, other unwind codes): no
            // fallback offset, the scan alone finds its call
            Caller{ "the explosion's impact function", 42679, 43851, 0 },
            // The projectile decal helper's own lookup, reached only when its IPCT argument is null: in .text only from the
            // rune placement in MagicCaster's launch helper AE34452 (SpawnCollisionEffects' call at 7ED1CB passes the IPCT
            // it looked up). AE only (no SE id read), scan only; 1.6.1170: one E8 at +0xB5 (RVA 7E9C95, E8 86 E3 B2 FF)
            Caller{ "rune decal (AE44186 via AE34452)", 0, 44186, 0 },
        };
        constexpr std::size_t kRuneLookup = 2;  // kCallers index with its own status (runeLookup), not in impactSites

        // The rune decal window: AE34452 (MagicCaster's launch helper) places a rune's waiting glyph right after Launch by
        // calling AE44186 at +0x52F with a null IPCT (RVA 5C028F, 1.6.1170 image read 2026-10-20, build/rune-decal); the
        // impact call sites never see it. Only on 1.6.1170, only with the argument setup and the E8 below
        constexpr std::uint64_t kRuneLauncherAe = 34452, kDecalHelperAe = 44186;
        constexpr std::uintptr_t kRuneDecalCallAe = 0x52F;
        // mov qword [rsp+28], 0 (the IPCT: null); mov byte [rsp+20], 1; mov r9d, edi; lea r8, [rsp+58]; mov rdx, rsi;
        // mov rcx, rbx (the projectile)
        constexpr std::array<std::uint8_t, 28> kRuneDecalArgs{ 0x48, 0xC7, 0x44, 0x24, 0x28, 0x00, 0x00, 0x00, 0x00, 0xC6,
            0x44, 0x24, 0x20, 0x01, 0x44, 0x8B, 0xCF, 0x4C, 0x8D, 0x44, 0x24, 0x58, 0x48, 0x8B, 0xD6, 0x48, 0x8B, 0xCB };
        constexpr const char* kRuneWindowName = "rune decal window (AE44186 call in AE34452)";

        // Saved and restored by each CopyWindow: the innermost decision wins
        thread_local bool t_copyWindow = false;
        thread_local const RE::TESForm* t_copyWindowBase = nullptr;  // the trace lines only: the base that opened the window
        std::atomic<std::uint64_t> g_windowCalls{ 0 }, g_copies{ 0 };
        std::atomic<std::uint64_t> g_runeCalls{ 0 }, g_runeWindows{ 0 };
        std::atomic<bool> g_failureReported{ false }, g_runeFailure{ false };
        CopySwapStatus g_status;  // written once at kDataLoaded
        std::array<bool, kCallers.size()> g_siteHooked{};  // which GetImpactDataHook<I> has its func set
        // Trace (Fire Rune glyph): one line per impact lookup in a copy window, the first ones per call site and IPDS
        // (LogLimit::PerKey: a concentration cast's impacts cannot use up the lines before a rune's show)
        std::atomic<bool> g_impactTraceFull{ false }, g_impactTraceFailure{ false };

        RE::FormID IdOf(const RE::TESForm* a_form) noexcept
        {
            return a_form ? a_form->GetFormID() : 0;
        }

        // The copy window's impact and the one it replaces (a_copy null: the original is used), with their decal sets
        // Its own guard (a fault skips the line only) and no throw out: the swap decision never depends on it
        void TraceImpact(std::size_t a_site, const RE::BGSImpactDataSet* a_set, const RE::BGSImpactData* a_original,
            const RE::BGSImpactData* a_copy, const char* a_siteName) noexcept
        {
            if (!a_original || g_impactTraceFull.load()) {
                return;
            }
            try {
                (void)GuardedKeep("copy impact (trace)", [&]() {
                    int line = 0, keyLine = 0;
                    if (!LogLimit::PerKey("impact trace", (static_cast<std::uint64_t>(a_site) << 32) | IdOf(a_set), line, keyLine)) {
                        return;
                    }
                    if (line >= LogLimit::kTraceLines) {
                        // the kind's total cap reached: later lookups return before PerKey's lock (one stopped by the
                        // per-key or key-count cap still takes it)
                        g_impactTraceFull.store(true);
                    }
                    const auto* used = a_copy ? a_copy : a_original;
                    const auto* base = t_copyWindowBase;
                    logger::info("BlackFlame: impact trace {}/{} ({} of this site and IPDS): {} in a copy window (base {:08X} {}): "
                                 "IPDS {:08X}, IPCT {:08X} -> {:08X} ({}); decal TXST {} / {} (source IPCT's {:08X} / {:08X})",
                        line, LogLimit::kTraceLines, keyLine, a_siteName, IdOf(base),
                        base ? RE::FormTypeToString(base->GetFormType()) : std::string_view("none: the synthetic impact self-test's window"),
                        IdOf(a_set), IdOf(a_original), IdOf(used), a_copy ? "copy" : "NO COPY: the original is used",
                        CopySwapDetail::TxstText(used->decalTextureSet), CopySwapDetail::TxstText(used->decalTextureSet2),
                        IdOf(a_original->decalTextureSet), IdOf(a_original->decalTextureSet2));
                });
            } catch (...) {  // (...) as this file's thunks: noexcept, and SehGuard passes every non-fault throw on to here
                if (!g_impactTraceFailure.exchange(true)) {
                    logger::warn("BlackFlame: the copy impact trace threw; the line was dropped (the impact is unchanged); further "
                                 "failures are not logged");
                }
            }
        }


        template <std::size_t I>
        struct GetImpactDataHook
        {
            static RE::BGSImpactData* thunk(RE::BGSImpactDataSet* a_set, RE::BGSMaterialType* a_material)
            {
                auto* original = func(a_set, a_material);
                if (!t_copyWindow || !original) {
                    return original;
                }
                const ThreadTrace::Scope trace("GetImpactData in a copy window");
                MainQueue::NoteJobSideHook();  // G1: T-threads judges by this count
                g_windowCalls.fetch_add(1);
                try {
                    RE::BGSImpactData* copy = nullptr;
                    (void)Guarded("copy impact", [&]() { copy = CopyOfAs(original); });
                    TraceImpact(I, a_set, original, copy, kCallers[I].name);
                    if (copy) {
                        g_copies.fetch_add(1);
                        return copy;
                    }
                } catch (...) {
                    if (!g_failureReported.exchange(true)) {
                        logger::error("BlackFlame: the copy impact thunk threw; the original was used; further failures "
                                      "are not logged");
                    }
                }
                return original;
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };

        // The caller's one E8 to GetImpactData (scan), or at the known 1.7.104 offset an E8 into another plugin's
        // trampoline stub (chained); anything else is left alone
        std::uintptr_t FindSite(const Caller& a_caller, std::uintptr_t a_callee, bool& a_chained)
        {
            const auto function = Patch::AddressOf(Patch::IdFor(a_caller.se, a_caller.ae));
            const auto sites = Patch::FindCalls(function, a_callee);
            if (sites.size() == 1) {
                return sites.front();
            }
            if (sites.size() > 1) {
                logger::warn("BlackFlame: copy impacts: {} has {} calls to GetImpactData - left alone", a_caller.name,
                    sites.size());
                return 0;
            }
            // The fallback offset is for a call another plugin already rewrote, and only on the builds whose code was read:
            // in-function offsets move between AE builds, so elsewhere only the scan counts
            if (!REL::Module::IsAE() || a_caller.knownOffsetAe == 0 || !Patch::IsCodeReadBuild()) {
                logger::warn("BlackFlame: copy impacts: no call to GetImpactData found in {}", a_caller.name);
                Patch::RecordHook(a_caller.name, false, function, Patch::HexBytes(function, Patch::kRel32Size),
                    "no E8 to GetImpactData found by the scan");
                return 0;
            }
            const auto site = function + a_caller.knownOffsetAe;
            if (*reinterpret_cast<const std::uint8_t*>(site) == Patch::kCallOpcode &&
                Patch::IsTrampolineStub(Patch::Rel32Target(site))) {
                a_chained = true;
                return site;
            }
            logger::warn("BlackFlame: copy impacts: {} +0x{:X} is not a call to GetImpactData or a trampoline stub (bytes "
                         "{}) - left alone", a_caller.name, a_caller.knownOffsetAe, Patch::HexBytes(site, Patch::kRel32Size));
            Patch::RecordHook(a_caller.name, false, site, Patch::HexBytes(site, Patch::kRel32Size),
                "neither the scan nor the 1.7.104 offset found the call");
            return 0;
        }

        template <std::size_t I>
        void HookSite(std::uintptr_t a_callee)
        {
            bool chained = false;
            const auto site = FindSite(kCallers[I], a_callee, chained);
            if (!site) {
                return;
            }
            chained = chained || Patch::Rel32Target(site) != a_callee;
            const auto before = Patch::HexBytes(site, Patch::kRel32Size);
            GetImpactDataHook<I>::func = Patch::WriteCall(kCallers[I].name, site, GetImpactDataHook<I>::thunk,
                chained ? "GetImpactData call, chained onto another plugin's redirect" : "GetImpactData call (scan and target check)");
            g_siteHooked[I] = true;
            if constexpr (I == kRuneLookup) {
                auto& status = g_status.runeLookup;
                status.hooked = true;
                status.chained = chained;
                status.offset = site - Patch::AddressOf(Patch::IdFor(kCallers[I].se, kCallers[I].ae));
                status.note = std::format("caller +0x{:X}{} (bytes before {})", status.offset, chained ? ", chained" : "", before);
            } else {
                ++g_status.impactSites;
                g_status.impactChained += chained ? 1 : 0;
            }
            logger::info("BlackFlame: copy impacts: the GetImpactData call in {} at {:X} hooked{}", kCallers[I].name,
                site - REL::Module::get().base(), chained ? " (chained onto another plugin's redirect)" : "");
        }

        // A few lines per copy base (LogLimit::PerKey): the rune decal window opened, with the IPCT argument (null: AE44186
        // looks it up itself, through the rune decal lookup site) and the thread (which thread runs AE34452 is not proven)
        void TraceRuneWindow(const RE::TESForm* a_base, const RE::BGSImpactData* a_impact) noexcept
        {
            try {
                int line = 0, keyLine = 0;
                if (LogLimit::PerKey("rune decal window", IdOf(a_base), line, keyLine)) {
                    logger::info("BlackFlame: rune decal window {}/{} ({} of this base): copy window open around AE44186 for base "
                                 "{:08X}, IPCT argument {:08X}, thread {}", line, LogLimit::kTraceLines, keyLine, IdOf(a_base),
                        IdOf(a_impact), REX::W32::GetCurrentThreadId());
                }
            } catch (...) {
                (void)g_runeFailure.exchange(true);
            }
        }

        // AE44186 at AE34452+0x52F: (projectile, position*, direction*, material, bool, IPCT (null here), decal group, bool force).
        // All eight forwarded as they came (integer registers and stack slots; no float argument); rax passed back
        struct RuneDecalHook
        {
            static std::uintptr_t thunk(RE::Projectile* a_projectile, std::uintptr_t a_position, std::uintptr_t a_direction,
                std::uintptr_t a_material, std::uintptr_t a_flag, RE::BGSImpactData* a_impact, std::uintptr_t a_decalGroup,
                std::uintptr_t a_force)
            {
                g_runeCalls.fetch_add(1);
                bool copyOpen = false;
                const RE::TESForm* base = nullptr;
                try {
                    (void)Guarded("rune decal window", [&]() {
                        base = a_projectile ? a_projectile->GetBaseObject() : nullptr;
                        copyOpen = base && IsCopy(base);
                    });
                } catch (...) {
                    copyOpen = false;
                    if (!g_runeFailure.exchange(true)) {
                        logger::error("BlackFlame: the rune decal window thunk threw; no window, the engine's decal; not logged again");
                    }
                }
                std::optional<ThreadTrace::Scope> trace;
                if (copyOpen) {
                    g_runeWindows.fetch_add(1);
                    trace.emplace("rune decal (copy window open)");
                    TraceRuneWindow(base, a_impact);
                }
                // As the impact call sites: a non-copy base closes the window (the innermost call decides)
                const CopyWindow window(copyOpen, copyOpen ? base : nullptr);
                return func(a_projectile, a_position, a_direction, a_material, a_flag, a_impact, a_decalGroup, a_force);
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };

        // 1.6.1170 only (the build whose AE34452 was read): the call at +0x52F must be an E8 to AE44186 after the argument
        // setup kRuneDecalArgs; anything else (another build, another plugin's rewrite) is logged and left alone
        void InstallRuneWindow()
        {
            auto& site = g_status.runeWindow;
            if (REL::Module::get().version() != REL::Version(1, 6, 1170, 0)) {
                site.note = std::format("AE 1.6.1170 only (its AE34452 is the one read; running {}): the rune glyph keeps its "
                                        "original colour, the lookup site alone does not change it", REL::Module::get().version().string("."));
                logger::info("BlackFlame: copy impacts: {} not installed: {}", kRuneWindowName, site.note);
                Patch::RecordHook(kRuneWindowName, false, 0, {}, site.note);
                return;
            }
            const auto caller = Patch::AddressOf(kRuneLauncherAe);
            const auto helper = Patch::AddressOf(kDecalHelperAe);
            const auto at = caller + kRuneDecalCallAe;
            const auto from = at - kRuneDecalArgs.size();
            const auto before = Patch::HexBytes(from, kRuneDecalArgs.size() + Patch::kRel32Size);
            const bool args = Patch::IsExecutable(from, kRuneDecalArgs.size() + Patch::kRel32Size) &&
                              std::memcmp(reinterpret_cast<const void*>(from), kRuneDecalArgs.data(), kRuneDecalArgs.size()) == 0;
            const bool call = args && *reinterpret_cast<const std::uint8_t*>(at) == Patch::kCallOpcode && Patch::Rel32Target(at) == helper;
            if (!call) {
                site.note = std::format("AE34452 +0x{:X} is not the null-IPCT argument setup and an E8 to AE44186 (bytes from -{}: {}) "
                                        "- left alone; the rune glyph keeps its original colour", kRuneDecalCallAe, kRuneDecalArgs.size(), before);
                logger::warn("BlackFlame: copy impacts: {}: {}", kRuneWindowName, site.note);
                Patch::RecordHook(kRuneWindowName, false, at, before, site.note);
                return;
            }
            RuneDecalHook::func = Patch::WriteCall(kRuneWindowName, at, RuneDecalHook::thunk,
                std::format("AE34452 +0x{:X}; bytes from -{}: {}", kRuneDecalCallAe, kRuneDecalArgs.size(), before));
            site.hooked = true;
            site.offset = kRuneDecalCallAe;
            site.note = std::format("AE34452 +0x{:X} (bytes from -{}: {})", kRuneDecalCallAe, kRuneDecalArgs.size(), before);
            logger::info("BlackFlame: copy impacts: {} at {:X} hooked (bytes from -{}: {})", kRuneWindowName,
                at - REL::Module::get().base(), kRuneDecalArgs.size(), before);
        }

        // The rune decal's two parts: the lookup inside AE44186 (any AE build, by scan) and the window around its null-IPCT
        // call (1.6.1170). Either alone changes nothing: a closed window returns the original, an unhooked lookup too
        // Neither rune decal part installed: both notes and both hook records say why the rune glyph keeps its colour
        void NoteRuneDecalOff(const std::string& a_reason)
        {
            const auto note = std::format("not installed: {}; the rune glyph keeps its original colour", a_reason);
            g_status.runeLookup.note = g_status.runeWindow.note = note;
            Patch::RecordHook(kCallers[kRuneLookup].name, false, 0, {}, note);
            Patch::RecordHook(kRuneWindowName, false, 0, {}, note);
            logger::info("BlackFlame: copy impacts: rune decal lookup and window {}", note);
        }

        void InstallRuneDecal(std::uintptr_t a_lookupCallee)
        {
            if (!REL::Module::IsAE()) {
                NoteRuneDecalOff(CopySwapDetail::kSeNotVerified);
                return;
            }
            if (!Patch::HasIds({ kDecalHelperAe, kRuneLauncherAe }, "copy impacts (rune decal)")) {
                NoteRuneDecalOff("an Address Library id is missing (AE 44186 or 34452)");
                return;
            }
            HookSite<kRuneLookup>(a_lookupCallee);
            if (!g_siteHooked[kRuneLookup] && g_status.runeLookup.note.empty()) {
                g_status.runeLookup.note = "no single E8 to GetImpactData in AE44186 - left alone; the rune glyph keeps its original colour";
                Patch::RecordHook(kCallers[kRuneLookup].name, false, 0, {}, g_status.runeLookup.note);
            }
            InstallRuneWindow();
        }
    }

    CopyWindow::CopyWindow(bool a_open, const RE::TESForm* a_base) noexcept :
        m_previous(t_copyWindow), m_previousBase(t_copyWindowBase)
    {
        t_copyWindow = a_open;
        t_copyWindowBase = a_open ? a_base : nullptr;
    }

    CopyWindow::~CopyWindow()
    {
        t_copyWindow = m_previous;
        t_copyWindowBase = m_previousBase;
    }

    bool CopyWindowOpen() noexcept
    {
        return t_copyWindow;
    }

    namespace CopySwapDetail
    {
        void InstallImpactSites(CopySwapStatus& a_status)
        {
            if (!GetConfig().copyImpacts) {
                a_status.impactNote = "copyImpacts false";
                NoteRuneDecalOff(a_status.impactNote);
                return;
            }
            const auto calleeId = Patch::IdFor(kGetImpactDataSe, kGetImpactDataAe);
            if (!Patch::HasIds({ calleeId, Patch::IdFor(kCallers[0].se, kCallers[0].ae), Patch::IdFor(kCallers[1].se, kCallers[1].ae) },
                    "copy impacts")) {
                a_status.impactNote = "an Address Library id is missing";
                NoteRuneDecalOff("an Address Library id of GetImpactData or its impact call sites is missing");
                return;
            }
            constexpr std::size_t kBytesPerSite = 14;
            constexpr std::size_t kSites = kCallers.size() + 1;  // + the rune decal window
            if (SKSE::GetTrampoline().free_size() < kSites * kBytesPerSite) {
                a_status.impactNote = "the SKSE trampoline has no room";
                logger::error("BlackFlame: copy impacts: the SKSE trampoline has no room - left off");
                NoteRuneDecalOff(a_status.impactNote);
                return;
            }
            const auto callee = Patch::AddressOf(calleeId);
            HookSite<0>(callee);
            HookSite<1>(callee);
            InstallRuneDecal(callee);
            a_status.impactNote = std::format("{} of {} call sites hooked; rune decal lookup {}, window {}", a_status.impactSites,
                kRuneLookup, a_status.runeLookup.hooked ? "hooked" : a_status.runeLookup.note,
                a_status.runeWindow.hooked ? "hooked" : a_status.runeWindow.note);
        }

        void AddImpactCounters(CopySwapCounters& a_out) noexcept
        {
            a_out.impactWindowCalls = g_windowCalls.load();
            a_out.impactCopies = g_copies.load();
            a_out.runeDecalCalls = g_runeCalls.load();
            a_out.runeDecalWindows = g_runeWindows.load();
        }
    }

    void InstallCopySwaps()
    {
        const auto counts = GetCopyCounts();
        g_status.copies = counts.projectiles + counts.explosions + counts.impacts + counts.hazards + counts.arts > 0;
        if (!g_status.copies) {
            logger::info("BlackFlame: copy swap off: no copies were filled");
            return;
        }
        CopySwapDetail::InstallLaunchDetours(g_status);
        CopySwapDetail::InstallImpactSites(g_status);
        CopySwapDetail::InstallArtSite(g_status);
        CopySwapDetail::InstallHazardDetours(g_status);
        const auto state = [](const Patch::Detour& a_detour) { return a_detour.hooked ? (a_detour.chained ? "chained" : "detoured") : "off"; };
        logger::info("BlackFlame: copy swap: Launch {}, explosion creation {}, impacts: {}, casting art {}, hazard creation {} "
                     "(spawn window {}); trampoline {} bytes free", state(g_status.launch), state(g_status.explosionCreate),
            g_status.impactNote, g_status.castingArt.hooked ? "hooked" : g_status.castingArt.note, state(g_status.hazardCreate),
            state(g_status.spawnHazard), SKSE::GetTrampoline().free_size());
    }

    SyntheticImpact RunSyntheticImpactCheck(const RE::BGSImpactDataSet* a_set)
    {
        SyntheticImpact out;
        using Thunk = RE::BGSImpactData* (*)(RE::BGSImpactDataSet*, RE::BGSMaterialType*);
        const Thunk thunk = g_siteHooked[0] ? &GetImpactDataHook<0>::thunk :
                            g_siteHooked[1] ? &GetImpactDataHook<1>::thunk : nullptr;
        if (!thunk || !a_set) {
            out.detail = !thunk ? "no GetImpactData call site is hooked" : "no impact data set";
            return out;
        }
        for (const auto& [material, impact] : a_set->impactMap) {
            auto* copy = CopyOfAs(impact);
            if (!copy) {
                continue;
            }
            auto* set = const_cast<RE::BGSImpactDataSet*>(a_set);
            auto* key = const_cast<RE::BGSMaterialType*>(material);
            RE::BGSImpactData* closed = nullptr;
            RE::BGSImpactData* open = nullptr;
            {
                const CopyWindow window(false);
                closed = thunk(set, key);
            }
            {
                const CopyWindow window(true);
                open = thunk(set, key);
            }
            out.ran = true;
            out.closedOriginal = closed == impact;
            out.openCopy = open == copy;
            out.detail = std::format("set {:08X}, material {:08X}: closed window -> {:08X} (engine's {:08X}), open window -> "
                                     "{:08X} (copy {:08X})", a_set->GetFormID(), material ? material->GetFormID() : 0,
                closed ? closed->GetFormID() : 0, impact->GetFormID(), open ? open->GetFormID() : 0, copy->GetFormID());
            return out;
        }
        out.detail = std::format("set {:08X} has no impact with a copy", a_set->GetFormID());
        return out;
    }

    const CopySwapStatus& GetCopySwapStatus() noexcept
    {
        return g_status;
    }

    CopySwapCounters GetCopySwapCounters() noexcept
    {
        CopySwapCounters out;
        CopySwapDetail::AddLaunchCounters(out);
        CopySwapDetail::AddImpactCounters(out);
        CopySwapDetail::AddArtCounters(out);
        CopySwapDetail::AddHazardCounters(out);
        return out;
    }
}

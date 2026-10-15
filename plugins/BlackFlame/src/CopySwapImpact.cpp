// =============================================================================
// COPY SWAP - the copy window and the GetImpactData call sites (CopySwap.h, device 3);
// the install entry point and the status
// =============================================================================

#include "CopySwap.h"

#include "Config.h"
#include "FormCopies.h"
#include "SehGuard.h"
#include "ThreadTrace.h"

#include <array>
#include <atomic>

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
        };

        // Saved and restored by each CopyWindow: the innermost decision wins
        thread_local bool t_copyWindow = false;
        std::atomic<std::uint64_t> g_windowCalls{ 0 }, g_copies{ 0 };
        std::atomic<bool> g_failureReported{ false };
        CopySwapStatus g_status;  // written once at kDataLoaded

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
                g_windowCalls.fetch_add(1);
                try {
                    RE::BGSImpactData* copy = nullptr;
                    (void)Guarded("copy impact", [&]() { copy = CopyOfAs(original); });
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
            if (!REL::Module::IsAE() || a_caller.knownOffsetAe == 0) {
                logger::warn("BlackFlame: copy impacts: no call to GetImpactData found in {}", a_caller.name);
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
            GetImpactDataHook<I>::func = SKSE::GetTrampoline().write_call<5>(site, GetImpactDataHook<I>::thunk);
            ++g_status.impactSites;
            g_status.impactChained += chained ? 1 : 0;
            logger::info("BlackFlame: copy impacts: the GetImpactData call in {} at {:X} hooked{}", kCallers[I].name,
                site - REL::Module::get().base(), chained ? " (chained onto another plugin's redirect)" : "");
        }
    }

    CopyWindow::CopyWindow(bool a_open) noexcept :
        m_previous(t_copyWindow)
    {
        t_copyWindow = a_open;
    }

    CopyWindow::~CopyWindow()
    {
        t_copyWindow = m_previous;
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
                return;
            }
            const auto calleeId = Patch::IdFor(kGetImpactDataSe, kGetImpactDataAe);
            if (!Patch::HasIds({ calleeId, Patch::IdFor(kCallers[0].se, kCallers[0].ae), Patch::IdFor(kCallers[1].se, kCallers[1].ae) },
                    "copy impacts")) {
                a_status.impactNote = "an Address Library id is missing";
                return;
            }
            constexpr std::size_t kBytesPerSite = 14;
            if (SKSE::GetTrampoline().free_size() < kCallers.size() * kBytesPerSite) {
                a_status.impactNote = "the SKSE trampoline has no room";
                logger::error("BlackFlame: copy impacts: the SKSE trampoline has no room - left off");
                return;
            }
            const auto callee = Patch::AddressOf(calleeId);
            HookSite<0>(callee);
            HookSite<1>(callee);
            a_status.impactNote = std::format("{} of {} call sites hooked", a_status.impactSites, kCallers.size());
        }

        void AddImpactCounters(CopySwapCounters& a_out) noexcept
        {
            a_out.impactWindowCalls = g_windowCalls.load();
            a_out.impactCopies = g_copies.load();
        }
    }

    void InstallCopySwaps()
    {
        const auto counts = GetCopyCounts();
        g_status.copies = counts.projectiles + counts.explosions + counts.impacts + counts.hazards > 0;
        if (!g_status.copies) {
            logger::info("BlackFlame: copy swap off: no copies were filled");
            return;
        }
        CopySwapDetail::InstallLaunchDetours(g_status);
        CopySwapDetail::InstallImpactSites(g_status);
        logger::info("BlackFlame: copy swap: Launch {}, explosion creation {}, impacts: {}; trampoline {} bytes free",
            g_status.launch.hooked ? (g_status.launch.chained ? "chained" : "detoured") : "off",
            g_status.explosionCreate.hooked ? (g_status.explosionCreate.chained ? "chained" : "detoured") : "off",
            g_status.impactNote, SKSE::GetTrampoline().free_size());
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
        return out;
    }
}

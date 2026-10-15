#include "ImpactSwap.h"

#include "Attribution.h"
#include "BuffState.h"
#include "Config.h"
#include "FireSet.h"
#include "GreyMap.h"
#include "CopySwap.h"
#include "FormCopies.h"
#include "Patch.h"
#include "ThreadTrace.h"
#include "PlayerArt.h"
#include "SehGuard.h"

#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <optional>
#include <atomic>
#include <cstring>
#include <vector>

#ifdef GetObject
#    undef GetObject
#endif

namespace BlackFlame
{
    namespace
    {
        // Address Library ids (SE, AE). The AE ids map to 1.7.104 code (research-swap notes, 1.6.1170's
        // .text is encrypted on disk); the SE ids are matched by algorithm. Checked at run time first.
        constexpr std::uint64_t kSpawnSe = 43022, kSpawnAe = 44213;  // Projectile::SpawnCollisionEffects
        struct Caller
        {
            std::uint64_t se;
            std::uint64_t ae;
            std::uint32_t knownOffsetSe;  // the call's offset where known (MuIF), 0 when not
            std::uint32_t knownOffsetAe;
        };
        constexpr std::array kCallers{
            Caller{ 42943, 44123, 0x410, 0x407 },  // Projectile::ProcessImpacts
            Caller{ 43014, 44205, 0, 0 },          // from Projectile::HandleHits
            Caller{ 42638, 43806, 0, 0 },          // from ConeProjectile::UpdateImpl
        };
        constexpr std::size_t kGetModelIndex = 4;              // RE/T/TESModel.h: GetModel, slot 04

        // Saved and restored by each ImpactWindow: the innermost decision wins (an NPC impact nested in the
        // player's explosion Update is closed)
        thread_local bool t_window = false;
        std::atomic<std::uint64_t> g_swaps{ 0 };
        std::atomic<std::uint64_t> g_mappedSeen{ 0 };  // GetModel inside an open window with the original in the map
        std::atomic<bool> g_failureReported{ false };

        void ReportFailure()
        {
            if (!g_failureReported.exchange(true)) {
                logger::error("BlackFlame: impact swap thunk threw; the original was used; further failures are not logged");
            }
        }
        ImpactSwapStatus g_status;  // written once at kDataLoaded

        // The player's fire projectile while stoked (cheap checks first)
        bool OpensWindow(RE::Projectile* a_projectile)
        {
            if (!a_projectile || !IsStoked() || !GetConfig().impactSwap) {
                return false;
            }
            auto& data = a_projectile->GetProjectileRuntimeData();
            if (!IsFireSpell(data.spell) && !IsBoundFireArrow(a_projectile)) {
                return false;
            }
            const auto shooter = data.shooter.get();
            return LinkToPlayer("impact", a_projectile, shooter.get(), NonActorCasterOf(shooter.get())) != PlayerLink::kNone;
        }

        template <std::size_t I>
        struct SpawnHook
        {
            static std::uintptr_t thunk(RE::Projectile* a_this, RE::TESObjectREFR* a_collidee, const RE::NiPoint3& a_point,
                const RE::NiPoint3& a_normal, RE::MATERIAL_ID a_material, bool a_flag)
            {
                bool open = false;
                bool copyOpen = false;
                try {
                    (void)Guarded("impact window", [&]() {
                        // v2: a projectile whose base is a copy opens the copy window (only the player's fire launches
                        // with a copy); the round-10 model window is for chains without copies
                        const auto* base = a_this ? a_this->GetBaseObject() : nullptr;
                        copyOpen = base && IsCopy(base);
                        open = !copyOpen && OpensWindow(a_this);
                    });
                } catch (...) {
                    open = false;
                    copyOpen = false;
                    ReportFailure();
                }
                std::optional<ThreadTrace::Scope> trace;
                if (open || copyOpen) {
                    trace.emplace("SpawnCollisionEffects (window open)");
                }
                const CopyWindow copyWindow(copyOpen);
                const ImpactWindow window(open);
                return func(a_this, a_collidee, a_point, a_normal, a_material, a_flag);
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };

        template <std::size_t I>
        bool HookSite(std::uintptr_t a_site, std::uintptr_t a_spawn)
        {
            const auto previous = Patch::Rel32Target(a_site);
            SpawnHook<I>::func = SKSE::GetTrampoline().write_call<5>(a_site, SpawnHook<I>::thunk);
            const bool chained = previous != a_spawn;
            g_status.sitesChained += chained ? 1 : 0;
            logger::info("BlackFlame: impact swap: call site {} at {:X} hooked{}", I, a_site - REL::Module::get().base(),
                chained ? " (chained onto another plugin's redirect)" : "");
            return true;
        }

        // The site of caller I: an E8 to SpawnCollisionEffects found by the scan, or at the known offset an E8
        // into another module (another plugin's trampoline: chained); anything else is left alone
        std::uintptr_t FindSite(std::size_t a_index, std::uintptr_t a_spawn)
        {
            const auto& caller = kCallers[a_index];
            const auto function = Patch::AddressOf(Patch::IdFor(caller.se, caller.ae));
            const auto sites = Patch::FindCalls(function, a_spawn);
            if (sites.size() == 1) {
                return sites.front();
            }
            if (sites.size() > 1) {
                logger::warn("BlackFlame: impact swap: caller {} has {} calls to SpawnCollisionEffects - left alone",
                    a_index, sites.size());
                return 0;
            }
            const auto offset = REL::Module::IsAE() ? caller.knownOffsetAe : caller.knownOffsetSe;
            if (offset == 0) {
                logger::warn("BlackFlame: impact swap: no call to SpawnCollisionEffects found in caller {}", a_index);
                return 0;
            }
            const auto site = function + offset;
            if (*reinterpret_cast<const std::uint8_t*>(site) != Patch::kCallOpcode) {
                logger::warn("BlackFlame: impact swap: caller {} +0x{:X} is not a call - left alone", a_index, offset);
                return 0;
            }
            const auto target = Patch::Rel32Target(site);
            if (target == a_spawn || Patch::IsTrampolineStub(target)) {
                return site;  // our callee past the scanned chunk, or another plugin's trampoline stub (chained)
            }
            logger::warn("BlackFlame: impact swap: caller {} +0x{:X} calls something that is neither "
                         "SpawnCollisionEffects nor a trampoline stub - left alone", a_index, offset);
            return 0;
        }

        struct ImpactModelHook
        {
            static const char* thunk(const RE::TESModel* a_this)
            {
                const char* original = func(a_this);
                try {
                    if (!t_window) {
                        return original;
                    }
                    const char* grey = nullptr;
                    (void)Guarded("impact model swap", [&]() { grey = GreyModelFor(original); });
                    if (grey) {
                        g_mappedSeen.fetch_add(1);
                        g_swaps.fetch_add(1);
                        return grey;
                    }
                } catch (...) {
                    ReportFailure();
                }
                return original;
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };
    }

    ImpactWindow::ImpactWindow(bool a_open) noexcept :
        m_previous(t_window)
    {
        t_window = a_open;
    }

    ImpactWindow::~ImpactWindow()
    {
        t_window = m_previous;
    }

    bool ImpactWindowOpen() noexcept
    {
        return t_window;
    }

    void InstallImpactSwap()
    {
        // The call sites open the round-10 model window and the v2 copy window: needed when either has work
        const bool modelSwap = GetConfig().impactSwap && GreyModelCount() > 0;
        const auto copies = GetCopyCounts();
        const bool copyWindow = GetConfig().copyImpacts && copies.impacts > 0;
        if (!modelSwap && !copyWindow) {
            logger::info("BlackFlame: impact call sites not hooked (impactSwap {}, grey models {}, copyImpacts {}, impact "
                         "copies {})", GetConfig().impactSwap, GreyModelCount(), GetConfig().copyImpacts, copies.impacts);
            return;
        }
        g_status.idsPresent = Patch::HasIds({ Patch::IdFor(kSpawnSe, kSpawnAe), Patch::IdFor(kCallers[0].se, kCallers[0].ae),
            Patch::IdFor(kCallers[1].se, kCallers[1].ae), Patch::IdFor(kCallers[2].se, kCallers[2].ae) }, "impact swap");
        if (!g_status.idsPresent) {
            return;
        }
        // A write_call on a trampoline without room ends in a fatal report: checked first, logged
        constexpr std::size_t kBytesPerSite = 14;  // an absolute jump in the trampoline per redirected call
        const auto freeBytes = SKSE::GetTrampoline().free_size();
        if (freeBytes < kCallers.size() * kBytesPerSite) {
            logger::error("BlackFlame: impact swap: the SKSE trampoline has {} free bytes, {} needed - the impact swap "
                          "stays off", freeBytes, kCallers.size() * kBytesPerSite);
            return;
        }
        logger::info("BlackFlame: impact swap: trampoline {} bytes free", freeBytes);
        const auto spawn = Patch::AddressOf(Patch::IdFor(kSpawnSe, kSpawnAe));
        std::array sites{ FindSite(0, spawn), FindSite(1, spawn), FindSite(2, spawn) };
        // One address is hooked once: a site two callers' scans both found (overlapping windows) is a FAIL
        for (std::size_t i = 1; i < sites.size(); ++i) {
            for (std::size_t j = 0; j < i; ++j) {
                if (sites[i] && sites[i] == sites[j]) {
                    logger::error("BlackFlame: impact swap FAIL: callers {} and {} found the same call site {:X} - hooked once",
                        j, i, sites[i] - REL::Module::get().base());
                    sites[i] = 0;
                }
            }
        }
        g_status.sitesHooked += sites[0] && HookSite<0>(sites[0], spawn) ? 1 : 0;
        g_status.sitesHooked += sites[1] && HookSite<1>(sites[1], spawn) ? 1 : 0;
        g_status.sitesHooked += sites[2] && HookSite<2>(sites[2], spawn) ? 1 : 0;
        if (modelSwap) {
            REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_BGSImpactData[1] };  // the TESModel sub-object's vtable
            ImpactModelHook::func = vtbl.write_vfunc(kGetModelIndex, ImpactModelHook::thunk);
            g_status.modelHooked = true;
        }
        logger::info("BlackFlame: impact call sites: {} of 3 hooked ({} chained); BGSImpactData GetModel {} (round-10 model "
                     "window), copy window {}", g_status.sitesHooked, g_status.sitesChained,
            g_status.modelHooked ? "hooked" : "not hooked", copyWindow ? "on" : "off");
    }

    ImpactSwapStatus GetImpactSwapStatus() noexcept
    {
        return g_status;
    }

    std::uint64_t ImpactMappedSeenCount() noexcept
    {
        return g_mappedSeen.load();
    }

    std::uint64_t ImpactSwapCount() noexcept
    {
        return g_swaps.load();
    }

    bool ImpactSetHasGreyModel(const RE::BGSImpactDataSet* a_set)
    {
        if (!a_set) {
            return false;
        }
        for (const auto& [material, impact] : a_set->impactMap) {
            if (impact && GreyModelFor(impact->GetModel())) {
                return true;
            }
        }
        return false;
    }
}

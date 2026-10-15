#include "ImpactSwap.h"

#include "Attribution.h"
#include "BuffState.h"
#include "Config.h"
#include "FireSet.h"
#include "GreyMap.h"
#include "PlayerArt.h"
#include "SehGuard.h"

#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
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
        constexpr std::size_t kFallbackScanBytes = 0x1000;     // when the function table has no entry
        constexpr std::uint8_t kCallOpcode = 0xE8;
        constexpr std::size_t kCallSize = 5;

        thread_local int t_window = 0;
        std::atomic<std::uint64_t> g_swaps{ 0 };
        ImpactSwapStatus g_status;  // written once at kDataLoaded

        // Whether the running game's Address Library has these ids, without the fatal lookup
        bool HasIds(std::initializer_list<std::uint64_t> a_ids)
        {
            const REL::Offset2ID table;
            std::vector<std::uint64_t> ids;
            ids.reserve(table.size());
            for (const auto& entry : table) {
                ids.push_back(entry.id);
            }
            std::ranges::sort(ids);
            bool all = true;
            for (const auto id : a_ids) {
                if (!std::ranges::binary_search(ids, id)) {
                    logger::warn("BlackFlame: impact swap: Address Library id {} is not in this game's database - the "
                                 "impact swap stays off", id);
                    all = false;
                }
            }
            return all;
        }

        std::uint64_t IdFor(std::uint64_t a_se, std::uint64_t a_ae)
        {
            return REL::Module::IsAE() ? a_ae : a_se;
        }

        std::uintptr_t AddressOf(std::uint64_t a_id)
        {
            return REL::ID(a_id).address();
        }

        bool InGameImage(std::uintptr_t a_address)
        {
            const auto base = REL::Module::get().base();
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            return a_address >= base && a_address < base + nt->OptionalHeader.SizeOfImage;
        }

        std::uintptr_t CallTarget(std::uintptr_t a_site)
        {
            std::int32_t rel = 0;
            std::memcpy(&rel, reinterpret_cast<const void*>(a_site + 1), sizeof(rel));
            return a_site + kCallSize + rel;
        }

        // Every E8 in the function's body (its function-table entry, or a fixed window) whose target is a_callee
        std::vector<std::uintptr_t> FindCalls(std::uintptr_t a_function, std::uintptr_t a_callee)
        {
            DWORD64 imageBase = 0;
            std::uintptr_t end = a_function + kFallbackScanBytes;
            if (const auto* entry = RtlLookupFunctionEntry(a_function, &imageBase, nullptr)) {
                end = static_cast<std::uintptr_t>(imageBase + entry->EndAddress);
            }
            std::vector<std::uintptr_t> sites;
            for (auto at = a_function; at + kCallSize <= end; ++at) {
                if (*reinterpret_cast<const std::uint8_t*>(at) == kCallOpcode && CallTarget(at) == a_callee) {
                    sites.push_back(at);
                }
            }
            return sites;
        }

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
                try {
                    (void)Guarded("impact window", [&]() { open = OpensWindow(a_this); });
                } catch (...) {
                    open = false;
                }
                const ImpactWindow window(open);
                return func(a_this, a_collidee, a_point, a_normal, a_material, a_flag);
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };

        template <std::size_t I>
        bool HookSite(std::uintptr_t a_site, std::uintptr_t a_spawn)
        {
            const auto previous = CallTarget(a_site);
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
            const auto function = AddressOf(IdFor(caller.se, caller.ae));
            const auto sites = FindCalls(function, a_spawn);
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
            if (*reinterpret_cast<const std::uint8_t*>(site) != kCallOpcode) {
                logger::warn("BlackFlame: impact swap: caller {} +0x{:X} is not a call - left alone", a_index, offset);
                return 0;
            }
            const auto target = CallTarget(site);
            if (target == a_spawn || !InGameImage(target)) {
                return site;  // our callee past the scanned chunk, or another plugin's trampoline (chained)
            }
            logger::warn("BlackFlame: impact swap: caller {} +0x{:X} calls something else - left alone", a_index, offset);
            return 0;
        }

        struct ImpactModelHook
        {
            static const char* thunk(const RE::TESModel* a_this)
            {
                const char* original = func(a_this);
                if (t_window <= 0) {
                    return original;
                }
                const char* grey = nullptr;
                (void)Guarded("impact model swap", [&]() { grey = GreyModelFor(original); });
                if (!grey) {
                    return original;
                }
                g_swaps.fetch_add(1);
                return grey;
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };
    }

    ImpactWindow::ImpactWindow(bool a_open) noexcept :
        m_open(a_open)
    {
        if (m_open) {
            ++t_window;
        }
    }

    ImpactWindow::~ImpactWindow()
    {
        if (m_open) {
            --t_window;
        }
    }

    bool ImpactWindowOpen() noexcept
    {
        return t_window > 0;
    }

    void InstallImpactSwap()
    {
        if (!GetConfig().impactSwap || GreyModelCount() == 0) {
            logger::info("BlackFlame: impact swap off ({})", GetConfig().impactSwap ? "no grey models" : "impactSwap false");
            return;
        }
        g_status.idsPresent = HasIds({ IdFor(kSpawnSe, kSpawnAe), IdFor(kCallers[0].se, kCallers[0].ae),
            IdFor(kCallers[1].se, kCallers[1].ae), IdFor(kCallers[2].se, kCallers[2].ae) });
        if (!g_status.idsPresent) {
            return;
        }
        const auto spawn = AddressOf(IdFor(kSpawnSe, kSpawnAe));
        const std::array sites{ FindSite(0, spawn), FindSite(1, spawn), FindSite(2, spawn) };
        g_status.sitesHooked += sites[0] && HookSite<0>(sites[0], spawn) ? 1 : 0;
        g_status.sitesHooked += sites[1] && HookSite<1>(sites[1], spawn) ? 1 : 0;
        g_status.sitesHooked += sites[2] && HookSite<2>(sites[2], spawn) ? 1 : 0;
        REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_BGSImpactData[1] };  // the TESModel sub-object's vtable
        ImpactModelHook::func = vtbl.write_vfunc(kGetModelIndex, ImpactModelHook::thunk);
        g_status.modelHooked = true;
        logger::info("BlackFlame: impact swap: {} of 3 call sites hooked ({} chained), BGSImpactData GetModel hooked",
            g_status.sitesHooked, g_status.sitesChained);
    }

    ImpactSwapStatus GetImpactSwapStatus() noexcept
    {
        return g_status;
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

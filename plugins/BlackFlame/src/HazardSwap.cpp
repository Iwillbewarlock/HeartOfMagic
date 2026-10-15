#include "HazardSwap.h"

#include "Attribution.h"
#include "BuffState.h"
#include "Config.h"
#include "FireSet.h"
#include "GreyMap.h"
#include "ImpactSwap.h"
#include "LogLimit.h"
#include "SehGuard.h"
#include "ScriptTags.h"

#include <atomic>
#include <format>

namespace BlackFlame
{
    namespace
    {
        constexpr std::size_t kLoad3DIndex = 0x6A;   // RE/T/TESObjectREFR.h: Load3D, slot 6A (Hazard does not override it)
        constexpr std::size_t kGetModelIndex = 4;    // RE/T/TESModel.h: GetModel, slot 04

        // Set only during the original Load3D of a hazard that gets its grey copy: which base, which path
        thread_local const RE::TESModel* t_model = nullptr;
        thread_local const char* t_grey = nullptr;

        std::atomic<std::uint64_t> g_swaps{ 0 };
        std::atomic<bool> g_hooked{ false };
        std::atomic<int> g_windowLogs{ 0 };
        constexpr int kMaxWindowLogs = 3;

        // The grey path for this hazard, or null (cheap checks first)
        const char* GreyFor(RE::Hazard* a_hazard, const RE::BGSHazard*& a_base)
        {
            if (!IsStoked() || !GetConfig().hazardSwap) {
                return nullptr;
            }
            auto* baseForm = a_hazard->GetBaseObject();
            a_base = baseForm ? baseForm->As<RE::BGSHazard>() : nullptr;
            if (!a_base || !IsFireHazard(a_base)) {
                return nullptr;
            }
            // Made inside the player's impact (SpawnCollisionEffects creates the impact hazard): tagged, so
            // ownership is known here and for the per-instance layer
            if (ImpactWindowOpen()) {
                TagSource(a_hazard);
                if (g_windowLogs.load() < kMaxWindowLogs && g_windowLogs.fetch_add(1) < kMaxWindowLogs) {
                    logger::info("BlackFlame: hazard {:08X} loaded inside the player's impact window: tagged",
                        a_base->GetFormID());
                }
            }
            const auto owner = a_hazard->GetHazardRuntimeData().ownerActor.get();
            if (LinkToPlayer("hazard swap", a_hazard, owner.get()) == PlayerLink::kNone) {
                return nullptr;
            }
            return GreyModelFor(a_base->GetModel());
        }

        struct Load3DHook
        {
            static RE::NiAVObject* thunk(RE::Hazard* a_this, bool a_backgroundLoading)
            {
                const RE::BGSHazard* base = nullptr;
                const char* grey = nullptr;
                try {
                    if (a_this) {
                        (void)Guarded("hazard swap", [&]() { grey = GreyFor(a_this, base); });
                    }
                } catch (...) {
                    grey = nullptr;
                }
                if (!grey) {
                    return func(a_this, a_backgroundLoading);
                }
                t_model = base;  // BGSHazard -> its TESModel sub-object
                t_grey = grey;
                auto* result = func(a_this, a_backgroundLoading);
                t_model = nullptr;
                t_grey = nullptr;
                g_swaps.fetch_add(1);
                int suppressed = 0;
                if (LogLimit::Allow("hazard swap", base->GetFormID(), suppressed)) {
                    logger::info("BlackFlame: hazard {:08X}: grey copy {} loaded{}", base->GetFormID(), grey,
                        suppressed ? std::format(" ({} similar lines held back)", suppressed) : "");
                }
                return result;
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };

        struct HazardModelHook
        {
            static const char* thunk(const RE::TESModel* a_this)
            {
                if (t_grey && a_this == t_model) {
                    return t_grey;
                }
                return func(a_this);
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };
    }

    void InstallHazardSwap()
    {
        if (!GetConfig().hazardSwap || GreyModelCount() == 0) {
            logger::info("BlackFlame: hazard swap off ({})", GetConfig().hazardSwap ? "no grey models" : "hazardSwap false");
            return;
        }
        REL::Relocation<std::uintptr_t> hazard{ RE::Hazard::VTABLE[0] };
        Load3DHook::func = hazard.write_vfunc(kLoad3DIndex, Load3DHook::thunk);
        REL::Relocation<std::uintptr_t> model{ RE::VTABLE_BGSHazard[2] };  // the TESModel sub-object's vtable
        HazardModelHook::func = model.write_vfunc(kGetModelIndex, HazardModelHook::thunk);
        g_hooked.store(true);
        logger::info("BlackFlame: hazard swap: Load3D (0x{:X}) on the Hazard vtable and BGSHazard GetModel (slot {}) "
                     "hooked", kLoad3DIndex, kGetModelIndex);
    }

    bool HazardSwapHooked() noexcept
    {
        return g_hooked.load();
    }

    std::uint64_t HazardSwapCount() noexcept
    {
        return g_swaps.load();
    }
}

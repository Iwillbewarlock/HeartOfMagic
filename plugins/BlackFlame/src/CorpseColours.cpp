#include "CorpseColours.h"

#include "Config.h"
#include "CorpseColourRules.h"
#include "CorpseMark.h"
#include "Patch.h"
#include "SehGuard.h"

#include <atomic>

namespace BlackFlame::HitPath::Detail
{
    namespace
    {
        using SRE = RE::ShaderReferenceEffect;
        constexpr std::size_t kUpdateSlot = 0x28;
        constexpr std::size_t kPopSlot = 0x35;
        constexpr int kMaxAttachLines = 48;
        constexpr int kMaxRefillLines = 16;
        std::atomic<int> g_attachLines{ 0 }, g_refillLines{ 0 };
        std::atomic<bool> g_failed{ false };

        struct UpdateHook
        {
            static bool thunk(SRE* a_sre, float a_delta)
            {
                const bool result = func(a_sre, a_delta);
                GreyCorpseMembrane(a_sre, "update");
                return result;
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };

        struct PopHook
        {
            static void thunk(SRE* a_sre)
            {
                func(a_sre);
                GreyCorpseMembrane(a_sre, "pop");
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };
    }

    void GreyCorpseMembrane(SRE* a_sre, const char* a_point)
    {
        if (!a_sre || (a_sre->flags.underlying() & kCorpseGreyBit) == 0 || !GetConfig().greyCorpseShaders) {
            return;
        }
        try {
            const bool ok = GuardedKeep("corpse membrane colours", [&]() {
                auto* data = a_sre->effectShaderData;
                if (!data) {
                    return;
                }
                const auto fill = data->fillColor;
                const auto rim = data->rimColor;
                const bool changed = CorpseColours::GreyMembrane(true, data);
                const bool attach = a_point[0] == 'a';
                auto& lines = attach ? g_attachLines : g_refillLines;
                const int cap = attach ? kMaxAttachLines : kMaxRefillLines;
                if ((!attach && !changed) || lines.load(std::memory_order_relaxed) >= cap || lines.fetch_add(1) >= cap) {
                    return;
                }
                const auto* shader = a_sre->effectData;
                logger::info("BlackFlame: corpse membrane colours ({}): SRE 0x{:X}, shader {:08X}, data 0x{:X}, "
                             "fill ({:.3f},{:.3f},{:.3f}) -> ({:.3f},{:.3f},{:.3f}), "
                             "rim ({:.3f},{:.3f},{:.3f}) -> ({:.3f},{:.3f},{:.3f}); alpha kept, shared record unchanged",
                    a_point, reinterpret_cast<std::uintptr_t>(a_sre), shader ? shader->GetFormID() : 0,
                    reinterpret_cast<std::uintptr_t>(data), fill.red, fill.green, fill.blue,
                    data->fillColor.red, data->fillColor.green, data->fillColor.blue, rim.red, rim.green, rim.blue,
                    data->rimColor.red, data->rimColor.green, data->rimColor.blue);
            });
            if (!ok && !g_failed.exchange(true)) {
                logger::error("BlackFlame: corpse membrane colours faulted; the original was called (see guard above)");
            }
        } catch (const std::exception& e) {
            if (!g_failed.exchange(true)) {
                logger::error("BlackFlame: corpse membrane colours threw ({}); the original was called", e.what());
            }
        }
    }

    bool InstallCorpseColours()
    {
        REL::Relocation<std::uintptr_t> table{ SRE::VTABLE[0] };
        const auto* entries = reinterpret_cast<const std::uintptr_t*>(table.address());
        if (!Patch::IsExecutable(entries[kUpdateSlot], 1) || !Patch::IsExecutable(entries[kPopSlot], 1)) {
            logger::error("BlackFlame: corpse colours not installed: Update or Pop vtable target is not executable");
            return false;
        }
        UpdateHook::func = Patch::WriteVfunc("corpse membrane colours (Update)", table, kUpdateSlot, UpdateHook::thunk);
        PopHook::func = Patch::WriteVfunc("corpse membrane colours (Pop)", table, kPopSlot, PopHook::thunk);
        logger::info("BlackFlame: corpse colours hooked: private particle keys; membrane after attach, Update and Pop; "
                     "shared EFSH records kept");
        return true;
    }
}

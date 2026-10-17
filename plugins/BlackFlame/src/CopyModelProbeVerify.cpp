// =============================================================================
// CopyModelProbeVerify - verify before open: a candidate group's NIFs Demanded in 8 ms slices
// =============================================================================
// The slicing and the order are CopyGroups' (CopyGroups.cpp); this file is the one Demand and its counters.

#include "CopyModelProbeVerify.h"

#include "FormCopiesFillModel.h"
#include "SehGuard.h"
#include "SlotAssign.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <format>


namespace BlackFlame::CopyModelProbe
{
    namespace
    {
        std::atomic<std::uint32_t> g_gatedDemands{ 0 };
        std::atomic<std::uint32_t> g_demands{ 0 };

        // Is a_recordPath the model a gated slot holds now? (a slot's model is never demanded while it is gated)
        bool IsGatedSlotModel(const std::string& a_recordPath)
        {
            for (const auto id : SlotAssign::Assigned()) {
                const auto& slot = SlotAssign::At(id);
                if (SlotAssign::GateOpen(id) || !slot.form) {
                    continue;
                }
                const auto* model = FormCopies::ModelOf(slot.form);
                const char* text = model ? model->GetModel() : nullptr;
                if (text && _stricmp(text, a_recordPath.c_str()) == 0) {
                    return true;
                }
            }
            return false;
        }
    }

    VerifyResult DemandOnce(const std::string& a_recordPath)
    {
        VerifyResult result;
        if (IsGatedSlotModel(a_recordPath)) {
            g_gatedDemands.fetch_add(1);
            logger::warn("BlackFlame: slots: verify: '{}' is a gated slot's current model - demanded anyway (counted)", a_recordPath);
        }
        g_demands.fetch_add(1);
        RE::NiPointer<RE::NiNode> out;
        auto code = RE::BSResource::ErrorCode::kNone;
        const auto begin = std::chrono::steady_clock::now();
        result.fault = !GuardedKeep("slot verify Demand", [&]() { code = RE::BSModelDB::Demand(a_recordPath.c_str(), out, {}); });
        result.ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count();
        result.code = static_cast<int>(code);
        result.node = out != nullptr;
        result.ok = !result.fault && code == RE::BSResource::ErrorCode::kNone && result.node;
        out.reset();  // our reference given back (the model DB keeps or frees its entry)
        return result;
    }

    std::string Describe(const std::string& a_recordPath, const VerifyResult& a_result)
    {
        return std::format("Demand(\"{}\"): {}error code {}, node {}, {} ms", a_recordPath, a_result.fault ? "FAULT; " : "", a_result.code,
            a_result.node ? "yes" : "NULL", a_result.ms);
    }

    std::uint32_t GatedSlotDemands() noexcept
    {
        return g_gatedDemands.load();
    }

    std::uint32_t VerifyDemands() noexcept
    {
        return g_demands.load();
    }
}

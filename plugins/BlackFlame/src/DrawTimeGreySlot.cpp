#include "DrawTimeGreyShared.h"

#include "TrackedMutex.h"

#include <algorithm>
#include <array>
#include <format>
#include <mutex>

// =============================================================================
// DRAW-TIME GREY - which PS texture slot holds the palette (DrawTimeGrey.h), measured on the render side
// =============================================================================
// Community Shaders' Effect.hlsl declares TexGrayscaleSampler at t4, but what the engine binds is read here: the one
// PSTexture[] index holding the player shape's own greyscale view after the original SetupGeometry (the engine binds
// material textures in SetupMaterial, vfunc 04, so a diff across SetupGeometry alone would show nothing). Only a shape
// whose greyscale view is not also its source texture's (that would name the diffuse slot) and sits in exactly one
// slot votes; the slot is used once kSlotAgreement distinct materials named the same one. Each eligible material is
// examined once, kMaxMeasuredMaterials per buff-on (ResetSlotVote); no allocation unless a line is logged.

namespace BlackFlame::DrawTime
{
    namespace
    {
        constexpr int kSlotLogs = 8;
        constexpr int kSlotAgreement = 3;  // distinct materials that must name the same slot before it is used

        struct SlotVote
        {
            int candidate = -1;
            std::array<const RE::BSShaderMaterial*, kSlotAgreement> materials{};
            int agreed = 0;
            std::array<const RE::BSShaderMaterial*, kMaxMeasuredMaterials> examined{};
            int examinedCount = 0;
        };

        // The render side try_locks, the buff-on reset (main thread) locks
        TrackedMutex g_voteMutex;
        SlotVote g_vote;
        std::atomic<int> g_examined{ 0 };  // g_vote.examinedCount, readable without the lock (the cap check)
        std::atomic<int> g_slotLogs{ 0 };
    }

    void MeasureSlot(const ShadowData& a_state, const Target& a_target)
    {
        if (g_examined.load(std::memory_order_relaxed) >= kMaxMeasuredMaterials) {
            return;
        }
        if (!a_target.source || a_target.source == a_target.base || !a_target.material) {
            return;  // not eligible: not counted
        }
        int found = -1;
        int matches = 0;
        int baseSlot = -1;
        for (int i = 0; i < kTextureSlots; ++i) {
            if (a_state.PSTexture[i] == a_target.source) {
                found = found < 0 ? i : found;
                ++matches;
            }
            if (a_target.base && baseSlot < 0 && a_state.PSTexture[i] == a_target.base) {
                baseSlot = i;
            }
        }
        int agreed = 0;
        bool locked = false;
        {
            std::unique_lock lock(g_voteMutex, std::try_to_lock);
            if (!lock.owns_lock()) {
                return;  // another thread holds the vote: not counted, this pass waits for the next
            }
            auto& vote = g_vote;
            const auto examinedEnd = vote.examined.begin() + vote.examinedCount;
            if (vote.examinedCount >= kMaxMeasuredMaterials ||
                std::find(vote.examined.begin(), examinedEnd, a_target.material) != examinedEnd) {
                return;  // the cap, or this material was looked at already: not counted again
            }
            vote.examined[static_cast<std::size_t>(vote.examinedCount++)] = a_target.material;
            g_examined.store(vote.examinedCount, std::memory_order_relaxed);
            if (matches == 1) {
                if (vote.candidate != found) {
                    // a different slot: the count starts again from this material (the examined list stays)
                    vote.candidate = found;
                    vote.materials = {};
                    vote.agreed = 0;
                }
                if (vote.agreed < kSlotAgreement) {
                    vote.materials[static_cast<std::size_t>(vote.agreed++)] = a_target.material;
                }
                if (vote.agreed >= kSlotAgreement) {
                    int expected = -1;
                    locked = GetCounters().slot.compare_exchange_strong(expected, found);
                }
            }
            agreed = vote.agreed;  // the real count, also for a material that did not vote
        }
        if (AllowLog(g_slotLogs, kSlotLogs)) {
            logger::info("BlackFlame: draw-time grey: slot measure on material 0x{:X}: greyscale view 0x{:X} in {} PS "
                         "slot(s), first {} (source texture view 0x{:X} in slot {}); votes {}/{}{}",
                Addr(a_target.material), Addr(a_target.source), matches, found, Addr(a_target.base), baseSlot, agreed,
                kSlotAgreement, locked ? std::format(" - slot {} in use from now on", found) : "");
        }
    }

    int ExaminedMaterials() noexcept
    {
        return g_examined.load(std::memory_order_relaxed);
    }

    void ResetSlotVote()
    {
        if (GetCounters().slot.load() >= 0) {
            return;  // measured: kept for the session
        }
        std::lock_guard lock(g_voteMutex);
        g_vote = SlotVote{};
        g_examined.store(0, std::memory_order_relaxed);
    }
}

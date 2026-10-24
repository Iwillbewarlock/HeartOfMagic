#include "HitArt.h"
#include "TrackedMutex.h"

#include "GameThread.h"
#include "HitEffectHook.h"
#include "LogLimit.h"
#include "Recolor.h"
#include "SehGuard.h"

#include <algorithm>
#include <atomic>
#include <format>
#include <memory>
#include <mutex>
#include <vector>

namespace BlackFlame
{
    namespace
    {
        constexpr int kMaxTicks = 20;            // 2 s of 100 ms ticks for the art's 3D to appear
        constexpr std::size_t kMaxPending = 64;  // more waiting at once are dropped (counted in the log)

        // Matched on (controller, art) only. The target is NOT stored: the engine's first GetHitEffectArt
        // call comes before SetTarget (target null), so a stored target never matched the live effect
        // (Vigilant flame ring, run 2). A freed controller's address reused by someone else's effect with
        // the same art is caught after the walk: the live controller must still name a fire effect the
        // player cast (TryRecolor). The target is read from the live ModelReferenceEffect for the log.
        struct PendingArt
        {
            std::uintptr_t controller = 0;  // an address to find the effect by (the stored value is never dereferenced)
            const RE::BGSArtObject* art = nullptr;
            std::uint32_t generation = 0;
            int ticks = 0;
        };

        TrackedMutex g_mutex;
        std::atomic<std::size_t> g_pendingCount{ 0 };
        std::atomic<int> g_overflow{ 0 };

        // Guarded by g_mutex; heap-held like the other layers' lists
        std::vector<PendingArt>& Pending()
        {
            static auto* pending = std::make_unique<std::vector<PendingArt>>().release();
            return *pending;
        }

        struct Found
        {
            RE::NiPointer<RE::ModelReferenceEffect> effect;
            RE::NiPointer<RE::NiAVObject> root;               // artObject3D, taken inside the lock
            RE::ReferenceEffectController* controller = nullptr;  // read inside, used only after the walk
        };

        // The live ModelReferenceEffect the controller made with this art. Inside the engine's
        // ProcessLists lock only comparisons and NiPointer copies happen (a fault there would unwind with
        // the lock held and hang the game); the controller is checked after the walk (StillPlayers).
        Found FindEffect(const PendingArt& a_art)
        {
            Found found;
            if (auto* lists = RE::ProcessLists::GetSingleton()) {
                lists->ForEachModelEffect([&](RE::ModelReferenceEffect* a_effect) {
                    if (a_effect->finished || reinterpret_cast<std::uintptr_t>(a_effect->controller) != a_art.controller ||
                        a_effect->artObject != a_art.art) {
                        return RE::BSContainer::ForEachResult::kContinue;
                    }
                    found.effect.reset(a_effect);
                    found.root = a_effect->artObject3D;
                    found.controller = a_effect->controller;
                    return RE::BSContainer::ForEachResult::kStop;
                });
            }
            return found;
        }

        void LogRecolor(const RE::ModelReferenceEffect& a_effect, const PendingArt& a_art, const RecolorStats& a_stats)
        {
            const auto artId = a_art.art ? a_art.art->GetFormID() : 0;
            int suppressed = 0;
            if (LogLimit::Allow("hit art", artId, suppressed)) {
                const auto target = a_effect.target.get();
                logger::info("BlackFlame: hit art {:08X} on {:08X} after {} tick(s): {}{}", artId,
                    target ? target->GetFormID() : 0, a_art.ticks, a_stats.Describe(),
                    suppressed ? std::format(" ({} similar lines held back)", suppressed) : "");
            }
        }

        void LogDropped(const PendingArt& a_art, bool a_found)
        {
            const auto artId = a_art.art ? a_art.art->GetFormID() : 0;
            int suppressed = 0;
            if (LogLimit::Allow("hit art dropped", artId, suppressed)) {
                logger::info("BlackFlame: hit art {:08X}: {} after {} ticks - not recoloured{}", artId,
                    a_found ? "its effect never got 3D" : "no live effect of that controller and art", kMaxTicks,
                    suppressed ? std::format(" ({} similar lines held back)", suppressed) : "");
            }
        }

        // One remembered art: recoloured (true, done), or kept for the next tick (false)
        bool TryRecolor(PendingArt& a_art)
        {
            Found found;
            if (!Guarded("hit art find", [&]() { found = FindEffect(a_art); })) {
                return true;
            }
            // The address match re-checked through the live controller itself, outside the lock: its effect
            // must still be a fire effect the player cast, so a reused address cannot pass for an ally's art
            if (found.effect) {
                bool stillPlayers = false;
                (void)GuardedKeep("hit art controller check", [&]() {
                    auto* controller = found.effect->finished ? nullptr :
                        skyrim_cast<RE::ActiveEffectReferenceEffectController*>(found.controller);
                    stillPlayers = controller && IsPlayerVisualEffect(controller->effect);
                });
                if (!stillPlayers) {
                    return true;  // someone else's effect now, or gone: dropped
                }
            }
            if (!found.root) {
                if (++a_art.ticks < kMaxTicks) {
                    return false;
                }
                LogDropped(a_art, found.effect != nullptr);
                return true;
            }
            RecolorStats stats;
            if (Guarded("hit art recolour", [&]() { stats = RecolorTree(found.root.get()); })) {
                LogRecolor(*found.effect, a_art, stats);
            }
            return true;
        }
    }

    void RememberHitArt(const RE::ActiveEffectReferenceEffectController* a_controller, RE::BGSArtObject* a_art)
    {
        const auto controller = reinterpret_cast<std::uintptr_t>(a_controller);
        std::lock_guard lock(g_mutex);
        auto& pending = Pending();
        // Asked again for the same art (a concentration effect may ask every frame): one entry is enough
        if (std::ranges::any_of(pending, [&](const auto& a_p) { return a_p.controller == controller && a_p.art == a_art; })) {
            return;
        }
        if (pending.size() >= kMaxPending) {
            g_overflow.fetch_add(1);
            return;
        }
        pending.push_back({ controller, a_art, WorldGeneration(), 0 });
        g_pendingCount.store(pending.size());
    }

    void TickHitArt()
    {
        if (g_pendingCount.load() == 0) {
            return;
        }
        std::vector<PendingArt> due;
        {
            std::lock_guard lock(g_mutex);
            due.swap(Pending());
        }
        const auto generation = WorldGeneration();
        std::vector<PendingArt> again;
        for (auto& art : due) {
            if (art.generation == generation && !TryRecolor(art)) {
                again.push_back(art);
            }
        }
        if (const int overflow = g_overflow.exchange(0); overflow > 0) {
            logger::info("BlackFlame: hit art: {} more arts than the {} that may wait were not recoloured", overflow,
                kMaxPending);
        }
        std::lock_guard lock(g_mutex);
        auto& pending = Pending();
        pending.insert(pending.end(), again.begin(), again.end());
        g_pendingCount.store(pending.size());
    }

    bool HasPendingHitArt() noexcept
    {
        return g_pendingCount.load() > 0;
    }

    void ResetHitArt()
    {
        std::lock_guard lock(g_mutex);
        Pending().clear();
        g_pendingCount.store(0);
    }
}

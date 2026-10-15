#include "HitArt.h"

#include "GameThread.h"
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

        struct PendingArt
        {
            std::uintptr_t controller = 0;  // compared only, never dereferenced (it may be freed by now)
            const RE::BGSArtObject* art = nullptr;
            // Also compared: a freed controller's address reused by an NPC's effect with the same art
            // must not match (its target differs)
            RE::ObjectRefHandle target;
            std::uint32_t generation = 0;
            int ticks = 0;
        };

        std::mutex g_mutex;
        std::atomic<std::size_t> g_pendingCount{ 0 };
        std::atomic<int> g_overflow{ 0 };

        // Guarded by g_mutex; heap-held like the other layers' lists
        std::vector<PendingArt>& Pending()
        {
            static auto* pending = std::make_unique<std::vector<PendingArt>>().release();
            return *pending;
        }

        // The live ModelReferenceEffect the controller made with this art (inside the ProcessLists lock)
        RE::NiPointer<RE::ModelReferenceEffect> FindEffect(const PendingArt& a_art)
        {
            RE::NiPointer<RE::ModelReferenceEffect> found;
            if (auto* lists = RE::ProcessLists::GetSingleton()) {
                lists->ForEachModelEffect([&](RE::ModelReferenceEffect* a_effect) {
                    if (!a_effect->finished && reinterpret_cast<std::uintptr_t>(a_effect->controller) == a_art.controller &&
                        a_effect->artObject == a_art.art && a_effect->target == a_art.target) {
                        found.reset(a_effect);
                        return RE::BSContainer::ForEachResult::kStop;
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
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
            auto effect = FindEffect(a_art);
            auto* root = effect ? effect->artObject3D.get() : nullptr;
            if (!root) {
                if (++a_art.ticks < kMaxTicks) {
                    return false;
                }
                LogDropped(a_art, effect != nullptr);
                return true;
            }
            RecolorStats stats;
            if (Guarded("hit art recolour", [&]() { stats = RecolorTree(root); })) {
                LogRecolor(*effect, a_art, stats);
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
        pending.push_back({ controller, a_art, a_controller->target, WorldGeneration(), 0 });
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

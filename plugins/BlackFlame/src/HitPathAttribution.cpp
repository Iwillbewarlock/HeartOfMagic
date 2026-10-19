#include "HitPathAttribution.h"

#include "FireSet.h"
#include "HitPathObserve.h"
#include "LogLimit.h"
#include "SehGuard.h"
#include "TrackedMutex.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace BlackFlame::HitPath
{
    namespace
    {
        constexpr std::size_t kMaxListedShaders = 64;  // the fire MGEF hit shader list line: the first ids, then "+N more"
        constexpr std::size_t kFireHitSlots = 32;      // targets the player's fire hits are remembered for (FireHits)
        constexpr int kMaxActorLines = 48;             // the actor pool's lines a session (ActorPoolOpen)
        constexpr int kMaxIdleResolves = 256;          // its idle resolves between resets (ActorPoolOpen)
        // used-up idle resolves come back this long after the last reset (steady clock ticks)
        constexpr std::int64_t kIdleResolvePeriod =
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::seconds(60)).count();

        std::atomic<int> g_actorLines{ 0 };    // never reset
        // ResetIdleResolves (each stoke window start, each load or new game), or ActorPoolOpen 60 s after the last reset
        std::atomic<int> g_idleResolves{ 0 };
        std::atomic<std::int64_t> g_idleResolvesResetAt{ 0 };  // steady clock ticks of the last reset (0: none yet)
        std::atomic<int> g_actorClosed{ 0 };         // calls skipped unresolved: the windowed pool full, the actor pool closed
        std::atomic<int> g_idleResets{ 0 };          // ActorPoolOpen's own 60 s resets

        // Written once at kDataLoaded before any write_call; read-only afterwards
        std::vector<RE::FormID> g_fireShaders;  // sorted: data.effectShader of every fire effect of a fire spell
        bool g_fireShadersBuilt = false;

        // The last hit of the player's fire effects of fire spells per target (HitEffectHook's 0x0D thunk, any thread):
        // kFireHitSlots targets, a target already held updated in place, else the slot with the oldest hit (an empty one
        // first) replaced; under the mutex for the copy only (never while logging)
        struct FireHits
        {
            TrackedMutex mutex;
            std::array<Detail::FireHit, kFireHitSlots> slots{};
        };

        FireHits& GetFireHits()
        {
            static auto* hits = std::make_unique<FireHits>().release();  // never destroyed: hooks may run at exit
            return *hits;
        }

        std::int64_t SteadyNow()
        {
            return std::chrono::steady_clock::now().time_since_epoch().count();
        }

        double SecondsSince(std::int64_t a_ticks, std::int64_t a_now)
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::duration(a_now - a_ticks)).count();
        }

        std::optional<Detail::FireHit> FindFireHit(RE::FormID a_target)
        {
            if (a_target == 0) {
                return std::nullopt;
            }
            auto& hits = GetFireHits();
            std::lock_guard lock(hits.mutex);
            for (const auto& hit : hits.slots) {
                if (hit.target == a_target) {
                    return hit;
                }
            }
            return std::nullopt;
        }

        std::string Rgb(const RE::Color& a_color)
        {
            return std::format("#{:02X}{:02X}{:02X}", static_cast<unsigned>(a_color.red), static_cast<unsigned>(a_color.green),
                static_cast<unsigned>(a_color.blue));
        }

        const char* LifeStateName(RE::ACTOR_LIFE_STATE a_state)
        {
            switch (a_state) {
            case RE::ACTOR_LIFE_STATE::kAlive:
                return "alive";
            case RE::ACTOR_LIFE_STATE::kDying:
                return "dying";
            case RE::ACTOR_LIFE_STATE::kDead:
                return "dead";
            case RE::ACTOR_LIFE_STATE::kUnconcious:
                return "unconscious";
            case RE::ACTOR_LIFE_STATE::kReanimate:
                return "reanimated";
            case RE::ACTOR_LIFE_STATE::kRecycle:
                return "recycle";
            case RE::ACTOR_LIFE_STATE::kRestrained:
                return "restrained";
            case RE::ACTOR_LIFE_STATE::kEssentialDown:
                return "essential down";
            case RE::ACTOR_LIFE_STATE::kBleedout:
                return "bleedout";
            default:
                return "unknown";
            }
        }

        // " (in the kDataLoaded fire-shader set: yes)": whether the hit path's fire test would count this shader now
        std::string FireSetText(const RE::TESEffectShader* a_shader)
        {
            if (!g_fireShadersBuilt) {
                return " (the kDataLoaded fire-shader set is not built: the hit path observation is off)";
            }
            return std::format(" (in the kDataLoaded fire-shader set of {}: {})", g_fireShaders.size(),
                Detail::IsFireShader(a_shader) ? "yes" : "no");
        }

        void ProbeHitShaders(const char* a_point)
        {
            // Diagnostic data, not a rule: a fixed probe list of vanilla fire MGEFs whose shader fields other plugins were seen to
            // change (one nulls data.effectShader at kDataLoaded, a later one restores some). Nothing decides by these ids
            constexpr std::array<RE::FormID, 4> kProbeMgefs{ 0x0010F7F1, 0x0001CEA1, 0x00013CA9, 0x00012F03 };
            const char* point = a_point;
            const auto thread = REX::W32::GetCurrentThreadId();
            for (const auto id : kProbeMgefs) {
                const auto* mgef = RE::TESForm::LookupByID<RE::EffectSetting>(id);
                if (!mgef) {
                    logger::info("BlackFlame: hit shader probe [{}]: MGEF {:08X}: not loaded - skipped", point, id);
                    continue;
                }
                const auto& d = mgef->data;
                logger::info("BlackFlame: hit shader probe [{}]: MGEF {:08X} '{}': effectShader {:08X}{}, enchantShader {:08X}, "
                             "impactDataSet {:08X}, thread {}", point, id, Detail::EditorIdOf(mgef),
                    d.effectShader ? d.effectShader->GetFormID() : 0, FireSetText(d.effectShader),
                    d.enchantShader ? d.enchantShader->GetFormID() : 0, d.impactDataSet ? d.impactDataSet->GetFormID() : 0, thread);
            }
            // FireSpellList is read-only after BuildFireSets (kDataLoaded) and LookupByID takes the form map's lock: fine from
            // the later points too (the main menu event, kPostLoadGame, the first stoke window)
            std::unordered_set<RE::FormID> seen;
            std::size_t spells = 0, withShader = 0, withoutShader = 0;
            for (auto* spell : FireSpellList()) {
                if (!spell) {
                    continue;
                }
                ++spells;
                for (const auto* effect : spell->effects) {
                    const auto* base = effect ? effect->baseEffect : nullptr;
                    if (!base || !IsBurnable(base, spell) || !seen.insert(base->GetFormID()).second) {
                        continue;
                    }
                    ++(base->data.effectShader ? withShader : withoutShader);
                }
            }
            logger::info("BlackFlame: hit shader probe [{}]: fire effects of {} fire spells: {} distinct MGEFs, data.effectShader "
                         "set on {}, null on {}, thread {}", point, spells, seen.size(), withShader, withoutShader, thread);
        }
    }

    namespace Detail
    {
        void BuildFireShaderSet()
        {
            for (auto* spell : FireSpellList()) {
                if (!spell) {
                    continue;
                }
                for (const auto* effect : spell->effects) {
                    const auto* base = effect ? effect->baseEffect : nullptr;
                    if (base && base->data.effectShader && IsBurnable(base, spell)) {
                        g_fireShaders.push_back(base->data.effectShader->GetFormID());
                    }
                }
            }
            std::ranges::sort(g_fireShaders);
            const auto [first, last] = std::ranges::unique(g_fireShaders);
            g_fireShaders.erase(first, last);
            g_fireShadersBuilt = true;
            std::string ids;
            for (std::size_t i = 0; i < g_fireShaders.size() && i < kMaxListedShaders; ++i) {
                ids += std::format("{}{:08X}", i ? " " : "", g_fireShaders[i]);
            }
            if (g_fireShaders.size() > kMaxListedShaders) {
                ids += std::format(" +{} more", g_fireShaders.size() - kMaxListedShaders);
            }
            logger::info("BlackFlame: hit path fire MGEF hit shaders ({}, data.effectShader of the fire effects of fire spells "
                         "now): {}", g_fireShaders.size(), ids.empty() ? std::string("none") : ids);
        }

        bool IsFireShader(const RE::TESEffectShader* a_shader)
        {
            return a_shader && std::ranges::binary_search(g_fireShaders, a_shader->GetFormID());
        }

        const char* EditorIdOf(const RE::TESForm* a_form)
        {
            const char* id = a_form ? a_form->GetFormEditorID() : nullptr;
            return id && *id ? id : "-";
        }

        std::size_t FireShaderCount() noexcept
        {
            return g_fireShaders.size();
        }

        // What a later "fire-coloured shader" rule would judge
        std::string ShaderColours(const RE::TESEffectShader* a_shader)
        {
            if (!a_shader) {
                return {};
            }
            const auto& d = a_shader->data;
            return std::format("; fill {} / {} / {}, edge {}, shader flags 0x{:08X}", Rgb(d.fillTextureEffectColorKey1),
                Rgb(d.fillTextureEffectColorKey2), Rgb(d.fillTextureEffectColorKey3), Rgb(d.edgeEffectColor),
                d.flags.underlying());
        }

        bool ActorFacts::CorpseNotable() const noexcept
        {
            // a fire hit alone does not count (living NPCs hit by the player's fire would use the session's lines up), nor
            // bleedout, essential down or a reanimated thrall: dying, dead, or the player's kill
            return life == RE::ACTOR_LIFE_STATE::kDying || life == RE::ACTOR_LIFE_STATE::kDead ||
                   (killer != 0 && playerHandle != 0 && killer == playerHandle);
        }

        // Plain field reads only (the life state bits, the killer handle's value): the killer handle is never resolved off the
        // main thread; "the player" compares it with PlayerHandleNative(), the cache the line's onPlayer test already reads
        // (primed on the game thread at kPostLoadGame and at each stoke window start)
        ActorFacts ReadActorFacts(RE::Actor* a_actor, RE::FormID a_targetId, std::uint32_t a_playerHandle)
        {
            ActorFacts facts;
            facts.playerHandle = a_playerHandle;
            if (!a_actor) {
                return facts;
            }
            const auto* state = a_actor->AsActorState();
            facts.life = state ? state->GetLifeState() : RE::ACTOR_LIFE_STATE::kAlive;
            facts.killer = a_actor->GetActorRuntimeData().myKiller.native_handle();
            facts.hit = FindFireHit(a_targetId);
            return facts;
        }

        std::string ActorAttribution(const ActorFacts& a_facts)
        {
            const auto killer = a_facts.killer;
            const char* killerText = killer == 0                    ? "none" :
                                     a_facts.playerHandle == 0      ? "unknown: the player's handle is not cached yet" :
                                     killer == a_facts.playerHandle ? "the player" :
                                                                      "not the player";
            std::string hit = std::format("none remembered (the last {} targets are kept)", kFireHitSlots);
            if (const auto& found = a_facts.hit) {
                const auto now = SteadyNow();
                hit = std::format("{:.1f} s ago (stoked {}), last stoked one {}", SecondsSince(found->lastAt, now),
                    found->lastStoked ? "yes" : "no",
                    found->lastStokedAt ? std::format("{:.1f} s ago", SecondsSince(found->lastStokedAt, now)) : std::string("none"));
            }
            return std::format(", life state {} ({}), killer handle 0x{:X} ({}), last player fire hit on it: {}",
                static_cast<std::uint32_t>(a_facts.life), LifeStateName(a_facts.life), killer, killerText, hit);
        }

        bool ActorPoolOpen() noexcept
        {
            if (g_actorLines.load(std::memory_order_relaxed) >= kMaxActorLines) {
                return false;
            }
            if (g_idleResolves.load(std::memory_order_relaxed) < kMaxIdleResolves) {
                return true;
            }
            // used up: the clock is read on this path only; one thread wins the stamp and resets the count
            const auto now = SteadyNow();
            auto stamp = g_idleResolvesResetAt.load();
            if (now - stamp < kIdleResolvePeriod || !g_idleResolvesResetAt.compare_exchange_strong(stamp, now)) {
                return false;
            }
            g_idleResolves.store(0);
            g_idleResets.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        bool TakeActorLine(RE::FormID a_target, int& a_line)
        {
            if (g_actorLines.load(std::memory_order_relaxed) >= kMaxActorLines) {
                return false;
            }
            int keyLine = 0;  // per target first (an FEC death applies 6 shaders): one corpse cannot use the pool up
            if (!LogLimit::PerKey("corpse lines", a_target, a_line, keyLine)) {
                return false;
            }
            a_line = g_actorLines.fetch_add(1) + 1;
            return a_line <= kMaxActorLines;
        }

        void NoteIdleResolve() noexcept
        {
            g_idleResolves.fetch_add(1, std::memory_order_relaxed);
        }

        void NoteActorClosed() noexcept
        {
            g_actorClosed.fetch_add(1, std::memory_order_relaxed);
        }

        std::string ActorPoolStats()
        {
            return std::format("actor pool {} of {} lines, idle resolves {} of {} ({} timed resets), {} calls skipped with it closed",
                std::min(g_actorLines.load(), kMaxActorLines), kMaxActorLines, std::min(g_idleResolves.load(), kMaxIdleResolves), kMaxIdleResolves,
                g_idleResets.load(), g_actorClosed.load());
        }

        void ResetIdleResolves() noexcept
        {
            g_idleResolvesResetAt.store(SteadyNow());
            g_idleResolves.store(0);
        }
    }

    void NoteFireHit(RE::FormID a_target, bool a_stoked)
    {
        if (a_target == 0) {
            return;
        }
        const auto now = SteadyNow();
        auto& hits = GetFireHits();
        std::lock_guard lock(hits.mutex);
        auto it = std::ranges::find(hits.slots, a_target, &Detail::FireHit::target);
        if (it == hits.slots.end()) {
            it = std::ranges::min_element(hits.slots, {}, &Detail::FireHit::lastAt);  // an empty slot has lastAt 0
            *it = Detail::FireHit{ a_target };
        }
        it->lastAt = now;
        it->lastStoked = a_stoked;
        if (a_stoked) {
            it->lastStokedAt = now;
        }
    }

    void ClearFireHits() noexcept
    {
        try {
            auto& hits = GetFireHits();
            std::lock_guard lock(hits.mutex);
            hits.slots.fill(Detail::FireHit{});
        } catch (const std::exception& e) {
            logger::warn("BlackFlame: hit path: the remembered fire hits were not cleared ({})", e.what());
        }
    }

    void LogHitShaderProbe(const char* a_point)
    {
        const char* point = a_point ? a_point : "?";
        // every caller (kDataLoaded, the main menu, kPostLoadGame, the first stoke window) passes through here: a fault or a
        // throw skips the probe only
        try {
            if (!GuardedKeep("hit shader probe", [&]() { ProbeHitShaders(point); })) {
                logger::warn("BlackFlame: hit shader probe [{}]: a read faulted - skipped", point);
            }
        } catch (const std::exception& e) {
            logger::warn("BlackFlame: hit shader probe [{}] threw ({}) - skipped", point, e.what());
        }
    }
}

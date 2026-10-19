#pragma once

#include "Common.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

// =============================================================================
// HIT PATH ATTRIBUTION - the log-only parts of the hit path observation that are not hooks
// (HitPathObserve.h items 3 and 4): the fire MGEF hit shader set, the player's fire hits per
// target, the corpse attribution of an ApplyEffectShader line, the hit shader probe.
// Nothing here decides a swap; HitPathObserve.cpp and HitEffectHook.cpp only read it.
// =============================================================================

namespace BlackFlame::HitPath::Detail
{
    /// kDataLoaded, before any write_call (HitPath::Install): data.effectShader of every fire effect of a fire spell, one line
    void BuildFireShaderSet();

    /// Any thread after kDataLoaded: a_shader is in that set (false while it is not built)
    [[nodiscard]] bool IsFireShader(const RE::TESEffectShader* a_shader);

    /// The set's size (0 too when BuildFireShaderSet never ran: another runtime, the observation is off)
    [[nodiscard]] std::size_t FireShaderCount() noexcept;

    /// The form's editor id, "-" when it has none
    [[nodiscard]] const char* EditorIdOf(const RE::TESForm* a_form);

    /// "; fill #.. / #.. / #.., edge #.., shader flags 0x..", or empty for null
    [[nodiscard]] std::string ShaderColours(const RE::TESEffectShader* a_shader);

    /// The player's last fire hit on one target (NoteFireHit): steady clock ticks, 0: none
    struct FireHit
    {
        RE::FormID target = 0;
        std::int64_t lastAt = 0;
        std::int64_t lastStokedAt = 0;
        bool lastStoked = false;
    };

    /// An actor's corpse attribution, read once per line: plain field reads (no handle resolved) and one look in the fire
    /// hit memory
    struct ActorFacts
    {
        RE::ACTOR_LIFE_STATE life = RE::ACTOR_LIFE_STATE::kAlive;
        std::uint32_t killer = 0;        // myKiller's handle value, never resolved here
        std::uint32_t playerHandle = 0;  // PlayerHandleNative() as the line read it (0: not cached yet)
        std::optional<FireHit> hit;

        /// Worth a line of the session-wide actor pool: not alive, or killed by the player (a fire hit alone is not enough;
        /// it is still printed on the line)
        [[nodiscard]] bool CorpseNotable() const noexcept;
    };

    [[nodiscard]] ActorFacts ReadActorFacts(RE::Actor* a_actor, RE::FormID a_targetId, std::uint32_t a_playerHandle);

    /// ", life state N (name), killer handle 0x.. (..), last player fire hit on it: .."
    [[nodiscard]] std::string ActorAttribution(const ActorFacts& a_facts);

    /// The ApplyEffectShader lines' actor pool (corpse-notable actors other than the player, any thread): 48 lines a session,
    /// no stoke window gives them back; it takes a line only when the windowed pool the line belongs to is full. A target
    /// resolved for this pool alone that is not notable is an idle resolve: at most 256 between resets (each stoke window
    /// start, each load or new game, and 60 s after the last reset once they are used up).
    /// Open: a line and a resolve are left
    [[nodiscard]] bool ActorPoolOpen() noexcept;
    /// at most kTracePerKey (LogLimit) of them per target; may throw (PerKey allocates): the hook thunk's catch reports it
    [[nodiscard]] bool TakeActorLine(RE::FormID a_target, int& a_line);
    void NoteIdleResolve() noexcept;
    void NoteActorClosed() noexcept;
    /// "actor pool N of 48 lines, idle resolves ..., N calls skipped with it closed" (the stats line)
    [[nodiscard]] std::string ActorPoolStats();
    void ResetIdleResolves() noexcept;
}

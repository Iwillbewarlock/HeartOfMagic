#pragma once

#include "Common.h"
#include "ElementalHitRules.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

// =============================================================================
// HIT PATH ATTRIBUTION - the log-only parts of the hit path observation that are not hooks
// (HitPathObserve.h items 3 and 4): the fire MGEF hit shader set, the last elemental hit per target
// (any caster, with its source and element), the corpse attribution of an ApplyEffectShader line, the hit
// shader probe. Nothing here decides a swap; the corpse mark (CorpseMark.h) reads the fire hits. FireSource, Element,
// HitOrigin, FireHit and the record rule itself: ElementalHitRules.h (pure, tested offline).
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

    /// Any thread (NoteFireEffect, the apply sink): the target's slot takes this hit, unless the target is down (dying or
    /// dead), its last hit is the player's stoked fire and this one is the player's fire, not stoked (Flame Cloak, an
    /// enchanted weapon on the corpse): then the stoked record is kept, one capped "kept stoked" line. Except when this is the
    /// effect start of the MGEF whose apply event made that record: the effect start knows the spell (a scroll, staff or
    /// enchantment sharing a fire spell's MGEF is not stoked), so it re-judges the same effect and overwrites. NPC fire and
    /// the player's frost or shock always overwrite (ElementalHitRules.h DecideRecord). Returns false when the record was kept
    bool RecordElementalHit(RE::FormID a_target, Element a_element, FireSource a_source, bool a_targetDown, HitOrigin a_origin,
        RE::FormID a_mgef);

    /// Why a non-player elemental effect is not recorded (nullptr: it is): no caster, the caster is the target itself, the
    /// caster is not an Actor (counted for the stats line)
    [[nodiscard]] const char* WhyNotRecorded(const RE::TESObjectREFR* a_caster, const RE::TESObjectREFR* a_target);

    /// The actor is dying or dead (plain field read)
    [[nodiscard]] bool IsDown(const RE::Actor* a_actor) noexcept;

    /// Any thread: the target's slot, copied under the lock (nullopt: not among the remembered targets)
    [[nodiscard]] std::optional<FireHit> FindFireHit(RE::FormID a_target);

    /// Any thread (the corpse mark): the target's window stays open until a_untilGame (GameClock seconds) whatever its last
    /// hit's age, while its last fire hit stays the player's stoked one; nothing when the target has no slot
    void ExtendCorpseWindow(RE::FormID a_target, double a_untilGame);

    /// Any thread: some remembered target's last elemental hit (any source) is at most a_window game seconds before a_now, or a
    /// mark keeps its window open: the corpse mark resolves its target only then (or while its log lines last)
    [[nodiscard]] bool AnyFireHitOpen(double a_now, double a_window);

    /// "N NPC fire hits on dying or dead actors (..), N elemental hits not recorded (..), N stoked records kept" (the stats line)
    [[nodiscard]] std::string FireHitStats();

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

// =============================================================================
// SELF-TEST v2 STEPS - E6 / E6-npc: a cloak's hit art takes its ARTO copy (the art spike)
// =============================================================================
// The candidate is picked by data, not by name: a fire spell (FireSet, kSpell) cast fire and forget with a
// Cloak-archetype effect that is one of its fire effects (IsBurnable), whose hit art - the engine's choice,
// MGEF hitVisuals' art (RFCT data.artObject) when set, else MGEF hitEffectArt - has an ARTO copy; of those the
// spell whose defining file comes last in TESDataHandler::files (a tie: the smaller FormID). Fire and forget only:
// CastSpellImmediate of a concentration cloak lasts one tick. No candidate: SKIP.
//   E6       the player casts it on itself while stoked: within kWatch a live ModelReferenceEffect on the player
//            whose artObject is the copy, with its 3D loaded and every effect shape on the grey palette path (INFO when
//            the copy NIF has no effect shape), no NEW effect with the original art (one already on the player before the
//            cast is INFO), and the GetHitEffectArt hook's copy counter advanced. One run settles both spike questions:
//            the engine takes the art the hook hands back, and loads an ARTO whose record has no DATA.
//   E6-drop  after the cloak is dispelled, the copy art is gone from the player within kWatch.
//   E6-npc   the deer caster casts the same spell on itself: its art is the original, never the copy.
// Each cloak is dispelled right after its judgement; T9 dispels the cloak and its associated spell on every actor in
// high process again (CleanupArtSpells), whatever path the steps took.

#include "SelfTestCtx.h"

#include "BuffState.h"
#include "Config.h"
#include "CopySwap.h"
#include "FireSet.h"
#include "FormCopies.h"
#include "HitEffectHook.h"
#include "SelfTestLog.h"
#include "SelfTestText.h"

#include <format>
#include <unordered_map>

namespace BlackFlame::SelfTest
{
    namespace
    {
        using namespace SelfTestText;

        constexpr auto kWatch = std::chrono::milliseconds(2000);  // the art's 3D is cloned on a background task

        enum Phase : int
        {
            kWatching,
            kDropping
        };

        struct ArtPick
        {
            RE::SpellItem* spell = nullptr;
            RE::EffectSetting* effect = nullptr;
            RE::BGSArtObject* art = nullptr;
            RE::BGSArtObject* copy = nullptr;
            std::size_t fileIndex = 0;
        };

        struct ArtState
        {
            std::optional<ArtPick> pick;
            std::uint64_t copiedBefore = 0;
            bool originalBefore = false;  // the player wore the original art before the cast (another cloak)
            Clock::time_point dispelAt{};
        };
        ArtState g_art;

        RE::PlayerCharacter* Player() { return RE::PlayerCharacter::GetSingleton(); }

        // The hit art the engine asks the controller for: the RFCT's art first (as 34845), else the MGEF's own
        RE::BGSArtObject* EngineHitArt(const RE::EffectSetting* a_effect)
        {
            const auto* visuals = a_effect->data.hitVisuals;
            return visuals && visuals->data.artObject ? visuals->data.artObject : a_effect->data.hitEffectArt;
        }

        std::optional<ArtPick> PickCloak(std::string& a_note)
        {
            auto* dh = RE::TESDataHandler::GetSingleton();
            if (!dh) {
                a_note = "no data handler";
                return std::nullopt;
            }
            std::unordered_map<const RE::TESFile*, std::size_t> order;
            std::size_t index = 0;
            for (const auto* file : dh->files) {
                order.emplace(file, index++);
            }
            std::optional<ArtPick> best;
            int withCopy = 0;
            int notFireAndForget = 0;
            for (auto* spell : dh->GetFormArray<RE::SpellItem>()) {
                if (!spell || !IsFireSpell(spell)) {
                    continue;
                }
                for (const auto* item : spell->effects) {
                    auto* effect = item ? item->baseEffect : nullptr;
                    if (!effect || !effect->HasArchetype(RE::EffectSetting::Archetype::kCloak) || !IsBurnable(effect, spell)) {
                        continue;
                    }
                    auto* art = EngineHitArt(effect);
                    auto* copy = CopyOfAs<RE::BGSArtObject>(art);
                    if (!copy) {
                        continue;
                    }
                    ++withCopy;
                    if (spell->GetCastingType() != RE::MagicSystem::CastingType::kFireAndForget) {
                        ++notFireAndForget;
                        continue;
                    }
                    const auto it = order.find(spell->GetFile(0));
                    const std::size_t at = it != order.end() ? it->second : 0;
                    if (!best || at > best->fileIndex || (at == best->fileIndex && spell->GetFormID() < best->spell->GetFormID())) {
                        best = ArtPick{ spell, effect, art, copy, at };
                    }
                    break;
                }
            }
            a_note = std::format("{} fire cloak effect(s) with an art copy, {} of them not fire and forget", withCopy,
                notFireAndForget);
            return best;
        }

        std::string Describe(const ArtPick& a_pick)
        {
            const auto* file = a_pick.spell->GetFile(0);
            return std::format("spell {:08X} '{}' ({}), cloak effect {:08X}, hit art {:08X} -> copy {:08X}",
                a_pick.spell->GetFormID(), a_pick.spell->GetName(), file ? file->GetFilename() : "?",
                a_pick.effect->GetFormID(), a_pick.art->GetFormID(), a_pick.copy->GetFormID());
        }

        struct ArtSeen
        {
            int onTarget = 0;  // live model effects on the actor
            bool copy = false;
            bool original = false;
            RE::NiPointer<RE::NiAVObject> copy3D;
        };

        // Collected inside the ProcessLists lock; nothing is called on the effects there but plain reads
        ArtSeen Look(RE::ObjectRefHandle a_target, const ArtPick& a_pick)
        {
            ArtSeen seen;
            auto* lists = RE::ProcessLists::GetSingleton();
            if (!lists) {
                return seen;
            }
            lists->ForEachModelEffect([&](RE::ModelReferenceEffect* a_effect) {
                if (a_effect && !a_effect->finished && a_effect->target == a_target) {
                    ++seen.onTarget;
                    if (a_effect->artObject == a_pick.copy) {
                        seen.copy = true;
                        if (a_effect->artObject3D) {
                            seen.copy3D = a_effect->artObject3D;
                        }
                    } else if (a_effect->artObject == a_pick.art) {
                        seen.original = true;
                    }
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
            return seen;
        }

        // First call: picks the spell (or says why there is none) and casts; false when the step is over
        bool Start(Ctx& a_c, const char* a_id, RE::Actor* a_caster)
        {
            if (!GetConfig().copyHitArt) {
                SelfTestLog::Skip(a_id, "not judged: copyHitArt is off");
                return false;
            }
            if (!g_art.pick) {
                std::string note;
                g_art.pick = PickCloak(note);
                if (!g_art.pick) {
                    SelfTestLog::Skip(a_id, std::format("not judged: no fire and forget fire spell with a cloak effect whose hit "
                        "art has a copy ({} art copies in the map; {})", GetCopyCounts().arts, note));
                    return false;
                }
                SelfTestLog::Info(a_id, std::format("picked {}; {}", Describe(*g_art.pick), note));
            }
            if (!a_caster) {
                SelfTestLog::Fail(a_id, "skipped: the caster is gone");
                return false;
            }
            g_art.copiedBefore = GetHitSlotStats(false).copied;
            g_art.originalBefore = Look(a_caster->GetHandle(), *g_art.pick).original;
            if (!SelfTestWorld::Cast(a_caster, g_art.pick->spell, a_caster)) {
                SelfTestLog::Fail(a_id, "CastSpellImmediate refused the cloak (no caster or spell)");
                return false;
            }
            a_c.lastCast = Clock::now();
            a_c.phase = kWatching;
            return true;
        }

        int DispelOn(RE::Actor* a_actor, const RE::SpellItem* a_spell)
        {
            return a_actor && a_spell ? SelfTestWorld::DispelSpell(a_actor, a_spell) : 0;
        }

        void EndCloak(const char* a_id, RE::Actor* a_caster)
        {
            const int dispelled = g_art.pick ? DispelOn(a_caster, g_art.pick->spell) : 0;
            g_art.dispelAt = Clock::now();
            SelfTestLog::Info(a_id, std::format("cloak dispelled: {} effect(s)", dispelled));
        }

        void JudgePlayer(const ArtSeen& a_seen)
        {
            const auto& pick = *g_art.pick;
            const auto copied = GetHitSlotStats(false).copied - g_art.copiedBefore;
            SelfTestProbe::Inspection grey;
            if (a_seen.copy3D) {
                grey = SelfTestProbe::Inspect(a_seen.copy3D.get());
            }
            const bool noShapes = a_seen.copy3D && grey.effectProperties == 0;
            const bool allGrey = noShapes || grey.withPalettePath == grey.effectProperties;
            const bool newOriginal = a_seen.original && !g_art.originalBefore;
            SelfTestLog::Check("E6", a_seen.copy && a_seen.copy3D && allGrey && copied > 0 && !newOriginal,
                std::format("{}: {} live art effect(s) on the player; the copy {} ({}), the original {}; effect shapes {}, with "
                    "the grey palette path {}; GetHitEffectArt returned the copy {} time(s) since the cast", Describe(pick),
                    a_seen.onTarget, a_seen.copy ? "FOUND" : "not found", a_seen.copy3D ? "3D loaded" : "no 3D yet",
                    newOriginal ? "ALSO found (new)" : (a_seen.original ? "found, already there before the cast" : "not found"),
                    grey.effectProperties, grey.withPalettePath, copied));
            if (noShapes) {
                SelfTestLog::Info("E6", "the copy's NIF has no effect shape: its grey is not judged here");
            }
            if (a_seen.original && g_art.originalBefore) {
                SelfTestLog::Info("E6", "the player wore the original art before the cast (another cloak): not counted against E6");
            }
        }
    }

    void ShiftArtClocks(Clock::duration a_paused)
    {
        g_art.dispelAt += a_paused;
    }

    bool StepE6Player(Ctx& a_c, bool a_first)
    {
        auto* player = Player();
        if (a_first) {
            if (!IsStoked()) {
                SelfTestLog::Fail("E6", "not stoked at the start (E2 ends with the buff cast again)");
                return true;
            }
            if (!Start(a_c, "E6", player)) {
                return true;
            }
            a_c.waitingFor = "the cloak's hit art on the player";
            return false;
        }
        const auto& pick = *g_art.pick;
        const auto seen = Look(player->GetHandle(), pick);
        if (a_c.phase == kWatching) {
            if (!seen.copy3D && Clock::now() - a_c.stepStart < kWatch) {
                return false;
            }
            JudgePlayer(seen);
            EndCloak("E6", player);
            if (!seen.copy) {
                return true;  // nothing to watch go
            }
            a_c.phase = kDropping;
            a_c.waitingFor = "the copy art to go after the dispel";
            return false;
        }
        const bool gone = !seen.copy;
        if (!gone && Clock::now() - g_art.dispelAt < kWatch) {
            return false;
        }
        SelfTestLog::Check("E6-drop", gone, std::format("{} ms after the dispel the copy art is {} on the player",
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g_art.dispelAt).count(),
            gone ? "gone" : "STILL there"));
        return true;
    }

    bool StepE6Npc(Ctx& a_c, bool a_first)
    {
        auto* caster = ActorOf(a_c.caster);
        if (a_first) {
            if (!Start(a_c, "E6-npc", caster)) {
                return true;
            }
            a_c.waitingFor = "the cloak's hit art on the caster";
            return false;
        }
        if (!caster) {
            SelfTestLog::Fail("E6-npc", "the caster is gone after the cast");
            return true;
        }
        const auto& pick = *g_art.pick;
        const auto seen = Look(caster->GetHandle(), pick);
        if (!seen.original && !seen.copy && Clock::now() - a_c.stepStart < kWatch) {
            return false;
        }
        SelfTestLog::Check("E6-npc", seen.original && !seen.copy, std::format("{}: on the caster the original {}, the copy {} "
            "({} live art effect(s))", Describe(pick), seen.original ? "found" : "NOT found", seen.copy ? "FOUND" : "not found",
            seen.onTarget));
        EndCloak("E6-npc", caster);
        return true;
    }

    // E5: PickCastingArtImpl called directly (no engine state read or changed) on a fire spell picked by data whose costliest
    // effect's casting art has a copy; then the session's SetCastingArt counters as INFO (when the engine calls it)
    bool StepE5(Ctx& a_c, bool)
    {
        const RE::SpellItem* spell = nullptr;
        RE::BGSArtObject* art = nullptr;
        for (const auto* candidate : FireSpellList()) {
            const auto* costliest = candidate->GetAVEffect();
            if (costliest && costliest->data.castingArt && CopyOf(costliest->data.castingArt)) {
                spell = candidate;
                art = costliest->data.castingArt;
                break;
            }
        }
        const auto counters = GetCopySwapCounters();
        const auto& site = GetCopySwapStatus().castingArt;
        SelfTestLog::Info("E5", std::format("SetCastingArt site {} ({}); calls this session {}, art copies handed on {}, rejected {}",
            site.hooked ? "hooked" : "NOT hooked", site.note, counters.castingArtCalls, counters.castingArtSwaps,
            counters.castingArtRejected));
        if (!spell) {
            SelfTestLog::Skip("E5", "no fire spell whose costliest effect's casting art has a copy");
            return true;
        }
        auto* player = Player();
        auto* copy = CopyOfAs<RE::BGSArtObject>(art);
        const auto right = RE::Actor::SlotTypes::kRightHand;
        const bool stoked = PickCastingArtImpl(player, right, spell, art, true) == copy;
        const bool unstoked = PickCastingArtImpl(player, right, spell, art, false) == art;
        const bool npc = PickCastingArtImpl(ActorOf(a_c.caster), right, spell, art, true) == art;
        const bool power = PickCastingArtImpl(player, RE::Actor::SlotTypes::kPowerOrShout, spell, art, true) == art;
        auto* other = copy;  // an art that is not the costliest effect's: the copy itself stands in
        const bool otherArt = PickCastingArtImpl(player, right, spell, other, true) == other;
        SelfTestLog::Check("E5", stoked && unstoked && npc && power && otherArt, std::format("spell {:08X}, casting art {:08X} -> copy "
            "{:08X}: player right hand stoked -> copy {}, not stoked -> original {}, the deer -> original {}, power slot -> original {}, "
            "another art -> unchanged {}", spell->GetFormID(), art->GetFormID(), copy->GetFormID(), stoked, unstoked, npc, power, otherArt));
        return true;
    }

    void CleanupArtSpells()
    {
        if (!g_art.pick) {
            return;
        }
        const auto* spell = g_art.pick->spell;
        const auto* associated = g_art.pick->effect->data.associatedForm ? g_art.pick->effect->data.associatedForm->As<RE::SpellItem>() :
                                                                           nullptr;
        std::vector<RE::Actor*> actors{ Player() };
        if (auto* lists = RE::ProcessLists::GetSingleton()) {
            for (auto& handle : lists->highActorHandles) {
                if (auto actor = handle.get(); actor && actor.get() != Player()) {
                    actors.push_back(actor.get());
                }
            }
        }
        int dispelled = 0;
        for (auto* actor : actors) {
            dispelled += DispelOn(actor, spell) + DispelOn(actor, associated);
        }
        SelfTestLog::Info("T9", std::format("E6's cloak {:08X} and its associated spell {:08X}: {} effect(s) dispelled on {} actor(s)",
            spell->GetFormID(), associated ? associated->GetFormID() : 0, dispelled, actors.size()));
    }
}

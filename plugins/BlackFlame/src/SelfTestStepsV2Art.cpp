// =============================================================================
// SELF-TEST v2 STEPS - E6 / E6-npc: a cloak's hit art takes its ARTO copy (the art spike)
// =============================================================================
// The candidate is picked by data, not by name: a fire spell (FireSet, kSpell) cast fire and forget with a
// Cloak-archetype effect that is one of its fire effects (IsBurnable), whose hit art - the engine's choice,
// MGEF hitVisuals' art (RFCT data.artObject) when set, else MGEF hitEffectArt - has an ARTO copy; of those the
// spell whose defining file comes last in TESDataHandler::files (a tie: the smaller FormID). Fire and forget only:
// CastSpellImmediate of a concentration cloak lasts one tick. No candidate: INFO, not judged.
//   E6     the player casts it on itself while stoked: within kWatch a live ModelReferenceEffect on the player
//          whose artObject is the copy, with its 3D loaded and every effect shape on the grey palette path, and the
//          GetHitEffectArt hook's copy counter advanced. One run settles both spike questions: the engine takes the
//          art the hook hands back (the 3D comes from the copy) and loads an ARTO whose record has no DATA.
//   E6-npc the deer caster casts the same spell on itself: its art is the original, never the copy.
// Each cloak is dispelled right after its judgement (its damage spell would hit anything near for 60 s and leave
// hit shaders the later unstoked checks read).

#include "SelfTestCtx.h"

#include "BuffState.h"
#include "Config.h"
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
            bool cast = false;
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

        // First call: picks the spell (or says why there is none); false when the step is over
        bool Start(Ctx& a_c, const char* a_id, RE::Actor* a_caster)
        {
            g_art.cast = false;
            if (!GetConfig().copyHitArt) {
                SelfTestLog::Info(a_id, "not judged: copyHitArt is off");
                return false;
            }
            if (!g_art.pick) {
                std::string note;
                g_art.pick = PickCloak(note);
                if (!g_art.pick) {
                    SelfTestLog::Info(a_id, std::format("not judged: no fire and forget fire spell with a cloak effect whose hit "
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
            if (!SelfTestWorld::Cast(a_caster, g_art.pick->spell, a_caster)) {
                SelfTestLog::Fail(a_id, "CastSpellImmediate refused the cloak (no caster or spell)");
                return false;
            }
            a_c.lastCast = Clock::now();
            g_art.cast = true;
            return true;
        }

        void EndCloak(const char* a_id, RE::Actor* a_caster)
        {
            const int dispelled = a_caster && g_art.pick ? SelfTestWorld::DispelSpell(a_caster, g_art.pick->spell) : 0;
            SelfTestLog::Info(a_id, std::format("cloak dispelled: {} effect(s)", dispelled));
        }
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
        const bool watched = Clock::now() - a_c.stepStart >= kWatch;
        if (!seen.copy3D && !watched) {
            return false;
        }
        const auto copied = GetHitSlotStats(false).copied - g_art.copiedBefore;
        SelfTestProbe::Inspection grey;
        if (seen.copy3D) {
            grey = SelfTestProbe::Inspect(seen.copy3D.get());
        }
        const bool allGrey = grey.effectProperties > 0 && grey.withPalettePath == grey.effectProperties;
        SelfTestLog::Check("E6", seen.copy && seen.copy3D && allGrey && copied > 0 && !seen.original,
            std::format("{}: {} live art effect(s) on the player; the copy {} ({}), the original {}; effect shapes {}, with "
                "the grey palette path {}; GetHitEffectArt returned the copy {} time(s) since the cast", Describe(pick),
                seen.onTarget, seen.copy ? "FOUND" : "not found", seen.copy3D ? "3D loaded" : "no 3D yet",
                seen.original ? "ALSO found" : "not found", grey.effectProperties, grey.withPalettePath, copied));
        EndCloak("E6", player);
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
}

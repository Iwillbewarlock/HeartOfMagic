#include "SpellCastHandler.h"

#include <atomic>

// =============================================================================
// IMMEDIATE CASTS
// =============================================================================
// Spells cast through MagicCaster::CastSpellImmediate send no TESSpellCastEvent,
// so SpellCastHandler never heard of them and they gave no XP. SpellHotbar 2
// casts every hotbar spell this way (casting_controller.cpp, cast_spell), and so
// does Papyrus Spell.Cast. ActorMagicCaster does not override slot 0x01, so its
// vtable entry is MagicCaster's own and this hook sees only actors' casters.
// Only the player's casts are passed on; the spell checks (a real spell the
// player knows, not counted twice) run on the game thread in HandlePlayerCast.

namespace
{
    std::atomic<bool> g_immediateFailureReported{ false };

    struct CastSpellImmediateHook
    {
        static void thunk(RE::MagicCaster* a_this, RE::MagicItem* a_spell, bool a_noHitEffectArt,
            RE::TESObjectREFR* a_target, float a_effectiveness, bool a_hostileEffectivenessOnly,
            float a_magnitudeOverride, RE::Actor* a_blameActor)
        {
            func(a_this, a_spell, a_noHitEffectArt, a_target, a_effectiveness, a_hostileEffectivenessOnly,
                a_magnitudeOverride, a_blameActor);

            // Entered from the engine's vtable: nothing may throw out of here
            try {
                if (!a_this || !a_spell) {
                    return;
                }
                auto* player = RE::PlayerCharacter::GetSingleton();
                if (!player || a_this->GetCasterAsActor() != player) {
                    return;
                }
                auto* spell = a_spell->As<RE::SpellItem>();
                if (!spell) {
                    return;
                }
                SpellCastHandler::GetSingleton()->OnImmediateCast(spell->GetFormID());
            } catch (...) {
                if (!g_immediateFailureReported.exchange(true)) {
                    logger::error("SpellCastHandler: immediate cast hook threw; further failures are not logged");
                }
            }
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };
}

void SpellCastHandler::InstallImmediateCastHook()
{
    REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_ActorMagicCaster[0] };
    CastSpellImmediateHook::func = vtbl.write_vfunc(0x01, CastSpellImmediateHook::thunk);
    logger::info("SpellCastHandler: Hooked CastSpellImmediate on ActorMagicCaster (casts without a cast event)");
}

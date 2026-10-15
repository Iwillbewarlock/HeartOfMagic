#include "SelfTestWorld.h"

#include "Forms.h"

#include <cmath>
#include <format>

namespace BlackFlame::SelfTestWorld
{
    namespace
    {
        constexpr float kFullEffectiveness = 1.0f;
        constexpr float kNoMagnitudeOverride = 0.0f;
        constexpr float kDummyHealth = 100000.0f;  // survives every test cast
    }

    bool LookupVanilla(VanillaForms& a_out, std::string& a_missing)
    {
        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) {
            a_missing = "TESDataHandler";
            return false;
        }
        a_out.firebolt = dh->LookupForm<RE::SpellItem>(kFireboltLocalId, kSkyrimMaster);
        a_out.fireball = dh->LookupForm<RE::SpellItem>(kFireballLocalId, kSkyrimMaster);
        a_out.dummy = dh->LookupForm<RE::TESNPC>(kDummyNpcLocalId, kSkyrimMaster);
        a_out.caster = dh->LookupForm<RE::TESNPC>(kCasterNpcLocalId, kSkyrimMaster);
        const auto add = [&](bool a_ok, const char* a_name) {
            if (!a_ok) {
                a_missing += a_missing.empty() ? a_name : std::format(", {}", a_name);
            }
        };
        add(a_out.firebolt, "Firebolt 012FD0");
        add(a_out.fireball, "Fireball 01C789");
        add(a_out.dummy, "EncDeer 0CF89D");
        add(a_out.caster, "WEFarmerFemale 1034E6");
        return a_missing.empty();
    }

    bool Cast(RE::Actor* a_caster, RE::SpellItem* a_spell, RE::TESObjectREFR* a_target)
    {
        if (!a_caster || !a_spell) {
            return false;
        }
        auto* caster = a_caster->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
        if (!caster) {
            return false;
        }
        caster->CastSpellImmediate(a_spell, false, a_target, kFullEffectiveness, false, kNoMagnitudeOverride, a_caster);
        return true;
    }

    RE::ObjectRefHandle Spawn(RE::TESNPC* a_base, float a_forward, float a_right)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !a_base) {
            return {};
        }
        const auto ref = player->PlaceObjectAtMe(a_base, false);
        if (!ref) {
            return {};
        }
        // Heading: forward is (sin z, cos z), right is (cos z, -sin z)
        const float heading = player->GetAngleZ();
        auto pos = player->GetPosition();
        pos.x += std::sin(heading) * a_forward + std::cos(heading) * a_right;
        pos.y += std::cos(heading) * a_forward - std::sin(heading) * a_right;
        ref->SetPosition(pos);
        return ref->GetHandle();
    }

    std::string MakePassive(RE::Actor* a_actor, bool a_raiseHealth)
    {
        if (!a_actor) {
            return "no actor";
        }
        a_actor->EnableAI(false);
        std::string done = std::format("AI {}", a_actor->IsAIEnabled() ? "still ON" : "off");
        if (a_raiseHealth) {
            if (auto* values = a_actor->AsActorValueOwner()) {
                values->SetBaseActorValue(RE::ActorValue::kHealth, kDummyHealth);
                done += std::format(", health base {} (now {:.0f})", kDummyHealth, values->GetActorValue(RE::ActorValue::kHealth));
            }
        }
        return done;
    }

    int DispelStoke()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        const auto* keyword = GetForms().stokedKeyword;
        auto* target = player ? player->AsMagicTarget() : nullptr;
        auto* effects = target ? target->GetActiveEffectList() : nullptr;
        if (!effects || !keyword) {
            return 0;
        }
        // Collected first: Dispel may change the list being walked
        std::vector<RE::ActiveEffect*> found;
        for (auto* effect : *effects) {
            const auto* base = effect ? effect->GetBaseObject() : nullptr;
            if (base && base->HasKeyword(keyword)) {
                found.push_back(effect);
            }
        }
        for (auto* effect : found) {
            effect->Dispel(true);  // RE/A/ActiveEffect.h:92
        }
        return static_cast<int>(found.size());
    }

    std::vector<ShaderOn> ShadersOn(RE::ObjectRefHandle a_target)
    {
        std::vector<ShaderOn> out;
        auto* lists = RE::ProcessLists::GetSingleton();
        if (!lists || !a_target) {
            return out;
        }
        lists->ForEachShaderEffect([&](RE::ShaderReferenceEffect* a_effect) {
            if (a_effect && a_effect->target == a_target) {
                out.push_back({ a_effect->effectData ? a_effect->effectData->GetFormID() : 0, a_effect->finished });
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });
        return out;
    }

    std::string Remove(RE::ObjectRefHandle a_ref)
    {
        const auto ref = a_ref.get();
        if (!ref) {
            return "already gone";
        }
        const auto id = ref->GetFormID();
        ref->Disable();       // RE/T/TESObjectREFR.h:330
        ref->SetDelete(true);  // RE/T/TESObjectREFR.h:218
        return std::format("{:08X} disabled and marked deleted", id);
    }
}

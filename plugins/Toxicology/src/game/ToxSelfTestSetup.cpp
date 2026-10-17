// =============================================================================
// Toxicology - dev self test: step 1, the picks, steps 5 and 7, helpers
// =============================================================================
// See game/ToxSelfTestState.h for the split between the self test's files.

#include "game/ToxSelfTestState.h"

#include "Common.h"
#include "game/ToxData.h"
#include "game/ToxMagicka.h"
#include "game/ToxSelection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <format>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>

namespace SelfTest = ToxGame::SelfTest;
using SelfTest::State;

namespace
{
    // =========================================================================
    // Constants
    // =========================================================================

    constexpr std::int32_t kBottlesA = 20;      // steps 2-4 may use some up (cast events charge Magicka)
    constexpr std::int32_t kBottlesB = 5;
    constexpr std::int32_t kDoseBottlesA = 2;   // step 7 (spec 9.7)
    constexpr std::int32_t kDoseBottlesB = 3;
    constexpr float kTargetHealth = 100000.0f;  // a DoT poison must not kill the target mid-test
    constexpr float kNoAggression = 0.0f;       // Unaggressive
    constexpr float kFoolhardy = 4.0f;          // Confidence 4: never flees
    constexpr int kMaxRemoveSteps = 32;
    constexpr std::uint32_t kRuntimeFormIndex = 0xFF;
    constexpr const wchar_t* kI5ModuleName = L"InventoryInjectorImproved.dll";

    std::string Lower(std::string_view a_text)
    {
        std::string out(a_text);
        std::transform(out.begin(), out.end(), out.begin(),
            [](char a_c) { return (a_c >= 'A' && a_c <= 'Z') ? static_cast<char>(a_c - 'A' + 'a') : a_c; });
        return out;
    }

    // =========================================================================
    // Poisons (step 1)
    // =========================================================================

    struct PoisonPick
    {
        RE::AlchemyItem* item = nullptr;
        std::string lower;
        float longest = 0.0f;
    };

    // Load order poisons the player holds none of, with a name and a longest effect of
    // kMinPoisonSeconds..maxPoisonSeconds; sorted by lowercased name, then FormID (the core's order)
    std::vector<PoisonPick> PoisonCandidates(const SelfTest::Config& a_config)
    {
        std::vector<PoisonPick> out;
        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) return out;
        for (auto* item : dh->GetFormArray<RE::AlchemyItem>()) {  // RE/T/TESDataHandler.h:89
            if (!item || !item->IsPoison() || (item->GetFormID() >> 24) == kRuntimeFormIndex) continue;
            const char* name = item->GetFullName();  // RE/T/TESFullName.h:23
            if (!name || !*name || SelfTest::BottleCount(item) > 0) continue;
            float longest = 0.0f;
            for (const auto* effect : item->effects) {
                // RE/E/Effect.h:34; GetDuration reads baseEffect unchecked (ToxApply.cpp HasLastingEffect)
                if (effect && effect->baseEffect) longest = std::max(longest, static_cast<float>(effect->GetDuration()));
            }
            if (longest < SelfTest::kMinPoisonSeconds || longest > a_config.maxPoisonSeconds) continue;
            out.push_back({ item, Lower(name), longest });
        }
        std::sort(out.begin(), out.end(), [](const PoisonPick& a_l, const PoisonPick& a_r) {
            return a_l.lower != a_r.lower ? a_l.lower < a_r.lower : a_l.item->GetFormID() < a_r.item->GetFormID();
        });
        return out;
    }

    RE::InventoryEntryData* EntryOf(RE::InventoryChanges* a_changes, const RE::TESBoundObject* a_object)
    {
        if (!a_changes || !a_changes->entryList) return nullptr;  // RE/I/InventoryChanges.h:101
        for (auto* entry : *a_changes->entryList) {
            if (entry && entry->object == a_object) return entry;  // RE/I/InventoryEntryData.h:65
        }
        return nullptr;
    }

    // InventoryChanges::SetFavorite (RE/I/InventoryChanges.h:69) on the entry's first list (null when it
    // has none), checked the two ways the addon reads it: entry IsFavorited (the choice, ToxSelection)
    // and a list with ExtraHotkey (the bottle order, ToxBottles)
    bool Favorite(RE::PlayerCharacter& a_player, RE::AlchemyItem* a_poison, std::string& a_why)
    {
        auto* changes = a_player.GetInventoryChanges();  // RE/T/TESObjectREFR.h:410
        auto* entry = EntryOf(changes, a_poison);
        if (!entry) {
            a_why = "no inventory entry for it after AddObjectToContainer";
            return false;
        }
        const bool hadList = entry->extraLists && !entry->extraLists->empty();
        auto* list = hadList ? entry->extraLists->front() : nullptr;
        changes->SetFavorite(entry, list);
        const bool entryFavorited = entry->IsFavorited();  // RE/I/InventoryEntryData.h:50
        bool hotkeyList = false;
        if (entry->extraLists) {
            for (auto* l : *entry->extraLists) {
                if (l && l->HasType<RE::ExtraHotkey>()) hotkeyList = true;  // RE/E/ExtraDataList.h:183
            }
        }
        if (entryFavorited && hotkeyList) return true;
        a_why = std::format("SetFavorite(entry, {}) left entry IsFavorited={} and a list with ExtraHotkey={}",
            hadList ? "its first list" : "no list", entryFavorited, hotkeyList);
        return false;
    }

    // All bottles of a_poison removed, then a_count added and favorited
    bool GiveFavorited(RE::AlchemyItem* a_poison, std::int32_t a_count, std::string& a_why)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            a_why = "no player";
            return false;
        }
        SelfTest::RemoveAllBottles(a_poison);
        player->AddObjectToContainer(a_poison, nullptr, a_count, nullptr);  // RE/T/TESObjectREFR.h:270
        if (const auto count = SelfTest::BottleCount(a_poison); count != a_count) {
            a_why = std::format("{} bottles after adding {}", count, a_count);
            return false;
        }
        return Favorite(*player, a_poison, a_why);
    }

    // =========================================================================
    // Poison spells by delivery (step 2)
    // =========================================================================

    void AddDelivery(std::vector<SelfTest::Delivery>& a_out, const char* a_label, RE::SpellItem* a_spell,
        RE::SpellItem* a_hitSpell, bool a_cloak)
    {
        const auto it = std::find_if(a_out.begin(), a_out.end(), [&](const SelfTest::Delivery& d) { return d.label == a_label; });
        if (it == a_out.end()) {
            a_out.push_back({ a_label, a_spell, a_hitSpell, a_cloak, false });
        } else if (a_spell->GetFormID() < it->spell->GetFormID()) {
            *it = { a_label, a_spell, a_hitSpell, a_cloak, false };  // the smallest FormID: the same pick every run
        }
    }

    const char* DeliveryLabel(const RE::SpellItem& a_spell)
    {
        using CT = RE::MagicSystem::CastingType;  // RE/M/MagicSystem.h:36-44
        using DT = RE::MagicSystem::Delivery;
        if (a_spell.GetCastingType() == CT::kConcentration) return "concentration";  // RE/S/SpellItem.h:69
        if (a_spell.GetCastingType() != CT::kFireAndForget) return nullptr;
        switch (a_spell.GetDelivery()) {  // RE/S/SpellItem.h:71
        case DT::kAimed:
            return "aimed";
        case DT::kTouch:
            return "touch";
        case DT::kTargetActor:
            return "target actor";
        case DT::kTargetLocation:
            return "target location";
        default:
            return nullptr;
        }
    }

    // One poison spell per delivery (cloak sub-spells only through their cloak), in a fixed order
    std::vector<SelfTest::Delivery> PickDeliveries()
    {
        std::vector<SelfTest::Delivery> found;
        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) return found;
        ToxGame::ReloadPoisonSpellsIfChanged();  // contract (ToxData.h)
        auto& spells = dh->GetFormArray<RE::SpellItem>();
        std::unordered_set<RE::FormID> subSpells;
        for (auto* spell : spells) {
            if (!spell) continue;
            for (const auto* effect : spell->effects) {
                const auto* base = effect ? effect->baseEffect : nullptr;
                if (!base || !base->HasArchetype(RE::EffectSetting::Archetype::kCloak) || !base->data.associatedForm) continue;
                auto* sub = base->data.associatedForm->As<RE::SpellItem>();  // RE/E/EffectSetting.h:71
                if (!sub || !ToxGame::IsPoisonSpell(sub->GetFormID())) continue;
                subSpells.insert(sub->GetFormID());
                AddDelivery(found, "cloak", spell, sub, true);
            }
        }
        for (auto* spell : spells) {
            if (!spell || !ToxGame::IsPoisonSpell(spell->GetFormID()) || subSpells.contains(spell->GetFormID())) continue;
            if (const char* label = DeliveryLabel(*spell)) AddDelivery(found, label, spell, spell, false);
        }
        static constexpr std::array<std::string_view, 6> kOrder{ "aimed", "touch", "target actor", "target location",
            "concentration", "cloak" };
        std::vector<SelfTest::Delivery> ordered;
        for (const auto label : kOrder) {
            for (const auto& d : found) {
                if (d.label == label) ordered.push_back(d);
            }
        }
        return ordered;
    }

    // "XXXXXX:Plugin.esp" (houseCARL's form) -> the NPC
    RE::TESNPC* LookupBase(const std::string& a_text, std::string& a_why)
    {
        const auto colon = a_text.find(':');
        if (colon == std::string::npos || colon == 0 || colon + 1 >= a_text.size()) {
            a_why = "not \"XXXXXX:Plugin.esp\"";
            return nullptr;
        }
        RE::FormID local = 0;
        try {
            local = static_cast<RE::FormID>(std::stoul(a_text.substr(0, colon), nullptr, 16));
        } catch (const std::exception&) {
            a_why = "the FormID part is not hex";
            return nullptr;
        }
        auto* dh = RE::TESDataHandler::GetSingleton();
        auto* npc = dh ? dh->LookupForm<RE::TESNPC>(local, a_text.substr(colon + 1)) : nullptr;  // RE/T/TESDataHandler.h:65
        if (!npc) a_why = "no NPC record with that id in this load order";
        return npc;
    }

    bool PickPoisons(State& s)
    {
        const auto candidates = PoisonCandidates(s.config);
        if (candidates.empty()) {
            SelfTest::Fail(s, "1 poisons", std::format("no load order poison the player holds none of with a lasting effect "
                "of {}..{} s (raise maxPoisonSeconds)", SelfTest::kMinPoisonSeconds, s.config.maxPoisonSeconds));
            return false;
        }
        const auto second = std::find_if(candidates.begin() + 1, candidates.end(),
            [&](const PoisonPick& p) { return p.lower != candidates.front().lower; });
        if (second == candidates.end()) {
            SelfTest::Fail(s, "1 poisons", "only one poison name qualifies - two are needed");
            return false;
        }
        s.poisonA = candidates.front().item;
        s.poisonB = second->item;
        SelfTest::Info("1 poisons", std::format("A {} longest effect {:.0f} s; B {} longest effect {:.0f} s ({} candidates)",
            SelfTest::FormText(s.poisonA), candidates.front().longest, SelfTest::FormText(s.poisonB), second->longest,
            candidates.size()));
        return true;
    }

    void CheckKeyword(State& s, std::string_view a_step, RE::AlchemyItem* a_on, RE::AlchemyItem* a_off)
    {
        auto* keyword = ToxGame::MimicKeyword();
        const bool on = a_on->HasKeyword(keyword);  // RE/B/BGSKeywordForm.h:26
        const bool off = a_off->HasKeyword(keyword);
        const bool wantOn = ToxGame::GetSettings().showIcon;
        const auto detail = std::format("HoMTox_MimicPoison on {}: {}, on {}: {} (showIcon {})", SelfTest::FormText(a_on), on,
            SelfTest::FormText(a_off), off, wantOn);
        SelfTest::Check(s, a_step, on == wantOn && !off, detail);
    }
}

// =============================================================================
// Inventory and effects
// =============================================================================

std::int32_t SelfTest::BottleCount(RE::AlchemyItem* a_poison)
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || !a_poison) return 0;
    // RE/T/TESObjectREFR.h:409 (base container included)
    const auto counts = player->GetInventoryCounts([a_poison](RE::TESBoundObject& a_object) { return &a_object == a_poison; });
    const auto it = counts.find(a_poison);
    return it != counts.end() ? it->second : 0;
}

void SelfTest::RemoveAllBottles(RE::AlchemyItem* a_poison)
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || !a_poison) return;
    for (int step = 0; step < kMaxRemoveSteps; ++step) {
        const auto before = BottleCount(a_poison);
        if (before <= 0) return;
        // A stack with its own list first (the favorited one), then the bottles outside any list (ToxBottles.cpp)
        RE::ExtraDataList* list = nullptr;
        std::int32_t count = before;
        if (auto* entry = EntryOf(player->GetInventoryChanges(), a_poison); entry && entry->extraLists) {
            for (auto* l : *entry->extraLists) {
                if (l) {
                    list = l;
                    count = std::min(before, static_cast<std::int32_t>(l->GetCount()));  // RE/E/ExtraDataList.h:201
                    break;
                }
            }
        }
        player->RemoveItem(a_poison, count, RE::ITEM_REMOVE_REASON::kRemove, list, nullptr);  // RE/T/TESObjectREFR.h:266
        if (BottleCount(a_poison) >= before) {
            logger::warn("SELFTEST WARN RemoveItem took no bottle of {} ({} held)", FormText(a_poison), before);
            return;
        }
    }
}

bool SelfTest::IsLiveEffect(const RE::ActiveEffect& a_effect)
{
    using Flag = RE::ActiveEffect::Flag;  // RE/A/ActiveEffect.h:45-54; spec 12
    return a_effect.flags.none(Flag::kInactive, Flag::kDispelled);
}

std::vector<RE::ActiveEffect*> SelfTest::LiveEffectsOf(RE::Actor* a_actor, const RE::MagicItem* a_item)
{
    std::vector<RE::ActiveEffect*> out;
    auto* list = a_actor ? a_actor->GetActiveEffectList() : nullptr;  // RE/A/Actor.h:373
    if (!list) return out;
    for (auto* effect : *list) {
        if (effect && effect->spell == a_item && IsLiveEffect(*effect)) out.push_back(effect);
    }
    return out;
}

std::vector<SelfTest::EffectRef> SelfTest::Refs(const std::vector<RE::ActiveEffect*>& a_effects)
{
    std::vector<EffectRef> out;
    for (const auto* effect : a_effects) out.push_back({ effect, effect->usUniqueID });
    return out;
}

RE::ActiveEffect* SelfTest::FindAgain(RE::Actor* a_actor, const EffectRef& a_ref)
{
    auto* list = a_actor ? a_actor->GetActiveEffectList() : nullptr;
    if (!list) return nullptr;
    for (auto* effect : *list) {
        if (effect == a_ref.effect && effect->usUniqueID == a_ref.uniqueId) return effect;
    }
    return nullptr;
}

bool SelfTest::CastFromPlayer(RE::MagicItem* a_item, RE::TESObjectREFR* a_target)
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* caster = player ? player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant) : nullptr;  // RE/A/Actor.h:324
    if (!caster) return false;
    caster->CastSpellImmediate(a_item, false, a_target, 1.0f, false, 0.0f, player);  // RE/M/MagicCaster.h:62
    return true;
}

// =============================================================================
// The target
// =============================================================================

RE::NiPointer<RE::Actor> SelfTest::TargetActor(State& s)
{
    if (!s.target.id) return {};
    auto actor = s.target.handle.get();  // RE/B/BSPointerHandle.h:155
    if (!actor || actor->GetFormID() != s.target.id) return {};
    return actor;
}

bool SelfTest::SpawnTarget(State& s, std::string_view step)
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || !s.targetBase) {
        Fail(s, step, "no player or no target base");
        return false;
    }
    // RE/T/TESObjectREFR.h:475 at the player; TargetReady moves it in front of them once its 3D is there
    // (heading from +y, clockwise: GetHeadingAngle, src/RE/T/TESObjectREFR.cpp:399)
    const auto ref = player->PlaceObjectAtMe(s.targetBase, false);
    auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
    if (!actor) {
        if (ref) {
            ref->Disable();  // RE/T/TESObjectREFR.h:330, :218
            ref->SetDelete(true);
        }
        Fail(s, step, std::format("PlaceObjectAtMe({}) gave {}", FormText(s.targetBase), ref ? "a ref that is not an actor" : "no ref"));
        return false;
    }
    const float angle = player->GetAngleZ();  // RE/T/TESObjectREFR.h:383
    auto pos = player->GetPosition();
    pos.x += std::sin(angle) * s.config.targetDistance;
    pos.y += std::cos(angle) * s.config.targetDistance;
    auto* values = actor->AsActorValueOwner();  // RE/A/Actor.h:778; RE/A/ActorValueOwner.h:19,22
    values->SetBaseActorValue(RE::ActorValue::kHealth, kTargetHealth);
    values->SetActorValue(RE::ActorValue::kAggression, kNoAggression);
    values->SetActorValue(RE::ActorValue::kConfidence, kFoolhardy);
    if (s.config.disableTargetAI) actor->EnableAI(false);  // RE/A/Actor.h:552
    s.target = { actor->GetHandle(), actor->GetFormID(), pos, 0 };  // RE/A/Actor.h:584
    const auto* crime = actor->GetCrimeFaction();  // RE/A/Actor.h:571
    Info(std::format("{} target", step), std::format("spawned [{:08X}] from {} {:.0f} units in front of the player; crime "
        "faction {}; AI {}", actor->GetFormID(), FormText(s.targetBase), s.config.targetDistance, crime ? FormText(crime) : "none",
        s.config.disableTargetAI ? "off" : "on (calmed every tick)"));
    return true;
}

SelfTest::Readiness SelfTest::TargetReady(State& s)
{
    const auto actor = TargetActor(s);
    if (actor && actor->Is3DLoaded() && actor->GetHighProcess() && !actor->IsDead()) {  // Actor.h:587, :363
        // Moved once its 3D (and character controller) is there: Actor::SetPosition, RE/A/Actor.h:384 (vfunc 0xA9)
        actor->SetPosition(s.target.placedAt, true);
        return Readiness::kReady;
    }
    return WaitOver(++s.target.waited, s.config.spawnWaitMs) ? Readiness::kTimedOut : Readiness::kWaiting;
}

void SelfTest::DeleteTarget(State& s)
{
    if (const auto actor = TargetActor(s)) {
        actor->Disable();
        actor->SetDelete(true);
    }
    s.target = {};
}

void SelfTest::CalmTarget(State& s)
{
    if (s.config.disableTargetAI) return;
    if (const auto actor = TargetActor(s); actor && actor->IsInCombat()) {  // RE/A/Actor.h:442, :444
        actor->StopCombat();
    }
}

std::string SelfTest::TargetText(State& s, RE::Actor* a_actor)
{
    if (!a_actor) return "target gone";
    const auto pos = a_actor->GetPosition();
    return std::format("target moved {:.0f} units, in combat {}, dead {}", pos.GetDistance(s.target.placedAt),
        a_actor->IsInCombat(), a_actor->IsDead());
}

void SelfTest::DispelCloak(State& s)
{
    if (!s.cloakOnPlayer) return;
    for (auto* effect : LiveEffectsOf(RE::PlayerCharacter::GetSingleton(), s.cloakOnPlayer)) {
        effect->Dispel(true);  // RE/A/ActiveEffect.h:92
    }
    s.cloakOnPlayer = nullptr;
}

// =============================================================================
// Step 1 (+ the picks for steps 2-4, step 5)
// =============================================================================

bool SelfTest::Setup(State& s)
{
    s.originalAccumulated = ToxGame::Accumulated();  // first: Undo puts it back whatever happens below
    auto* player = RE::PlayerCharacter::GetSingleton();
    std::ostringstream thread;
    thread << std::this_thread::get_id();
    Info("threads", std::format("the test's ticks run on thread {} (the game's main thread). The lines 'spell hit events "
        "arrive on the ... thread', 'co-save ... callback runs on the ... thread' and 'the player's hand casters update on "
        "the ... thread' carry their own thread id in the [thread] column", thread.str()));
    if (!player || !ToxGame::Ability() || !ToxGame::MimicKeyword()) {
        Fail(s, "setup", "no player, or the ESL is not loaded (no ability or keyword)");
        return false;
    }
    if (!ToxGame::GetSettings().enabled) {
        Fail(s, "setup", "the setting \"enabled\" is false - the addon does nothing to test");
        return false;
    }
    if (!ToxGame::PlayerHasAbility()) {
        player->AddSpell(ToxGame::Ability());  // RE/A/Actor.h:518
        s.addedAbility = true;
    }
    if (!ToxGame::PlayerHasAbility()) {
        Fail(s, "1 ability", "AddSpell did not give the player the Toxicology ability");
        return false;
    }
    Pass(s, "1 ability", s.addedAbility ? "added" : "already held");
    if (!PickPoisons(s)) return false;

    std::string why;
    if (!GiveFavorited(s.poisonA, kBottlesA, why) || !GiveFavorited(s.poisonB, kBottlesB, why)) {
        Fail(s, "1 favorite", why);
        return false;
    }
    Pass(s, "1 favorite", std::format("{} x{} and {} x{} given and favorited", FormText(s.poisonA), kBottlesA,
        FormText(s.poisonB), kBottlesB));
    ToxGame::OnSelectionMaybeChanged();
    auto* choice = ToxGame::CurrentPoison(true);
    if (choice == s.poisonA) {
        Pass(s, "1 choice", std::format("CurrentPoison is {}, the name-first of the two", FormText(choice)));
    } else {
        Fail(s, "1 choice", std::format("CurrentPoison is {}, expected {} (a favorited poison of the player's own sorting "
            "first? use a character without favorited poisons)", FormText(choice), FormText(s.poisonA)));
    }
    CheckKeyword(s, "1 keyword", s.poisonA, s.poisonB);

    s.deliveries = PickDeliveries();
    std::string picked;
    for (const auto& d : s.deliveries) {
        picked += std::format("{}{} {}", picked.empty() ? "" : "; ", d.label, FormText(d.spell));
        if (d.cloak) picked += std::format(" (hits with {})", FormText(d.hitSpell));
    }
    Info("2 spells", picked.empty() ? "no poison spell to cast (no catalog, or no poison spell of a known delivery)" : picked);
    if (s.deliveries.empty()) Fail(s, "2 spells", "no poison spell - steps 2-4 skipped");
    s.targetBase = LookupBase(s.config.targetBase, why);
    if (!s.targetBase) Fail(s, "2 target", std::format("targetBase {}: {} - steps 2-4 skipped", s.config.targetBase, why));
    StepI5();
    return true;
}

void SelfTest::StepI5()
{
    const bool loaded = REX::W32::GetModuleHandleW(kI5ModuleName) != nullptr;  // REX/W32/KERNEL32.h:462
    Info("5 I5", std::format("InventoryInjectorImproved.dll loaded: {}; Toxicology attempts no purge of its icon cache "
        "(spec 5.5: its console command cannot be reached from a plugin)", loaded ? "yes" : "no"));
}

// =============================================================================
// Step 7: Magicka spent -> bottles
// =============================================================================

void SelfTest::StepDoses(State& s)
{
    s.dosesRan = true;
    const float perDose = ToxGame::GetSettings().magickaPerDose;
    std::string why;
    if (!GiveFavorited(s.poisonA, kDoseBottlesA, why) || !GiveFavorited(s.poisonB, kDoseBottlesB, why)) {
        Fail(s, "7 setup", why);
        return;
    }
    ToxGame::SetAccumulated(0.0f);
    ToxGame::OnSelectionMaybeChanged();
    if (ToxGame::CurrentPoison(true) != s.poisonA) {
        Fail(s, "7 setup", std::format("the copied poison is {}, not {}", FormText(ToxGame::CurrentPoison(true)), FormText(s.poisonA)));
        return;
    }
    const float spent = perDose * 2.5f;
    ToxGame::AddSpentMagicka(spent);
    const auto a = BottleCount(s.poisonA);
    const auto b = BottleCount(s.poisonB);
    const float left = ToxGame::Accumulated();
    auto* next = ToxGame::CurrentPoison(true);
    const auto bottles = std::format("spent {:.1f} (2.5 x magickaPerDose {:.1f}): A {} -> {}, B {} -> {} (expected 0 and {})",
        spent, perDose, kDoseBottlesA, a, kDoseBottlesB, b, kDoseBottlesB);
    Check(s, "7 bottles", a == 0 && b == kDoseBottlesB, bottles);
    const auto rest = std::format("count toward the next bottle {:.2f} (expected {:.2f})", left, perDose * 0.5f);
    Check(s, "7 remainder", std::fabs(left - perDose * 0.5f) < kMagickaTolerance, rest);
    const auto choice = std::format("copied poison after A ran out: {} (expected {})", FormText(next), FormText(s.poisonB));
    Check(s, "7 next poison", next == s.poisonB, choice);
    CheckKeyword(s, "7 keyword", s.poisonB, s.poisonA);

    // Doses that cross from one poison to the next in one call
    if (!GiveFavorited(s.poisonA, 1, why)) {
        Fail(s, "7 span setup", why);
        return;
    }
    ToxGame::SetAccumulated(0.0f);
    ToxGame::OnSelectionMaybeChanged();
    ToxGame::AddSpentMagicka(perDose * 2.0f);
    const auto a2 = BottleCount(s.poisonA);
    const auto b2 = BottleCount(s.poisonB);
    const auto span = std::format("2 doses with A x1, B x{}: A {}, B {}, count {:.2f} (expected 0, {}, 0)", kDoseBottlesB,
        a2, b2, ToxGame::Accumulated(), kDoseBottlesB - 1);
    Check(s, "7 doses span poisons", a2 == 0 && b2 == kDoseBottlesB - 1 && std::fabs(ToxGame::Accumulated()) < kMagickaTolerance, span);
}

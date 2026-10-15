#include "game/GameNpcs.h"

#include "core/NpcSpellsSlots.h"
#include "core/NpcSpellsTemplate.h"
#include "PluginNames.h"
#include "tomereach/TomeReachRecords.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace
{
    using NpcSpells::Category;

    constexpr std::size_t kMaxTemplateDepth = 8;  // a longer chain is malformed data; the walk stops and ProviderIndex reports it unresolvable
    constexpr std::string_view kNoSourceFile = "no source file";
    constexpr std::string_view kNoRace = "no race";
    constexpr std::string_view kDeletedOrIgnored = "deleted or ignored record";
    constexpr std::string_view kFacePreset = "face preset";

    static_assert(sizeof(RE::CLASS_DATA::SkillWeights) == std::tuple_size_v<decltype(NpcSpells::ClassWeights::w)>);
    static_assert(std::tuple_size_v<decltype(NpcSpellsGame::NpcStats::byGroup)> == NpcSpells::kGroups.size());
    static_assert(std::tuple_size_v<decltype(NpcSpellsGame::NpcStats::byTier)> == static_cast<std::size_t>(NpcSpells::ClassTier::Mage) + 1);

    // The rules' records as runtime FormIDs, resolved once. Forms are matched by FormID only, never by
    // GetFormEditorID() (empty for most forms without powerofthree's Tweaks).
    struct ResolvedRules
    {
        std::unordered_set<RE::FormID> necromancerClasses, beastRaces, undeadExceptNpcs;
        std::optional<RE::FormID> redguardRace, keywordNpc, keywordCreature, keywordAnimal;
        std::unordered_map<RE::FormID, std::uint8_t> factionGroup;  // faction -> kGroups index (not the index in rules.groups)
    };

    class RefResolver
    {
    public:
        explicit RefResolver(std::size_t& unresolved) : m_unresolved(unresolved) {}

        // A ref not in this load order drops that ref only; the rest of its list or group stays (spec section 7)
        std::optional<RE::FormID> Resolve(const NpcSpells::FormRef& ref)
        {
            if (ref.pluginLower.empty()) {
                return std::nullopt;
            }
            if (auto* form = NpcSpellsGame::LookupLocalForm(m_lookup, ref.pluginLower, ref.local)) {
                return form->GetFormID();
            }
            // One warning per distinct ref; every dropped occurrence is counted
            if (m_warned.insert(std::format("{:06X}:{}", ref.local, ref.pluginLower)).second) {
                logger::warn("NPCSpells: rules: <{:06X}:{}> not found in this load order, ignored", ref.local, ref.pluginLower);
            }
            ++m_unresolved;
            return std::nullopt;
        }

        void ResolveInto(const std::vector<NpcSpells::FormRef>& refs, std::unordered_set<RE::FormID>& out)
        {
            for (const auto& ref : refs) {
                if (const auto id = Resolve(ref)) {
                    out.insert(*id);
                }
            }
        }

    private:
        NpcSpellsGame::PluginLookup m_lookup;
        std::unordered_set<std::string> m_warned;
        std::size_t& m_unresolved;
    };

    ResolvedRules ResolveRules(const NpcSpells::Rules& rules, std::size_t& unresolved)
    {
        ResolvedRules out;
        RefResolver r(unresolved);
        r.ResolveInto(rules.necromancerClasses, out.necromancerClasses);
        r.ResolveInto(rules.beastRaces, out.beastRaces);
        r.ResolveInto(rules.undeadExceptNpcs, out.undeadExceptNpcs);
        out.redguardRace = r.Resolve(rules.redguardRace);
        out.keywordNpc = r.Resolve(rules.keywordNpc);
        out.keywordCreature = r.Resolve(rules.keywordCreature);
        out.keywordAnimal = r.Resolve(rules.keywordAnimal);
        for (const auto& group : rules.groups) {
            const auto def = std::ranges::find_if(NpcSpells::kGroups, [&](const NpcSpells::GroupDef& d) { return d.name == group.name; });
            if (def == NpcSpells::kGroups.end()) {
                continue;  // ParseRules keeps built-in groups only; GameFiles logged the others
            }
            const auto groupIndex = static_cast<std::uint8_t>(def - NpcSpells::kGroups.begin());
            for (const auto& ref : group.factions) {
                if (const auto id = r.Resolve(ref)) {
                    out.factionGroup.emplace(*id, groupIndex);  // a faction in two groups keeps the first
                }
            }
        }
        if (!out.keywordNpc) {
            logger::warn("NPCSpells: rules: the NPC keyword is not resolved; NPCs are not judged creatures by its absence");
        }
        return out;
    }

    // Template chain (RE/T/TESActorBaseData.h:136). A link may be a TESLevCharacter, which has no actorData:
    // it is never read as an NPC. npcs[i] is chain[i]'s TESNPC, null for any other link.
    void WalkChain(RE::TESNPC* npc, std::vector<NpcSpells::ChainLink>& chain, std::vector<RE::TESNPC*>& npcs)
    {
        chain.clear();
        npcs.clear();
        RE::TESForm* cur = npc;
        for (std::size_t depth = 0; cur && depth < kMaxTemplateDepth; ++depth) {
            auto* n = cur->As<RE::TESNPC>();
            chain.push_back({ n ? n->actorData.templateUseFlags.underlying() : std::uint16_t{ 0 }, cur->Is(RE::TESLevCharacter::FORMTYPE) });
            npcs.push_back(n);
            if (!n) {
                break;
            }
            cur = n->baseTemplateForm;
        }
    }

    // The concrete NPC providing a category; the record itself when the chain cannot tell before spawn
    RE::TESNPC* Provider(RE::TESNPC* npc, const std::vector<NpcSpells::ChainLink>& chain, const std::vector<RE::TESNPC*>& npcs,
        Category category, bool& unresolvable)
    {
        const auto at = NpcSpells::ProviderIndex(chain, category);
        // ProviderIndex never stops on a leveled link; a link of another form type (malformed TPLT) has no NPC either
        if (at && *at < npcs.size() && npcs[*at]) {
            return npcs[*at];
        }
        unresolvable = true;
        return npc;
    }

    void ReadClass(const RE::TESNPC* provider, const ResolvedRules& rr, NpcSpells::NpcFacts& f)
    {
        const auto* cls = provider->npcClass;  // RE/T/TESNPC.h:272
        f.hasClass = cls != nullptr;
        if (!cls) {
            return;
        }
        // CLASS_DATA::SkillWeights: 18 contiguous uint8 in record order (RE/T/TESClass.h:36-59)
        std::memcpy(f.weights.w.data(), &cls->data.skillWeights, f.weights.w.size());
        f.isNecromancerClass = rr.necromancerClasses.contains(cls->GetFormID());
    }

    void ReadRace(const RE::TESRace* race, const ResolvedRules& rr, NpcSpells::NpcFacts& f)
    {
        // RE/B/BGSKeywordForm.h:36. An unresolved keyword's test is left out: without the NPC keyword every
        // race would read as a creature.
        const auto has = [race](const std::optional<RE::FormID>& kw) { return kw && race->HasKeywordID(*kw); };
        f.isBeast = rr.beastRaces.contains(race->GetFormID()) || has(rr.keywordAnimal);
        f.isCreature = has(rr.keywordCreature) || (rr.keywordNpc && !race->HasKeywordID(*rr.keywordNpc));
        f.isRedguard = rr.redguardRace && race->GetFormID() == *rr.redguardRace;
    }

    void ReadFactions(const RE::TESNPC* provider, const ResolvedRules& rr, NpcSpells::NpcFacts& f)
    {
        for (const auto& rank : provider->factions) {  // RE/T/TESActorBaseData.h:139, RE/F/FactionRank.h:11
            if (!rank.faction) {
                continue;
            }
            const auto it = rr.factionGroup.find(rank.faction->GetFormID());
            if (it != rr.factionGroup.end() && std::ranges::find(f.groupsMatched, it->second) == f.groupsMatched.end()) {
                f.groupsMatched.push_back(it->second);
            }
        }
    }
}

NpcSpellsGame::NpcStats NpcSpellsGame::AttachToNpcs(const NpcSpells::Rules& rules, const NpcSpells::ListIndex& index,
    const EslRecords& records, std::vector<NpcSpells::NpcLine>& lines)
{
    NpcStats stats;
    auto* dh = RE::TESDataHandler::GetSingleton();
    if (!dh) {
        return stats;
    }
    const ResolvedRules rr = ResolveRules(rules, stats.unresolvedRefs);

    std::vector<NpcSpells::ChainLink> chain;
    std::vector<RE::TESNPC*> npcs;
    std::vector<RE::TESLevSpell*> toAdd;
    for (auto* npc : dh->GetFormArray<RE::TESNPC>()) {  // RE/T/TESDataHandler.h:89
        if (!npc) {
            continue;
        }
        ++stats.scanned;
        if (npc->IsDeleted() || npc->IsIgnored()) {  // RE/T/TESForm.h:331, :336
            ++stats.skipped[std::string(kDeletedOrIgnored)];
            continue;
        }
        if (npc->IsPreset()) {  // character-creation face preset (RE/T/TESActorBaseData.h:105), never placed in the world
            ++stats.skipped[std::string(kFacePreset)];
            continue;
        }
        const auto* file = npc->GetFile(0);  // the plugin that defines the record
        if (!file) {
            ++stats.skipped[std::string(kNoSourceFile)];
            continue;
        }
        NpcSpells::NpcFacts f;
        f.pluginLower = TomeReach::LowerAscii(file->GetFilename());
        f.localId = npc->GetLocalFormID();
        f.isPlayer = npc->IsPlayer();

        WalkChain(npc, chain, npcs);
        f.inheritsSpellList = (chain[0].templateFlags & static_cast<std::uint16_t>(Category::Spells)) != 0;
        auto* classNpc = Provider(npc, chain, npcs, Category::Stats, f.unresolvableStats);
        auto* raceNpc = Provider(npc, chain, npcs, Category::Traits, f.unresolvableTraits);
        auto* factionNpc = Provider(npc, chain, npcs, Category::Factions, f.unresolvableFactions);
        if (f.unresolvableStats || f.unresolvableTraits || f.unresolvableFactions) {
            ++stats.unresolvableTemplate;
        }
        const auto* race = raceNpc->GetRace();  // RE/T/TESNPC.h:252
        if (!race) {
            ++stats.skipped[std::string(kNoRace)];
            continue;
        }
        ReadClass(classNpc, rr, f);
        ReadRace(race, rr, f);
        ReadFactions(factionNpc, rr, f);
        if (f.groupsMatched.size() > 1) {
            ++stats.multiGroup;
        }
        f.isUndeadException = rr.undeadExceptNpcs.contains(npc->GetFormID());

        auto d = NpcSpells::DecideNpc(f, rules, index);
        if (d.eligible) {
            ++stats.eligible;
        }
        if (d.skipReason) {
            ++stats.skipped[d.skipReason];
        }
        if (d.lists.empty()) {
            continue;
        }
        toAdd.clear();
        for (const auto& a : d.lists) {
            // ListIndex marks only (slot, chunk) pairs FillLists wrote, so the record is there
            if (auto* lev = records.byRecord[a.slotIndex * NpcSpells::kMaxChunks + a.chunk]) {
                toAdd.push_back(lev);
            }
        }
        if (toAdd.empty()) {
            continue;
        }
        // RE/T/TESSpellList.h:24: skips lists the NPC already has and copies the array once. No AddChange:
        // the lists are rebuilt at every data load and never written to the save.
        npc->GetSpellList()->AddLevSpells(toAdd);
        ++stats.attached;
        stats.listsAttached += toAdd.size();
        ++stats.byTier[static_cast<std::size_t>(d.tier)];
        ++stats.byGroup[d.group];
        // The game gives names in the system code page; the report is UTF-8 (src/EncodingUtils.cpp)
        const char* name = npc->GetName();
        lines.push_back({ PluginNames::MakePersistentId(file->fileName, f.localId),
            EncodingUtils::SanitizeToUTF8(std::string(name ? name : ""), PluginNames::kSystemCodePage), std::move(d) });
    }
    return stats;
}

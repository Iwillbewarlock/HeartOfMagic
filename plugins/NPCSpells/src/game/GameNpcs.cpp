#include "game/GameNpcs.h"

#include "core/NpcSpellsPlan.h"
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
    constexpr std::size_t kMaxSharedWarnings = 5;  // log lines for shared leveled-list arrays; every one is counted

    static_assert(sizeof(RE::CLASS_DATA::SkillWeights) == std::tuple_size_v<decltype(NpcSpells::ClassWeights::w)>);
    static_assert(std::tuple_size_v<decltype(NpcSpellsGame::NpcStats::byGroup)> == NpcSpells::kGroups.size());
    static_assert(std::tuple_size_v<decltype(NpcSpellsGame::NpcStats::byTier)> == NpcSpells::kClassTierCount);

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
    // it is never read as an NPC. npcs[i] is chain[i]'s TESNPC, null for any other link. A record met again (a template
    // cycle, malformed data) ends the walk: the last link then names a template the chain does not hold (hasTemplate), so
    // ProviderIndex reports a category it would take from there unresolvable.
    void WalkChain(RE::TESNPC* npc, std::vector<NpcSpells::ChainLink>& chain, std::vector<RE::TESNPC*>& npcs)
    {
        chain.clear();
        npcs.clear();
        RE::TESForm* cur = npc;
        for (std::size_t depth = 0; cur && depth < kMaxTemplateDepth; ++depth) {
            auto* n = cur->As<RE::TESNPC>();
            if (n && std::ranges::find(npcs, n) != npcs.end()) {
                break;
            }
            chain.push_back({ n ? n->actorData.templateUseFlags.underlying() : std::uint16_t{ 0 }, cur->Is(RE::TESLevCharacter::FORMTYPE),
                n && n->baseTemplateForm != nullptr });
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

    std::vector<RE::TESLevSpell*> ListsFor(const NpcSpells::NpcDecision& d, const NpcSpellsGame::EslRecords& records)
    {
        std::vector<RE::TESLevSpell*> out;
        for (const auto& a : d.lists) {
            // ListIndex marks only (slot, chunk) pairs FillLists wrote, so the record is there
            if (auto* lev = records.byRecord[a.slotIndex * NpcSpells::kMaxChunks + a.chunk]) {
                out.push_back(lev);
            }
        }
        return out;
    }

    // The game gives names in the system code page; the report is UTF-8 (src/EncodingUtils.cpp)
    NpcSpells::NpcLine MakeLine(const RE::TESNPC* npc, const RE::TESFile* file, NpcSpells::NpcDecision decision, std::string inheritedFrom = {})
    {
        const char* name = npc->GetName();
        return { PluginNames::MakePersistentId(file->fileName, npc->GetLocalFormID()),
            EncodingUtils::SanitizeToUTF8(std::string(name ? name : ""), PluginNames::kSystemCodePage), std::move(decision), std::move(inheritedFrom) };
    }

    // One record of the form array as pass 1 read it (its PlanRecord is at the same index)
    struct Scanned
    {
        RE::TESNPC* npc = nullptr;
        const RE::TESFile* file = nullptr;      // null: skipped before its decision, never written
        RE::TESNPC* holder = nullptr;           // inheriting: its spell list holder
        RE::TESNPC* sharesWith = nullptr;       // inheriting: the chain record whose spell list data it is
        std::size_t own = NpcSpells::kNoRecord; // decides for itself with lists: its index in the own decisions
    };

    struct OwnDecision
    {
        NpcSpells::NpcDecision decision;
        std::vector<RE::TESLevSpell*> toAdd;  // our lists only: what an inheriting NPC gets, never the holder's whole array
    };

    // The memory audit, over every NPC record of the form array before anything is written: AddLevSpells frees the record's
    // old leveled-list array (src/RE/T/TESSpellList.cpp:16-28 CopySpellList), so a write to a record whose array another
    // record's distinct spell list data (RE/T/TESSpellList.h:66 actorEffects, :44 levSpells) also points at would leave
    // that one pointing at freed memory. Spell list data shared as a whole is memory-safe (one struct, one array): every
    // record written through it adds its lists to that one struct, so all records sharing it get them all
    // (CountSharedSpellDataOutsideChain). Only NPC records are read: race spell lists (TESRace is a TESSpellList,
    // RE/T/TESRace.h:144) are assumed not to share a spell list data or an array with an NPC.
    struct ArrayOwner
    {
        const RE::TESSpellList::SpellData* first = nullptr;  // the first spell list data seen with this array
        bool shared = false;                                 // another, distinct one points at it too
    };
    using ArrayOwners = std::unordered_map<const RE::TESLevSpell* const*, ArrayOwner>;

    ArrayOwners MapLevArrays(RE::BSTArray<RE::TESNPC*>& forms)
    {
        ArrayOwners out;
        out.reserve(forms.size());
        for (const auto* npc : forms) {
            const auto* d = npc ? npc->actorEffects : nullptr;
            if (!d || !d->levSpells) {
                continue;
            }
            const auto [it, added] = out.try_emplace(d->levSpells, ArrayOwner{ d, false });
            if (!added && it->second.first != d) {
                it->second.shared = true;
            }
        }
        return out;
    }

    bool SharesLevArray(const RE::TESNPC* npc, const ArrayOwners& owners)
    {
        const auto* d = npc->actorEffects;
        if (!d || !d->levSpells) {
            return false;
        }
        const auto it = owners.find(d->levSpells);
        return it != owners.end() && it->second.shared;
    }

    // The record of the child's template chain (not the child, every link checked) whose spell list data the child is: the
    // holder when it is one of them, else the first. Null when none.
    RE::TESNPC* SpellDataOwnerInChain(const RE::TESNPC* child, const std::vector<RE::TESNPC*>& npcs, const RE::TESNPC* holder)
    {
        const auto* mine = child->actorEffects;
        RE::TESNPC* first = nullptr;
        for (std::size_t i = 1; mine && i < npcs.size(); ++i) {
            auto* other = npcs[i];
            if (!other || other == child || other->actorEffects != mine) {
                continue;
            }
            if (other == holder) {
                return other;
            }
            if (!first) {
                first = other;
            }
        }
        return first;
    }

    // Spell list data (one struct) two or more NPC records point at, not all on one template chain (expected 0). A record of
    // such a group explained by its own chain (a record of it shares the struct: a template child with its template's data)
    // is in-chain; a group with two or more records not explained that way is counted (two sibling children sharing a struct
    // their template does not have included). O(n) plus one template walk per record of a shared group.
    std::size_t CountSharedSpellDataOutsideChain(RE::BSTArray<RE::TESNPC*>& forms)
    {
        using SpellData = RE::TESSpellList::SpellData;
        std::unordered_map<const SpellData*, std::size_t> users;
        users.reserve(forms.size());
        for (const auto* npc : forms) {
            if (npc && npc->actorEffects) {
                ++users[npc->actorEffects];
            }
        }
        std::unordered_map<const SpellData*, std::size_t> unexplained;
        std::vector<NpcSpells::ChainLink> chain;
        std::vector<RE::TESNPC*> npcs;
        for (auto* npc : forms) {
            if (!npc || !npc->actorEffects || users.find(npc->actorEffects)->second < 2) {
                continue;
            }
            WalkChain(npc, chain, npcs);
            if (!SpellDataOwnerInChain(npc, npcs, nullptr)) {
                ++unexplained[npc->actorEffects];
            }
        }
        return static_cast<std::size_t>(std::ranges::count_if(unexplained, [](const auto& e) { return e.second > 1; }));
    }

    // Counts for an NPC that inherits its spell list (logged at kDataLoaded, read without playing). Returns its spell list
    // holder, null when the chain cannot tell it before spawn (a leveled link first, a malformed chain).
    RE::TESNPC* ReadInheriting(const RE::TESNPC* npc, const std::vector<NpcSpells::ChainLink>& chain, const std::vector<RE::TESNPC*>& npcs,
        const std::unordered_set<RE::FormID>& seen, NpcSpellsGame::NpcStats& stats)
    {
        ++stats.inheriting;
        using Flag = RE::ACTOR_BASE_DATA::TEMPLATE_USE_FLAG;  // RE/T/TESActorBaseData.h:46-62, the field at :74
        if (!npc->actorData.templateUseFlags.all(Flag::kCopiedTemplate)) {
            // WalkChain's leveled-link flag (TESActorBase::IsLeveled is never true for a LVLN in CommonLib)
            const bool leveled = std::ranges::any_of(chain, [](const NpcSpells::ChainLink& l) { return l.isLeveledCharacter; });
            ++(leveled ? stats.notCopiedLeveledChain : stats.notCopiedPlainChain);
        }
        const auto at = NpcSpells::SpellListProviderIndex(chain);
        if (!at || *at == 0 || *at >= npcs.size() || !npcs[*at]) {
            return nullptr;
        }
        auto* holder = npcs[*at];
        if (*at + 1 == chain.size() && (chain[*at].templateFlags & static_cast<std::uint16_t>(Category::Spells)) != 0) {
            ++stats.holderFlagNoTemplate;  // the chain's end has the Spells flag and no template: it keeps its own list (ProviderIndex)
        }
        if (!seen.contains(holder->GetFormID())) {
            ++stats.beforeHolder;  // GetFormArray is in load order, not template order
        }
        for (const auto category : { Category::Stats, Category::Traits, Category::Factions }) {
            if (NpcSpells::ProviderIndex(chain, category) != at) {
                ++stats.otherProvider;  // class, race or factions from another record: counted only, the lists come from the holder
                break;
            }
        }
        return holder;
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
    auto& forms = dh->GetFormArray<RE::TESNPC>();  // RE/T/TESDataHandler.h:89
    const ArrayOwners arrays = MapLevArrays(forms);
    stats.sharedSpellDataOutsideChain = CountSharedSpellDataOutsideChain(forms);

    // Pass 1 reads and decides every record; nothing is written until the plan (core/NpcSpellsPlan.h) is made
    using Flag = RE::ACTOR_BASE_DATA::TEMPLATE_USE_FLAG;  // RE/T/TESActorBaseData.h:46-62, the field at :74
    std::vector<NpcSpells::ChainLink> chain;
    std::vector<RE::TESNPC*> npcs;
    std::vector<Scanned> scanned;
    std::vector<NpcSpells::PlanRecord> plan;
    std::vector<OwnDecision> owns;
    std::unordered_map<const RE::TESNPC*, std::size_t> at;  // record -> its index in scanned and plan
    std::unordered_set<RE::FormID> seen;                    // records pass 1 has reached (for beforeHolder)
    scanned.reserve(forms.size());
    plan.reserve(forms.size());
    at.reserve(forms.size());
    for (auto* npc : forms) {
        if (!npc) {
            continue;
        }
        ++stats.scanned;
        seen.insert(npc->GetFormID());
        at.emplace(npc, scanned.size());
        auto& s = scanned.emplace_back();
        auto& pr = plan.emplace_back();
        s.npc = npc;
        pr.hasSpellData = npc->actorEffects != nullptr;
        pr.copiedTemplate = npc->actorData.templateUseFlags.all(Flag::kCopiedTemplate);
        pr.blocked = SharesLevArray(npc, arrays);
        stats.sharedLevSpells += pr.blocked ? 1 : 0;
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
        s.file = file;
        NpcSpells::NpcFacts f;
        f.pluginLower = TomeReach::LowerAscii(file->GetFilename());
        f.localId = npc->GetLocalFormID();
        f.isPlayer = npc->IsPlayer();

        WalkChain(npc, chain, npcs);
        // The Spells flag inherits only through a template link: with no TPLT (18 such NPCs in a 2026-10 load order, mod
        // records and a COTH-overridden vanilla mage) the record's own spell list is the one the game uses; the same for a
        // template at the chain's end (ProviderIndex makes it the holder)
        f.inheritsSpellList = (chain[0].templateFlags & static_cast<std::uint16_t>(Category::Spells)) != 0 && chain.size() > 1;
        auto* classNpc = Provider(npc, chain, npcs, Category::Stats, f.unresolvableStats);
        auto* raceNpc = Provider(npc, chain, npcs, Category::Traits, f.unresolvableTraits);
        const auto* race = raceNpc->GetRace();  // RE/T/TESNPC.h:252
        if (f.inheritsSpellList && !f.isPlayer) {
            // It takes its holder's decision (the plan); its own race and class only for the race rules. A chain that
            // cannot tell its holder before spawn is the plan's kSkipTemplateUnresolvable, not an NPC deciding for itself.
            pr.inheriting = true;
            s.holder = ReadInheriting(npc, chain, npcs, seen, stats);
            pr.holderResolved = s.holder != nullptr;
            if (!s.holder) {
                continue;
            }
            s.sharesWith = SpellDataOwnerInChain(npc, npcs, s.holder);
            stats.sharedSpellData += s.sharesWith ? 1 : 0;
            // Its own race rules only when its class and race are read from its own chain: a leveled link before its Stats or
            // Traits provider, or no race, leaves the holder's decision as it is (not the record's own class or race)
            if (race && !f.unresolvableStats && !f.unresolvableTraits) {
                ReadClass(classNpc, rr, f);
                ReadRace(race, rr, f);
                pr.ownSkip = NpcSpells::InheritedRaceSkip(f, rules);
            }
            continue;
        }
        auto* factionNpc = Provider(npc, chain, npcs, Category::Factions, f.unresolvableFactions);
        if (f.unresolvableStats || f.unresolvableTraits || f.unresolvableFactions) {
            ++stats.unresolvableTemplate;
        }
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
        auto toAdd = ListsFor(d, records);
        if (!toAdd.empty()) {
            pr.hasLists = true;
            s.own = owns.size();
            owns.push_back({ std::move(d), std::move(toAdd) });
        }
    }
    const auto indexOf = [&at](const RE::TESNPC* npc) {
        const auto it = npc ? at.find(npc) : at.end();
        return it != at.end() ? it->second : NpcSpells::kNoRecord;
    };
    for (std::size_t i = 0; i < scanned.size(); ++i) {
        plan[i].holder = indexOf(scanned[i].holder);
        plan[i].sharesSpellData = indexOf(scanned[i].sharesWith);
    }
    const auto steps = NpcSpells::PlanWrites(plan);

    // Pass 2: the NPCs that decide for themselves, in scan order. RE/T/TESSpellList.h:24 AddLevSpells skips lists the NPC
    // already has and copies the array once. No AddChange: the lists are rebuilt at every data load and never written to the save.
    std::vector<std::size_t> lineOf(scanned.size(), NpcSpells::kNoRecord);
    for (std::size_t i = 0; i < steps.size(); ++i) {
        const auto& step = steps[i];
        if (step.skipReason) {
            ++stats.skipped[step.skipReason];
        }
        if (step.blockedWrite && ++stats.blockedWrites <= kMaxSharedWarnings) {
            logger::warn("NPCSpells: {:08X} not given lists: its leveled-list array is shared with another record's spell list data",
                scanned[i].npc->GetFormID());
        }
        if (step.action != NpcSpells::PlanAction::kWriteOwn) {
            continue;
        }
        const auto& own = owns[scanned[i].own];
        scanned[i].npc->GetSpellList()->AddLevSpells(own.toAdd);  // RE/T/TESNPC.h:251, makes an empty one when the record has none
        ++stats.attached;
        stats.listsAttached += own.toAdd.size();
        ++stats.byTier[static_cast<std::size_t>(own.decision.tier)];
        ++stats.byGroup[own.decision.group];
        lineOf[i] = lines.size();
        lines.push_back(MakeLine(scanned[i].npc, scanned[i].file, own.decision));
    }

    // Pass 3: each inheriting NPC the plan writes gets its holder's lists (InheritDecision: the holder's decision, no dice of
    // its own); one sharing its spell list data with a written record already has them. byTier and byGroup stay the own decisions'.
    for (std::size_t i = 0; i < steps.size(); ++i) {
        const auto& step = steps[i];
        if (step.action == NpcSpells::PlanAction::kViaShared) {
            ++stats.inheritedViaShared;
            stats.viaSharedDespiteOwnSkip += step.despiteOwnSkip ? 1 : 0;  // its own race rule would have given it nothing
            continue;
        }
        if (step.action != NpcSpells::PlanAction::kWriteInherited && step.action != NpcSpells::PlanAction::kCreateInherited) {
            continue;
        }
        const auto h = step.listsFrom;
        const char* skipReason = nullptr;
        auto d = NpcSpells::InheritDecision(&lines[lineOf[h]].decision, skipReason);
        if (!d) {
            ++stats.skipped[skipReason];  // not reached: the plan writes only children of a written holder
            continue;
        }
        auto* npc = scanned[i].npc;
        const auto& toAdd = owns[scanned[h].own].toAdd;
        // kCreateInherited: kCopiedTemplate and no spell list data (the engine copied an empty holder): GetSpellList makes it
        auto* list = step.action == NpcSpells::PlanAction::kCreateInherited ? npc->GetSpellList() : npc->actorEffects;
        list->AddLevSpells(toAdd);
        ++stats.inheritedAttached;
        stats.inheritedCreated += step.action == NpcSpells::PlanAction::kCreateInherited ? 1 : 0;
        stats.inheritedLists += toAdd.size();
        std::string holderId = lines[lineOf[h]].persistentId;  // copied before the push may move the rows
        lines.push_back(MakeLine(npc, scanned[i].file, std::move(*d), std::move(holderId)));
    }
    return stats;
}

RE::TESNPC* NpcSpellsGame::SpellListHolder(RE::TESNPC* npc, bool& unresolvable)
{
    std::vector<NpcSpells::ChainLink> chain;
    std::vector<RE::TESNPC*> npcs;
    WalkChain(npc, chain, npcs);
    // The Spells flag with no template link keeps the record's own list, as AttachToNpcs reads it (inheritsSpellList)
    const auto at = NpcSpells::SpellListProviderIndex(chain);
    if (at && *at < npcs.size() && npcs[*at]) {
        return npcs[*at];
    }
    unresolvable = true;
    return npc;
}

std::vector<RE::TESNPC*> NpcSpellsGame::TemplateChain(RE::TESNPC* npc)
{
    std::vector<NpcSpells::ChainLink> chain;
    std::vector<RE::TESNPC*> npcs;
    WalkChain(npc, chain, npcs);
    std::erase(npcs, nullptr);
    return npcs;
}

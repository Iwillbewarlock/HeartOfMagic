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
    // An inheriting NPC without spell list data: no copy of its holder's list to add to (GetSpellList would make one holding
    // only our lists, in place of whatever the engine gives it)
    constexpr std::string_view kNoOwnSpellList = "template spell list, no spell list data of its own";
    constexpr std::string_view kSharedSpellData = "template spell list, spell list data shared with its template";
    constexpr std::string_view kSharedLevArray = "leveled-list array shared with a template chain record (not written)";
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

    // An NPC that decides for itself; written after the whole scan, once every memory audit is done
    struct HolderWrite
    {
        RE::TESNPC* npc;
        const RE::TESFile* file;
        NpcSpells::NpcDecision decision;
        std::vector<RE::TESLevSpell*> toAdd;  // our lists only: what an inheriting NPC gets, never the holder's whole array
        std::size_t line = 0;                 // its row in lines, once written
    };

    struct InheritingNpc
    {
        RE::TESNPC* npc;
        const RE::TESFile* file;
        RE::TESNPC* holder;
    };

    enum class Sharing { kNone, kSpellData, kLevSpells };

    // The memory audit of an NPC that inherits its spell list, against every NPC record of its template chain, before any
    // record is written: AddLevSpells frees the record's old array (src/RE/T/TESSpellList.cpp:16-28 CopySpellList), so a
    // write to one of two records sharing memory would leave the other pointing at freed memory. Spell list data shared
    // (RE/T/TESSpellList.h:65 actorEffects): the child is skipped, a write to the template reaches it. Only the leveled-list
    // array shared (:45 levSpells): neither record is written (both go into `blocked`).
    Sharing AuditChain(const RE::TESNPC* child, const std::vector<RE::TESNPC*>& npcs, std::unordered_set<const RE::TESNPC*>& blocked)
    {
        const auto* mine = child->actorEffects;
        auto out = Sharing::kNone;
        for (std::size_t i = 1; mine && i < npcs.size(); ++i) {
            const auto* theirs = npcs[i] ? npcs[i]->actorEffects : nullptr;
            if (!theirs) {
                continue;
            }
            if (theirs == mine) {
                return Sharing::kSpellData;
            }
            if (mine->levSpells && mine->levSpells == theirs->levSpells) {
                blocked.insert(child);
                blocked.insert(npcs[i]);
                out = Sharing::kLevSpells;
            }
        }
        return out;
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

    // Pass 1 reads and decides every record and audits the inheriting ones' memory; nothing is written until it ends
    std::vector<NpcSpells::ChainLink> chain;
    std::vector<RE::TESNPC*> npcs;
    std::vector<HolderWrite> holders;
    std::vector<InheritingNpc> inheriting;
    std::unordered_set<RE::FormID> seen;             // records pass 1 has reached (for beforeHolder)
    std::unordered_set<const RE::TESNPC*> blocked;   // share a leveled-list array with a chain record: never written
    for (auto* npc : dh->GetFormArray<RE::TESNPC>()) {  // RE/T/TESDataHandler.h:89
        if (!npc) {
            continue;
        }
        ++stats.scanned;
        seen.insert(npc->GetFormID());
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
        // The Spells flag inherits only through a template link: with no TPLT (18 such NPCs in a 2026-10 load order, mod
        // records and a COTH-overridden vanilla mage) the record's own spell list is the one the game uses
        f.inheritsSpellList = (chain[0].templateFlags & static_cast<std::uint16_t>(Category::Spells)) != 0 && chain.size() > 1;
        if (f.inheritsSpellList && !f.isPlayer) {
            // Its own facts are not read: it gets its holder's lists after pass 1. A holder that cannot be told before spawn
            // falls through to DecideNpc, which skips it (kSkipTemplateUnresolvable).
            auto* holder = ReadInheriting(npc, chain, npcs, seen, stats);
            const auto sharing = AuditChain(npc, npcs, blocked);
            if (sharing == Sharing::kSpellData) {
                ++stats.sharedSpellData;
            } else if (sharing == Sharing::kLevSpells && ++stats.sharedLevSpells <= kMaxSharedWarnings) {
                logger::warn("NPCSpells: {:08X} shares its leveled-list array with a record of its template chain: neither is given lists",
                    npc->GetFormID());
            }
            if (holder && sharing == Sharing::kSpellData) {
                ++stats.skipped[std::string(kSharedSpellData)];
                continue;
            }
            if (holder) {
                inheriting.push_back({ npc, file, holder });
                continue;
            }
        }
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
        auto toAdd = ListsFor(d, records);
        if (!toAdd.empty()) {
            holders.push_back({ npc, file, std::move(d), std::move(toAdd) });
        }
    }

    // Pass 2: the NPCs that decide for themselves. RE/T/TESSpellList.h:24 AddLevSpells skips lists the NPC already has and
    // copies the array once. No AddChange: the lists are rebuilt at every data load and never written to the save.
    std::unordered_map<RE::FormID, const HolderWrite*> written;
    for (auto& h : holders) {
        if (blocked.contains(h.npc)) {
            ++stats.skipped[std::string(kSharedLevArray)];
            continue;
        }
        h.npc->GetSpellList()->AddLevSpells(h.toAdd);  // RE/T/TESNPC.h:251, makes an empty one when the record has none
        ++stats.attached;
        stats.listsAttached += h.toAdd.size();
        ++stats.byTier[static_cast<std::size_t>(h.decision.tier)];
        ++stats.byGroup[h.decision.group];
        h.line = lines.size();
        lines.push_back(MakeLine(h.npc, h.file, h.decision));
        written.emplace(h.npc->GetFormID(), &h);
    }

    // Pass 3: each inheriting NPC gets its holder's lists (InheritDecision: the holder's decision, no dice of its own). A
    // holder that was skipped or got no list gives nothing. byTier and byGroup stay the own decisions'.
    for (const auto& [npc, file, holder] : inheriting) {
        if (blocked.contains(npc)) {
            ++stats.skipped[std::string(kSharedLevArray)];
            continue;
        }
        const auto it = written.find(holder->GetFormID());
        const HolderWrite* h = it != written.end() ? it->second : nullptr;
        const char* skipReason = nullptr;
        auto d = NpcSpells::InheritDecision(h ? &lines[h->line].decision : nullptr, skipReason);
        if (!d) {
            ++stats.skipped[skipReason];
            continue;
        }
        if (!npc->actorEffects) {
            ++stats.skipped[std::string(kNoOwnSpellList)];
            continue;
        }
        npc->actorEffects->AddLevSpells(h->toAdd);
        ++stats.inheritedAttached;
        stats.inheritedLists += h->toAdd.size();
        std::string holderId = lines[h->line].persistentId;  // copied before the push may move the rows
        lines.push_back(MakeLine(npc, file, std::move(*d), std::move(holderId)));
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

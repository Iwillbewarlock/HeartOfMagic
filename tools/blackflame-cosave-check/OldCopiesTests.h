#pragma once

// =============================================================================
// OLD COPIES TESTS - the old-copies cleanup's decision (plugins/BlackFlame/src/OldCopiesRule.h)
// =============================================================================
// The keep set from a ledger (published and previous objects, their deps, a TXST's texture strings) and from every kept key
// and this session's request keys followed through their progress lines; the folder name rule (and key names inside paths);
// the mirror name; the per-key plan over Data and staging; the budget; the grace; the root containment check; an empty keep
// set. Included by main.cpp; the file side is OldCopiesFilesTests.h.

#include "../../plugins/BlackFlame/src/OldCopiesRule.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace OldCopiesTests
{
    namespace Rule = BlackFlame::OldCopiesRule;
    namespace LM = BlackFlame::SlotLedgerModel;
    namespace CC = BlackFlame::CopyContract;

    inline constexpr std::uint64_t kShown = 0x1111111111111111ull;     // a slot's published model
    inline constexpr std::uint64_t kShownTex = 0x2222222222222222ull;  // its texture dep
    inline constexpr std::uint64_t kOlder = 0x3333333333333333ull;     // the slot's previous model
    inline constexpr std::uint64_t kOlderTex = 0xAAAAAAAAAAAAAAAAull;  // the previous model's own texture dep (another key)
    inline constexpr std::uint64_t kTxstTex = 0x4444444444444444ull;   // a TXST object's TX00 (bare string)
    inline constexpr std::uint64_t kAsked = 0x5555555555555555ull;     // this session's request key (a NIF)
    inline constexpr std::uint64_t kAskedTex = 0x6666666666666666ull;  // only a dep of the asked NIF's progress line
    inline constexpr std::uint64_t kAskedPal = 0x7777777777777777ull;  // a dep of that texture's own line (a palette)
    inline constexpr std::uint64_t kOrphan = 0x8888888888888888ull;    // an ok progress line nothing asks for
    inline constexpr std::uint64_t kStale = 0x9999999999999999ull;     // an old generation's folder, in no list at all
    inline constexpr std::uint64_t kShownPal = 0xBBBBBBBBBBBBBBBBull;  // a dep of the published texture's own line only

    inline std::string Mesh(std::uint64_t a_key) { return "meshes\\heartofmagic\\blackflame\\copies\\" + CC::Hex16(a_key) + "\\copy.nif"; }
    inline std::string Tex(std::uint64_t a_key) { return "textures\\heartofmagic\\blackflame\\copies\\" + CC::Hex16(a_key) + "\\copy.dds"; }

    inline LM::Object Model(std::uint64_t a_key, std::uint64_t a_texture)
    {
        LM::Object object;
        object.role = LM::Role::kModel;
        object.key = a_key;
        object.path = Mesh(a_key);
        object.deps.push_back({ false, 0, Tex(a_texture), 1 });
        object.deps.push_back({ true, 40012, "", 0 });
        return object;
    }

    inline LM::Ledger Ledger()
    {
        LM::Ledger ledger;
        LM::Entry entry;
        entry.slot = 0x800;
        entry.published.push_back(Model(kShown, kShownTex));
        entry.previous.push_back(Model(kOlder, kOlderTex));
        ledger.entries.push_back(entry);
        LM::Entry txst;
        txst.slot = 0xC00;
        LM::Object set;
        set.role = LM::Role::kTxst;
        set.key = 0xABCDEFull;  // a TXST object's own key (no folder of its own)
        set.textures = { "HeartOfMagic\\BlackFlame\\Copies\\" + CC::Hex16(kTxstTex) + "\\copy.dds", "", "textures\\other\\x.dds" };
        txst.published.push_back(set);
        ledger.entries.push_back(txst);
        return ledger;
    }

    inline CC::ProgressLine Line(CC::ProgressKind a_kind, std::uint64_t a_key, std::string a_path, std::vector<std::string> a_deps = {})
    {
        CC::ProgressLine line;
        line.kind = a_kind;
        line.key = a_key;
        line.path = std::move(a_path);
        line.bytes = 10;
        line.fnv64 = 1;
        for (auto& dep : a_deps) {
            line.deps.push_back({ false, 0, std::move(dep), 1 });
        }
        return line;
    }

    inline std::map<std::uint64_t, CC::ProgressLine> Progress()
    {
        std::map<std::uint64_t, CC::ProgressLine> lines;
        lines[kAsked] = Line(CC::ProgressKind::kNif, kAsked, Mesh(kAsked), { Tex(kAskedTex) });
        lines[kAskedTex] = Line(CC::ProgressKind::kDds, kAskedTex, Tex(kAskedTex), { Tex(kAskedPal) });
        lines[kOrphan] = Line(CC::ProgressKind::kNif, kOrphan, Mesh(kOrphan), { Tex(kStale) });
        lines[kShownTex] = Line(CC::ProgressKind::kDds, kShownTex, Tex(kShownTex), { Tex(kShownPal) });
        return lines;
    }

    // OldCopies.cpp AddClosure: seeded with every key the keep set holds (a_fromKeep) and the request
    inline Rule::KeySet Keep(const std::vector<std::uint64_t>& a_request, bool a_fromKeep = true)
    {
        Rule::KeySet keep;
        Rule::AddLedger(keep, Ledger());
        const auto lines = Progress();
        std::vector<std::uint64_t> seeds = a_request;
        if (a_fromKeep) {
            seeds.insert(seeds.end(), keep.begin(), keep.end());
        }
        Rule::AddRequestClosure(keep, seeds, [&](std::uint64_t a_key) -> std::optional<CC::ProgressLine> {
            const auto it = lines.find(a_key);
            return it == lines.end() ? std::nullopt : std::optional<CC::ProgressLine>(it->second);
        });
        return keep;
    }

    inline std::vector<std::string> Names(const std::vector<std::uint64_t>& a_keys)
    {
        std::vector<std::string> names;
        for (const auto key : a_keys) {
            names.push_back(CC::Hex16(key));
        }
        return names;
    }

    template <class Check>
    void Run(Check&& a_check)
    {
        const auto keep = Keep({ kAsked });
        const auto all = Names({ kShown, kShownTex, kOlder, kOlderTex, kTxstTex, kAsked, kAskedTex, kAskedPal, kShownPal, kOrphan, kStale });
        const auto plan = Rule::PlanKeys(all, {}, keep);
        a_check(plan.kept == 9 && plan.remove.size() == 2 && plan.remove[0].key == kOrphan && plan.remove[1].key == kStale &&
                    plan.remove[0].inData && !plan.remove[0].inStaging && plan.notOurs == 0 && !plan.refusedEmptyKeep,
            "old copies: the published model and its texture dep, the previous model and its own texture dep, a TXST's bare "
            "texture string, the request key and what its lines need are kept; an ok progress line nothing reaches and an old "
            "folder in no list are removed");

        a_check(keep.count(kOlder) == 1 && keep.count(kOlderTex) == 1 && Rule::PlanKeys(Names({ kOlderTex }), {}, keep).remove.empty(),
            "old copies: a previous object's texture dep with a key of its own is kept (a later start may show it)");

        a_check(keep.count(kAskedTex) == 1 && keep.count(kAskedPal) == 1 && keep.count(kOrphan) == 0 && keep.count(kStale) == 0,
            "old copies: a key only in progress is kept when this session's request reaches it (a dep of an asked line, and a dep "
            "of that dep's own line); a progress key nothing reaches is not (the builder's compaction keeps every staged line)");

        a_check(keep.count(kShownPal) == 1 && Keep({ kAsked }, false).count(kShownPal) == 0,
            "old copies: the closure is seeded with every kept key, not only the request (review round 1, Gemini): a ledger "
            "texture's own line's palette is kept");

        const auto unseen = Keep({ 0x0102030405060708ull });
        a_check(unseen.count(0x0102030405060708ull) == 1 && unseen.count(kAsked) == 0,
            "old copies: a request key with no progress line yet is kept (the builder may still write it)");

        const std::vector<std::string> odd = { "palettes", "_tint", CC::Hex16(kStale).substr(1), CC::Hex16(kStale) + "0",
            "0123456789ABCDEF", "0123456789abcdeg", "..", "", CC::Hex16(kStale) };
        const auto oddPlan = Rule::PlanKeys(odd, {}, keep);
        a_check(oddPlan.notOurs == odd.size() - 1 && oddPlan.remove.size() == 1 && oddPlan.remove[0].key == kStale &&
                    !Rule::FolderKey("0123456789ABCDEF") && Rule::FolderKey("0123456789abcdef") == 0x0123456789ABCDEFull,
            "old copies: a folder name that is not 16 lower-case hex digits (palettes, _tint, 15 or 17 digits, upper case, ..) "
            "is never touched");

        Rule::KeySet inPaths;
        const auto key = CC::Hex16(kStale);
        Rule::AddKeysOfText(inPaths, "meshes\\heartofmagic\\blackflame\\copies\\" + key + "0\\copy.nif");
        Rule::AddKeysOfText(inPaths, "textures/HeartOfMagic/BlackFlame/Copies/" + key.substr(1) + "/copy.dds");
        Rule::AddKeysOfText(inPaths, "meshes\\heartofmagic\\blackflame\\copies\\" + key + "f");
        const auto beforeGood = inPaths.size();
        Rule::AddKeysOfText(inPaths, "meshes/heartofmagic/blackflame/copies/" + CC::Hex16(kOrphan) + "/x.nif;copies\\" + key + "\\x");
        a_check(beforeGood == 0 && inPaths.size() == 1 && inPaths.count(kOrphan) == 1,
            "old copies: a 17- or 15-digit name inside a copy path is no key (the folder must end at the 16th digit); a real one "
            "is taken, and a bare 'copies\\' outside heartofmagic\\blackflame is not");

        a_check(Rule::IsMirrorName("HoMBF_slots.0123abcd.json") && !Rule::IsMirrorName("HoMBF_slots.0123ABCD.json") &&
                    !Rule::IsMirrorName("HoMBF_slots.0123abc.json") && !Rule::IsMirrorName("HoMBF_slots.0123abcde.json") &&
                    !Rule::IsMirrorName("HoMBF_slots.json") && !Rule::IsMirrorName("HoMBF_slots.1.json") &&
                    !Rule::IsMirrorName("HoMBF_slots.0123abcd.json.tmp") && !Rule::IsMirrorName("hombf_slots.0123abcd.json"),
            "old copies: a ledger mirror's name is HoMBF_slots.<8 lower-case hex>.json only (no backup, temporary file or case)");

        const auto both = Rule::PlanKeys(Names({ kStale, kShown }), Names({ kStale, kOrphan, kShown }), keep);
        a_check(both.remove.size() == 2 && both.remove[0].key == kOrphan && !both.remove[0].inData && both.remove[0].inStaging &&
                    both.remove[1].key == kStale && both.remove[1].inData && both.remove[1].inStaging && both.kept == 2,
            "old copies: a key with folders in Data and staging is one candidate with both (removed staging first); a staging-only "
            "key is one too; each kept folder is counted");

        const std::filesystem::path root = L"C:\\Game\\Data\\meshes\\heartofmagic\\blackflame\\copies";
        a_check(Rule::ContainedIn(root, root / CC::Hex16(kStale)) && !Rule::ContainedIn(root, root / L".." / CC::Hex16(kStale)) &&
                    !Rule::ContainedIn(root, root / L"." / CC::Hex16(kStale)) && !Rule::ContainedIn(root, root / L"a" / CC::Hex16(kStale)) &&
                    !Rule::ContainedIn(root, L"C:\\Other\\" + std::filesystem::path(CC::Hex16(kStale)).wstring()) &&
                    !Rule::ContainedIn(root, root) && !Rule::ContainedIn(root, root.parent_path()) &&
                    !Rule::ContainedIn(L"Data\\copies", std::filesystem::path(L"Data\\copies") / CC::Hex16(kStale)) &&
                    !Rule::ContainedIn(root, root / L"..\\..\\..\\..\\Windows"),
            "old copies: only a direct child of an absolute, normal root is removable ('..', '.', a deeper folder, another root, "
            "the root itself, a relative root refused)");

        const auto emptyPlan = Rule::PlanKeys(all, all, Rule::KeySet{});
        Rule::KeySet none;
        Rule::AddLedger(none, LM::Ledger{});
        a_check(emptyPlan.refusedEmptyKeep && emptyPlan.remove.empty() && emptyPlan.kept == 0 && none.empty() &&
                    !Rule::HasObjects(LM::Ledger{}) && Rule::HasObjects(Ledger()),
            "old copies: an empty keep set removes nothing (an empty ledger gives none and has no objects)");

        std::size_t budget = 3;
        const bool first = Rule::TakeBudget(budget, 2);
        const bool second = Rule::TakeBudget(budget, 2);
        const bool third = Rule::TakeBudget(budget, 1);
        a_check(first && !second && third && budget == 0,
            "old copies: a key's folders (staging and Data) are taken from the session's budget together or not at all");

        constexpr std::uint64_t kNow = 133000000000000000ull;
        constexpr auto kDay = Rule::kFileTimeTicksPerDay;
        a_check(Rule::WithinGrace(kNow - 13 * kDay, kNow, 14) && !Rule::WithinGrace(kNow - 15 * kDay, kNow, 14) &&
                    !Rule::WithinGrace(kNow - 14 * kDay, kNow, 14) && Rule::WithinGrace(kNow + kDay, kNow, 14) &&
                    !Rule::WithinGrace(kNow - 1, kNow, 0) && Rule::WithinGrace(kNow, kNow, 1) && kDay == 864000000000ull,
            "old copies: a key folder whose newest file is younger than oldCopiesGraceDays (13 of 14 days; one written after now) "
            "is kept; 14 or 15 days old, or a grace of 0, is not");
        a_check(!Rule::GraceDaysValid(0) && Rule::GraceDaysValid(1) && Rule::GraceDaysValid(14) && Rule::GraceDaysValid(3650) &&
                    !Rule::GraceDaysValid(3651) && Rule::kMinGraceDays == 1,
            "old copies: the config refuses oldCopiesGraceDays 0 (and over 3650), keeping its default: a grace of at least a day "
            "(review round 2, reviewer 3)");
    }
}

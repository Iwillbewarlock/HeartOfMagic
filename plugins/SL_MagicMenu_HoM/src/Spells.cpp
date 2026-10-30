// =============================================================================
// SPELLS - see Spells.h.
// =============================================================================

#include "Common.h"

#include "Spells.h"

#include "GameThread.h"
#include "Hom.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <format>
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <string_view>
#include <tuple>

namespace Spells
{
    namespace
    {
        // the tree is read straight from HoM's saved file
        constexpr const char* kTreePath = "Data/SKSE/Plugins/SpellLearning/spell_tree.json";

        std::optional<Data> g_data;

        // ---------------------------------------------------------------------
        // FormID resolution
        // ---------------------------------------------------------------------

        // Local ids a plugin can hold (HoM's SpellScannerFormId.cpp checks the same): a persistent id saved while
        // its plugin was a full one, read after the plugin was ESL-flagged, names no record of it.
        constexpr RE::FormID kLightLocalMask = 0xFFF, kFullLocalMask = 0xFFFFFF;
        constexpr std::uint8_t kNotLoaded = 0xFF;  // TESFile compile index of a plugin that is not in the load order

        struct Persistent
        {
            std::string_view plugin;
            RE::FormID local{};
        };

        // "Plugin.esp|0x000ABC" -> plugin and local id; nullopt when the text is no well-formed persistent id
        std::optional<Persistent> ParsePersistent(std::string_view a_text)
        {
            const auto bar = a_text.find('|');
            if (bar == std::string_view::npos || bar == 0) return std::nullopt;
            auto local = a_text.substr(bar + 1);
            if (local.starts_with("0x") || local.starts_with("0X")) local.remove_prefix(2);
            if (local.empty()) return std::nullopt;
            RE::FormID localId = 0;
            const auto [ptr, ec] = std::from_chars(local.data(), local.data() + local.size(), localId, 16);
            if (ec != std::errc{} || ptr != local.data() + local.size()) return std::nullopt;
            return Persistent{ a_text.substr(0, bar), localId };
        }

        enum class ResolvedBy
        {
            kNone,
            kPersistent,
            kSaved,
        };

        // HoM writes a persistent id's plugin name converted from the system code page to UTF-8
        // (spelllearning/include/PluginNames.h); the game's LookupModByName compares raw bytes. So, as HoM does
        // (SpellScannerFormId.cpp FindPlugin): the raw lookup first, then, for a non-ASCII name, the loaded files'
        // names converted the same way (compared ignoring ASCII case, like PluginNames::Lookup).
        constexpr std::uint32_t kAnsiCodePage = 0;      // CP_ACP
        constexpr std::uint32_t kUtf8CodePage = 65001;  // CP_UTF8 (a windows.h macro clashes with REX::W32::CP_UTF8)
        constexpr std::uint32_t kErrInvalidChars = 0x8; // MB_ERR_INVALID_CHARS (not in REX::W32)

        bool HasNonAscii(std::string_view a_text)
        {
            return std::ranges::any_of(a_text, [](char c) { return static_cast<unsigned char>(c) >= 0x80; });
        }

        // EncodingUtils::SanitizeToUTF8's rule: valid UTF-8 stays, anything else is read in the system code page
        std::string ToUtf8(std::string_view a_raw)
        {
            const auto length = static_cast<std::int32_t>(a_raw.size());
            if (length == 0 || REX::W32::MultiByteToWideChar(kUtf8CodePage, kErrInvalidChars, a_raw.data(), length, nullptr, 0) > 0) {
                return std::string(a_raw);
            }
            const auto wideLength = REX::W32::MultiByteToWideChar(kAnsiCodePage, 0, a_raw.data(), length, nullptr, 0);
            if (wideLength <= 0) return {};
            std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
            REX::W32::MultiByteToWideChar(kAnsiCodePage, 0, a_raw.data(), length, wide.data(), wideLength);
            return SKSE::stl::utf16_to_utf8(wide).value_or(std::string{});
        }

        bool EqualsIgnoreAsciiCase(std::string_view a, std::string_view b)
        {
            const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
            return a.size() == b.size() && std::ranges::equal(a, b, [&](char x, char y) { return lower(x) == lower(y); });
        }

        const RE::TESFile* IfLoaded(const RE::TESFile* a_file)
        {
            return a_file && a_file->GetCompileIndex() != kNotLoaded ? a_file : nullptr;
        }

        // Finds the loaded plugin a persistent id names; one per tree pass (the file list is converted once)
        class PluginFinder
        {
        public:
            const RE::TESFile* Find(RE::TESDataHandler& a_handler, std::string_view a_name)
            {
                if (const auto* file = IfLoaded(a_handler.LookupModByName(a_name))) return file;
                if (!HasNonAscii(a_name)) return nullptr;
                if (!m_ready) {
                    for (const auto* file : a_handler.files) {
                        if (IfLoaded(file) && HasNonAscii(file->GetFilename())) m_converted.emplace_back(ToUtf8(file->GetFilename()), file);
                    }
                    m_ready = true;
                }
                for (const auto& [name, file] : m_converted) {
                    if (EqualsIgnoreAsciiCase(name, a_name)) return file;
                }
                return nullptr;
            }

        private:
            std::vector<std::pair<std::string, const RE::TESFile*>> m_converted;
            bool m_ready = false;
        };

        // The runtime FormID from the file's own indexes (not LookupFormID, which looks the name up again by raw bytes)
        RE::FormID FormIdIn(const RE::TESFile& a_file, RE::FormID a_local)
        {
            constexpr RE::FormID kLightPrefix = 0xFE000000;
            constexpr int kLightFileShift = 12, kFileShift = 24;
            if (a_file.IsLight()) return kLightPrefix | (static_cast<RE::FormID>(a_file.GetSmallFileCompileIndex()) << kLightFileShift) | a_local;
            return (static_cast<RE::FormID>(a_file.GetCompileIndex()) << kFileShift) | a_local;
        }

        // HoM's rule (SpellScannerFormId.cpp JudgeNode): a well-formed persistent id wins, and when it does not
        // resolve the node stays unresolved (the saved number may name another plugin's record after a load order
        // change); only a node without one (or with a malformed one) falls back to its saved formId
        std::pair<RE::FormID, ResolvedBy> Resolve(const TreeData::Node& a_node, PluginFinder& a_plugins)
        {
            if (const auto persistent = ParsePersistent(a_node.persistentId)) {
                auto* handler = RE::TESDataHandler::GetSingleton();
                const auto* file = handler ? a_plugins.Find(*handler, persistent->plugin) : nullptr;
                if (!file) return { 0, ResolvedBy::kNone };
                if (persistent->local > (file->IsLight() ? kLightLocalMask : kFullLocalMask)) return { 0, ResolvedBy::kNone };
                const auto id = FormIdIn(*file, persistent->local);
                if (RE::TESForm::LookupByID<RE::SpellItem>(id)) return { id, ResolvedBy::kPersistent };
                return { 0, ResolvedBy::kNone };
            }
            if (a_node.formId && RE::TESForm::LookupByID<RE::SpellItem>(a_node.formId)) return { a_node.formId, ResolvedBy::kSaved };
            return { 0, ResolvedBy::kNone };
        }

        // ---------------------------------------------------------------------
        // HoM state snapshot
        // ---------------------------------------------------------------------

        std::mutex g_currentLock;
        std::shared_ptr<const Snapshot> g_current = std::make_shared<const Snapshot>();

        std::mutex g_pendingLock;
        std::set<std::string> g_pending;     // schools asked for since the last refresh ran
        bool g_queued = false;                // a refresh task waits (guarded by g_pendingLock)

        bool PlayerKnows(RE::FormID a_id)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(a_id);
            return player && spell && player->HasSpell(spell);
        }

        std::size_t Mix(std::size_t a_seed, std::size_t a_value)
        {
            return a_seed ^ (a_value + 0x9E3779B97F4A7C15ull + (a_seed << 6) + (a_seed >> 2));
        }

        // Entries are summed, so the hash does not depend on the maps' iteration order
        std::size_t ContentHash(const Snapshot& a_snapshot)
        {
            constexpr float kHundredths = 100.0f;
            std::size_t sum = 0;
            for (const auto& [name, school] : a_snapshot.schools) {
                std::size_t h = std::hash<std::string>{}(name);
                h = Mix(h, school.target);
                h = Mix(h, static_cast<std::size_t>(school.percent * kHundredths));
                h = Mix(h, static_cast<std::size_t>(school.required));
                sum += h;
            }
            for (const auto& [id, spell] : a_snapshot.spells) {
                std::size_t h = Mix(id, spell.mastered);
                h = Mix(h, static_cast<std::size_t>(spell.percent * kHundredths));
                h = Mix(h, static_cast<std::size_t>(spell.required));
                h = Mix(h, spell.known);
                sum += h;
            }
            return sum;
        }

        void AddSpell(Snapshot& a_snapshot, RE::FormID a_id)
        {
            if (!a_id || a_snapshot.spells.contains(a_id)) return;
            a_snapshot.spells.emplace(a_id,
                SpellState{ Hom::Mastered(a_id), Hom::ProgressPercent(a_id), Hom::RequiredXP(a_id), PlayerKnows(a_id) });
        }

        // On the main thread: HoM's state of every node of the school and of their prerequisites.
        // Simplification: the whole school each time (Conjuration ~600 nodes, three API calls and a HasSpell each),
        // asked for on changes and every 15 frames; upgrade path if it shows in a profile: only the window and the
        // learnable candidates.
        void AddSchool(Snapshot& a_snapshot, const std::string& a_name)
        {
            const auto* school = SchoolNamed(a_name);
            if (!school) return;
            const auto target = Hom::Target(a_name);
            a_snapshot.schools[a_name] = { target, Hom::ProgressPercent(target), Hom::RequiredXP(target) };
            AddSpell(a_snapshot, target);
            for (const int index : school->nodes) {
                const auto& node = g_data->tree.nodes[index];
                AddSpell(a_snapshot, g_data->gameIds[index]);
                for (const auto saved : node.hard) AddSpell(a_snapshot, FindSaved(saved, node.school).second);
                for (const auto saved : node.soft) AddSpell(a_snapshot, FindSaved(saved, node.school).second);
            }
        }

        void Refresh()
        {
            std::set<std::string> schools;
            {
                // drain and clear together: a request after this point finds the flag down and queues the next refresh
                const std::scoped_lock lock(g_pendingLock);
                schools.swap(g_pending);
                g_queued = false;
            }
            if (schools.empty() || !g_data) return;
            if (!Hom::Ready() || !GameThread::On()) {
                // the menu would say "loading" until its no-data timeout; the reason is logged once (review round 1 #3)
                static std::atomic<bool> s_logged{ false };
                if (!s_logged.exchange(true)) {
                    logger::warn("Spells: HoM's state not read - {} (thread {}, game thread {}); logged once",
                        !Hom::Ready() ? "its API has not arrived" : "the refresh task is not on the game thread", REX::W32::GetCurrentThreadId(),
                        GameThread::Id());
                }
                return;
            }
            auto next = std::make_shared<Snapshot>();
            for (const auto& school : schools) AddSchool(*next, school);
            if (next->schools.empty()) return;  // nothing read: the previous snapshot stays
            next->contentHash = ContentHash(*next);
            const std::scoped_lock lock(g_currentLock);
            if (next->contentHash == g_current->contentHash) return;
            next->epoch = g_current->epoch + 1;
            g_current = std::move(next);
        }

        bool PrereqsMet(const Snapshot& a_snapshot, const TreeData::Node& a_node)
        {
            // HoM's API IsSpellAvailableToLearn is false for any spell without a progress record
            // (ProgressionManagerXP.cpp) and the records are cleared on load, so availability is worked out here
            // from the tree's lists (normalised as HoM evaluates them, TreeData). Known gap against HoM's
            // AreTreePrerequisitesMet: reverse unlock (a later spell already known opening its prerequisites).
            if (a_node.hard.empty() && a_node.soft.empty()) return true;
            for (const auto saved : a_node.hard) {
                if (!PrereqMastered(a_snapshot, saved, a_node.school)) return false;
            }
            if (a_node.soft.empty()) return true;
            const auto mastered = std::ranges::count_if(a_node.soft, [&](std::uint32_t a_saved) {
                return PrereqMastered(a_snapshot, a_saved, a_node.school);
            });
            return mastered >= a_node.softNeeded;
        }
    }

    bool Load()
    {
        // spell_tree.json is read here directly
        std::string error;
        auto tree = TreeData::Load(kTreePath, &error);
        if (!tree) {
            logger::warn("Spells: no spell tree at {} ({}) - the tree window stays empty", kTreePath, error);
            return false;
        }
        Data data{ std::move(*tree), {}, {} };
        data.gameIds.reserve(data.tree.nodes.size());
        int unresolved = 0, byPersistent = 0, bySaved = 0;
        PluginFinder plugins;
        for (int i = 0; i < static_cast<int>(data.tree.nodes.size()); ++i) {
            const auto [id, by] = Resolve(data.tree.nodes[i], plugins);
            data.gameIds.push_back(id);
            if (id) data.byGameId[id].push_back(i);
            unresolved += by == ResolvedBy::kNone;
            byPersistent += by == ResolvedBy::kPersistent;
            bySaved += by == ResolvedBy::kSaved;
        }
        const auto& t = data.tree;
        logger::info("Spells: tree {} - schools {}, nodes {}, without x/y {}, structural edges {}, lock edges {}, unparsed ids {}, "
                     "FormIDs: {} by persistent id, {} by saved formId, {} unresolved",
            kTreePath, t.schools.size(), t.nodes.size(), t.nodesWithoutXY, t.structuralEdges, t.lockEdges, t.unparsedIds, byPersistent,
            bySaved, unresolved);
        g_data = std::move(data);
        return true;
    }

    const Data* Get()
    {
        return g_data ? &*g_data : nullptr;
    }

    bool IsDup(const TreeData::Node& a_node)
    {
        return a_node.id.find("_dup_") != std::string::npos;
    }

    int FindNode(RE::FormID a_id, const std::string& a_school)
    {
        if (!g_data || !a_id) return -1;
        const auto it = g_data->byGameId.find(a_id);
        if (it == g_data->byGameId.end() || it->second.empty()) return -1;
        const auto& nodes = g_data->tree.nodes;
        int schoolAny = -1, nonDup = -1;
        for (const int index : it->second) {
            const auto& node = nodes[index];
            const bool school = node.school == a_school;
            if (school && !IsDup(node)) return index;
            if (school && schoolAny < 0) schoolAny = index;
            if (!IsDup(node) && nonDup < 0) nonDup = index;
        }
        return schoolAny >= 0 ? schoolAny : nonDup >= 0 ? nonDup : it->second.front();
    }

    int PrimaryNode(RE::FormID a_id)
    {
        if (!g_data || !a_id) return -1;
        const auto it = g_data->byGameId.find(a_id);
        if (it == g_data->byGameId.end() || it->second.empty()) return -1;
        const auto nonDup = std::ranges::find_if(it->second, [](int a_index) { return !IsDup(g_data->tree.nodes[a_index]); });
        return nonDup != it->second.end() ? *nonDup : it->second.front();
    }

    std::pair<int, RE::FormID> FindSaved(std::uint32_t a_saved, const std::string& a_school)
    {
        if (!g_data) return { -1, 0 };
        const int index = TreeData::Find(g_data->tree, a_saved, a_school);
        if (index >= 0) return { index, g_data->gameIds[index] };
        return { -1, RE::TESForm::LookupByID<RE::SpellItem>(a_saved) ? a_saved : 0 };
    }

    const TreeData::School* SchoolNamed(const std::string& a_name)
    {
        if (!g_data) return nullptr;
        for (const auto& school : g_data->tree.schools) {
            if (school.name == a_name) return &school;
        }
        return nullptr;
    }

    std::string NodeName(int a_index)
    {
        const auto& node = g_data->tree.nodes[a_index];
        if (!node.name.empty()) return node.name;
        const auto* form = RE::TESForm::LookupByID(g_data->gameIds[a_index]);
        return form ? form->GetName() : std::format("{:08X}", node.formId);
    }

    const SpellState* Snapshot::Spell(RE::FormID a_id) const
    {
        const auto it = spells.find(a_id);
        return it != spells.end() ? &it->second : nullptr;
    }

    RE::FormID Snapshot::Target(const std::string& a_school) const
    {
        const auto it = schools.find(a_school);
        return it != schools.end() ? it->second.target : 0;
    }

    std::shared_ptr<const Snapshot> Current()
    {
        const std::scoped_lock lock(g_currentLock);
        return g_current;
    }

    void RequestRefresh(const std::vector<std::string>& a_schools)
    {
        {
            // the pending set and the queued flag change together (Refresh drains and clears under this lock too)
            const std::scoped_lock lock(g_pendingLock);
            for (const auto& school : a_schools) {
                if (!school.empty()) g_pending.insert(school);
            }
            if (g_pending.empty() || g_queued) return;
            g_queued = true;
        }
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            const std::scoped_lock lock(g_pendingLock);
            g_queued = false;
            logger::error("Spells: no SKSE task interface - HoM's state is not read");
            return;
        }
        tasks->AddTask([] { Refresh(); });
    }

    // Precedence: mastered > ready > learning target > known (shown as learned) > available > locked.
    // HoM's IsSpellMastered is already true at 100% progress, before the player has the spell
    // (ProgressionManagerTargets.cpp IsOwnSpellMastered). Approximation: 100% without the spell is "ready",
    // mastered is HoM's word for a spell that is known or not yet full.
    Look LookOf(const Snapshot& a_snapshot, int a_index)
    {
        const auto id = g_data->gameIds[a_index];
        const auto* spell = id ? a_snapshot.Spell(id) : nullptr;
        if (!spell) return {};
        const auto& node = g_data->tree.nodes[a_index];
        if (spell->mastered && (spell->percent < 100.0f || spell->known)) return { State::kMastered, 100.0f };
        if (spell->percent >= 100.0f && !spell->known) return { State::kReady, spell->percent };
        if (id == a_snapshot.Target(node.school)) return { State::kTarget, spell->percent };
        // known while HoM does not count it mastered: HoM's early-learned tracking (IsSpellMastered is true for a
        // known spell outside it), so HoM's prerequisite checks do not count it yet either; not drawn gold
        if (spell->known) return { State::kLearning, spell->percent };
        if (PrereqsMet(a_snapshot, node)) return { State::kAvailable, spell->percent };
        return { State::kLocked, spell->percent };
    }

    Prereq PrereqOf(const Snapshot& a_snapshot, std::uint32_t a_saved, const std::string& a_school)
    {
        const auto id = FindSaved(a_saved, a_school).second;
        if (!id) return Prereq::kNotInGame;
        const auto* spell = a_snapshot.Spell(id);
        if (!spell) return Prereq::kUnknown;
        return spell->mastered ? Prereq::kMastered : Prereq::kNotMastered;
    }

    bool PrereqMastered(const Snapshot& a_snapshot, std::uint32_t a_saved, const std::string& a_school)
    {
        return PrereqOf(a_snapshot, a_saved, a_school) == Prereq::kMastered;
    }

    std::vector<int> Learnable(const Snapshot& a_snapshot, const std::string& a_school)
    {
        std::vector<int> nodes;
        const auto* school = SchoolNamed(a_school);
        if (!school) return nodes;
        for (const int index : school->nodes) {
            if (!IsDup(g_data->tree.nodes[index]) && LookOf(a_snapshot, index).state == State::kAvailable) nodes.push_back(index);
        }
        const auto key = [](int a_index) {
            const auto& node = g_data->tree.nodes[a_index];
            return std::tie(node.tier, node.name, node.formId);
        };
        std::ranges::sort(nodes, [&](int a, int b) { return key(a) < key(b); });
        return nodes;
    }

    const char* StateText(State a_state)
    {
        switch (a_state) {
        case State::kMastered: return "Mastered";
        case State::kTarget: return "Learning now";
        case State::kReady: return "Ready to unlock";
        case State::kLearning: return "Learning (known early)";
        case State::kAvailable: return "Can learn";
        case State::kLocked: return "Locked";
        default: return "Unknown";
        }
    }
}

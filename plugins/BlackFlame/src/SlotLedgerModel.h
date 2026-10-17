#pragma once

// =============================================================================
// SLOT LEDGER MODEL - which source each pool slot holds for good, and what was last published in it (no game types)
// =============================================================================
// The ledger (HoMBF_slots.json, SlotLedger.h) keeps the slot pool's assignment across sessions, because saved references
// to a slot form must keep their meaning: an assigned slot keeps its source for good (a tombstone when the source leaves
// the closure, active again when it comes back), a slot is never given to another source, and a reservation is never
// discarded. This header is the logic, C++17 and nlohmann only, so tools/blackflame-cosave-check tests it offline:
//   - JSON form (format 1): epoch, version (bumped at each write), sessions, slots[] { slot, type, plugin | pluginHex,
//     localId, state active|tombstone|conflict, since, conflict, published[], parseFailures[], terminal[] }
//   - Merge: a backup, the mirror or a save's BFSL pairs added to a ledger - the union of reservations; one slot bound to
//     two different sources is a conflict (both named, the slot never published again: CopyOf keeps the source)
//   - Assign: a session's closure sources to slots - kept, re-activated, or the lowest never-used id of its pool
//   - Compare / ApplySave: a loaded save's BFSL pairs against the session's; the save's pairs win for the slots involved
// Plugin names compare ASCII case-insensitively (the P0 rule for BFSL names); ids are lower-case hex without 0x.

#include "CopyContract.h"
#include "SlotCoSave.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace BlackFlame::SlotLedgerModel
{
    namespace CC = CopyContract;
    using Json = nlohmann::json;

    inline constexpr std::uint32_t kFormat = 1;

    enum class State : std::uint8_t
    {
        kActive,     // bound and in this (or the last) session's closure
        kTombstone,  // bound, its source left the closure (filled from it while it resolves; never given to another)
        kConflict,   // two ledgers or a save bind it to different sources: never published again
    };

    enum class Role : std::uint8_t
    {
        kModel,   // a form's main model (PROJ, EXPL, IPCT, HAZD, ARTO)
        kMuzzle,  // a PROJ's muzzle flash model
        kMps,     // an ADDN slot's master particle system
        kTxst,    // a TXST slot's texture set (no file of its own; its textures are the deps)
    };

    struct Dep
    {
        bool isAddn = false;
        std::uint32_t addn = 0;  // isAddn: an ADDN slot index (40000+) the NIF names
        std::string path;        // else a Data-relative file (UTF-8, as the progress line had it)
        std::uint64_t fnv64 = 0;
        [[nodiscard]] bool operator==(const Dep& a_o) const
        {
            return isAddn == a_o.isAddn && addn == a_o.addn && path == a_o.path && fnv64 == a_o.fnv64;
        }
    };

    /// One verified, published object of a slot: the generation the slot showed last
    struct Object
    {
        Role role = Role::kModel;
        std::uint64_t key = 0;
        std::string path;  // the copy's Data-relative path ("" for kTxst)
        std::uint64_t bytes = 0;
        std::uint64_t fnv64 = 0;
        std::vector<Dep> deps;
    };

    struct ParseFailures
    {
        std::uint32_t count = 0;        // sessions whose verification of this key failed to parse
        std::uint32_t lastSession = 0;  // the session counted last (one count a session)
    };

    struct Entry
    {
        std::uint16_t slot = 0;
        CC::SlotType type = CC::SlotType::kNone;
        std::string plugin;  // the source's plugin file name (its bytes)
        std::uint32_t localId = 0;
        State state = State::kActive;
        std::uint32_t since = 0;    // the session it was assigned in
        std::string conflict;       // kConflict: the other binding, "plugin|localId"
        std::vector<Object> published;  // at most one per role
        std::map<std::uint64_t, ParseFailures> parseFailures;
        std::set<std::uint64_t> terminal;  // keys terminal "parse" (CopyContract::kLedgerParseState)
    };

    struct Ledger
    {
        std::uint64_t epoch = 0;
        std::uint32_t version = 0;   // bumped at each write
        std::uint32_t sessions = 0;  // bumped at each start that loads it
        std::vector<Entry> entries;  // by slot, one entry per slot
    };

    [[nodiscard]] inline const char* StateText(State a_state) noexcept
    {
        return a_state == State::kTombstone ? "tombstone" : a_state == State::kConflict ? "conflict" : "active";
    }

    [[nodiscard]] inline const char* RoleText(Role a_role) noexcept
    {
        switch (a_role) {
            case Role::kMuzzle: return "muzzle";
            case Role::kMps: return "mps";
            case Role::kTxst: return "txst";
            case Role::kModel: break;
        }
        return "model";
    }

    [[nodiscard]] inline bool SamePlugin(std::string_view a_lhs, std::string_view a_rhs) noexcept
    {
        if (a_lhs.size() != a_rhs.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a_lhs.size(); ++i) {
            const auto l = static_cast<unsigned char>(a_lhs[i]);
            const auto r = static_cast<unsigned char>(a_rhs[i]);
            const auto fold = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? static_cast<unsigned char>(c - 'A' + 'a') : c; };
            if (fold(l) != fold(r)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] inline bool SameSource(const Entry& a_entry, CC::SlotType a_type, std::string_view a_plugin, std::uint32_t a_localId)
    {
        return a_entry.type == a_type && a_entry.localId == a_localId && SamePlugin(a_entry.plugin, a_plugin);
    }

    [[nodiscard]] inline std::string HexId(std::uint32_t a_value)
    {
        return CC::Detail::HexId(a_value);
    }

    /// "plugin|localId" for logs (the plugin's bytes as they are)
    [[nodiscard]] inline std::string SourceText(std::string_view a_plugin, std::uint32_t a_localId)
    {
        return std::string(a_plugin) + "|" + HexId(a_localId);
    }

    [[nodiscard]] inline Entry* FindSlot(Ledger& a_ledger, std::uint16_t a_slot)
    {
        const auto it = std::lower_bound(a_ledger.entries.begin(), a_ledger.entries.end(), a_slot,
            [](const Entry& a_e, std::uint16_t a_s) { return a_e.slot < a_s; });
        return it != a_ledger.entries.end() && it->slot == a_slot ? &*it : nullptr;
    }

    [[nodiscard]] inline const Entry* FindSlot(const Ledger& a_ledger, std::uint16_t a_slot)
    {
        return FindSlot(const_cast<Ledger&>(a_ledger), a_slot);
    }

    /// Inserts a_entry in slot order (the caller checked the slot is free)
    inline Entry& Insert(Ledger& a_ledger, Entry a_entry)
    {
        const auto it = std::lower_bound(a_ledger.entries.begin(), a_ledger.entries.end(), a_entry.slot,
            [](const Entry& a_e, std::uint16_t a_s) { return a_e.slot < a_s; });
        return *a_ledger.entries.insert(it, std::move(a_entry));
    }

    [[nodiscard]] inline const Object* PublishedOf(const Entry& a_entry, Role a_role)
    {
        for (const auto& object : a_entry.published) {
            if (object.role == a_role) {
                return &object;
            }
        }
        return nullptr;
    }

    /// Replaces the slot's published object of a_object's role
    inline void SetPublished(Entry& a_entry, Object a_object)
    {
        auto& objects = a_entry.published;
        objects.erase(std::remove_if(objects.begin(), objects.end(), [&](const Object& a_o) { return a_o.role == a_object.role; }),
            objects.end());
        objects.push_back(std::move(a_object));
    }

    /// One failed parse of a_key this session (counted once a session); true when it became terminal
    inline bool NoteParseFailure(Entry& a_entry, std::uint64_t a_key, std::uint32_t a_session)
    {
        auto& failures = a_entry.parseFailures[a_key];
        if (failures.lastSession != a_session) {
            failures.lastSession = a_session;
            ++failures.count;
        }
        if (failures.count >= static_cast<std::uint32_t>(CC::kParseFailuresTerminal)) {
            return a_entry.terminal.insert(a_key).second;
        }
        return false;
    }

    // =========================================================================
    // Assignment
    // =========================================================================

    struct Want
    {
        CC::SlotType type = CC::SlotType::kNone;
        std::string plugin;
        std::uint32_t localId = 0;
        bool txstDodt = false;  // TXST: the source has a DODT, so only a slot with the DODT stub fits (CopyContract TxstHasDodt)
    };

    /// A TXST slot fits a want only when its DODT stub matches the source's DODT; every other type fits any slot of its pool
    [[nodiscard]] inline bool SlotFits(std::uint16_t a_slot, const Want& a_want) noexcept
    {
        return a_want.type != CC::SlotType::kTXST || CC::TxstHasDodt(a_slot) == a_want.txstDodt;
    }

    struct AssignResult
    {
        std::vector<std::optional<std::uint16_t>> slots;  // per want, in order; nullopt: no slot (pool full, new refused)
        std::vector<std::size_t> poolFull;                // wants whose type pool had no never-used id left
        std::vector<std::size_t> refused;                 // wants that needed a new slot while new ones are not allowed
        std::size_t kept = 0;
        std::size_t reactivated = 0;
        std::size_t assigned = 0;
        std::size_t tombstoned = 0;  // active entries whose source is not wanted this session
        bool changed = false;        // the ledger differs (save it)
    };

    /// a_wants (each source once) get their slot: the source's active slot (lowest when a merge left two; the other
    /// becomes a tombstone), else its tombstone back to active, else (only with a_allowNew) the lowest local id of its
    /// type's pool that no entry holds in any state. Every other active entry becomes a tombstone. Conflict entries are
    /// never given out; a TXST slot whose DODT stub no longer matches its source's DODT is not kept (SlotFits: the source
    /// takes a fitting slot, the old one stays a reserved tombstone). a_session: the session number for new entries.
    [[nodiscard]] inline AssignResult Assign(Ledger& a_ledger, const std::vector<Want>& a_wants, bool a_allowNew,
        std::uint32_t a_session)
    {
        AssignResult out;
        out.slots.resize(a_wants.size());
        std::set<std::uint16_t> used;  // slots a want took this session
        for (std::size_t i = 0; i < a_wants.size(); ++i) {
            const auto& want = a_wants[i];
            Entry* best = nullptr;
            for (auto& entry : a_ledger.entries) {
                if (entry.state == State::kConflict || !SameSource(entry, want.type, want.plugin, want.localId) ||
                    used.count(entry.slot) != 0 || !SlotFits(entry.slot, want)) {
                    continue;
                }
                const bool better = !best || (entry.state == State::kActive && best->state != State::kActive);
                if (better) {
                    best = &entry;  // entries are in slot order: the first of a state is the lowest
                }
            }
            if (best) {
                if (best->state == State::kActive) {
                    ++out.kept;
                } else {
                    best->state = State::kActive;
                    ++out.reactivated;
                    out.changed = true;
                }
                used.insert(best->slot);
                out.slots[i] = best->slot;
                continue;
            }
            const auto pool = CC::PoolOfType(want.type);
            if (!pool) {
                out.poolFull.push_back(i);
                continue;
            }
            if (!a_allowNew) {
                out.refused.push_back(i);
                continue;
            }
            std::optional<std::uint16_t> free;
            for (std::uint32_t id = pool->first; id < static_cast<std::uint32_t>(pool->first) + pool->count; ++id) {
                if (!FindSlot(a_ledger, static_cast<std::uint16_t>(id)) && SlotFits(static_cast<std::uint16_t>(id), want)) {
                    free = static_cast<std::uint16_t>(id);
                    break;
                }
            }
            if (!free) {
                out.poolFull.push_back(i);
                continue;
            }
            Entry entry;
            entry.slot = *free;
            entry.type = want.type;
            entry.plugin = want.plugin;
            entry.localId = want.localId;
            entry.state = State::kActive;
            entry.since = a_session;
            Insert(a_ledger, std::move(entry));
            used.insert(*free);
            out.slots[i] = *free;
            ++out.assigned;
            out.changed = true;
        }
        for (auto& entry : a_ledger.entries) {
            if (entry.state == State::kActive && used.count(entry.slot) == 0) {
                entry.state = State::kTombstone;
                ++out.tombstoned;
                out.changed = true;
            }
        }
        return out;
    }

    // =========================================================================
    // Merge (backups, the mirror, saves): the union of reservations
    // =========================================================================

    /// a_other's reservations added to a_into. A slot a_into lacks is added (a_other's state kept, published data kept);
    /// the same slot with the same source keeps a_into's entry (its terminal keys and parse counts joined); the same slot
    /// with another source becomes kConflict (a line naming both in a_conflicts). Returns how many slots were added.
    inline std::size_t Merge(Ledger& a_into, const Ledger& a_other, std::vector<std::string>& a_conflicts, std::string_view a_what)
    {
        std::size_t added = 0;
        for (const auto& other : a_other.entries) {
            Entry* mine = FindSlot(a_into, other.slot);
            if (!mine) {
                Insert(a_into, other);
                ++added;
                continue;
            }
            if (SameSource(*mine, other.type, other.plugin, other.localId)) {
                mine->terminal.insert(other.terminal.begin(), other.terminal.end());
                for (const auto& [key, failures] : other.parseFailures) {
                    auto& own = mine->parseFailures[key];
                    own.count = (std::max)(own.count, failures.count);
                    own.lastSession = (std::max)(own.lastSession, failures.lastSession);
                }
                continue;
            }
            if (mine->state != State::kConflict) {
                mine->state = State::kConflict;
                mine->conflict = SourceText(other.plugin, other.localId);
                mine->published.clear();
            }
            a_conflicts.push_back("slot " + HexId(other.slot) + " (" + CC::SlotTypeTag(other.type).data() + ") is " +
                                  SourceText(mine->plugin, mine->localId) + " here and " + SourceText(other.plugin, other.localId) +
                                  " in " + std::string(a_what) + ": refused, the slot is never published again");
        }
        return added;
    }

    /// A save's BFSL pairs as a ledger (every slot a tombstone: whether it is wanted is the next Assign's call)
    [[nodiscard]] inline Ledger FromSnapshot(const SlotCoSave::Snapshot& a_snapshot)
    {
        Ledger ledger;
        ledger.epoch = a_snapshot.epoch;
        ledger.version = a_snapshot.ledgerVersion;
        for (const auto& slot : a_snapshot.slots) {
            if (FindSlot(ledger, slot.slot)) {
                continue;  // Validate already refuses a slot twice
            }
            Entry entry;
            entry.slot = slot.slot;
            entry.type = slot.type;
            entry.plugin = slot.plugin;
            entry.localId = slot.sourceLocalId;
            entry.state = State::kTombstone;
            Insert(ledger, std::move(entry));
        }
        return ledger;
    }

    /// The pairs a save made now holds: every entry's binding (a conflict slot is filled from its own binding too)
    [[nodiscard]] inline SlotCoSave::Snapshot ToSnapshot(const Ledger& a_ledger)
    {
        SlotCoSave::Snapshot snapshot;
        snapshot.epoch = a_ledger.epoch;
        snapshot.ledgerVersion = a_ledger.version;
        for (const auto& entry : a_ledger.entries) {
            snapshot.slots.push_back({ entry.slot, entry.type, entry.plugin, entry.localId });
        }
        return snapshot;
    }

    struct Mismatch
    {
        std::uint16_t slot = 0;
        SlotCoSave::Slot save;     // what the save holds
        bool unknown = false;      // the session has no entry for the slot (true) or another binding (false)
        std::string session;       // the session's binding, "" when unknown
    };

    /// Each slot of a_save whose pair differs from a_session's (or that a_session does not hold)
    [[nodiscard]] inline std::vector<Mismatch> Compare(const Ledger& a_session, const SlotCoSave::Snapshot& a_save)
    {
        std::vector<Mismatch> out;
        for (const auto& slot : a_save.slots) {
            const Entry* entry = FindSlot(a_session, slot.slot);
            if (!entry) {
                out.push_back({ slot.slot, slot, true, "" });
            } else if (!SameSource(*entry, slot.type, slot.plugin, slot.sourceLocalId)) {
                out.push_back({ slot.slot, slot, false, SourceText(entry->plugin, entry->localId) });
            }
        }
        return out;
    }

    /// The save's pairs win for a_mismatches' slots: the slot takes the save's source as a tombstone (its published
    /// objects and parse state dropped: they were another source's), a slot the ledger lacked is added the same way.
    /// The current source of such a slot gets a new slot at the next Assign
    inline void ApplySave(Ledger& a_ledger, const std::vector<Mismatch>& a_mismatches, std::uint32_t a_session)
    {
        for (const auto& mismatch : a_mismatches) {
            Entry* entry = FindSlot(a_ledger, mismatch.slot);
            if (!entry) {
                Entry added;
                added.slot = mismatch.slot;
                entry = &Insert(a_ledger, std::move(added));
            }
            entry->type = mismatch.save.type;
            entry->plugin = mismatch.save.plugin;
            entry->localId = mismatch.save.sourceLocalId;
            entry->state = State::kTombstone;
            entry->since = a_session;
            entry->conflict.clear();
            entry->published.clear();
            entry->parseFailures.clear();
            entry->terminal.clear();
        }
    }
}

#include "SlotLedgerJson.h"

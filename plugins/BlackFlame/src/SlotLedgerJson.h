#pragma once

// =============================================================================
// SLOT LEDGER JSON - HoMBF_slots.json's text (included by SlotLedgerModel.h)
// =============================================================================
// { "format": 1, "epoch": "<16 hex>", "version": n, "sessions": n, "slots": [
//     { "slot": "800", "type": "PROJ", "plugin": "Skyrim.esm" | "pluginHex": "<bytes as hex>", "localId": "10fbed",
//       "state": "active|tombstone|conflict", "since": n, ["conflict": "plugin|id"],
//       "published": [ { "role": "model|muzzle|mps|txst", "key": "<16 hex>", "path": "...", "bytes": n, "fnv64": "<16 hex>",
//                        "deps": [ { "path": "...", "fnv64": "<16 hex>" } | { "addn": 40012 } ] } ],
//       "parseFailures": [ { "key": "<16 hex>", "count": n, "session": n } ], "terminal": [ "<16 hex>" ] } ] }
// A plugin name with a byte outside printable ASCII is written as pluginHex (JSON text must be UTF-8; the engine's names are
// the system code page's bytes). FromJson is strict: an unknown format, a slot outside its type's pool, a slot twice, or a
// field of the wrong kind refuses the whole file (the caller then reads a backup or the mirror).

#include "SlotLedgerModel.h"

#include <string>

namespace BlackFlame::SlotLedgerModel
{
    namespace JsonDetail
    {
        inline constexpr unsigned char kFirstPrintable = 0x20;
        inline constexpr unsigned char kLastPrintable = 0x7E;

        [[nodiscard]] inline bool PrintableAscii(std::string_view a_text) noexcept
        {
            return std::all_of(a_text.begin(), a_text.end(), [](char c) {
                const auto byte = static_cast<unsigned char>(c);
                return byte >= kFirstPrintable && byte <= kLastPrintable;
            });
        }

        [[nodiscard]] inline std::optional<std::uint32_t> ParseHexId(const Json& a_value, std::uint32_t a_max)
        {
            if (!a_value.is_string()) {
                return std::nullopt;
            }
            const auto& text = a_value.get_ref<const std::string&>();
            if (text.empty() || text.size() > 8) {
                return std::nullopt;
            }
            std::uint32_t value = 0;
            for (const char c : text) {
                int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
                if (digit < 0) {
                    return std::nullopt;
                }
                value = (value << 4) | static_cast<std::uint32_t>(digit);
            }
            return value <= a_max ? std::optional<std::uint32_t>(value) : std::nullopt;
        }

        [[nodiscard]] inline std::optional<std::uint64_t> Key(const Json& a_value)
        {
            return a_value.is_string() ? CC::ParseHex16(a_value.get_ref<const std::string&>()) : std::nullopt;
        }

        [[nodiscard]] inline std::optional<std::uint32_t> Count(const Json& a_object, const char* a_name)
        {
            const auto it = a_object.find(a_name);
            if (it == a_object.end() || !it->is_number_unsigned() || it->get<std::uint64_t>() > UINT32_MAX) {
                return std::nullopt;
            }
            return static_cast<std::uint32_t>(it->get<std::uint64_t>());
        }

        [[nodiscard]] inline std::optional<CC::SlotType> TypeOf(const Json& a_value)
        {
            if (!a_value.is_string()) {
                return std::nullopt;
            }
            for (const auto& pool : CC::kSlotPools) {
                if (CC::SlotTypeTag(pool.type) == a_value.get_ref<const std::string&>()) {
                    return pool.type;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] inline Json DepsJson(const std::vector<Dep>& a_deps)
        {
            Json deps = Json::array();
            for (const auto& dep : a_deps) {
                deps.push_back(dep.isAddn ? Json{ { "addn", dep.addn } } : Json{ { "path", dep.path }, { "fnv64", CC::Hex16(dep.fnv64) } });
            }
            return deps;
        }

        [[nodiscard]] inline bool ReadDeps(const Json& a_value, std::vector<Dep>& a_out)
        {
            if (!a_value.is_array()) {
                return false;
            }
            for (const auto& item : a_value) {
                Dep dep;
                if (!item.is_object()) {
                    return false;
                }
                if (const auto addn = item.find("addn"); addn != item.end()) {
                    if (!addn->is_number_unsigned() || !CC::SlotOfAddnIndex(static_cast<std::uint32_t>(addn->get<std::uint64_t>()))) {
                        return false;
                    }
                    dep.isAddn = true;
                    dep.addn = static_cast<std::uint32_t>(addn->get<std::uint64_t>());
                } else {
                    const auto path = item.find("path");
                    const auto fnv = item.find("fnv64");
                    if (path == item.end() || !path->is_string() || fnv == item.end() || !Key(*fnv)) {
                        return false;
                    }
                    dep.path = path->get<std::string>();
                    dep.fnv64 = *Key(*fnv);
                }
                a_out.push_back(std::move(dep));
            }
            return true;
        }
    }

    [[nodiscard]] inline Json ToJson(const Ledger& a_ledger)
    {
        using namespace JsonDetail;
        Json root;
        root["format"] = kFormat;
        root["epoch"] = CC::Hex16(a_ledger.epoch);
        root["version"] = a_ledger.version;
        root["sessions"] = a_ledger.sessions;
        root["slots"] = Json::array();
        for (const auto& entry : a_ledger.entries) {
            Json item;
            item["slot"] = HexId(entry.slot);
            item["type"] = std::string(CC::SlotTypeTag(entry.type));
            if (PrintableAscii(entry.plugin)) {
                item["plugin"] = entry.plugin;
            } else {
                item["pluginHex"] = CC::LowerHex(entry.plugin);
            }
            item["localId"] = HexId(entry.localId);
            item["state"] = StateText(entry.state);
            item["since"] = entry.since;
            if (!entry.conflict.empty()) {
                item["conflict"] = PrintableAscii(entry.conflict) ? entry.conflict : CC::LowerHex(entry.conflict);
            }
            item["published"] = Json::array();
            for (const auto& object : entry.published) {
                item["published"].push_back({ { "role", RoleText(object.role) }, { "key", CC::Hex16(object.key) },
                    { "path", object.path }, { "bytes", object.bytes }, { "fnv64", CC::Hex16(object.fnv64) },
                    { "deps", DepsJson(object.deps) } });
            }
            item["parseFailures"] = Json::array();
            for (const auto& [key, failures] : entry.parseFailures) {
                item["parseFailures"].push_back({ { "key", CC::Hex16(key) }, { "count", failures.count }, { "session", failures.lastSession } });
            }
            item["terminal"] = Json::array();
            for (const auto key : entry.terminal) {
                item["terminal"].push_back(CC::Hex16(key));
            }
            root["slots"].push_back(std::move(item));
        }
        return root;
    }

    [[nodiscard]] inline std::optional<Ledger> FromJson(const Json& a_root, std::string& a_why)
    {
        using namespace JsonDetail;
        const auto refuse = [&](std::string a_text) -> std::optional<Ledger> {
            a_why = std::move(a_text);
            return std::nullopt;
        };
        if (!a_root.is_object() || Count(a_root, "format") != kFormat) {
            return refuse("not a format 1 ledger");
        }
        Ledger ledger;
        const auto epoch = a_root.contains("epoch") ? Key(a_root["epoch"]) : std::nullopt;
        const auto version = Count(a_root, "version");
        const auto sessions = Count(a_root, "sessions");
        if (!epoch || !version || !sessions || !a_root.contains("slots") || !a_root["slots"].is_array()) {
            return refuse("epoch, version, sessions or slots missing or of the wrong kind");
        }
        ledger.epoch = *epoch;
        ledger.version = *version;
        ledger.sessions = *sessions;
        for (const auto& item : a_root["slots"]) {
            if (!item.is_object()) {
                return refuse("a slot entry is not an object");
            }
            Entry entry;
            const auto slot = item.contains("slot") ? ParseHexId(item["slot"], CC::kLastEslLocalId) : std::nullopt;
            const auto type = item.contains("type") ? TypeOf(item["type"]) : std::nullopt;
            const auto local = item.contains("localId") ? ParseHexId(item["localId"], SlotCoSave::kMaxSourceLocalId) : std::nullopt;
            const auto pool = slot ? CC::PoolOf(*slot) : std::nullopt;
            if (!slot || !type || !local || !pool || pool->type != *type) {
                return refuse("a slot entry's slot, type or localId is not valid (" + item.dump() + ")");
            }
            entry.slot = static_cast<std::uint16_t>(*slot);
            entry.type = *type;
            entry.localId = *local;
            if (const auto plugin = item.find("plugin"); plugin != item.end() && plugin->is_string()) {
                entry.plugin = plugin->get<std::string>();
            } else if (const auto hex = item.find("pluginHex"); hex != item.end() && hex->is_string()) {
                const auto bytes = DataPathRule::FromHex(hex->get<std::string>());
                if (!bytes) {
                    return refuse("a slot entry's pluginHex is not hex");
                }
                entry.plugin = *bytes;
            }
            if (entry.plugin.empty() || entry.plugin.size() > SlotCoSave::kMaxPluginName) {
                return refuse("slot " + HexId(entry.slot) + ": no plugin name, or one over 260 bytes");
            }
            const auto state = item.contains("state") && item["state"].is_string() ? item["state"].get<std::string>() : std::string();
            if (state == "active") {
                entry.state = State::kActive;
            } else if (state == "tombstone") {
                entry.state = State::kTombstone;
            } else if (state == "conflict") {
                entry.state = State::kConflict;
            } else {
                return refuse("slot " + HexId(entry.slot) + ": unknown state");
            }
            entry.since = Count(item, "since").value_or(0);
            if (const auto conflict = item.find("conflict"); conflict != item.end() && conflict->is_string()) {
                entry.conflict = conflict->get<std::string>();
            }
            if (const auto published = item.find("published"); published != item.end()) {
                if (!published->is_array()) {
                    return refuse("slot " + HexId(entry.slot) + ": published is not an array");
                }
                for (const auto& p : *published) {
                    Object object;
                    const auto role = p.is_object() && p.contains("role") && p["role"].is_string() ? p["role"].get<std::string>() : std::string();
                    const auto key = p.is_object() && p.contains("key") ? Key(p["key"]) : std::nullopt;
                    const auto fnv = p.is_object() && p.contains("fnv64") ? Key(p["fnv64"]) : std::nullopt;
                    const auto path = p.is_object() && p.contains("path") && p["path"].is_string();
                    const bool known = role == "model" || role == "muzzle" || role == "mps" || role == "txst";
                    if (!known || !key || !fnv || !path || !Count(p, "bytes") || !p.contains("deps") || !ReadDeps(p["deps"], object.deps)) {
                        return refuse("slot " + HexId(entry.slot) + ": a published object is not valid");
                    }
                    object.role = role == "muzzle" ? Role::kMuzzle : role == "mps" ? Role::kMps : role == "txst" ? Role::kTxst : Role::kModel;
                    object.key = *key;
                    object.fnv64 = *fnv;
                    object.path = p["path"].get<std::string>();
                    object.bytes = p["bytes"].get<std::uint64_t>();
                    SetPublished(entry, std::move(object));
                }
            }
            if (const auto failures = item.find("parseFailures"); failures != item.end() && failures->is_array()) {
                for (const auto& f : *failures) {
                    const auto key = f.is_object() && f.contains("key") ? Key(f["key"]) : std::nullopt;
                    const auto count = f.is_object() ? Count(f, "count") : std::nullopt;
                    if (key && count) {
                        entry.parseFailures[*key] = { *count, Count(f, "session").value_or(0) };
                    }
                }
            }
            if (const auto terminal = item.find("terminal"); terminal != item.end() && terminal->is_array()) {
                for (const auto& t : *terminal) {
                    if (const auto key = Key(t)) {
                        entry.terminal.insert(*key);
                    }
                }
            }
            if (FindSlot(ledger, entry.slot)) {
                return refuse("slot " + HexId(entry.slot) + " listed twice");
            }
            Insert(ledger, std::move(entry));
        }
        return ledger;
    }
}

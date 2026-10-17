#pragma once

// =============================================================================
// SLOT LEDGER JSON - HoMBF_slots.json's text (included by SlotLedgerModel.h)
// =============================================================================
// { "format": 1, "epoch": "<16 hex>", "version": n, "sessions": n, "slots": [
//     { "slot": "800", "type": "PROJ", "plugin": "Skyrim.esm" | "pluginHex": "<bytes as hex>", "localId": "10fbed",
//       "state": "active|tombstone|conflict", "since": n, ["conflict": "plugin|id" | "conflictHex": "<bytes as hex>"],
//       "published": [ { "role": "model|muzzle|mps|txst", "key": "<16 hex>", "keyString": "...", "path": "...", "bytes": n,
//                        "fnv64": "<16 hex>",
//                        "deps": [ { "path": "...", "fnv64": "<16 hex>" } | { "addn": 40012 } ] } ],
//       "parseFailures": [ { "key": "<16 hex>", "count": n, "session": n } ], "terminal": [ "<16 hex>" ] } ] }
// A plugin name with a byte outside printable ASCII is written as pluginHex, a key string or a dep path likewise as
// keyStringHex / pathHex (JSON text must be UTF-8; the engine's names and paths are
// the system code page's bytes); a conflict text likewise as conflictHex. FromJson is strict: an unknown format, a slot
// outside its type's pool, a slot twice, or a field that is there but of the wrong kind (since, conflict, keyString, a
// parse failure, a terminal key) refuses the whole file (the caller then reads a backup or the mirror).

#include "SlotLedgerModel.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

namespace BlackFlame::SlotLedgerModel
{
    namespace JsonDetail
    {
        inline constexpr unsigned char kFirstPrintable = 0x20;
        inline constexpr std::size_t kMaxQuotedEntry = 200;  // how much of a refused entry a log line quotes
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
                // review agy redo 2.1: a source path may hold the code page's bytes: hex, never raw text that dump() refuses
                deps.push_back(dep.isAddn ? Json{ { "addn", dep.addn } } :
                               PrintableAscii(dep.path) ? Json{ { "path", dep.path }, { "fnv64", CC::Hex16(dep.fnv64) } } :
                                                          Json{ { "pathHex", CC::LowerHex(dep.path) }, { "fnv64", CC::Hex16(dep.fnv64) } });
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
                const bool hasPath = item.contains("path") || item.contains("pathHex");
                if (const auto addn = item.find("addn"); addn != item.end()) {
                    if (hasPath || item.contains("fnv64") || !addn->is_number_unsigned() || addn->get<std::uint64_t>() > UINT32_MAX ||
                        !CC::SlotOfAddnIndex(static_cast<std::uint32_t>(addn->get<std::uint64_t>()))) {
                        return false;
                    }
                    dep.isAddn = true;
                    dep.addn = static_cast<std::uint32_t>(addn->get<std::uint64_t>());
                } else {
                    const auto path = item.find("path");
                    const auto hex = item.find("pathHex");
                    const auto fnv = item.find("fnv64");
                    if ((path != item.end()) == (hex != item.end()) || fnv == item.end() || !Key(*fnv)) {
                        return false;  // exactly one of path and pathHex
                    }
                    if (path != item.end()) {
                        if (!path->is_string()) {
                            return false;
                        }
                        dep.path = path->get<std::string>();
                    } else {
                        const auto bytes = hex->is_string() ? DataPathRule::FromHex(hex->get<std::string>()) : std::nullopt;
                        if (!bytes) {
                            return false;
                        }
                        dep.path = *bytes;
                    }
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
                item[PrintableAscii(entry.conflict) ? "conflict" : "conflictHex"] =
                    PrintableAscii(entry.conflict) ? entry.conflict : CC::LowerHex(entry.conflict);  // review Gemini 18: read back
            }
            item["published"] = Json::array();
            for (const auto& object : entry.published) {
                Json p{ { "role", RoleText(object.role) }, { "key", CC::Hex16(object.key) }, { "path", object.path },
                    { "bytes", object.bytes }, { "fnv64", CC::Hex16(object.fnv64) }, { "deps", DepsJson(object.deps) } };
                // review agy redo 2.1: a key string names source paths, which may hold the code page's bytes
                p[PrintableAscii(object.keyText) ? "keyString" : "keyStringHex"] =
                    PrintableAscii(object.keyText) ? object.keyText : CC::LowerHex(object.keyText);
                item["published"].push_back(std::move(p));
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
                auto text = item.dump();
                if (text.size() > kMaxQuotedEntry) {
                    text = text.substr(0, kMaxQuotedEntry) + "...";  // review agy redo 2.10
                }
                return refuse("a slot entry's slot, type or localId is not valid (" + text + ")");
            }
            entry.slot = static_cast<std::uint16_t>(*slot);
            entry.type = *type;
            entry.localId = *local;
            const auto plugin = item.find("plugin");
            const auto pluginHex = item.find("pluginHex");
            if ((plugin != item.end() && !plugin->is_string()) || (pluginHex != item.end() && !pluginHex->is_string()) ||
                (plugin != item.end() && pluginHex != item.end())) {
                return refuse("slot " + HexId(entry.slot) + ": plugin / pluginHex not one text");  // review agy redo 2.6
            }
            if (plugin != item.end()) {
                entry.plugin = plugin->get<std::string>();
            } else if (pluginHex != item.end()) {
                const auto bytes = DataPathRule::FromHex(pluginHex->get<std::string>());
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
            // review Gemini 18: a field that is there must be of its kind (a damaged entry is no silent 0)
            if (item.contains("since")) {
                const auto since = Count(item, "since");
                if (!since) {
                    return refuse("slot " + HexId(entry.slot) + ": since is not a count");
                }
                entry.since = *since;
            }
            if (const auto conflict = item.find("conflict"); conflict != item.end()) {
                if (!conflict->is_string()) {
                    return refuse("slot " + HexId(entry.slot) + ": conflict is not text");
                }
                entry.conflict = conflict->get<std::string>();
            } else if (const auto hex = item.find("conflictHex"); hex != item.end()) {
                const auto bytes = hex->is_string() ? DataPathRule::FromHex(hex->get<std::string>()) : std::nullopt;
                if (!bytes) {
                    return refuse("slot " + HexId(entry.slot) + ": conflictHex is not hex");
                }
                entry.conflict = *bytes;
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
                    // review agy redo 2.3: bytes read as written (64 bits)
                    const bool bytes = p.is_object() && p.contains("bytes") && p["bytes"].is_number_unsigned();
                    if (!known || !key || !fnv || !path || !bytes || !p.contains("deps") || !ReadDeps(p["deps"], object.deps)) {
                        return refuse("slot " + HexId(entry.slot) + ": a published object is not valid");
                    }
                    object.role = role == "muzzle" ? Role::kMuzzle : role == "mps" ? Role::kMps : role == "txst" ? Role::kTxst : Role::kModel;
                    object.key = *key;
                    object.fnv64 = *fnv;
                    object.path = p["path"].get<std::string>();
                    const auto text = p.find("keyString");
                    const auto textHex = p.find("keyStringHex");
                    if (text != p.end() && textHex != p.end()) {
                        return refuse("slot " + HexId(entry.slot) + ": both keyString and keyStringHex");
                    }
                    if (text != p.end()) {
                        if (!text->is_string()) {
                            return refuse("slot " + HexId(entry.slot) + ": a keyString is not text");
                        }
                        object.keyText = text->get<std::string>();
                    } else if (textHex != p.end()) {
                        const auto keyBytes = textHex->is_string() ? DataPathRule::FromHex(textHex->get<std::string>()) : std::nullopt;
                        if (!keyBytes) {
                            return refuse("slot " + HexId(entry.slot) + ": a keyStringHex is not hex");
                        }
                        object.keyText = *keyBytes;
                    }
                    if (PublishedOf(entry, object.role)) {
                        return refuse("slot " + HexId(entry.slot) + ": two published objects of one role");  // review agy redo 2.8
                    }
                    object.bytes = p["bytes"].get<std::uint64_t>();
                    // a path the DLL would hash, register and set as a model must be a copy path (a TXST object has none);
                    // one that is not is dropped (the slot shows its source until a new generation is published)
                    const bool pathOk = object.role == Role::kTxst ? object.path.empty() : DataPathRule::ParseCopyPath(object.path).has_value();
                    if (pathOk) {
                        SetPublished(entry, std::move(object));
                    }
                }
            }
            if (const auto failures = item.find("parseFailures"); failures != item.end()) {
                if (!failures->is_array()) {
                    return refuse("slot " + HexId(entry.slot) + ": parseFailures is not an array");
                }
                for (const auto& f : *failures) {
                    const auto key = f.is_object() && f.contains("key") ? Key(f["key"]) : std::nullopt;
                    const auto count = f.is_object() ? Count(f, "count") : std::nullopt;
                    const auto session = f.is_object() ? Count(f, "session") : std::nullopt;
                    if (!key || !count || !session) {
                        return refuse("slot " + HexId(entry.slot) + ": a parse failure is not valid");
                    }
                    if (!entry.parseFailures.emplace(*key, ParseFailures{ *count, *session }).second) {
                        return refuse("slot " + HexId(entry.slot) + ": a parse failure key twice");  // review agy redo 2.8
                    }
                }
            }
            if (const auto terminal = item.find("terminal"); terminal != item.end()) {
                if (!terminal->is_array()) {
                    return refuse("slot " + HexId(entry.slot) + ": terminal is not an array");
                }
                for (const auto& t : *terminal) {
                    const auto key = Key(t);
                    if (!key) {
                        return refuse("slot " + HexId(entry.slot) + ": a terminal key is not 16 hex digits");
                    }
                    entry.terminal.insert(*key);
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

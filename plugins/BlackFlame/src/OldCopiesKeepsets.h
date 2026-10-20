#pragma once

// =============================================================================
// OLD COPIES KEEPSETS - one keep set file per ledger epoch and the registry of epochs (no game types; OldCopies.h)
// =============================================================================
// The ledger mirror's name hashes the save folder's path, and under MO2 that path is the virtual one (in game 2026-10-18:
// "...\__MO_Saves\..."), the same for every profile with its own saves: the profiles write one mirror over each other, and
// one profile's cleanup would not see another's keys. So every ledger epoch keeps its own file in the shared staging folder:
//   keepset   staging\keepsets\<epoch hex16>.json  {"format":1,"epoch":"<hex16>","keys":["<hex16>",...]}: merged with the
//             ledger's keys at every ledger write (WriteKeepset a_merge), replaced by the session's full keep set when its
//             cleanup runs
//   registry  staging\keepsets\registry.json  {"format":1,"epochs":["<hex16>",...]}: every epoch that wrote a keep set
//   union     ReadAll: every keep set file's keys; a registered epoch without a readable keep set file, a keep set that does
//             not read, or a registry that does not read stops the cleanup (ok false, a_why)
//   adopt     AdoptMirrors: at session start every mirror of another epoch (a profile that ran a build without keep sets)
//             registered and its keys merged into that epoch's keep set file, before this session's mirror write
// Files through SlotLedgerFiles::WriteReplacing (a temporary file, flushed, a replacing write-through rename). Not thread
// safe: OldCopies.cpp holds one lock around every call. C++17: blackflame-cosave-check tests it (OldCopiesFilesTests.h).

#include "OldCopiesRule.h"
#include "SlotLedgerFiles.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace BlackFlame::OldCopiesKeepsets
{
    namespace fs = std::filesystem;
    namespace CC = CopyContract;
    using Json = SlotLedgerModel::Json;
    using KeySet = OldCopiesRule::KeySet;

    inline constexpr int kFormat = 1;
    inline constexpr const char* kFolderName = "keepsets";
    inline constexpr const char* kRegistryName = "registry.json";
    inline constexpr std::string_view kExtension = ".json";
    inline constexpr std::uintmax_t kMaxBytes = 64ull * 1024ull * 1024ull;

    [[nodiscard]] inline fs::path KeepsetPath(const fs::path& a_folder, std::uint64_t a_epoch)
    {
        return a_folder / (CC::Hex16(a_epoch) + std::string(kExtension));
    }

    /// <hex16>.json: the epoch a keep set file's name gives, else nullopt (registry.json, a .tmp, anything else)
    [[nodiscard]] inline std::optional<std::uint64_t> EpochOfName(std::string_view a_name)
    {
        if (a_name.size() != DataPathRule::kKey16Length + kExtension.size() || a_name.substr(DataPathRule::kKey16Length) != kExtension) {
            return std::nullopt;
        }
        return CC::ParseHex16(a_name.substr(0, DataPathRule::kKey16Length));
    }

    [[nodiscard]] inline std::string KeepsetText(std::uint64_t a_epoch, const KeySet& a_keys)
    {
        Json keys = Json::array();
        for (const auto key : a_keys) {
            keys.push_back(CC::Hex16(key));
        }
        return Json{ { "format", kFormat }, { "epoch", CC::Hex16(a_epoch) }, { "keys", std::move(keys) } }.dump() + "\n";
    }

    /// Strict: the format, a_epoch, and every key 16 lower-case hex digits; nullopt with a_why otherwise
    [[nodiscard]] inline std::optional<KeySet> ParseKeepset(const std::string& a_text, std::uint64_t a_epoch, std::string& a_why)
    {
        const auto root = Json::parse(a_text, nullptr, false);
        if (root.is_discarded() || !root.is_object()) {
            a_why = "not a JSON object (a write cut short?)";
            return std::nullopt;
        }
        const auto format = root.find("format");
        const auto epoch = root.find("epoch");
        const auto keys = root.find("keys");
        if (format == root.end() || !format->is_number_integer() || format->get<int>() != kFormat) {
            a_why = "not format " + std::to_string(kFormat);
            return std::nullopt;
        }
        if (epoch == root.end() || !epoch->is_string() || CC::ParseHex16(epoch->get<std::string>()) != a_epoch) {
            a_why = "its epoch is not its name's (" + CC::Hex16(a_epoch) + ")";
            return std::nullopt;
        }
        if (keys == root.end() || !keys->is_array()) {
            a_why = "no key list";
            return std::nullopt;
        }
        KeySet out;
        for (const auto& item : *keys) {
            const auto key = item.is_string() ? CC::ParseHex16(item.get<std::string>()) : std::nullopt;
            if (!key) {
                a_why = "a key that is not 16 lower-case hex digits: " + item.dump();
                return std::nullopt;
            }
            out.insert(*key);
        }
        return out;
    }

    [[nodiscard]] inline std::string RegistryText(const std::set<std::uint64_t>& a_epochs)
    {
        Json epochs = Json::array();
        for (const auto epoch : a_epochs) {
            epochs.push_back(CC::Hex16(epoch));
        }
        return Json{ { "format", kFormat }, { "epochs", std::move(epochs) } }.dump() + "\n";
    }

    [[nodiscard]] inline std::optional<std::set<std::uint64_t>> ParseRegistry(const std::string& a_text, std::string& a_why)
    {
        const auto root = Json::parse(a_text, nullptr, false);
        if (root.is_discarded() || !root.is_object()) {  // nothing but is_discarded() on a discarded value
            a_why = "not a JSON object (a write cut short?)";
            return std::nullopt;
        }
        const auto format = root.find("format");
        const auto epochs = root.find("epochs");
        if (format == root.end() || !format->is_number_integer() ||
            format->get<int>() != kFormat || epochs == root.end() || !epochs->is_array()) {
            a_why = "not a format " + std::to_string(kFormat) + " registry";
            return std::nullopt;
        }
        std::set<std::uint64_t> out;
        for (const auto& item : *epochs) {
            const auto epoch = item.is_string() ? CC::ParseHex16(item.get<std::string>()) : std::nullopt;
            if (!epoch) {
                a_why = "an epoch that is not 16 lower-case hex digits: " + item.dump();
                return std::nullopt;
            }
            out.insert(*epoch);
        }
        return out;
    }

    enum class Read : std::uint8_t
    {
        kRead,
        kMissing,
        kBad,  // there but not read (a_why)
    };

    [[nodiscard]] inline Read ReadText(const fs::path& a_file, std::string& a_text, std::string& a_why)
    {
        std::error_code ec;
        const bool exists = fs::exists(a_file, ec);
        if (ec || !exists) {
            a_why = ec ? "it could not be looked at: " + ec.message() : "missing";
            return ec ? Read::kBad : Read::kMissing;
        }
        const auto size = fs::file_size(a_file, ec);
        if (ec || size > kMaxBytes) {
            a_why = ec ? "its size could not be read: " + ec.message() : "larger than 64 MiB";
            return Read::kBad;
        }
        std::ifstream in(a_file, std::ios::binary);
        std::ostringstream text;
        if (!in || !(text << in.rdbuf())) {
            a_why = "it does not read";
            return Read::kBad;
        }
        a_text = text.str();
        return Read::kRead;
    }

    /// a_epoch's keep set written as a_keys, or (a_merge) as a_keys with the keys its file holds now (one that does not read
    /// is replaced, said in a_note). False with a_why when the write failed
    [[nodiscard]] inline bool WriteKeepset(const fs::path& a_folder, std::uint64_t a_epoch, KeySet a_keys, bool a_merge,
        std::string& a_note, std::string& a_why)
    {
        const auto file = KeepsetPath(a_folder, a_epoch);
        if (a_merge) {
            std::string text;
            std::string why;
            const auto read = ReadText(file, text, why);
            const auto held = read == Read::kRead ? ParseKeepset(text, a_epoch, why) : std::nullopt;
            if (held) {
                a_keys.insert(held->begin(), held->end());
            } else if (read != Read::kMissing) {
                a_note = "the keep set file there did not read (" + why + "): replaced";
            }
        }
        return SlotLedgerFiles::WriteReplacing(file, KeepsetText(a_epoch, a_keys), a_why);
    }

    /// a_epoch added to the registry (written only when it was not listed). A registry that does not read is not
    /// replaced (the epochs it names would lose their protection): false with a_why
    [[nodiscard]] inline bool Register(const fs::path& a_folder, std::uint64_t a_epoch, std::string& a_why)
    {
        const auto file = a_folder / kRegistryName;
        std::string text;
        std::string why;
        const auto read = ReadText(file, text, why);
        std::set<std::uint64_t> epochs;
        if (read == Read::kBad) {
            a_why = "the registry '" + SlotLedgerFiles::PathText(file) + "' does not read (" + why + ")";
            return false;
        }
        if (read == Read::kRead) {
            const auto parsed = ParseRegistry(text, why);
            if (!parsed) {
                a_why = "the registry '" + SlotLedgerFiles::PathText(file) + "' does not read (" + why + ")";
                return false;
            }
            epochs = *parsed;
        }
        if (!epochs.insert(a_epoch).second) {
            return true;
        }
        return SlotLedgerFiles::WriteReplacing(file, RegistryText(epochs), a_why);
    }

    /// The cleanup's own keep set write (OldCopies.cpp AddKeepsets): a plain replace only when no ledger write was counted
    /// (NoteLedgerWritten) since the count read just before the ledger copy it stands on; otherwise a merge, so a late
    /// write's keys are not dropped (review round 2, reviewer 2)
    [[nodiscard]] inline bool MergeOwnKeepset(std::uint64_t a_writesAtCopy, std::uint64_t a_writesNow)
    {
        return a_writesNow != a_writesAtCopy;
    }

    struct Mirrors
    {
        bool ok = true;
        std::vector<SlotLedgerModel::Ledger> ledgers;  // every mirror that read, in listing order
        std::size_t bad = 0;                           // mirrors that did not read (ok false; a_why names the first)
        std::string why;
    };

    /// Every ledger mirror (OldCopiesRule::IsMirrorName) in a_folder: no folder is no mirror; a mirror that does not read, or
    /// a folder that does not list, makes ok false (the rest still read)
    [[nodiscard]] inline Mirrors ReadMirrors(const fs::path& a_folder)
    {
        Mirrors out;
        std::error_code ec;
        const bool exists = fs::exists(a_folder, ec);
        if (ec) {
            out.ok = false;
            out.why = "the mirror folder could not be looked at: " + ec.message();
            return out;
        }
        for (auto it = exists ? fs::directory_iterator(a_folder, ec) : fs::directory_iterator(); !ec && it != fs::directory_iterator();
             it.increment(ec)) {
            if (!OldCopiesRule::IsMirrorName(SlotLedgerFiles::PathText(it->path().filename()))) {
                continue;
            }
            std::string why;
            if (auto ledger = SlotLedgerFiles::ReadOne(it->path(), why)) {
                out.ledgers.push_back(std::move(*ledger));
            } else if (out.bad++ == 0) {
                out.ok = false;
                out.why = "the ledger mirror '" + SlotLedgerFiles::PathText(it->path()) + "' does not read (" + why + ")";
            }
        }
        if (ec) {
            out.ok = false;
            out.why = "the mirror folder could not be listed: " + ec.message();
        }
        return out;
    }

    struct Adopted
    {
        std::size_t epochs = 0;          // other epochs registered with a keep set from their mirror
        std::vector<std::string> notes;  // one per epoch that failed, or whose old keep set file was replaced
        std::size_t failed = 0;          // epochs not registered or without a keep set written
    };

    /// Migration (review round 2, Codex P1): at session start, before this session's first ledger write, every mirror of
    /// another epoch than a_current (a profile that ran an older build: no keep set, not registered, and this session's
    /// mirror write may replace its mirror) is registered, then its ledger's keys merged into that epoch's keep set file
    /// (merged: a keep set its own sessions wrote stays whole). Registered first: a keep set that then fails to write
    /// leaves a registered epoch without one, which stops every cleanup (ReadAll) rather than leaving its copies unguarded
    [[nodiscard]] inline Adopted AdoptMirrors(const fs::path& a_keepsets, const std::vector<SlotLedgerModel::Ledger>& a_mirrors,
        std::uint64_t a_current)
    {
        Adopted out;
        for (const auto& ledger : a_mirrors) {
            if (ledger.epoch == a_current) {
                continue;
            }
            KeySet keys;
            OldCopiesRule::AddLedger(keys, ledger);
            std::string note;
            std::string why;
            const auto epoch = CC::Hex16(ledger.epoch);
            if (!Register(a_keepsets, ledger.epoch, why) || !WriteKeepset(a_keepsets, ledger.epoch, std::move(keys), true, note, why)) {
                out.notes.push_back("epoch " + epoch + " seen in a mirror without a keep set: not protected: " + why);
                ++out.failed;
                continue;
            }
            ++out.epochs;
            if (!note.empty()) {
                out.notes.push_back("epoch " + epoch + ": " + note);
            }
        }
        return out;
    }

    struct Union
    {
        bool ok = false;
        KeySet keys;                // every keep set file's keys
        std::size_t files = 0;      // keep set files read
        std::size_t registered = 0; // epochs the registry names
        std::string why;            // !ok: what stopped it
    };

    /// Every keep set file in a_folder and the registry; ok only when each registered epoch has a keep set file that reads
    [[nodiscard]] inline Union ReadAll(const fs::path& a_folder)
    {
        Union out;
        std::string text;
        std::string why;
        std::set<std::uint64_t> registered;
        const auto registry = a_folder / kRegistryName;
        if (const auto read = ReadText(registry, text, why); read == Read::kBad) {
            out.why = "the registry '" + SlotLedgerFiles::PathText(registry) + "' does not read (" + why + ")";
            return out;
        } else if (read == Read::kRead) {
            const auto parsed = ParseRegistry(text, why);
            if (!parsed) {
                out.why = "the registry '" + SlotLedgerFiles::PathText(registry) + "' does not read (" + why + ")";
                return out;
            }
            registered = *parsed;
        }
        out.registered = registered.size();
        std::set<std::uint64_t> found;
        std::error_code ec;
        const bool exists = fs::exists(a_folder, ec);
        for (fs::directory_iterator it = exists ? fs::directory_iterator(a_folder, ec) : fs::directory_iterator(), end; !ec && it != end;
             it.increment(ec)) {
            const auto name = SlotLedgerFiles::PathText(it->path().filename());
            const auto epoch = EpochOfName(name);
            if (!epoch) {
                continue;
            }
            const auto read = ReadText(it->path(), text, why);
            const auto keys = read == Read::kRead ? ParseKeepset(text, *epoch, why) : std::nullopt;
            if (!keys) {
                out.why = "the keep set '" + SlotLedgerFiles::PathText(it->path()) + "' does not read (" + why + ")";
                return out;
            }
            out.keys.insert(keys->begin(), keys->end());
            found.insert(*epoch);
            ++out.files;
        }
        if (ec) {
            out.why = "the keep set folder '" + SlotLedgerFiles::PathText(a_folder) + "' could not be listed: " + ec.message();
            return out;
        }
        for (const auto epoch : registered) {
            if (found.count(epoch) == 0) {
                out.why = "ledger epoch " + CC::Hex16(epoch) + " is registered but its keep set file is missing (that profile's "
                          "copies cannot be told apart)";
                return out;
            }
        }
        out.ok = true;
        return out;
    }
}

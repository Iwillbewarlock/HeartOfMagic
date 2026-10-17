#pragma once

// =============================================================================
// SLOT LEDGER FILES - HoMBF_slots.json, its 5 backups and its mirror on disk (no game types)
// =============================================================================
// Redundancy (design v3.2 §7): the ledger beside the saves (main), HoMBF_slots.1.json .. .5.json beside it (rolled at each
// write, newest .1), and a mirror in <SKSE log folder>\HeartOfMagic_BlackFlame\. LoadAll reads every one that is there;
// the base is the readable one with the highest version (main first on a tie), and every other readable one is merged into
// it (SlotLedgerModel Merge: the union of reservations, conflicts named) - so a lost or cut main loses nothing a backup or
// the mirror still holds. SaveAll writes main through a temporary file and a replacing rename after rolling the backups
// (main is copied to .1 first, so a crash at any point leaves a readable main or .1), then the mirror the same way.
// C++17 std::filesystem and nlohmann only: blackflame-cosave-check tests it in a temporary folder.

#include "SlotLedgerModel.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace BlackFlame::SlotLedgerFiles
{
    namespace fs = std::filesystem;
    using SlotLedgerModel::Ledger;

    inline constexpr int kBackups = 5;
    inline constexpr std::uintmax_t kMaxLedgerBytes = 64ull * 1024ull * 1024ull;  // 2000 slots stay far below this
    inline constexpr int kJsonIndent = 1;

    /// HoMBF_slots.json -> HoMBF_slots.<n>.json
    [[nodiscard]] inline fs::path BackupPath(const fs::path& a_main, int a_n)
    {
        auto name = a_main.stem();
        name += "." + std::to_string(a_n);
        name += a_main.extension();
        return a_main.parent_path() / name;
    }

    [[nodiscard]] inline std::string PathText(const fs::path& a_path)
    {
        const auto text = a_path.u8string();
        return std::string(text.begin(), text.end());
    }

    /// The ledger in a_file, or nullopt with a_why ("missing" when there is no file)
    [[nodiscard]] inline std::optional<Ledger> ReadOne(const fs::path& a_file, std::string& a_why)
    {
        std::error_code ec;
        if (!fs::exists(a_file, ec)) {
            a_why = "missing";
            return std::nullopt;
        }
        const auto size = fs::file_size(a_file, ec);
        if (ec || size > kMaxLedgerBytes) {
            a_why = ec ? "its size could not be read: " + ec.message() : "larger than 64 MiB";
            return std::nullopt;
        }
        std::ifstream in(a_file, std::ios::binary);
        if (!in) {
            a_why = "does not open";
            return std::nullopt;
        }
        std::ostringstream text;
        text << in.rdbuf();
        const auto root = SlotLedgerModel::Json::parse(text.str(), nullptr, false);
        if (root.is_discarded()) {
            a_why = "not JSON (a write cut short?)";
            return std::nullopt;
        }
        return SlotLedgerModel::FromJson(root, a_why);
    }

    struct Loaded
    {
        std::optional<Ledger> ledger;     // the merged ledger, nullopt when no file was readable
        std::vector<std::string> notes;   // one line per file (read, missing, refused and why)
        std::vector<std::string> conflicts;
        int readable = 0;
        int unreadable = 0;               // present but refused (not counting missing ones)
        bool mainReadable = false;
    };

    /// Reads main, its backups and the mirror (a_mirror may be empty) and merges them
    [[nodiscard]] inline Loaded LoadAll(const fs::path& a_main, const fs::path& a_mirror)
    {
        Loaded out;
        struct Read
        {
            std::string what;
            Ledger ledger;
        };
        std::vector<Read> reads;
        std::vector<std::pair<std::string, fs::path>> files{ { "main", a_main } };
        for (int n = 1; n <= kBackups; ++n) {
            files.emplace_back("backup " + std::to_string(n), BackupPath(a_main, n));
        }
        if (!a_mirror.empty()) {
            files.emplace_back("mirror", a_mirror);
        }
        for (const auto& [what, file] : files) {
            std::string why;
            auto ledger = ReadOne(file, why);
            if (ledger) {
                out.notes.push_back(what + " '" + PathText(file) + "': version " + std::to_string(ledger->version) + ", " +
                                    std::to_string(ledger->entries.size()) + " slots, epoch " + CopyContract::Hex16(ledger->epoch));
                out.mainReadable = out.mainReadable || what == "main";
                reads.push_back({ what, std::move(*ledger) });
                ++out.readable;
            } else if (why != "missing") {
                out.notes.push_back(what + " '" + PathText(file) + "' refused: " + why);
                ++out.unreadable;
            }
        }
        if (reads.empty()) {
            return out;
        }
        std::size_t base = 0;
        for (std::size_t i = 1; i < reads.size(); ++i) {
            if (reads[i].ledger.version > reads[base].ledger.version) {
                base = i;
            }
        }
        Ledger merged = reads[base].ledger;
        for (std::size_t i = 0; i < reads.size(); ++i) {
            if (i != base) {
                (void)SlotLedgerModel::Merge(merged, reads[i].ledger, out.conflicts, reads[i].what);
            }
        }
        out.notes.push_back("base: " + reads[base].what + "; " + std::to_string(merged.entries.size()) + " slots after the union");
        out.ledger = std::move(merged);
        return out;
    }

    /// a_text into a_file through a_file + ".tmp" and a replacing rename
    [[nodiscard]] inline bool WriteReplacing(const fs::path& a_file, const std::string& a_text, std::string& a_why)
    {
        std::error_code ec;
        fs::create_directories(a_file.parent_path(), ec);
        auto temp = a_file;
        temp += ".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out.write(a_text.data(), static_cast<std::streamsize>(a_text.size()));
            out.flush();
            if (!out) {
                a_why = "writing '" + PathText(temp) + "' failed";
                return false;
            }
        }
        fs::rename(temp, a_file, ec);  // MSVC: MoveFileExW with MOVEFILE_REPLACE_EXISTING
        if (ec) {
            a_why = "renaming onto '" + PathText(a_file) + "' failed: " + ec.message();
            fs::remove(temp, ec);
            return false;
        }
        return true;
    }

    /// Rolls the backups (.4 -> .5 ... main -> .1, main copied) when a_rotate, writes main, then the mirror (a_mirror may be
    /// empty). The DLL rotates once a session, so five backups are five sessions, not five writes of one. a_mainWhy /
    /// a_mirrorWhy say what failed; the return is main's result (the mirror is best effort)
    [[nodiscard]] inline bool SaveAll(const Ledger& a_ledger, const fs::path& a_main, const fs::path& a_mirror,
        std::string& a_mainWhy, std::string& a_mirrorWhy, bool a_rotate = true)
    {
        std::string text;
        try {
            text = SlotLedgerModel::ToJson(a_ledger).dump(kJsonIndent) + "\n";
        } catch (const SlotLedgerModel::Json::exception& e) {
            a_mainWhy = std::string("the ledger is not JSON text: ") + e.what();
            return false;
        }
        std::error_code ec;
        if (a_rotate && fs::exists(a_main, ec)) {
            for (int n = kBackups - 1; n >= 1; --n) {
                const auto from = BackupPath(a_main, n);
                if (fs::exists(from, ec)) {
                    fs::rename(from, BackupPath(a_main, n + 1), ec);
                }
            }
            fs::copy_file(a_main, BackupPath(a_main, 1), fs::copy_options::overwrite_existing, ec);
            if (ec) {
                a_mainWhy = "the backup copy failed: " + ec.message() + " (main written anyway)";
            }
        }
        const bool mainOk = WriteReplacing(a_main, text, a_mainWhy);
        if (!a_mirror.empty() && !WriteReplacing(a_mirror, text, a_mirrorWhy)) {
            a_mirrorWhy = "mirror: " + a_mirrorWhy;
        }
        return mainOk;
    }
}

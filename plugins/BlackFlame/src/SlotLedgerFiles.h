#pragma once

// =============================================================================
// SLOT LEDGER FILES - HoMBF_slots.json, its 5 backups and its mirror on disk (no game types)
// =============================================================================
// Redundancy (design v3.2 §7): the ledger beside the saves (main), HoMBF_slots.1.json .. .5.json beside it (rolled at each
// write, newest .1), and a mirror per save folder in <SKSE log folder>\HeartOfMagic_BlackFlame\ (its name carries a hash
// of the save folder's path). LoadAll reads every one that is there; the base is the readable one with the highest version
// of main's epoch (any epoch when main does not read; main first on a tie), and every other readable one is merged into
// it, newest first (SlotLedgerModel Merge: the union of reservations, conflicts named, an older copy of a binding stale) -
// so a lost or cut main loses nothing a backup or the mirror still holds. SaveAll copies main beside backup 1, rolls the
// backups, renames the copy in as backup 1, then writes main through a temporary file and a write-through rename, then the
// mirror the same way. C++17 std::filesystem, nlohmann and the Win32 file API (FlushFileBuffers, MoveFileExW; Windows.h
// comes from the including unit): blackflame-cosave-check tests it in a temporary folder.

#include "SlotLedgerModel.h"

#include <algorithm>
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
        const bool exists = fs::exists(a_file, ec);
        if (ec) {
            a_why = "it could not be looked at: " + ec.message();  // review agy redo 1: an error is no missing file
            return std::nullopt;
        }
        if (!exists) {
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
        // The mirror lives outside the save folder (its file name carries a hash of that folder): it is merged only when its
        // epoch is the one the save folder's own files hold, so another profile's ledger never mixes in
        const auto mirror = std::find_if(reads.begin(), reads.end(), [](const Read& a_read) { return a_read.what == "mirror"; });
        const auto own = std::find_if(reads.begin(), reads.end(), [](const Read& a_read) { return a_read.what != "mirror"; });
        if (mirror != reads.end() && own != reads.end() && mirror->ledger.epoch != own->ledger.epoch) {
            out.notes.push_back("mirror not merged: epoch " + CopyContract::Hex16(mirror->ledger.epoch) + ", the save folder's ledger " +
                                CopyContract::Hex16(own->ledger.epoch));
            reads.erase(mirror);
        }
        if (reads.empty()) {
            return out;
        }
        // review agy redo 3: the base is of main's epoch when main reads (a new epoch restarts at a low version; an older
        // epoch's backup with a higher version must not take the ledger back to that epoch); its files merge as usual
        const auto mainRead = std::find_if(reads.begin(), reads.end(), [](const Read& a_read) { return a_read.what == "main"; });
        std::size_t base = mainRead != reads.end() ? static_cast<std::size_t>(mainRead - reads.begin()) : 0;
        for (std::size_t i = 0; i < reads.size(); ++i) {
            const bool sameEpoch = mainRead == reads.end() || reads[i].ledger.epoch == mainRead->ledger.epoch;
            if (sameEpoch && reads[i].ledger.version > reads[base].ledger.version) {
                base = i;
            }
        }
        Ledger merged = reads[base].ledger;
        // review Gemini 17: the others newest first, so a slot the base lacks takes the newest file's binding (an older
        // copy of the same epoch then loses to it as stale; files read in name order let backup 5 win over the mirror)
        std::vector<std::size_t> order;
        for (std::size_t i = 0; i < reads.size(); ++i) {
            if (i != base) {
                order.push_back(i);
            }
        }
        std::stable_sort(order.begin(), order.end(),
            [&](std::size_t a_l, std::size_t a_r) { return reads[a_l].ledger.version > reads[a_r].ledger.version; });
        for (const auto i : order) {
            std::vector<std::string> stale;
            (void)SlotLedgerModel::Merge(merged, reads[i].ledger, out.conflicts, reads[i].what, &stale);
            out.notes.insert(out.notes.end(), stale.begin(), stale.end());
        }
        out.notes.push_back("base: " + reads[base].what + "; " + std::to_string(merged.entries.size()) + " slots after the union");
        out.ledger = std::move(merged);
        return out;
    }

    /// a_text into a_file through a_file + ".tmp": written, flushed to disk (FlushFileBuffers), then a replacing,
    /// write-through rename (MoveFileExW); a failed step removes the temporary file
    [[nodiscard]] inline bool WriteReplacing(const fs::path& a_file, const std::string& a_text, std::string& a_why)
    {
        std::error_code ec;
        fs::create_directories(a_file.parent_path(), ec);
        auto temp = a_file;
        temp += ".tmp";
        const HANDLE file = ::CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            a_why = "creating '" + PathText(temp) + "' failed (Windows error " + std::to_string(::GetLastError()) + ")";
            return false;
        }
        DWORD written = 0;
        const bool ok = a_text.size() <= MAXDWORD &&
                        ::WriteFile(file, a_text.data(), static_cast<DWORD>(a_text.size()), &written, nullptr) && written == a_text.size() &&
                        ::FlushFileBuffers(file);
        const DWORD error = ok ? 0 : ::GetLastError();  // before CloseHandle can change it
        ::CloseHandle(file);
        if (!ok) {
            a_why = "writing '" + PathText(temp) + "' failed (Windows error " + std::to_string(error) + ")";
            fs::remove(temp, ec);
            return false;
        }
        if (!::MoveFileExW(temp.c_str(), a_file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            a_why = "renaming onto '" + PathText(a_file) + "' failed (Windows error " + std::to_string(::GetLastError()) + ")";
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
        const bool mainThere = fs::exists(a_main, ec);
        // review agy redo 1: main that cannot be looked at is not written (nor are the backups rolled); the mirror, an
        // independent copy, still is (Codex: as before that change)
        const bool mainRefused = static_cast<bool>(ec);
        if (mainRefused) {
            a_mainWhy = "main could not be looked at (" + ec.message() + "): main not written; ";
        }
        std::string mainNow;
        // an unreadable main is no backup: it would push a good one out
        if (!mainRefused && a_rotate && mainThere && ReadOne(a_main, mainNow)) {
            // review agy redo 4: main copied beside backup 1 first; only with that copy are the backups rolled (a failed
            // copy leaves every backup where it was), and the copy is renamed in as backup 1 after the roll
            auto copy = BackupPath(a_main, 1);
            copy += ".tmp";
            fs::copy_file(a_main, copy, fs::copy_options::overwrite_existing, ec);
            bool rolled = !ec;
            if (ec) {
                a_mainWhy += "the backup copy failed (" + ec.message() + "): backups left as they were; ";
            }
            for (int n = kBackups - 1; n >= 1 && rolled; --n) {
                const auto from = BackupPath(a_main, n);
                if (fs::exists(from, ec) && !::MoveFileExW(from.c_str(), BackupPath(a_main, n + 1).c_str(),
                                                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                    rolled = false;  // stop at the first failure: no backup is overwritten out of order
                    a_mainWhy += "the backups were not rolled (Windows error " + std::to_string(::GetLastError()) + "); ";
                }
            }
            if (rolled && !::MoveFileExW(copy.c_str(), BackupPath(a_main, 1).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                a_mainWhy += "the backup rename failed (Windows error " + std::to_string(::GetLastError()) + "); ";
            }
            fs::remove(copy, ec);  // gone already after the rename
        }
        std::string writeWhy;  // review agy redo 5: what the roll said is kept
        const bool mainOk = !mainRefused && WriteReplacing(a_main, text, writeWhy);
        a_mainWhy += writeWhy;
        if (!a_mirror.empty()) {
            if (!WriteReplacing(a_mirror, text, a_mirrorWhy)) {
                a_mirrorWhy = "mirror: " + a_mirrorWhy;
            } else if (!mainOk) {
                a_mirrorWhy = "the mirror was written";  // the log names which copy holds this version
            }
        }
        return mainOk;
    }
}

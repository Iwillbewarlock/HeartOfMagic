// =============================================================================
// SlotLedger - HoMBF_slots.json (PrepareFileSavePath, 5 backups, the mirror, epoch, lastPublished) and the BFSL callbacks
// =============================================================================

#include "SlotLedger.h"

#include "Config.h"
#include "CoSaveFile.h"
#include "SlotLedgerFiles.h"
#include "SlotNotice.h"
#include "TrackedMutex.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <mutex>
#include <random>

namespace BlackFlame::SlotLedger
{
    namespace
    {
        namespace fs = std::filesystem;
        using SlotLedgerModel::Ledger;
        using SlotLedgerModel::Mismatch;

        constexpr std::size_t kSavePathChars = 0x104;            // PrepareFileSavePath's a_dst (BSSaveDataSystemUtility.h:70)
        constexpr std::size_t kMaxCoSaves = 10000;                // a save folder with more is not scanned past this
        constexpr std::uintmax_t kMaxCoSaveBytes = 256ull << 20;  // a co-save larger than this is not read (counted bad)
        constexpr const char* kFolderName = "HeartOfMagic_BlackFlame";
        constexpr std::string_view kSaveExtension = ".ess";
        constexpr std::string_view kCoSaveExtension = ".skse";

        TrackedMutex g_mutex;  // everything below
        Ledger g_ledger;
        Ledger g_fill;         // what the slot forms are filled from this session
        Status g_status;
        LoadedSave g_loaded;
        std::atomic<std::uint32_t> g_session{ 0 };
        std::function<void(const std::vector<Mismatch>&, MismatchWhen)> g_handler;

        std::string Text(const fs::path& a_path) { return SlotLedgerFiles::PathText(a_path); }

        bool EndsWithName(std::string_view a_path, std::string_view a_name)
        {
            return a_path.size() >= a_name.size() &&
                   _strnicmp(a_path.data() + a_path.size() - a_name.size(), a_name.data(), a_name.size()) == 0;
        }

        // The ledger's full path in the folder the game saves to, or "" with a_why
        fs::path LedgerPath(std::string& a_why)
        {
            auto* utility = RE::BSWin32SaveDataSystemUtility::GetSingleton();
            if (!utility) {
                a_why = "no BSWin32SaveDataSystemUtility";
                return {};
            }
            std::array<char, kSavePathChars> buffer{};
            errno_t code = 0;
            const bool ok = [&]() {
                try {
                    // through the base: CommonLib declares the override but defines no body; this is the engine's vtable call
                    code = static_cast<RE::BSSaveDataSystemUtility*>(utility)->PrepareFileSavePath(kLedgerFileName, buffer.data(), false, false);
                    return true;
                } catch (...) {
                    return false;
                }
            }();
            buffer.back() = '\0';
            const std::string path(buffer.data());
            if (!ok || code != 0 || path.empty()) {
                a_why = std::format("PrepareFileSavePath(\"{}\") failed (error {}, path '{}')", kLedgerFileName, code, path);
                return {};
            }
            if (!EndsWithName(path, kLedgerFileName)) {
                a_why = std::format("PrepareFileSavePath(\"{}\") gave '{}', which does not end with the name: refused", kLedgerFileName, path);
                return {};
            }
            logger::info("BlackFlame: slots: PrepareFileSavePath(\"{}\") = '{}'", kLedgerFileName, path);
            return fs::path(path);  // the system code page's bytes, as MSVC's narrow path reads them
        }

        std::uint64_t NewEpoch()
        {
            std::random_device device;
            const std::uint64_t high = device();
            const std::uint64_t low = device();
            const auto now = static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
            return ((high << 32) | low) ^ now;
        }

        std::optional<std::vector<unsigned char>> ReadFile(const fs::path& a_file, std::uintmax_t a_max)
        {
            std::error_code ec;
            const auto size = fs::file_size(a_file, ec);
            if (ec || size > a_max) {
                return std::nullopt;
            }
            std::ifstream in(a_file, std::ios::binary);
            if (!in) {
                return std::nullopt;
            }
            std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            return bytes.size() == size ? std::optional(std::move(bytes)) : std::nullopt;
        }

        // v3.3 #6: no ledger at all - the union of every co-save's BFSL in the save folder (under g_mutex)
        void UnionFromCoSaves(Ledger& a_ledger)
        {
            std::error_code ec;
            std::size_t seen = 0;
            for (const auto& item : fs::directory_iterator(g_status.saveFolder, ec)) {
                std::error_code fileError;
                if (!item.is_regular_file(fileError) || _wcsicmp(item.path().extension().c_str(), L".skse") != 0) {
                    continue;
                }
                if (++seen > kMaxCoSaves) {
                    ++g_status.coSavesBad;  // not read: counted, so new slots are refused
                    break;
                }
                const auto bytes = ReadFile(item.path(), kMaxCoSaveBytes);
                const auto read = bytes ? CoSaveFile::ReadBfsl(bytes->data(), bytes->size()) : CoSaveFile::BfslRead{};
                if (read.state == CoSaveFile::BfslState::kBad) {
                    ++g_status.coSavesBad;
                    logger::warn("BlackFlame: slots: total loss: co-save '{}' {}", Text(item.path()), bytes ? read.why : "could not be read");
                    continue;
                }
                ++g_status.coSavesRead;
                if (read.state == CoSaveFile::BfslState::kRead) {
                    ++g_status.coSavesWithSlots;
                    (void)SlotLedgerModel::Merge(a_ledger, SlotLedgerModel::FromSnapshot(read.snapshot), g_status.conflicts,
                        "co-save " + Text(item.path().filename()));
                }
            }
            if (ec) {
                ++g_status.coSavesBad;
                logger::warn("BlackFlame: slots: total loss: the save folder '{}' could not be listed: {}", Text(g_status.saveFolder),
                    ec.message());
            }
        }

        bool SaveLocked(const char* a_reason)
        {
            if (!g_status.available) {
                return false;
            }
            ++g_ledger.version;
            std::string mainWhy;
            std::string mirrorWhy;
            const bool ok = SlotLedgerFiles::SaveAll(g_ledger, g_status.main, g_status.mirror, mainWhy, mirrorWhy);
            if (ok) {
                logger::info("BlackFlame: slots: ledger version {} written ({}): {} slots{}{}", g_ledger.version, a_reason,
                    g_ledger.entries.size(), mainWhy.empty() ? "" : "; " + mainWhy, mirrorWhy.empty() ? "" : "; " + mirrorWhy);
            } else {
                logger::error("BlackFlame: slots: the ledger could not be written ({}): {}{}", a_reason, mainWhy,
                    mirrorWhy.empty() ? "" : "; " + mirrorWhy);
            }
            return ok;
        }

        void CallHandler(const std::vector<Mismatch>& a_mismatches, MismatchWhen a_when)
        {
            std::function<void(const std::vector<Mismatch>&, MismatchWhen)> handler;
            {
                std::lock_guard lock(g_mutex);
                handler = g_handler;
            }
            if (handler) {
                handler(a_mismatches, a_when);
            }
        }

        // The save's pairs against the session's fill; the save wins in the ledger (saved at once)
        std::vector<Mismatch> CompareAndApply(const SlotCoSave::Snapshot& a_save, const char* a_what)
        {
            std::lock_guard lock(g_mutex);
            auto mismatches = SlotLedgerModel::Compare(g_fill, a_save);
            if (mismatches.empty()) {
                return mismatches;
            }
            SlotLedgerModel::ApplySave(g_ledger, mismatches, g_session.load());
            for (const auto& m : mismatches) {
                logger::warn("BlackFlame: slots: {}: slot {:X} is {} in the save, {} this session", a_what, m.slot,
                    SlotLedgerModel::SourceText(m.save.plugin, m.save.sourceLocalId), m.unknown ? "unassigned" : m.session);
            }
            (void)SaveLocked("a save's slot pairs differ: the save's win");
            return mismatches;
        }

        void OnSave(SKSE::SerializationInterface* a_intfc)
        {
            SlotCoSave::Snapshot snapshot;
            {
                std::lock_guard lock(g_mutex);
                if (!g_status.available) {
                    return;
                }
                snapshot = SlotLedgerModel::ToSnapshot(g_ledger);
            }
            const auto result = SlotCoSave::Write(*a_intfc, snapshot);  // checks, then opens the record itself
            if (result != SlotCoSave::Result::Ok) {
                logger::error("BlackFlame: slots: BFSL not written whole: {}", SlotCoSave::Describe(result));
            } else {
                logger::info("BlackFlame: slots: BFSL written: {} slots, epoch {:016x}, ledger version {}", snapshot.slots.size(),
                    snapshot.epoch, snapshot.ledgerVersion);
            }
        }

        void OnLoad(SKSE::SerializationInterface* a_intfc)
        {
            LoadedSave loaded;
            loaded.seen = true;
            std::uint32_t type = 0;
            std::uint32_t version = 0;
            std::uint32_t length = 0;
            SlotCoSave::Snapshot snapshot;
            while (a_intfc->GetNextRecordInfo(type, version, length)) {
                if (type != SlotCoSave::kRecordType) {
                    continue;
                }
                loaded.hadRecord = true;
                const auto result = SlotCoSave::Read(*a_intfc, version, length, snapshot);
                loaded.read = result == SlotCoSave::Result::Ok;
                loaded.why = SlotCoSave::Describe(result);
            }
            bool available = false;
            {
                std::lock_guard lock(g_mutex);
                available = g_status.available;
            }
            if (loaded.hadRecord && !loaded.read && available) {
                logger::warn("BlackFlame: slots: the loaded save's BFSL could not be read ({}): every gate closes", loaded.why);
                CallHandler({}, MismatchWhen::kUnreadable);
            } else if (loaded.read && available) {
                loaded.slots = snapshot.slots.size();
                const auto mismatches = CompareAndApply(snapshot, "the loaded save");
                loaded.mismatches = mismatches.size();
                if (!mismatches.empty()) {
                    CallHandler(mismatches, MismatchWhen::kAfterLoad);
                }
                logger::info("BlackFlame: slots: the loaded save's BFSL: {} slots, epoch {:016x}, {} differ from this session's",
                    snapshot.slots.size(), snapshot.epoch, mismatches.size());
            }
            std::lock_guard lock(g_mutex);
            g_loaded = loaded;
        }

        void OnRevert(SKSE::SerializationInterface*)
        {
            std::lock_guard lock(g_mutex);
            g_loaded = {};
        }

        // The co-save beside a save named by kPreLoadGame's data: a bare name, a name with .ess, or a full path
        fs::path CoSavePathFor(std::string a_name)
        {
            while (!a_name.empty() && a_name.back() == '\0') {
                a_name.pop_back();
            }
            if (a_name.size() > kSaveExtension.size() && EndsWithName(a_name, kSaveExtension)) {
                a_name.resize(a_name.size() - kSaveExtension.size());
            }
            const bool full = a_name.find(':') != std::string::npos || a_name.starts_with("\\\\");
            fs::path path = full ? fs::path(a_name) : g_status.saveFolder / a_name;
            path += std::string(kCoSaveExtension);
            return path;
        }
    }

    bool Load()
    {
        const auto start = std::chrono::steady_clock::now();
        std::lock_guard lock(g_mutex);
        g_status = {};
        std::string why;
        g_status.main = LedgerPath(why);
        if (g_status.main.empty()) {
            g_status.why = why;
            logger::error("BlackFlame: slots: no ledger path ({}) - no slots this session", why);
            return false;
        }
        g_status.saveFolder = g_status.main.parent_path();
        if (const auto log = SKSE::log::log_directory()) {
            g_status.mirror = *log / kFolderName / kLedgerFileName;
        }
        const auto loaded = SlotLedgerFiles::LoadAll(g_status.main, g_status.mirror);
        for (const auto& note : loaded.notes) {
            logger::info("BlackFlame: slots: ledger {}", note);
        }
        g_status.conflicts = loaded.conflicts;
        g_status.filesRead = static_cast<std::size_t>(loaded.readable);
        if (loaded.ledger) {
            g_ledger = *loaded.ledger;
            g_status.restoredFromBackup = !loaded.mainReadable;
        } else {
            g_ledger = {};
            g_ledger.epoch = NewEpoch();
            g_status.totalLoss = true;
            UnionFromCoSaves(g_ledger);
            if (g_status.coSavesBad > 0) {
                g_status.allowNew = false;
                g_status.why = std::format("no ledger, backup or mirror, and {} co-save(s) could not be read whole: no new slot "
                                           "this session (one of them may hold slots)", g_status.coSavesBad);
            }
            logger::warn("BlackFlame: slots: no ledger, backup or mirror ({} unreadable): {} co-save(s) read, {} with a slot list, "
                         "{} unreadable; {} slots from their union; new epoch {:016x}", loaded.unreadable, g_status.coSavesRead,
                g_status.coSavesWithSlots, g_status.coSavesBad, g_ledger.entries.size(), g_ledger.epoch);
        }
        for (const auto& conflict : g_status.conflicts) {
            logger::warn("BlackFlame: slots: conflict: {}", conflict);
        }
        ++g_ledger.sessions;
        g_session.store(g_ledger.sessions);
        g_status.available = true;
        logger::info("BlackFlame: slots: ledger in use: {} slots, epoch {:016x}, version {}, session {}{}{}; save folder '{}', mirror "
                     "'{}', {} ms", g_ledger.entries.size(), g_ledger.epoch, g_ledger.version, g_ledger.sessions,
            g_status.restoredFromBackup ? ", restored from a backup or the mirror" : "", g_status.allowNew ? "" : ", NEW SLOTS REFUSED",
            Text(g_status.saveFolder), Text(g_status.mirror),
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
        return true;
    }

    Status GetStatus()
    {
        std::lock_guard lock(g_mutex);
        return g_status;
    }

    void Edit(const std::function<void(Ledger&)>& a_work)
    {
        std::lock_guard lock(g_mutex);
        a_work(g_ledger);
    }

    Ledger Copy()
    {
        std::lock_guard lock(g_mutex);
        return g_ledger;
    }

    std::uint32_t Session() noexcept
    {
        return g_session.load();
    }

    bool Save(const char* a_reason)
    {
        std::lock_guard lock(g_mutex);
        return SaveLocked(a_reason);
    }

    void SetSessionFill(Ledger a_fill)
    {
        std::lock_guard lock(g_mutex);
        g_fill = std::move(a_fill);
    }

    void NoteRefilled(std::uint16_t a_slot, const SlotCoSave::Slot& a_save)
    {
        std::lock_guard lock(g_mutex);
        SlotLedgerModel::ApplySave(g_fill, { Mismatch{ a_slot, a_save, false, "" } }, g_session.load());
    }

    void SetMismatchHandler(std::function<void(const std::vector<Mismatch>&, MismatchWhen)> a_handler)
    {
        std::lock_guard lock(g_mutex);
        g_handler = std::move(a_handler);
    }

    void RegisterCoSave()
    {
        auto* serialization = SKSE::GetSerializationInterface();
        if (!serialization) {
            logger::error("BlackFlame: slots: no SKSE serialization interface - no BFSL in the saves (a lost ledger cannot be "
                          "restored from them)");
            return;
        }
        serialization->SetUniqueID(SlotCoSave::kUniqueId);
        serialization->SetSaveCallback(OnSave);
        serialization->SetLoadCallback(OnLoad);
        serialization->SetRevertCallback(OnRevert);
        logger::info("BlackFlame: slots: co-save registered (unique id 'HBFS', record BFSL v{})", SlotCoSave::kVersion);
    }

    void OnPreLoadGame(const void* a_data, std::uint32_t a_length)
    {
        fs::path coSave;
        {
            std::lock_guard lock(g_mutex);
            if (!g_status.available || !a_data || a_length == 0) {
                return;
            }
            coSave = CoSavePathFor(std::string(static_cast<const char*>(a_data), a_length));
        }
        const bool refill = GetConfig().slotPreflight;
        std::error_code ec;
        if (!fs::exists(coSave, ec)) {
            logger::info("BlackFlame: slots: preflight: no co-save at '{}' (the message's name was {} bytes): nothing to compare",
                Text(coSave), a_length);
            return;
        }
        const auto bytes = ReadFile(coSave, kMaxCoSaveBytes);
        const auto read = bytes ? CoSaveFile::ReadBfsl(bytes->data(), bytes->size()) : CoSaveFile::BfslRead{};
        if (read.state == CoSaveFile::BfslState::kBad) {
            logger::warn("BlackFlame: slots: preflight: '{}' could not be read whole ({}){}", Text(coSave), bytes ? read.why : "not opened",
                refill ? ": every gate closes" : " (slotPreflight off: only logged)");
            if (refill) {
                CallHandler({}, MismatchWhen::kUnreadable);
            }
            return;
        }
        if (read.state != CoSaveFile::BfslState::kRead) {
            logger::info("BlackFlame: slots: preflight: '{}': {}", Text(coSave), CoSaveFile::Describe(read.state));
            return;
        }
        if (!refill) {
            Ledger fill;
            {
                std::lock_guard lock(g_mutex);
                fill = g_fill;
            }
            logger::info("BlackFlame: slots: preflight: '{}' holds {} slots, {} differ from this session's (slotPreflight off: not "
                         "refilled; the load callback handles them)", Text(coSave), read.snapshot.slots.size(),
                SlotLedgerModel::Compare(fill, read.snapshot).size());
            return;
        }
        const auto mismatches = CompareAndApply(read.snapshot, "preflight");
        logger::info("BlackFlame: slots: preflight: '{}' holds {} slots, {} differ: refilled before the load", Text(coSave),
            read.snapshot.slots.size(), mismatches.size());
        if (!mismatches.empty()) {
            CallHandler(mismatches, MismatchWhen::kPreflight);
        }
    }

    LoadedSave LastLoadedSave()
    {
        std::lock_guard lock(g_mutex);
        return g_loaded;
    }

    bool RoundTripNow(std::string& a_detail)
    {
        const auto snapshot = SlotLedgerModel::ToSnapshot(Copy());
        struct Buffer
        {
            std::vector<unsigned char> bytes;
            std::size_t at = 0;
            bool OpenRecord(std::uint32_t, std::uint32_t) { return true; }
            bool WriteRecordData(const void* a_d, std::uint32_t a_n)
            {
                bytes.insert(bytes.end(), static_cast<const unsigned char*>(a_d), static_cast<const unsigned char*>(a_d) + a_n);
                return true;
            }
            std::uint32_t ReadRecordData(void* a_d, std::uint32_t a_n)
            {
                const auto n = (std::min)(static_cast<std::size_t>(a_n), bytes.size() - at);
                std::memcpy(a_d, bytes.data() + at, n);
                at += n;
                return static_cast<std::uint32_t>(n);
            }
        } buffer;
        SlotCoSave::Snapshot back;
        const auto written = SlotCoSave::Write(buffer, snapshot);
        const auto read = written == SlotCoSave::Result::Ok ?
                              SlotCoSave::Read(buffer, SlotCoSave::kVersion, static_cast<std::uint32_t>(buffer.bytes.size()), back) :
                              written;
        const auto stored = SlotCoSave::Canonical(snapshot);  // names lower-cased, slots sorted: what a save holds
        const bool same = read == SlotCoSave::Result::Ok && back.slots == stored.slots && back.epoch == snapshot.epoch;
        a_detail = std::format("{} slots, {} bytes: write {}, read {}{}", snapshot.slots.size(), buffer.bytes.size(),
            SlotCoSave::Describe(written), SlotCoSave::Describe(read), same ? ", equal" : ", NOT equal");
        return same;
    }
}

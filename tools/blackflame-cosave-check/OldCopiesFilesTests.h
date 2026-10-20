#pragma once

// =============================================================================
// OLD COPIES FILES TESTS - the old-copies cleanup's file side (OldCopiesFiles.h) and keep set files (OldCopiesKeepsets.h)
// =============================================================================
// In a fresh folder under the system temp folder, removed after: RootSafe down from the base (the base itself or a folder
// on the way a junction: refused), ListFolder and RemoveListed (a plain key folder removed; one holding a junction, or one
// that is a junction, left whole and the junction's target untouched), FinalPath through a junction, the overwrite root
// from a sample, keep set files and the registry (union, a registered epoch without its file, a file or registry that does
// not read), a mirror of another epoch adopted at start and the own keep set merged after a late write (review round 2).
// Junctions are made with FSCTL_SET_REPARSE_POINT (a mount point needs no privilege); when that fails the
// junction cases are skipped with the reason. Included by main.cpp.

#include "../../plugins/BlackFlame/src/OldCopiesFiles.h"
#include "../../plugins/BlackFlame/src/OldCopiesKeepsets.h"

#include <winioctl.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace OldCopiesFilesTests
{
    namespace fs = std::filesystem;
    namespace Files = BlackFlame::OldCopiesFiles;
    namespace Keepsets = BlackFlame::OldCopiesKeepsets;

    inline void Write(const fs::path& a_file, const std::string& a_text)
    {
        std::ofstream(a_file, std::ios::binary) << a_text;
    }

    // a_link (made here, empty) as a junction to a_target; false with a_why
    inline bool MakeJunction(const fs::path& a_link, const fs::path& a_target, std::string& a_why)
    {
        std::error_code ec;
        fs::create_directory(a_link, ec);
        if (ec) {
            a_why = "the junction's folder could not be made: " + ec.message();
            return false;
        }
        const std::wstring target = fs::absolute(a_target).lexically_normal().wstring();
        const std::wstring substitute = L"\\??\\" + target;
        constexpr std::size_t kHeader = 8;   // ReparseTag, ReparseDataLength, Reserved
        constexpr std::size_t kOffsets = 8;  // the mount point buffer's four name offsets and lengths
        const std::size_t names = (substitute.size() + 1 + target.size() + 1) * sizeof(wchar_t);
        std::vector<unsigned char> buffer(kHeader + kOffsets + names, 0);
        const auto put16 = [&](std::size_t a_at, std::size_t a_value) {
            const auto value = static_cast<WORD>(a_value);
            std::memcpy(buffer.data() + a_at, &value, sizeof(value));
        };
        const DWORD tag = IO_REPARSE_TAG_MOUNT_POINT;
        std::memcpy(buffer.data(), &tag, sizeof(tag));
        put16(4, kOffsets + names);
        put16(8, 0);
        put16(10, substitute.size() * sizeof(wchar_t));
        put16(12, (substitute.size() + 1) * sizeof(wchar_t));
        put16(14, target.size() * sizeof(wchar_t));
        std::memcpy(buffer.data() + kHeader + kOffsets, substitute.c_str(), substitute.size() * sizeof(wchar_t));
        std::memcpy(buffer.data() + kHeader + kOffsets + (substitute.size() + 1) * sizeof(wchar_t), target.c_str(),
            target.size() * sizeof(wchar_t));
        const HANDLE link = ::CreateFileW(a_link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (link == INVALID_HANDLE_VALUE) {
            a_why = "the junction's folder does not open (Windows error " + std::to_string(::GetLastError()) + ")";
            return false;
        }
        DWORD returned = 0;
        const bool ok = ::DeviceIoControl(link, FSCTL_SET_REPARSE_POINT, buffer.data(), static_cast<DWORD>(buffer.size()), nullptr, 0,
            &returned, nullptr);
        const DWORD error = ::GetLastError();
        ::CloseHandle(link);
        if (!ok) {
            a_why = "FSCTL_SET_REPARSE_POINT failed (Windows error " + std::to_string(error) + ")";
        }
        return ok;
    }

    inline fs::path Copies(const fs::path& a_base)
    {
        return a_base / L"meshes" / L"heartofmagic" / L"blackflame" / L"copies";
    }

    template <class Check, class Skip>
    void RunKeepsets(Check& a_check, Skip& a_skip, const fs::path& a_temp)
    {
        (void)a_skip;
        const auto folder = a_temp / L"staging" / Keepsets::kFolderName;
        constexpr std::uint64_t kEpochA = 0x00000000000000a1ull;
        constexpr std::uint64_t kEpochB = 0x00000000000000b2ull;
        std::string note;
        std::string why;
        const bool wrote = Keepsets::WriteKeepset(folder, kEpochA, { 1, 2 }, true, note, why) && Keepsets::Register(folder, kEpochA, why);
        auto all = Keepsets::ReadAll(folder);
        a_check(wrote && all.ok && all.keys == Keepsets::KeySet{ 1, 2 } && all.files == 1 && all.registered == 1 && note.empty(),
            "old copies: a keep set file and its registered epoch read back as the union (" + why + all.why + ")");

        const bool merged = Keepsets::WriteKeepset(folder, kEpochA, { 3 }, true, note, why);
        all = Keepsets::ReadAll(folder);
        const bool mergedOk = merged && all.ok && all.keys == Keepsets::KeySet{ 1, 2, 3 };
        const bool replaced = Keepsets::WriteKeepset(folder, kEpochA, { 4 }, false, note, why);
        all = Keepsets::ReadAll(folder);
        a_check(mergedOk && replaced && all.ok && all.keys == Keepsets::KeySet{ 4 },
            "old copies: a ledger write merges its keys into the epoch's keep set file; the cleanup's own write replaces it");

        const bool registeredB = Keepsets::Register(folder, kEpochB, why);
        all = Keepsets::ReadAll(folder);
        a_check(registeredB && !all.ok && all.why.find(BlackFlame::CopyContract::Hex16(kEpochB)) != std::string::npos,
            "old copies: a registered epoch without a keep set file stops the cleanup (" + all.why + ")");

        const bool wroteB = Keepsets::WriteKeepset(folder, kEpochB, { 9 }, false, note, why);
        all = Keepsets::ReadAll(folder);
        a_check(wroteB && all.ok && all.keys == Keepsets::KeySet{ 4, 9 } && all.files == 2 && all.registered == 2,
            "old copies: every epoch's keep set file is in the union (another MO2 profile's keys kept)");

        Write(folder / L"00000000000000c3.json", "{\"format\":1,\"epoch\":\"00000000000000c3\",\"keys\":[\"0123\"]}");
        all = Keepsets::ReadAll(folder);
        a_check(!all.ok && all.why.find("does not read") != std::string::npos,
            "old copies: a keep set file that does not read stops the cleanup (" + all.why + ")");
        fs::remove(folder / L"00000000000000c3.json");
        Write(folder / L"00000000000000d4.json", "{\"format\":1,\"epoch\":\"00000000000000a1\",\"keys\":[]}");
        all = Keepsets::ReadAll(folder);
        a_check(!all.ok, "old copies: a keep set file whose epoch is not its name's stops the cleanup");
        fs::remove(folder / L"00000000000000d4.json");
        Write(folder / L"notes.txt", "x");
        Write(folder / L"00000000000000a1.json.tmp", "cut");
        all = Keepsets::ReadAll(folder);
        a_check(all.ok && all.files == 2 && !Keepsets::EpochOfName("00000000000000a1.json.tmp") &&
                    !Keepsets::EpochOfName("registry.json") && Keepsets::EpochOfName("00000000000000a1.json") == kEpochA &&
                    !Keepsets::EpochOfName("00000000000000A1.json"),
            "old copies: only <16 lower-case hex>.json files are keep sets (a .tmp, the registry, other files are not)");

        Write(folder / Keepsets::kRegistryName, "{\"format\":1,\"epochs\":[\"x\"]}");
        all = Keepsets::ReadAll(folder);
        const bool refused = !Keepsets::Register(folder, kEpochA, why);
        Write(folder / Keepsets::kRegistryName, "{\"format\":1,\"epo");
        const auto cut = Keepsets::ReadAll(folder);
        const bool cutRefused = !Keepsets::Register(folder, kEpochA, why);
        a_check(!all.ok && refused && !cut.ok && cutRefused && cut.why.find("registry") != std::string::npos,
            "old copies: a registry that does not read (a bad epoch, or cut short) stops the cleanup and is never replaced");

        const auto none = Keepsets::ReadAll(a_temp / L"no such folder");
        a_check(none.ok && none.keys.empty() && none.files == 0,
            "old copies: no keep set folder yet is no keep set and no registered epoch (the own file is written before the read)");
    }

    // A ledger of a_epoch whose slot 0x800 published a model in copies\<a_key>\ (a mirror's content)
    inline BlackFlame::SlotLedgerModel::Ledger MirrorLedger(std::uint64_t a_epoch, std::uint64_t a_key)
    {
        namespace LM = BlackFlame::SlotLedgerModel;
        LM::Ledger ledger;
        ledger.epoch = a_epoch;
        (void)LM::Assign(ledger, { { BlackFlame::CopyContract::SlotType::kPROJ, "Base.esp", 1 } }, true, 1);
        if (!ledger.entries.empty()) {
            LM::Object model;
            model.key = a_key;
            model.path = "meshes\\heartofmagic\\blackflame\\copies\\" + BlackFlame::CopyContract::Hex16(a_key) + "\\copy.nif";
            LM::SetPublished(ledger.entries.front(), model);
        }
        return ledger;
    }

    // Review round 2: a mirror of another epoch adopted at start (Codex P1); the own keep set merged after a late write
    template <class Check>
    void RunAdoption(Check& a_check, const fs::path& a_temp)
    {
        namespace LM = BlackFlame::SlotLedgerModel;
        const auto keepsets = a_temp / L"staging2" / Keepsets::kFolderName;
        const auto mirrors = a_temp / L"log";
        fs::create_directories(mirrors);
        constexpr std::uint64_t kOwn = 0x00000000000000e5ull;    // this session's epoch
        constexpr std::uint64_t kOther = 0x00000000000000f6ull;  // a profile that ran an older build (no keep set)
        constexpr std::uint64_t kOtherKey = 0x0f0f0f0f0f0f0f0full;
        std::string why;
        const bool wrote = BlackFlame::SlotLedgerFiles::WriteReplacing(mirrors / L"HoMBF_slots.0000abcd.json",
                               LM::ToJson(MirrorLedger(kOther, kOtherKey)).dump(1) + "\n", why) &&
                           BlackFlame::SlotLedgerFiles::WriteReplacing(mirrors / L"HoMBF_slots.1111abcd.json",
                               LM::ToJson(MirrorLedger(kOwn, 0x1234ull)).dump(1) + "\n", why);
        Write(mirrors / L"HoMBF_slots.json", "not a mirror name: never read");
        const auto read = Keepsets::ReadMirrors(mirrors);
        const auto adopted = Keepsets::AdoptMirrors(keepsets, read.ledgers, kOwn);
        auto all = Keepsets::ReadAll(keepsets);
        a_check(wrote && read.ok && read.ledgers.size() == 2 && adopted.epochs == 1 && adopted.notes.empty() && all.ok &&
                    all.registered == 1 && all.files == 1 && all.keys.count(kOtherKey) == 1 && all.keys.count(0x1234ull) == 0,
            "old copies: at start a mirror of another epoch is registered and its keys written as that epoch's keep set (its "
            "copies protected before this session's mirror write replaces it); this epoch's own mirror is not adopted (" + why +
                read.why + all.why + ")");

        std::string note;
        const bool richer = Keepsets::WriteKeepset(keepsets, kOther, { 0x77 }, false, note, why);
        const auto again = Keepsets::AdoptMirrors(keepsets, read.ledgers, kOwn);
        all = Keepsets::ReadAll(keepsets);
        a_check(richer && again.epochs == 1 && all.ok && all.keys.count(0x77) == 1 && all.keys.count(kOtherKey) == 1,
            "old copies: adopting a mirror again merges into the epoch's keep set file (what its own sessions wrote stays)");

        Write(mirrors / L"HoMBF_slots.2222abcd.json", "{\"cut");
        const auto bad = Keepsets::ReadMirrors(mirrors);
        a_check(!bad.ok && bad.bad == 1 && bad.ledgers.size() == 2 && bad.why.find("2222abcd") != std::string::npos,
            "old copies: a mirror that does not read is named (the cleanup stops on it) and the others still read");

        const bool ownWrote = Keepsets::WriteKeepset(keepsets, kOwn, { 0x10 }, true, note, why);  // a ledger write
        const bool lateMerge = Keepsets::MergeOwnKeepset(4, 5);
        const bool own = Keepsets::WriteKeepset(keepsets, kOwn, { 0x20 }, lateMerge, note, why);  // the cleanup's write
        all = Keepsets::ReadAll(keepsets);
        const bool merged = all.keys.count(0x10) == 1 && all.keys.count(0x20) == 1;
        const bool plain = !Keepsets::MergeOwnKeepset(5, 5) && Keepsets::WriteKeepset(keepsets, kOwn, { 0x20 }, false, note, why);
        all = Keepsets::ReadAll(keepsets);
        a_check(ownWrote && lateMerge && own && merged && plain && all.keys.count(0x10) == 0 && all.keys.count(0x20) == 1,
            "old copies: a ledger write after the cleanup's ledger copy makes its keep set write a merge (the late write's keys "
            "kept); with no write since, a replace (review round 2, reviewer 2)");
    }

    template <class Check, class Skip>
    void RunFiles(Check& a_check, Skip& a_skip, const fs::path& a_temp)
    {
        const auto base = a_temp / L"Data";
        const auto root = Copies(base);
        const auto key = root / L"0123456789abcdef";
        fs::create_directories(key);
        Write(key / L"copy.nif", "12345");
        Write(key / L"copy.dds", "123");
        bool present = false;
        std::string why;
        a_check(Files::RootSafe(base, root, present, why) && present, "old copies: a root of plain folders down from the base is safe");
        Files::Listing listing;
        const auto listed = Files::ListFolder(key, listing, why);
        a_check(listed == Files::Listed::kPlain && listing.files.size() == 2 && listing.bytes == 8 && listing.newest > 0 &&
                    Files::NowTicks() >= listing.newest,
            "old copies: a key folder of plain files lists with its sizes and its newest file's write time");
        std::uint64_t bytes = 0;
        a_check(Files::RemoveListed(key, listing, bytes, why) && bytes == 8 && !fs::exists(key),
            "old copies: a listed key folder is removed, files then folder (" + why + ")");

        bool absentPresent = true;
        a_check(Files::RootSafe(a_temp / L"Empty", Copies(a_temp / L"Empty"), absentPresent, why) && !absentPresent &&
                    !Files::RootSafe(fs::path(L"Data"), Copies(fs::path(L"Data")), present, why) &&
                    !Files::RootSafe(base, a_temp / L"elsewhere", present, why) && !Files::RootSafe(base, base, present, why),
            "old copies: an absent base is nothing to clean; a relative base, a root outside the base or the base itself refused");

        const auto outside = a_temp / L"Outside";
        fs::create_directories(outside);
        Write(outside / L"keep.txt", "must stay");
        Write(base / L"meshes" / L"file", "a file where a folder should be");
        a_check(!Files::RootSafe(base / L"meshes" / L"file", Copies(base / L"meshes" / L"file"), present, why),
            "old copies: a base that is a file is refused");

        const auto baseJunction = a_temp / L"DataLink";
        if (!MakeJunction(baseJunction, outside, why)) {
            a_skip("old copies: the junction cases (" + why + ")");
            return;
        }
        const auto onTheWay = a_temp / L"Data2";
        fs::create_directories(onTheWay / L"meshes");
        std::string junctionWhy;
        const bool madeOnTheWay = MakeJunction(onTheWay / L"meshes" / L"heartofmagic", outside, junctionWhy);
        fs::create_directories(outside / L"blackflame" / L"copies");
        a_check(!Files::RootSafe(baseJunction, Copies(baseJunction), present, why) && madeOnTheWay &&
                    !Files::RootSafe(onTheWay, Copies(onTheWay), present, why) && why.find("junction") != std::string::npos,
            "old copies: a base that is a junction, or a junction on the way down to the root, is refused (review round 1 Codex "
            "P2) (" + junctionWhy + why + ")");

        const auto holding = root / L"1111111111111111";
        fs::create_directories(holding);
        Write(holding / L"copy.nif", "x");
        const bool madeInside = MakeJunction(holding / L"sub", outside, junctionWhy);
        const auto linked = root / L"2222222222222222";
        const bool madeKey = MakeJunction(linked, outside, junctionWhy);
        a_check(madeInside && madeKey && Files::ListFolder(holding, listing, why) == Files::Listed::kNotPlain &&
                    Files::ListFolder(linked, listing, why) == Files::Listed::kNotPlain && fs::exists(outside / L"keep.txt") &&
                    fs::exists(holding / L"copy.nif"),
            "old copies: a key folder holding a junction, or one that is a junction, is left whole and the target untouched");

        const auto direct = Files::FinalPath(outside / L"keep.txt", why);
        const auto through = Files::FinalPath(baseJunction / L"keep.txt", why);
        a_check(direct && through && Files::Under(*direct, *through, false) && Files::Under(*through, *direct, false) &&
                    direct->native().rfind(L"\\\\?\\", 0) != 0 && Files::RootFromSample(*through, L"keep.txt") &&
                    Files::SameName(*Files::RootFromSample(*through, L"keep.txt"), direct->parent_path()),
            "old copies: FinalPath gives a junction's file its real path, without \\\\?\\ (" +
                (through ? Files::Text(*through) : why) + ")");
        ::RemoveDirectoryW((onTheWay / L"meshes" / L"heartofmagic").c_str());  // the junctions themselves, never their target
        ::RemoveDirectoryW((holding / L"sub").c_str());
        ::RemoveDirectoryW(linked.c_str());
        ::RemoveDirectoryW(baseJunction.c_str());
    }

    template <class Check, class Skip>
    void Run(Check& a_check, Skip& a_skip)
    {
        const std::filesystem::path overwrite = L"C:\\MO2\\overwrite";
        const auto root = Files::RootFromSample(L"C:\\MO2\\overwrite\\meshes\\heartofmagic\\blackflame\\copies\\0123\\copy.nif",
            L"meshes\\HeartOfMagic\\BlackFlame\\copies\\0123\\copy.nif");
        a_check(root && Files::SameName(*root, overwrite) &&
                    !Files::RootFromSample(L"C:\\MO2\\mods\\X\\meshes\\other\\copy.nif", L"meshes\\heartofmagic\\copy.nif") &&
                    !Files::RootFromSample(L"meshes\\a.nif", L"meshes\\a.nif") &&
                    Files::Under(overwrite, L"c:\\mo2\\OVERWRITE\\meshes\\a.nif") && !Files::Under(overwrite, L"C:\\MO2\\overwrite2\\a") &&
                    !Files::Under(overwrite, overwrite) && Files::Under(overwrite, overwrite, false) &&
                    !Files::Under(L"C:\\Game\\Data", L"C:\\MO2\\overwrite\\a") &&
                    Files::StripFinalPrefix(L"\\\\?\\C:\\a") == L"C:\\a" &&
                    Files::StripFinalPrefix(L"\\\\?\\UNC\\server\\share\\a") == L"\\\\server\\share\\a" &&
                    Files::StripFinalPrefix(L"C:\\a") == L"C:\\a",
            "old copies: the overwrite folder is a sample's real path less its Data path (case folded); 'under' is by whole "
            "components (overwrite2 is not overwrite); \\\\?\\ and \\\\?\\UNC\\ are taken off");

        std::error_code ec;
        const auto temp = fs::temp_directory_path(ec) / ("hom-oldcopies-" + std::to_string(::GetCurrentProcessId()));
        if (ec) {
            a_skip("old copies: no temp folder (" + ec.message() + ")");
            return;
        }
        fs::remove_all(temp, ec);
        fs::create_directories(temp, ec);
        RunFiles(a_check, a_skip, temp);
        RunKeepsets(a_check, a_skip, temp);
        RunAdoption(a_check, temp);
        fs::remove_all(temp, ec);
    }
}

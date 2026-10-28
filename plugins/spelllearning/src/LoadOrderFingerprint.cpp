#include "Common.h"
#include "AsciiText.h"
#include "LoadOrderFingerprint.h"
#include "ScanRecordRules.h"
#include "SpellScanner.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
    using ScanRecordRules::Fnv;
    using ScanRecordRules::PluginStamp;

    constexpr std::string_view kNameEnd = "\n";
    constexpr std::string_view kLightPluginsMark = "|light|\n";
    constexpr const char* kDataDir = "Data";

    // Set once by Current() on the game thread; read from any thread through Known()
    LoadOrderFingerprint::Value s_value;
    std::atomic<const LoadOrderFingerprint::Value*> s_published{ nullptr };

    // What the engine noted when it found the file (TESFile::fileData, the find data of
    // its Data folder walk - under MO2 the real file's, through its virtual file system)
    PluginStamp StampFromFindData(const RE::TESFile* file)
    {
        PluginStamp stamp;
        stamp.name = std::string(file->GetFilename());
        stamp.bytes = (static_cast<std::uint64_t>(file->fileData.fileSizeHi) << 32) | file->fileData.fileSizeLo;
        stamp.writeTime = static_cast<std::uint64_t>(file->fileData.lastWriteTime);
        return stamp;
    }

    // The same read from the file itself: the engine's own folder for it, and Data/
    // when that has no such file (the way TomeReach opens plugins, TomeReach.cpp).
    // Zeros when neither can be read.
    PluginStamp StampFromDisk(const RE::TESFile* file)
    {
        PluginStamp stamp;
        stamp.name = std::string(file->GetFilename());
        const std::string_view dir(file->path);
        const std::filesystem::path data(kDataDir);
        for (const auto& path : { (dir.empty() ? data : std::filesystem::path(dir)) / stamp.name, data / stamp.name }) {
            std::error_code ec;
            const auto bytes = std::filesystem::file_size(path, ec);
            if (ec) continue;
            const auto written = std::filesystem::last_write_time(path, ec);
            if (ec) continue;
            stamp.bytes = static_cast<std::uint64_t>(bytes);
            stamp.writeTime = static_cast<std::uint64_t>(written.time_since_epoch().count());
            break;
        }
        return stamp;
    }

    // The engine's find data holds this file's values: its name is the file's and it is not blank
    bool FindDataUsable(const RE::TESFile* file)
    {
        const auto& data = file->fileData;
        const std::string_view found(data.fileName, strnlen(data.fileName, sizeof(data.fileName)));
        return AsciiText::EqualsIgnoreCase(found, file->GetFilename()) &&
               (data.fileSizeLo != 0 || data.fileSizeHi != 0 || static_cast<std::uint64_t>(data.lastWriteTime) != 0);
    }

    // Each plugin's size and last-write time, in load order: from the engine's find data
    // when it holds that file's values (nothing read from disk), from a stat otherwise
    std::vector<PluginStamp> PluginStamps(const std::vector<const RE::TESFile*>& files)
    {
        std::vector<PluginStamp> stamps;
        stamps.reserve(files.size());
        const auto started = std::chrono::steady_clock::now();
        std::size_t fromFindData = 0;
        std::size_t unknown = 0;
        for (const auto* file : files) {
            if (FindDataUsable(file)) {
                stamps.push_back(StampFromFindData(file));
                ++fromFindData;
            } else {
                stamps.push_back(StampFromDisk(file));
                if (stamps.back().bytes == 0 && stamps.back().writeTime == 0) ++unknown;
            }
        }
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        logger::info("LoadOrderFingerprint: plugin sizes and write times of {} plugins: {} from the engine's find data, "
                     "{} from the files on disk ({} not found), {} ms", stamps.size(), fromFindData,
            stamps.size() - fromFindData, unknown, ms);
        return stamps;
    }
}

namespace LoadOrderFingerprint
{
    const Value& Current()
    {
        static const Value s_unknown{};
        if (const auto* known = s_published.load(std::memory_order_acquire)) return *known;

        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) return s_unknown;

        Value result;
        std::uint64_t hash = ScanRecordRules::kFnvOffsetBasis;
        std::vector<const RE::TESFile*> files;
        auto addFile = [&](const RE::TESFile* file) {
            if (!file) return;
            Fnv(hash, file->GetFilename());
            Fnv(hash, kNameEnd);
            files.push_back(file);
            ++result.plugins;
        };
        for (std::uint32_t i = 0; i < dh->GetLoadedModCount(); ++i) addFile(dh->GetLoadedMods()[i]);
        Fnv(hash, kLightPluginsMark);
        if (const auto* light = dh->GetLoadedLightMods()) {
            for (std::uint32_t i = 0; i < dh->GetLoadedLightModCount(); ++i) addFile(light[i]);
        }
        result.spells = dh->GetFormArray<RE::SpellItem>().size();
        Fnv(hash, std::to_string(result.spells));
        result.hash = ScanRecordRules::Hex(hash);
        result.scan = ScanRecordRules::ScanHash(result.hash, PluginStamps(files), SpellScanner::GateRulesFileHash(),
            ScanRecordRules::kScannerFormatVersion);

        s_value = std::move(result);
        s_published.store(&s_value, std::memory_order_release);
        return s_value;
    }

    const Value* Known() noexcept
    {
        return s_published.load(std::memory_order_acquire);
    }
}

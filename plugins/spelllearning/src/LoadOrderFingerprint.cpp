#include "Common.h"
#include "LoadOrderFingerprint.h"
#include "ScanRecordRules.h"
#include "SpellScanner.h"

#include <atomic>
#include <chrono>
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

    // Each plugin's size and last-write time, in load order. The engine's find data
    // when it holds them (nothing read from disk); a stat of every plugin otherwise.
    std::vector<PluginStamp> PluginStamps(const std::vector<const RE::TESFile*>& files)
    {
        std::vector<PluginStamp> stamps;
        stamps.reserve(files.size());
        if (files.empty()) return stamps;
        const auto started = std::chrono::steady_clock::now();
        const auto& first = files.front()->fileData;
        const bool findData = first.fileSizeLo != 0 || first.fileSizeHi != 0 ||
                              static_cast<std::uint64_t>(first.lastWriteTime) != 0;
        for (const auto* file : files) stamps.push_back(findData ? StampFromFindData(file) : StampFromDisk(file));
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        logger::info("LoadOrderFingerprint: plugin sizes and write times of {} plugins from {} in {} ms (first: '{}', "
                     "{} bytes)", stamps.size(), findData ? "the engine's find data" : "the files on disk", ms,
            stamps.front().name, stamps.front().bytes);
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

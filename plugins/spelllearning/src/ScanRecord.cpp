#include "Common.h"
#include "DataPaths.h"
#include "FileUtils.h"
#include "JsonText.h"
#include "PathText.h"
#include "ScanRecord.h"
#include "ScanRecordRules.h"
#include "SpellTomes.h"
#include "librarian/Librarian.h"
#include "tomereach/TomeReach.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace
{
    constexpr const char* kRecordFile = "Data/SKSE/Plugins/SpellLearning/spell_catalog_loadorder.json";
    constexpr const char* kRulesDir = "Data/SKSE/Plugins/SpellLearning/librarian";   // LibrarianInternal.h RULE_SUBDIR
    constexpr int kReadTries = 3;                                                    // a file held a moment (a virus scan)
    constexpr auto kReadRetryWait = std::chrono::milliseconds(25);
    constexpr int kRecordVersion = ScanRecordRules::kRecordVersion;
    constexpr int kRecordIndent = 2;
    constexpr std::size_t kReadChunk = 1 << 16;

    using ScanRecordRules::Fnv;
    using ScanRecordRules::Hex;
    using ScanRecordRules::kFnvOffsetBasis;

    struct FileHash
    {
        std::uintmax_t bytes = 0;
        std::string hash;   // 16 hex digits
    };

    // FNV-1a 64 of a file (ScanRecordRules::Fnv), read in chunks (the catalog is several MB)
    std::optional<FileHash> HashFileOnce(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return std::nullopt;
        std::uint64_t hash = kFnvOffsetBasis;
        FileHash result;
        std::string buffer(kReadChunk, '\0');
        while (file) {
            file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto got = file.gcount();
            Fnv(hash, std::string_view(buffer.data(), static_cast<std::size_t>(got)));
            result.bytes += static_cast<std::uintmax_t>(got);
        }
        if (file.bad()) return std::nullopt;
        result.hash = Hex(hash);
        return result;
    }

    std::optional<FileHash> HashFile(const std::filesystem::path& path)
    {
        for (int attempt = 1; attempt <= kReadTries; ++attempt) {
            if (auto result = HashFileOnce(path)) return result;
            if (attempt < kReadTries) std::this_thread::sleep_for(kReadRetryWait);
        }
        return std::nullopt;
    }

    // What else the catalog is made from: the catalog version and the librarian's
    // rule files (names and bytes, in name order). A Heart of Magic update with
    // new rules, or a rule pack added, makes a scan of the same load order due.
    // Empty when a rule file cannot be read.
    std::string RulesFingerprint()
    {
        std::uint64_t hash = kFnvOffsetBasis;
        Fnv(hash, std::to_string(Librarian::CATALOG_VERSION));
        Fnv(hash, "\n");
        std::error_code ec;
        if (!std::filesystem::is_directory(kRulesDir, ec)) return Hex(hash);
        std::vector<std::filesystem::path> files;
        for (std::filesystem::recursive_directory_iterator it(kRulesDir, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file(ec)) files.push_back(it->path());
        }
        if (ec) {
            logger::warn("ScanRecord: could not list {}: {}", kRulesDir, ec.message());
            return {};
        }
        std::sort(files.begin(), files.end());
        for (const auto& path : files) {
            const auto file = HashFile(path);
            if (!file) {
                logger::warn("ScanRecord: could not read {}", PathText::Utf8(path));
                return {};
            }
            Fnv(hash, PathText::Utf8(path.lexically_relative(kRulesDir)));
            Fnv(hash, "\n");
            Fnv(hash, file->hash);
            Fnv(hash, "\n");
        }
        return Hex(hash);
    }

    std::string TextField(const nlohmann::json& data, const char* key)
    {
        const auto found = data.find(key);
        return found != data.end() && found->is_string() ? found->get<std::string>() : std::string();
    }

    // The installed tome pairs' key now (SpellTomes::InstalledPairs, one snapshot), empty before
    // they were first read. A save's own pairs (Spell Research's script part) are not in it: the
    // main menu never has them, and a scan during play would otherwise make the next start scan again
    std::string TomePairsKey()
    {
        std::vector<std::pair<RE::FormID, RE::FormID>> pairs;
        if (!SpellTomes::InstalledPairs(pairs)) return {};
        return ScanRecordRules::TomePairsKey(std::move(pairs));
    }

    // The pairs key a write records: the scan's own (the snapshot its marks read) while the installed
    // pairs still give it. One that changed since (a re-run patch read, flags cleared after the marks)
    // gives none, so that catalog is not recorded; a save's own pairs changing does not matter
    std::string WrittenPairsKey(std::string_view scanPairs)
    {
        const std::string now = TomePairsKey();
        if (scanPairs.empty() || now != scanPairs) {
            logger::info("ScanRecord: the installed tome pairs are not the ones the scan's marks read (scan {}, now "
                         "{}) - not recorded", scanPairs.empty() ? "none" : scanPairs, now.empty() ? "none" : now);
            return {};
        }
        return now;
    }

    // The fingerprint the record holds and is matched by: the load order's scan
    // fingerprint with how the tome reach pass ended (empty while it runs) and the
    // tome pairs (a re-run SynthesizedPatch.json over the same plugins makes a scan due)
    std::string RecordFingerprint(const LoadOrderFingerprint::Value& loadOrder, std::string_view pairsKey)
    {
        return ScanRecordRules::WithTomePairs(ScanRecordRules::WithTomeReach(loadOrder.scan, TomeReach::OutcomeKey()), pairsKey);
    }

    // The record's writers (a full scan's catalog, the scan at game start's dump, a removal)
    // one at a time: the dump goes only into the record its scan wrote
    std::mutex s_recordMutex;
    std::atomic<std::uint64_t> s_writeCount{ 0 };

    void RemoveRecord(const char* why)
    {
        std::error_code ec;
        if (std::filesystem::remove(kRecordFile, ec)) {
            logger::info("ScanRecord: removed {} ({})", kRecordFile, why);
        } else if (ec) {
            logger::warn("ScanRecord: could not remove {} ({}): {}", kRecordFile, why, ec.message());
        }
    }
}

namespace ScanRecord
{
    std::string CatalogWritten(bool fullScan, const LoadOrderFingerprint::Value* loadOrder, std::string_view incomplete,
        std::string_view scanPairs) noexcept
    {
        try {
            std::lock_guard lock(s_recordMutex);
            const std::string pairsKey = loadOrder && fullScan ? WrittenPairsKey(scanPairs) : std::string();
            const std::string fingerprint = loadOrder ? RecordFingerprint(*loadOrder, pairsKey) : std::string();
            const auto decision = ScanRecordRules::DecideRecord(fullScan,
                loadOrder ? std::string_view(loadOrder->hash) : std::string_view(), fingerprint, incomplete);
            if (decision.action != ScanRecordRules::RecordAction::kWrite) {
                RemoveRecord(decision.why.c_str());
                return {};
            }
            const auto catalog = HashFile(DataPaths::kCatalog);
            if (!catalog) {
                RemoveRecord("the catalog just written could not be read back");
                return {};
            }
            // This write's own mark: a later writer's record has another (DumpWritten)
            const auto writeId = std::format("{}-{:x}-{}", loadOrder->hash,
                std::chrono::steady_clock::now().time_since_epoch().count(), ++s_writeCount);
            nlohmann::json data;
            data["version"] = kRecordVersion;
            data["loadOrder"] = loadOrder->hash;
            data["scan"] = fingerprint;
            data["tomeReach"] = TomeReach::OutcomeKey();
            data["tomePairs"] = pairsKey;
            data["plugins"] = loadOrder->plugins;
            data["spells"] = loadOrder->spells;
            data["catalogBytes"] = catalog->bytes;
            data["catalogHash"] = catalog->hash;
            data["rules"] = RulesFingerprint();
            data[ScanRecordRules::kWriteIdField] = writeId;
            if (!FileUtils::WriteAtomically(kRecordFile, JsonText::Dump(data, kRecordIndent), false)) {
                logger::warn("ScanRecord: could not write {} - the next game start scans again", kRecordFile);
                RemoveRecord("its write failed");
                return {};
            }
            logger::info("ScanRecord: catalog made under load order {} (scan fingerprint {}, {} plugins, {} spell forms)",
                loadOrder->hash, fingerprint, loadOrder->plugins, loadOrder->spells);
            return writeId;
        } catch (const std::exception& e) {
            logger::error("ScanRecord: the record was not kept: {}", e.what());
        } catch (...) {
            logger::error("ScanRecord: the record was not kept (unknown error)");
        }
        return {};
    }

    void Forget(const char* why) noexcept
    {
        try {
            std::lock_guard lock(s_recordMutex);
            RemoveRecord(why);
        } catch (...) {
            logger::error("ScanRecord: the record could not be removed");
        }
    }

    void DumpWritten(std::string_view writeId, std::string_view dumpText) noexcept
    {
        try {
            std::lock_guard lock(s_recordMutex);
            std::ifstream file(kRecordFile, std::ios::binary);
            if (!file.is_open()) return;
            auto data = nlohmann::json::parse(file, nullptr, false);
            file.close();
            if (data.is_discarded() || !data.is_object()) return;
            if (!ScanRecordRules::MarkDumpIfOwn(data, writeId, dumpText)) {
                logger::info("ScanRecord: another scan wrote the record since - the dump is not recorded (no tree graft from it)");
                return;
            }
            if (!FileUtils::WriteAtomically(kRecordFile, JsonText::Dump(data, kRecordIndent), false)) {
                logger::warn("ScanRecord: could not add the scan dump to {} - no tree graft from it", kRecordFile);
            }
        } catch (const std::exception& e) {
            logger::error("ScanRecord: the scan dump was not recorded: {}", e.what());
        } catch (...) {
            logger::error("ScanRecord: the scan dump was not recorded (unknown error)");
        }
    }

    std::string RecordedLoadOrder() noexcept
    {
        try {
            std::ifstream file(kRecordFile, std::ios::binary);
            if (!file.is_open()) return {};
            const auto data = nlohmann::json::parse(file, nullptr, false);
            return data.is_discarded() ? std::string() : TextField(data, "loadOrder");
        } catch (...) {
            return {};
        }
    }

    bool DumpMatches(std::string_view dumpText) noexcept
    {
        try {
            std::ifstream file(kRecordFile, std::ios::binary);
            if (!file.is_open()) return false;
            const auto data = nlohmann::json::parse(file, nullptr, false);
            return !data.is_discarded() && ScanRecordRules::DumpFits(data, dumpText);
        } catch (...) {
            return false;
        }
    }

    bool CatalogMatches(const LoadOrderFingerprint::Value& now) noexcept
    {
        try {
            const std::string fingerprint = RecordFingerprint(now, TomePairsKey());
            if (fingerprint.empty()) return false;
            std::ifstream file(kRecordFile, std::ios::binary);
            if (!file.is_open()) return false;
            const auto data = nlohmann::json::parse(file, nullptr, false);
            if (data.is_discarded() || !data.is_object()) {
                logger::warn("ScanRecord: {} could not be read - the catalog's load order is unknown", kRecordFile);
                return false;
            }
            if (!ScanRecordRules::RecordFits(data, fingerprint)) {
                const auto version = data.find("version");
                if (version == data.end() || !version->is_number_integer() || version->get<long long>() != kRecordVersion) {
                    logger::info("ScanRecord: {} is from another version - the catalog's load order is unknown", kRecordFile);
                }
                return false;
            }
            const auto catalog = HashFile(DataPaths::kCatalog);
            if (!catalog) return false;
            const auto bytes = data.find("catalogBytes");
            if (TextField(data, "catalogHash") != catalog->hash || bytes == data.end() || !bytes->is_number_unsigned() ||
                bytes->get<std::uintmax_t>() != catalog->bytes) {
                return false;
            }
            const std::string rules = TextField(data, "rules");
            if (rules.empty() || rules != RulesFingerprint()) {
                logger::info("ScanRecord: the librarian's rules changed since the catalog was made");
                return false;
            }
            return true;
        } catch (const std::exception& e) {
            logger::warn("ScanRecord: could not compare the record: {}", e.what());
        } catch (...) {
            logger::warn("ScanRecord: could not compare the record (unknown error)");
        }
        return false;
    }
}

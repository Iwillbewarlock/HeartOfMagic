#pragma once

#include "FileUtils.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string>
#include <system_error>

// =============================================================================
// CONFIG FILE - what reading config.json found (UIManager::OnLoadUnifiedConfig)
// =============================================================================
//
// No game types, so an offline check compiles it as it is. The caller merges
// the settings, logs what happened, writes the file when WritesDefaults says so
// and saves for the rest of the session as SaveModeFor says. Only a file that
// was read and is not a JSON object is moved aside; config.json.bak from the
// last good save is then read in its place and, when it is an object, becomes
// the new config.json (the .bak itself stays: nothing is at the path to move
// over it). One that could not be opened or read (held by an antivirus or a
// sync tool, blocked by its permissions), or a broken one that could not be
// moved, is never moved or written over: the defaults hold for that session
// and no save writes the file until a later load reads it.
namespace ConfigFile
{
    enum class State
    {
        Loaded,       ///< read, a JSON object: `config` holds it
        Missing,      ///< no file: defaults, legacy settings.json merged, file created
        Unreadable,   ///< could not check, open or read it: defaults this session, file untouched, no saves
        MovedAside,   ///< did not parse / not an object, moved to `movedTo`: the .bak's object
                      ///< (`fromBackup`) or else the defaults written, legacy not merged
        BrokenKept,   ///< did not parse / not an object, could not be moved: defaults this session, file untouched, no saves
    };

    /// How the panel's saves treat config.json until the next load
    enum class SaveMode
    {
        Normal,     ///< merge and write, the previous file kept as .bak
        NoBackup,   ///< merge and write, .bak left as it is (it holds the last good settings)
        Blocked,    ///< do not write: the file on disk was never read this session
    };

    struct LoadResult
    {
        State state = State::Missing;
        nlohmann::json config;          ///< the file's object when Loaded, the .bak's when fromBackup
        std::filesystem::path movedTo;  ///< where a MovedAside file went
        bool fromBackup = false;        ///< MovedAside: config.json.bak read in its place
        std::string detail;             ///< why it was not Loaded, for the log
        std::string backupDetail;       ///< MovedAside without fromBackup: why the .bak was not used
    };

    /// Write config.json: only when there was none, or it was moved aside
    [[nodiscard]] inline bool WritesDefaults(State state) noexcept
    {
        return state == State::Missing || state == State::MovedAside;
    }

    /// Merge the pre-unified settings.json: only into a first config.json, never over a broken one
    [[nodiscard]] inline bool MergesLegacy(State state) noexcept
    {
        return state == State::Missing;
    }

    /// Saves after this load. Defaults written over a broken file must not push
    /// the last good .bak out, and a file never read must not be written over.
    [[nodiscard]] inline SaveMode SaveModeFor(const LoadResult& result) noexcept
    {
        switch (result.state) {
            case State::Unreadable:
            case State::BrokenKept:
                return SaveMode::Blocked;
            case State::MovedAside:
                return result.fromBackup ? SaveMode::Normal : SaveMode::NoBackup;
            default:
                return SaveMode::Normal;
        }
    }

    namespace Detail
    {
        enum class ReadOutcome { Object, Unreadable, NotJson, NotObject };

        /// Read `path` whole and parse it; `out` gets the object, `why` the reason otherwise
        [[nodiscard]] inline ReadOutcome ReadObject(const std::filesystem::path& path,
                                                    nlohmann::json& out, std::string& why)
        {
            std::string text;
            {
                std::ifstream file(path, std::ios::binary);
                if (!file.is_open()) {
                    why = "cannot be opened";
                    return ReadOutcome::Unreadable;
                }
                text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
                if (file.bad()) {
                    why = "reading it failed";
                    return ReadOutcome::Unreadable;
                }
            }  // closed here: a file still open cannot be moved

            auto parsed = nlohmann::json::parse(text, nullptr, false);
            if (parsed.is_discarded()) {
                why = "not valid JSON";
                return ReadOutcome::NotJson;
            }
            if (!parsed.is_object()) {
                why = "not a JSON object";
                return ReadOutcome::NotObject;
            }
            out = std::move(parsed);
            return ReadOutcome::Object;
        }
    }

    [[nodiscard]] inline LoadResult Load(const std::filesystem::path& path)
    {
        LoadResult result;
        std::error_code ec;
        const bool exists = std::filesystem::exists(path, ec);
        if (ec) {
            result.state = State::Unreadable;
            result.detail = ec.message();
            return result;
        }
        if (!exists) {
            result.state = State::Missing;
            return result;
        }

        const auto outcome = Detail::ReadObject(path, result.config, result.detail);
        if (outcome == Detail::ReadOutcome::Object) {
            result.state = State::Loaded;
            return result;
        }
        if (outcome == Detail::ReadOutcome::Unreadable) {
            result.state = State::Unreadable;
            return result;
        }

        result.movedTo = FileUtils::MoveAside(path, ".broken");
        if (result.movedTo.empty()) {
            result.state = State::BrokenKept;
            return result;
        }
        result.state = State::MovedAside;

        // The last good save, if it is one: its settings instead of the defaults
        const auto backup = FileUtils::BackupPath(path);
        if (!std::filesystem::exists(backup, ec) || ec) {
            result.backupDetail = ec ? ec.message() : "missing";
        } else if (Detail::ReadObject(backup, result.config, result.backupDetail) == Detail::ReadOutcome::Object) {
            result.fromBackup = true;
        }
        return result;
    }
}

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
// the settings, logs what happened and writes the defaults when WritesDefaults
// says so. Only a file that was read and is not a JSON object is moved aside;
// one that could not be opened or read (held by an antivirus or a sync tool,
// blocked by its permissions) is never moved or written over - the defaults
// hold for that session only.
namespace ConfigFile
{
    enum class State
    {
        Loaded,       ///< read, a JSON object: `config` holds it
        Missing,      ///< no file: defaults, legacy settings.json merged, file created
        Unreadable,   ///< could not check, open or read it: defaults this session, file untouched
        MovedAside,   ///< did not parse / not an object, moved to `movedTo`: defaults written, nothing else merged
        BrokenKept,   ///< did not parse / not an object, could not be moved: defaults this session, file untouched
    };

    struct LoadResult
    {
        State state = State::Missing;
        nlohmann::json config;          ///< the file's object when Loaded
        std::filesystem::path movedTo;  ///< where a MovedAside file went
        std::string detail;             ///< why it was not Loaded, for the log
    };

    /// Create config.json with the defaults: only when there was none, or it was moved aside
    [[nodiscard]] inline bool WritesDefaults(State state) noexcept
    {
        return state == State::Missing || state == State::MovedAside;
    }

    /// Merge the pre-unified settings.json: only into a first config.json, never over a broken one
    [[nodiscard]] inline bool MergesLegacy(State state) noexcept
    {
        return state == State::Missing;
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

        std::string text;
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) {
                result.state = State::Unreadable;
                result.detail = "cannot be opened";
                return result;
            }
            text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            if (file.bad()) {
                result.state = State::Unreadable;
                result.detail = "reading it failed";
                return result;
            }
        }  // closed here: a file still open cannot be moved

        auto parsed = nlohmann::json::parse(text, nullptr, false);
        if (!parsed.is_discarded() && parsed.is_object()) {
            result.state = State::Loaded;
            result.config = std::move(parsed);
            return result;
        }

        result.detail = parsed.is_discarded() ? "not valid JSON" : "not a JSON object";
        result.movedTo = FileUtils::MoveAside(path, ".broken");
        result.state = result.movedTo.empty() ? State::BrokenKept : State::MovedAside;
        return result;
    }
}

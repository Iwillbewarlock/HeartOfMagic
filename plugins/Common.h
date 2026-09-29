#pragma once

#include "PCH.h"
#include "WideFileSink.h"

#include <spdlog/sinks/msvc_sink.h>

namespace logger = SKSE::log;

// CommonLib's logger writes every info line through to disk before the logging
// call returns (flush_on(info)), on whichever thread logged it - usually the
// game thread. Only warnings and errors are written through now; info lines
// wait in the file buffer until a warning, a FlushLog() call (game load, save,
// new game) or the buffer filling up writes them. No flusher thread
// (spdlog::flush_every): its destructor runs at DLL unload and can hang the
// game's exit if Windows stopped the thread in the middle of a flush.
inline constexpr auto kLogFlushImmediateLevel = spdlog::level::warn;

/// Write buffered log lines to disk now (see kLogFlushImmediateLevel).
inline void FlushLog()
{
    if (auto log = spdlog::default_logger()) {
        log->flush();
    }
}

/// The log file when CommonLib's logger::init() threw: the same file, opened
/// by its wide path (WideFileSink.h). Returns false when there is none either.
inline bool SetupWideLog()
{
    try {
        auto path = logger::log_directory();
        if (!path) {
            return false;
        }
        std::error_code ec;
        std::filesystem::create_directories(*path, ec);
        *path /= std::format("{}.log", SKSE::GetPluginName());

        std::vector<spdlog::sink_ptr> sinks{
            std::make_shared<WideFileSink>(*path),
            std::make_shared<spdlog::sinks::msvc_sink_mt>()
        };
        spdlog::set_default_logger(std::make_shared<spdlog::logger>("global", sinks.begin(), sinks.end()));
        return true;
    } catch (...) {
        return false;
    }
}

/// Shared log setup + startup banner for all Heart of Magic plugins.
/// Never throws: a plugin without a log file still loads.
/// @param extraLine  Optional plugin-specific message printed inside the banner.
inline void SetupLog(const char* extraLine = nullptr) noexcept
{
    // logger::init() throws for a Documents path the ANSI code page cannot
    // hold (std::filesystem::path::string()), and nothing above SKSEPluginLoad
    // catches it: the whole plugin failed to load over its log file.
    bool wideLog = false;
    try {
        logger::init();
    } catch (...) {
        wideLog = SetupWideLog();
        if (!wideLog) {
            // No file log this session; the debugger output still gets the lines
            try {
                spdlog::set_default_logger(std::make_shared<spdlog::logger>(
                    "global", std::make_shared<spdlog::sinks::msvc_sink_mt>()));
            } catch (...) {
            }
        }
    }

    try {
        // pattern: [2024-01-01 12:00:00.000] [info] [1234] [sourcefile.cpp:123] Log message
        spdlog::set_pattern("[%Y-%m-%d %T.%e] [%l] [%t] [%s:%#] %v");
        spdlog::set_level(spdlog::level::info);
        spdlog::flush_on(kLogFlushImmediateLevel);

        logger::info("===========================================");
        logger::info("{} v{} by {} loading...",
            SKSE::GetPluginName(), SKSE::GetPluginVersion(), SKSE::GetPluginAuthor());
        if (extraLine) {
            logger::info("  {}", extraLine);
        }
        logger::info("  built using CommonLibSSE-NG v{}", COMMONLIBSSE_VERSION);
        logger::info("  Running on Skyrim v{}", REL::Module::get().version().string());
        if (wideLog) {
            logger::info("  log file opened by its wide path (the Documents path is outside the ANSI code page)");
        }
        logger::info("===========================================");
    } catch (...) {
    }
}

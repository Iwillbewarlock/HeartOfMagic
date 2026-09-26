#pragma once

#include "PCH.h"

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

/// Shared log setup + startup banner for all Heart of Magic plugins.
/// @param extraLine  Optional plugin-specific message printed inside the banner.
inline void SetupLog(const char* extraLine = nullptr)
{
    logger::init();
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
    logger::info("===========================================");
}

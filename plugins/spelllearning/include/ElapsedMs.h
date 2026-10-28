#pragma once

#include <chrono>

// Milliseconds since `started` (steady clock), for the timing lines in the log - the one
// helper the UIManager files, the scan at game start and the tree graft all use
[[nodiscard]] inline double MsSince(std::chrono::steady_clock::time_point started)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
}

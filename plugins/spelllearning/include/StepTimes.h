#pragma once

#include "ElapsedMs.h"

#include <chrono>
#include <format>
#include <string>
#include <string_view>

// One timing line for a run of steps (data loaded, new game, save loaded): each
// Mark closes the step since the previous mark, Text() lists them as "name N, ..."
// in ms and TotalMs() is the whole run so far.
class StepTimes
{
public:
    void Mark(std::string_view step)
    {
        const auto now = std::chrono::steady_clock::now();
        if (!m_text.empty()) m_text += ", ";
        m_text += std::format("{} {:.0f}", step, std::chrono::duration<double, std::milli>(now - m_last).count());
        m_last = now;
    }

    [[nodiscard]] double TotalMs() const { return MsSince(m_started); }
    [[nodiscard]] const std::string& Text() const { return m_text; }

private:
    std::chrono::steady_clock::time_point m_started = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point m_last = m_started;
    std::string m_text;
};

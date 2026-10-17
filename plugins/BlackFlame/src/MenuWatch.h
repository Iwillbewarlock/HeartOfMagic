#pragma once

// =============================================================================
// MENU WATCH - the MenuOpenCloseEvent sink, always registered: W2 opens at the main menu, closes at a load
// =============================================================================
// Single launch (design v3.2 §5, v3.3 #2): copies are published only before the first save load or new game of a session.
//   W1    kDataLoaded itself (synchronous), and the main-thread drains after it until the main menu opens
//   W2    the main menu: opens at the first MenuOpenCloseEvent with opening and menuName == RE::MainMenu::MENU_NAME
// The publish window closes, for the rest of the session, on the first of: SKSE kPreLoadGame, kNewGame (Main.cpp calls
// Close), and once the main menu has opened, LoadingMenu open (RE::LoadingMenu::MENU_NAME) or MainMenu close. Before the
// main menu has opened, a menu event closes nothing (the start-up screens are not a load; only the two SKSE messages are).
// The close flag is atomic; every publish step re-checks it on the main thread right before SetModel and RegisterLoose. The
// SKSE messages close it on the main thread, so no close falls inside a main-thread step; a menu event that closes it on
// another thread (not seen: its thread is logged) is honoured from the next check on.
// Main-thread proof (v3.3 #2), at kDataLoaded before any publish is scheduled: the drains may publish (W1 drains and W2)
// only when MainQueue::Installed() and the kDataLoaded thread is RE::Main's threadID; otherwise this session publishes at
// kDataLoaded itself only, and the log says why. Publication never goes through SKSE's AddTask fallback.
// The sink is registered whatever the config says (DevAutomation's devAutoLoad reads the main menu's first opening from it).

#include "Common.h"

#include <chrono>
#include <functional>
#include <optional>
#include <string>

namespace BlackFlame::MenuWatch
{
    using Clock = std::chrono::steady_clock;

    /// kDataLoaded, first thing (after the kDataLoaded thread id is stored): registers the sink and makes the main-thread
    /// proof; the publish window opens here
    void Install();

    /// The kDataLoaded thread is RE::Main's thread and the main-thread queue is installed (Install decided it)
    [[nodiscard]] bool MainThreadProven() noexcept;

    /// The proof as one line ("queue installed yes, kDataLoaded thread 1234, Main::threadID 1234: drains publish")
    [[nodiscard]] std::string ProofText();

    /// The publish window is open: Install ran and nothing closed it yet (any thread)
    [[nodiscard]] bool PublishOpen() noexcept;

    /// A drain may publish now: the window is open, the proof holds, and the caller is the main thread
    [[nodiscard]] bool DrainMayPublish() noexcept;

    /// The main menu has opened this session (W2 began)
    [[nodiscard]] bool MainMenuOpened() noexcept;

    /// When the main menu first opened (the anchor of the timeouts and the HUD), if it has
    [[nodiscard]] std::optional<Clock::time_point> MainMenuOpenedAt();

    /// Closes the publish window for the session (idempotent; the first reason is kept and logged with its thread)
    void Close(const char* a_reason);

    /// Why the window closed ("" while open)
    [[nodiscard]] std::string CloseReason();

    /// a_listener runs once, on the menu event's thread, when the main menu first opens; when it already has, at once on the
    /// caller's thread (the callers register at kDataLoaded, on the main thread)
    void OnMainMenuOpen(std::function<void()> a_listener);
}

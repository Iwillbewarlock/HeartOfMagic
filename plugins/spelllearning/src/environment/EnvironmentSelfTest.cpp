#include "environment/EnvironmentSelfTest.h"
#include "ThreadUtils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <optional>
#include <system_error>
#include <thread>
#include <utility>

// =============================================================================
// EnvironmentSelfTest - the switch, the wait and the run (EnvironmentSelfTest.h)
//
// A load is followed by the engine's own work for a while, and a test there
// competes with it on a large load order (a frozen screen in an earlier mod's
// self-test), so the run waits for unpaused play, not for the load event.
// =============================================================================

namespace Environment::SelfTest
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        constexpr float kDefaultDelaySeconds = 30.0f;
        constexpr float kMinDelaySeconds = 5.0f;
        constexpr float kMaxDelaySeconds = 600.0f;
        constexpr auto kTickInterval = std::chrono::seconds(1);
        constexpr auto kMaxTickGap = std::chrono::seconds(2);      // a hitch or a long frame counts this much at most
        constexpr auto kArmedLimit = std::chrono::minutes(15);     // never started by then (the player never played): give up

        std::atomic<bool> g_tickerRunning{ false };
        std::atomic<bool> g_finished{ false };

        // Game thread only
        bool g_ran = false;
        bool g_armed = false;
        float g_delaySeconds = kDefaultDelaySeconds;
        Clock::time_point g_armedAt;
        Clock::duration g_play{};
        std::optional<Clock::time_point> g_lastTick;
        int g_pass = 0;
        int g_fail = 0;
        int g_skip = 0;
        int g_answer = 0;

        // The switch file: {"enabled": true, "delaySeconds": 30}. Missing, not
        // JSON or enabled not true: off.
        bool ReadSwitch(float& delaySeconds)
        {
            std::ifstream file(kSwitchFile, std::ios::binary);
            if (!file.is_open()) return false;
            const auto document = nlohmann::json::parse(file, nullptr, false);
            if (document.is_discarded() || !document.is_object()) {
                logger::warn("{} {} is not a JSON object - self-test off", kPrefix, kSwitchFile);
                return false;
            }
            const auto enabled = document.find("enabled");
            if (enabled == document.end() || !enabled->is_boolean() || !enabled->get<bool>()) return false;
            delaySeconds = kDefaultDelaySeconds;
            const auto delay = document.find("delaySeconds");
            if (delay != document.end()) {
                if (delay->is_number()) {
                    delaySeconds = std::clamp(delay->get<float>(), kMinDelaySeconds, kMaxDelaySeconds);
                } else {
                    logger::warn("{} delaySeconds {} is not a number - using {}", kPrefix, delay->dump(), kDefaultDelaySeconds);
                }
            }
            return true;
        }

        bool PlayerReady()
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* ui = RE::UI::GetSingleton();
            return player && ui && !ui->GameIsPaused() && player->Is3DLoaded() && player->GetParentCell();
        }

        void Run()
        {
            logger::info("{} START after {:.0f} s of unpaused play (selftest.json delaySeconds)", kPrefix, g_delaySeconds);
            FlushLog();
            const std::pair<const char*, void (*)()> checks[] = {
                { "H2", CheckH2 }, { "twins", CheckTwins }, { "mysticism-patch", CheckMysticismPatch },
                { "frostwalker", CheckFrostwalker }, { "adapters", CheckAdapters },
                { "sr-paired-tomes", CheckSpellTomes }, { "game-thread", CheckGameThreadFlag }
            };
            for (const auto& [name, check] : checks) {
                try {
                    check();
                } catch (const std::exception& e) {
                    Report(name, Outcome::kFail, std::string("threw: ") + e.what());
                }
            }
            logger::info("{} DONE pass={} fail={} skip={} answer={} (fail counts defects only; ANSWER lines are H2's findings)",
                kPrefix, g_pass, g_fail, g_skip, g_answer);
            FlushLog();
        }

        void Tick()
        {
            if (g_ran || !g_armed) return;
            const auto now = Clock::now();
            const auto delay = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(g_delaySeconds));
            if (now - g_armedAt > kArmedLimit + delay) {
                logger::info("{} disarmed: it could not start within {} minutes", kPrefix,
                    std::chrono::duration_cast<std::chrono::minutes>(kArmedLimit).count());
                g_armed = false;
                g_finished.store(true);
                return;
            }
            const auto gap = g_lastTick ? std::min<Clock::duration>(now - *g_lastTick, kMaxTickGap) : Clock::duration{};
            g_lastTick = now;
            if (!PlayerReady()) return;   // a menu, a load, a pause: not counted
            g_play += gap;
            if (g_play < delay) return;
            g_armed = false;
            g_ran = true;
            g_finished.store(true);
            Run();
        }

        // One ticker at a time. When it is done it hands off: it clears
        // g_tickerRunning, then takes the ticker back if a load re-armed in
        // between (that load's StartTicker saw the old ticker still running and
        // left the work to it).
        void TickerLoop()
        {
            for (;;) {
                while (!g_finished.load()) {
                    std::this_thread::sleep_for(kTickInterval);
                    AddTaskToGameThread("EnvironmentSelfTestTick", [] { Tick(); });
                }
                g_tickerRunning.store(false);
                if (g_finished.load() || g_tickerRunning.exchange(true)) return;
            }
        }

        void StartTicker()
        {
            if (g_tickerRunning.exchange(true)) return;
            try {
                std::thread(TickerLoop).detach();
            } catch (const std::system_error& e) {
                g_tickerRunning.store(false);
                logger::warn("{} no thread to wait on ({}) - self-test not run", kPrefix, e.what());
            }
        }
    }

    void OnGameLoaded()
    {
        if (g_ran) return;
        float delay = kDefaultDelaySeconds;
        if (!ReadSwitch(delay)) {
            // Turned off since the last load: no run from that arm
            if (g_armed) {
                g_armed = false;
                g_play = {};
                g_lastTick.reset();
                g_finished.store(true);
                logger::info("{} disarmed: selftest.json is gone or not enabled any more", kPrefix);
            }
            return;
        }
        // A second load before the start: the wait begins again
        g_delaySeconds = delay;
        g_armed = true;
        g_armedAt = Clock::now();
        g_play = {};
        g_lastTick.reset();
        g_finished.store(false);
        logger::info("{} armed: starts after {:.0f} s of unpaused play with the player in a loaded cell "
                     "(menus, loading and pauses are not counted)", kPrefix, g_delaySeconds);
        StartTicker();
    }

    void Report(std::string_view id, Outcome outcome, const std::string& observed)
    {
        const char* word = outcome == Outcome::kPass ? "PASS" : outcome == Outcome::kFail ? "FAIL" : "SKIP";
        (outcome == Outcome::kPass ? g_pass : outcome == Outcome::kFail ? g_fail : g_skip)++;
        if (outcome == Outcome::kFail) {
            logger::warn("{} {} {} {}", kPrefix, id, word, observed);
        } else {
            logger::info("{} {} {} {}", kPrefix, id, word, observed);
        }
        FlushLog();
    }

    void Answer(std::string_view id, bool yes, const std::string& observed)
    {
        ++g_answer;
        logger::info("{} {} ANSWER {} {}", kPrefix, id, yes ? "yes" : "no", observed);
        FlushLog();
    }

    void Note(std::string_view id, const std::string& text)
    {
        logger::info("{} {} INFO {}", kPrefix, id, text);
        FlushLog();
    }
}

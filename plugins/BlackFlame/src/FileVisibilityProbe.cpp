#include "FileVisibilityProbe.h"

#include "Config.h"
#include "DevAutomation.h"
#include "FileProbeRun.h"
#include "MainThreadQueue.h"
#include "SehGuard.h"
#include "TrackedMutex.h"

#include <array>
#include <atomic>
#include <format>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>

namespace BlackFlame::FileProbe
{
    void Note(Run& a_run, std::string a_text)
    {
        logger::info("BlackFlame: file visibility probe: {}", a_text);
        FlushLog();
        a_run.lines.push_back(std::move(a_text));
    }

    void SetVerdict(Run& a_run, std::string_view a_test, std::string a_word, std::string a_why)
    {
        auto& verdict = a_run.results[std::string(a_test)];
        Note(a_run, std::format("{} {}: {}", a_test, a_word, a_why));
        verdict.word = std::move(a_word);
        verdict.why = std::move(a_why);
    }
}

namespace BlackFlame::FileVisibilityProbe
{
    namespace
    {
        using FileProbe::Clock;
        using FileProbe::Note;
        using FileProbe::Run;

        enum class Step
        {
            kPrepare,
            kPreRequest,
            kWrite,
            kA,
            kB,
            kC,
            kD,
            kDCheck,
            kWait,
            kJudge,
            kFinish
        };

        constexpr const char* kLinePrefix = "BlackFlame: file visibility probe: ";
        constexpr auto kGiveUpPoll = std::chrono::seconds(1);

        std::atomic<bool> g_menuSeen{ false };
        std::atomic<bool> g_started{ false };
        std::atomic<bool> g_finished{ false };  // the result file written (by the run's end or the give-up thread)
        TrackedMutex g_fileMutex;

        const char* StepName(Step a_step)
        {
            constexpr std::array kNames{ "prepare", "pre-request", "write files", "test A", "test B", "test C", "test D",
                "test D check", "wait for the GPU textures", "judge textures", "finish" };
            return kNames[static_cast<std::size_t>(a_step)];
        }

        void WriteResultFile(const std::vector<std::string>& a_lines)
        {
            std::lock_guard lock(g_fileMutex);
            const auto dir = logger::log_directory();
            if (!dir) {
                logger::error("BlackFlame: file visibility probe: no SKSE log folder - {} not written", kResultFile);
                return;
            }
            std::ofstream out(*dir / kResultFile, std::ios::binary | std::ios::trunc);
            out << "Heart of Magic Black Flame file visibility probe (dev), plugin " << PluginVersionText() << ", game "
                << REL::Module::get().version().string() << "\r\n";
            for (const auto& line : a_lines) {
                out << kLinePrefix << line << "\r\n";
            }
            out.close();
            if (!out) {
                logger::error("BlackFlame: file visibility probe: writing {} failed", kResultFile);
            }
        }

        // devAutoQuit ends the unattended run through DevAutomation's own quit (unless the self-test runs: it quits then)
        void Quit(const char* a_label)
        {
            const auto& config = GetConfig();
            if (!config.devAutoQuit) {
                return;
            }
            if (config.selfTest) {
                logger::info("BlackFlame: file visibility probe: devAutoQuit left to the self-test (selfTest is on)");
                return;
            }
            logger::info("BlackFlame: file visibility probe: asking dev automation to quit ({})", a_label);
            DevAutomation::OnSelfTestDone(a_label);
        }

        std::string Summary(const Run& a_run)
        {
            std::string main;
            std::string negative;
            std::string control;
            for (std::size_t i = 0; i < FileProbe::kTests.size(); ++i) {
                const auto it = a_run.results.find(FileProbe::kTests[i]);
                const std::string word = it == a_run.results.end() ? "NOT RUN" : it->second.word;
                auto& part = i < FileProbe::kMainTests ? main : (i < FileProbe::kMainTests + FileProbe::kNegativeTests ? negative : control);
                part += std::format("{}{} {}", part.empty() ? "" : ", ", FileProbe::kTests[i], word);
            }
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - a_run.begun).count();
            return std::format("{} (negative cache: {}; {}; run {}, {} ms; every line in {})", main, negative, control, a_run.tag, ms,
                kResultFile);
        }

        void Finish(Run& a_run)
        {
            if (g_finished.exchange(true)) {
                return;  // the give-up thread wrote its file already
            }
            for (const auto test : FileProbe::kTests) {
                if (!a_run.results.contains(test)) {
                    a_run.results[std::string(test)] = { "NOT RUN", "its step did not get to it (see the lines above)" };
                }
            }
            Note(a_run, Summary(a_run));
            WriteResultFile(a_run.lines);
            Quit("file probe");
        }

        void Schedule(std::shared_ptr<Run> a_run, Step a_step);

        void Body(Run& a_run, Step a_step)
        {
            switch (a_step) {
            case Step::kPrepare:
                if (!FileProbe::Prepare(a_run)) {
                    for (const auto test : FileProbe::kTests) {
                        FileProbe::SetVerdict(a_run, test, "SKIPPED", "no source files (see above)");
                    }
                }
                break;
            case Step::kPreRequest:
                FileProbe::PreRequest(a_run);
                break;
            case Step::kWrite:
                FileProbe::WriteFiles(a_run);
                break;
            case Step::kA:
                FileProbe::TestA(a_run);
                break;
            case Step::kB:
                FileProbe::TestB(a_run);
                break;
            case Step::kC:
                FileProbe::TestC(a_run);
                break;
            case Step::kD:
                FileProbe::TestD(a_run);
                break;
            case Step::kDCheck:
                FileProbe::TestDCheck(a_run);
                break;
            case Step::kJudge:
                FileProbe::JudgeTextures(a_run);
                break;
            default:
                break;
            }
        }

        void RunStep(const std::shared_ptr<Run>& a_run, Step a_step)
        {
            auto& run = *a_run;
            if (a_step < Step::kFinish && Clock::now() - run.begun > kRunDeadline) {
                Note(run, std::format("the {} s deadline passed before step '{}': the rest is SKIPPED", kRunDeadline.count(), StepName(a_step)));
                for (const auto test : FileProbe::kTests) {
                    if (!run.results.contains(test)) {
                        FileProbe::SetVerdict(run, test, "SKIPPED", "the run's deadline passed");
                    }
                }
                a_step = Step::kFinish;
            }
            if (a_step == Step::kWait) {
                // E: a texture passes only with its GPU texture made; wait for every one, kTextureWait at most
                const bool ready = FileProbe::TexturesReady(run);
                if (!ready && Clock::now() < run.judgeAt) {
                    Schedule(a_run, Step::kWait);
                    return;
                }
                const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - run.judgeAt + kTextureWait);
                Note(run, std::format("textures judged after {} ms ({})", waited.count(),
                    ready ? "every asked texture has its GPU texture or came back empty" : "the wait's limit"));
                Schedule(a_run, Step::kJudge);
                return;
            }
            if (a_step == Step::kFinish) {
                Finish(run);
                return;
            }
            const bool skip = a_step != Step::kPrepare && !run.ready;  // no source bytes: only the finish is left
            if (!skip) {
                try {
                    if (!GuardedKeep("file probe step", [&]() { Body(run, a_step); })) {
                        Note(run, std::format("step '{}' FAULTED (the hardware exception is logged above); going on", StepName(a_step)));
                    }
                } catch (const std::exception& e) {
                    Note(run, std::format("step '{}' threw ({}); going on", StepName(a_step), e.what()));
                } catch (...) {
                    Note(run, std::format("step '{}' threw an exception of an unknown type; going on", StepName(a_step)));
                }
            }
            if (a_step == Step::kDCheck) {
                run.judgeAt = Clock::now() + kTextureWait;
            }
            Schedule(a_run, skip ? Step::kFinish : static_cast<Step>(static_cast<int>(a_step) + 1));
        }

        void Schedule(std::shared_ptr<Run> a_run, Step a_step)
        {
            if (MainQueue::Post("BlackFlameFileProbe", [a_run, a_step]() { RunStep(a_run, a_step); })) {
                return;
            }
            logger::error("BlackFlame: file visibility probe: step '{}' could not be queued - the run ends here", StepName(a_step));
            if (!g_finished.exchange(true)) {
                a_run->lines.push_back(std::format("ENDED: step '{}' could not be queued for the main thread", StepName(a_step)));
                WriteResultFile(a_run->lines);
                Quit("file probe (queue refused)");
            }
        }

        void Start()
        {
            if (g_started.exchange(true)) {
                return;
            }
            auto run = std::make_shared<Run>();
            const auto now = std::chrono::system_clock::now().time_since_epoch();
            run->tag = std::format("{:x}", std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
            Note(*run, std::format("run {} starts ({} s after the main menu opened; game {}, {})", run->tag, kMenuSettle.count(),
                REL::Module::get().version().string(), REL::Module::IsAE() ? "AE" : "SE"));
            Schedule(std::move(run), Step::kPrepare);
        }

        // Re-posted every frame until kMenuSettle has passed since the main menu opened
        void WaitThenStart(Clock::time_point a_menuAt)
        {
            const bool queued = MainQueue::Post("BlackFlameFileProbeWait", [a_menuAt]() {
                if (Clock::now() - a_menuAt < kMenuSettle) {
                    WaitThenStart(a_menuAt);
                } else {
                    Start();
                }
            });
            if (!queued) {
                logger::error("BlackFlame: file visibility probe: the wait for the main menu could not be queued - no probe");
            }
        }

        void OnMainMenu()
        {
            if (g_menuSeen.exchange(true)) {
                return;
            }
            logger::info("BlackFlame: file visibility probe: main menu open; the probe starts in {} s", kMenuSettle.count());
            WaitThenStart(Clock::now());
        }

        class MenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
                RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
            {
                if (a_event && a_event->opening && a_event->menuName == RE::MainMenu::MENU_NAME) {
                    OnMainMenu();
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };
        MenuSink g_menuSink;

        // devAutoQuit: an unattended run ends even when the probe never starts or never finishes
        void GiveUpLoop()
        {
            const auto until = Clock::now() + kGiveUp;
            while (Clock::now() < until) {
                if (g_finished.load()) {
                    return;
                }
                std::this_thread::sleep_for(kGiveUpPoll);
            }
            if (g_finished.exchange(true)) {
                return;
            }
            logger::warn("BlackFlame: file visibility probe: TIMEOUT: no end within {} minutes of kDataLoaded (started: {})",
                kGiveUp.count(), g_started.load() ? "yes" : "no");
            WriteResultFile({ std::format("TIMEOUT: no end within {} minutes of kDataLoaded (started: {}); see "
                                          "HeartOfMagic_BlackFlame.log for the lines it got to",
                kGiveUp.count(), g_started.load() ? "yes" : "no") });
            Quit("file probe timeout");
        }
    }

    void Install()
    {
        const auto& config = GetConfig();
        if (!config.devFileVisibilityProbe) {
            return;
        }
        logger::warn("BlackFlame: file visibility probe ON (dev only, devFileVisibilityProbe): it runs {} s after the main menu "
                     "opens{}", kMenuSettle.count(), config.devAutoQuit && !config.selfTest ? "; devAutoQuit quits after it" : "");
        if (config.devAutoQuit && !config.selfTest) {
            std::thread(&GiveUpLoop).detach();  // detached, never joined (DevAutomation.cpp: a join at unload can hang the exit)
        }
        if (!MainQueue::Installed()) {
            logger::error("BlackFlame: file visibility probe: no main-thread queue - the probe does not run");
            g_finished.store(true);
            WriteResultFile({ "SKIPPED: the main-thread queue is not installed (MainThreadQueue.h); nothing was probed" });
            Quit("file probe (no queue)");
            return;
        }
        auto* ui = RE::UI::GetSingleton();
        if (!ui) {
            logger::error("BlackFlame: file visibility probe: no RE::UI - the main menu cannot be seen; the probe does not run");
            return;
        }
        ui->AddEventSink<RE::MenuOpenCloseEvent>(&g_menuSink);
        if (ui->IsMenuOpen(RE::MainMenu::MENU_NAME)) {
            OnMainMenu();
        }
    }
}

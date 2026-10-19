// =============================================================================
// GreySelfTestKeyedRuns - --self-test of the key mode: outcomes, the CLI, the dev mode, the throttle (GreySelfTestKeyed.h)
// =============================================================================
// Split from GreySelfTestKeyed.cpp (the 600-line rule, review round 1): the same checks, on the same fixture. A tiny job
// memory cap (too_large), a missing source palette and a BC5 decal (fallback), a texture outside the dump caps
// (not_dumped), an unread one, a broken dump, DLL slips, refused runs, the CLI refusals, the dev mode (--out) on the same v5
// request, and the throttle's wiring (a real named event for this process's pid, set before a run whose BC7 texture would
// otherwise get a WARP device).

#include "GreySelfTestKeyed.h"

#include "../../plugins/BlackFlame/src/BuilderSignal.h"
#include "../../plugins/BlackFlame/src/DataPathRule.h"

#include <omp.h>

#include <fmt/format.h>
#include <iostream>
#include <sstream>

namespace GreyBuild::SelfTest::Keyed
{
    namespace CC = BlackFlame::CopyContract;

    namespace
    {
        // This exe with a_args (output to nothing); its exit code
        [[nodiscard]] DWORD RunSelf(const std::wstring& a_args)
        {
            std::wstring command = L"\"" + ThisExecutable().wstring() + L"\" " + a_args;
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            if (!::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
                return MAXDWORD;
            }
            ::CloseHandle(process.hThread);
            if (::WaitForSingleObject(process.hProcess, kChildTimeoutMs) == WAIT_TIMEOUT) {
                ::TerminateProcess(process.hProcess, MAXDWORD);  // a hang is a failure, never a self-test that never ends
                ::WaitForSingleObject(process.hProcess, INFINITE);
            }
            DWORD code = MAXDWORD;
            ::GetExitCodeProcess(process.hProcess, &code);
            ::CloseHandle(process.hProcess);
            return code;
        }
    }

    void CheckOutcomes(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_sourceNif)
    {
        const auto tiny = MakeFixture(a_dir, "tiny", a_sourceNif, Variant::kNormal);
        const int exit = Run(tiny, kTinyCap);
        const auto progress = Progress(tiny);
        const auto* model = Find(progress, kModel);
        const auto* plain = Find(progress, kPlain);
        const auto* txst = Find(progress, kTxst);
        a_check.Check(exit == 0 && model && model->outcome == CC::Outcome::kTooLarge && model->cap == kTinyCap &&
                          txst && txst->outcome == CC::Outcome::kTooLarge && plain && plain->outcome == CC::Outcome::kOk,
            fmt::format("job memory cap {}: textures over it make their objects too_large (cap in the reason), the plain NIF ok",
                kTinyCap));
        ProgressLog log;
        std::string why;
        a_check.Check(log.Open(tiny.staging, why) && log.Reusable(kModel, kTinyCap) && !log.Reusable(kModel, kTinyCap * 2),
            "too_large: terminal while the cap stays, built again under a larger cap");
        const int again = Run(tiny);  // the default cap: built this time
        const auto larger = Progress(tiny);
        a_check.Check(again == 0 && Find(larger, kModel) && Find(larger, kModel)->outcome == CC::Outcome::kFallback &&
                          Find(larger, kTxst) && Find(larger, kTxst)->outcome == CC::Outcome::kOk,
            "too_large: the next run under a larger cap builds the model and the TXST (the last line for a key wins)");

        const auto missing = MakeFixture(a_dir, "palette", a_sourceNif, Variant::kMissingPalette);
        const int missingExit = Run(missing);
        const auto fallback = Progress(missing);
        const auto* fallbackModel = Find(fallback, kModel);
        const auto* bc5 = Find(fallback, kTxst);
        a_check.Check(missingExit == 0 && fallbackModel && fallbackModel->outcome == CC::Outcome::kFallback &&
                          fallbackModel->reason.find("source palette") != std::string::npos && !fallbackModel->path.empty() && bc5 &&
                          bc5->outcome == CC::Outcome::kFallback && bc5->deps.empty() && bc5->reason.find("blue") != std::string::npos &&
                          bc5->reason.find("no colour slot") != std::string::npos,
            fmt::format("fallback: a missing source palette publishes, slot left ('{}'); a TXST whose BC5 decal got no "
                        "copy is a fallback with no dep ('{}')", fallbackModel ? fallbackModel->reason : "",
                bc5 ? bc5->reason : ""));

        const auto capped = MakeFixture(a_dir, "capped", a_sourceNif, Variant::kNotDumped);
        const int cappedExit = Run(capped);
        const auto notDumped = Progress(capped);
        const auto* cappedModel = Find(notDumped, kModel);
        a_check.Check(cappedExit == 0 && cappedModel && cappedModel->outcome == CC::Outcome::kNotDumped && cappedModel->path.empty() &&
                          Find(notDumped, kTxst) && Find(notDumped, kTxst)->outcome == CC::Outcome::kOk,
            "not_dumped: a texture the rule picks outside the dump caps makes its object not_dumped (no file); the TXST ok");

        const auto unread = MakeFixture(a_dir, "unread", a_sourceNif, Variant::kUnread);
        const int unreadExit = Run(unread);
        const auto unreadProgress = Progress(unread);
        const auto* unreadModel = Find(unreadProgress, kModel);
        a_check.Check(unreadExit == 0 && unreadModel && unreadModel->outcome == CC::Outcome::kFailed &&
                          unreadModel->reason.find("unread") != std::string::npos,
            fmt::format("failed: a texture the game could not read (state unread) fails its object, retried at the next start ('{}')",
                unreadModel ? unreadModel->reason : ""));

        const auto broken = MakeFixture(a_dir, "broken", a_sourceNif, Variant::kNormal);
        std::filesystem::remove(broken.dump / kFxFire);  // listed as dumped, gone from the dump
        const int brokenExit = Run(broken);
        const auto brokenProgress = Progress(broken);
        const auto* brokenModel = Find(brokenProgress, kModel);
        a_check.Check(brokenExit == 0 && brokenModel && brokenModel->outcome == CC::Outcome::kFailed &&
                          brokenModel->reason.find("listed as dumped") != std::string::npos,
            fmt::format("failed: a texture the request lists as dumped that is not in the dump fails its object, never a "
                        "fallback that would be reused ('{}')", brokenModel ? brokenModel->reason : ""));

        const auto slip = MakeFixture(a_dir, "slip", a_sourceNif, Variant::kNormal);
        auto slipText = ReadAll(slip.folder / "request.json");
        const auto replaceEntry = [&](const std::string& a_start, const std::string& a_entry) {
            const auto at = slipText.find(a_start);
            slipText.replace(at, slipText.find('}', at) - at + 1, a_entry);
        };
        replaceEntry(R"({"path":"meshes\\magic\\keyed.nif")", R"({"path":"meshes\\magic\\other.nif","size":1,"ok":false})");
        replaceEntry(R"({"path":"meshes\\mps\\a.nif")", R"({"path":"MPS\\A.nif","size":1,"ok":false})");  // MeshDataPath folds it
        Write(slip.folder / "request.json", slipText);
        const int slipExit = Run(slip);
        const auto slipProgress = Progress(slip);
        const auto* slipModel = Find(slipProgress, kModel);
        const auto* slipMps = Find(slipProgress, kMps);
        a_check.Check(slipExit == 0 && slipModel && slipModel->outcome == CC::Outcome::kFailed &&
                          slipModel->reason.find("does not list") != std::string::npos && slipMps &&
                          slipMps->outcome == CC::Outcome::kFailed && slipMps->reason.find("could not read") != std::string::npos &&
                          Find(slipProgress, kPlain) && Find(slipProgress, kPlain)->outcome == CC::Outcome::kOk,
            fmt::format("failed: a NIF nifs[] does not list fails its object alone ('{}'); an ok false path matched after "
                        "MeshDataPath ('{}'); the rest builds", slipModel ? slipModel->reason : "", slipMps ? slipMps->reason : ""));

        const auto otherExe = MakeFixture(a_dir, "identity", a_sourceNif, Variant::kNormal, 0.6, kGameRequestVersionKeyed,
            std::to_string(CC::kBuilderOutputVersion + 1));
        const auto wrongTint = MakeFixture(a_dir, "tint", a_sourceNif, Variant::kNormal, 0.7);
        const auto v4 = MakeFixture(a_dir, "v4", a_sourceNif, Variant::kNormal, 0.6, kGameRequestVersion);
        std::error_code ec;
        a_check.Check(Run(otherExe) == 1 && Run(wrongTint) == 1 && Run(v4) == 2 && !std::filesystem::exists(otherExe.staging / CC::kProgressFile, ec) &&
                          !std::filesystem::exists(wrongTint.staging / CC::kProgressFile, ec) &&
                          !std::filesystem::exists(v4.staging / CC::kProgressFile, ec),
            "refused runs: VerifyKeyPrefix refuses keys of another output identity and a prefix not of its options (exit 1, "
            "nothing written); a v4 request (exit 2)");
    }

    void CheckCommandLine(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_sourceNif)
    {
        const auto fixture = MakeFixture(a_dir, "cli", a_sourceNif, Variant::kNormal);
        const std::wstring folder = L"\"" + fixture.folder.wstring() + L"\"";
        const std::wstring staging = L"\"" + fixture.staging.wstring() + L"\"";
        const auto key = [&](const std::wstring& a_more) { return RunSelf(L"--from-game " + folder + L" --staging " + staging + L" " + a_more); };
        a_check.Check(key(L"--tint 0.5") == 2 && key(L"--palette-mode godskin") == 2 && key(L"--out " + staging) == 2 &&
                          key(L"--job-memory 10") == 2 && RunSelf(L"--staging " + staging) == 2,
            "CLI: the key mode refuses --tint, --palette-mode, --out, a job memory under 64 MiB, and --staging without --from-game");
        const DWORD gone = RunSelf(L"--from-game " + folder + L" --staging " + staging + L" --parent-pid 4294967290");
        a_check.Check(gone == static_cast<DWORD>(kExitParentGone), fmt::format("CLI: a parent pid that names no process stops the run at "
                                                                                "once (exit {})", gone));
        const DWORD full = key(L"");
        a_check.Check(full == 0 && CC::ParseProgress(ReadAll(fixture.staging / CC::kProgressFile)).lines.size() >= 5,
            fmt::format("CLI: --from-game <folder> --staging <dir> builds (exit {})", full));
        const auto collide = fixture.folder / "request.json";
        a_check.Check(RunSelf(L"--collisions \"" + collide.wstring() + L"\"") == 1, "CLI: --collisions exits 1 for a request with ADDN 40001");
    }

    void CheckDevMode(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_sourceNif)
    {
        const auto fixture = MakeFixture(a_dir, "dev", a_sourceNif, Variant::kNormal);
        const auto out = a_dir / "dev_out";
        std::filesystem::create_directories(out);
        const int exit = RunFromGame(fixture.folder, out, kDefaultTint, false);
        std::error_code ec;
        a_check.Check(exit == 0 && std::filesystem::exists(out / kCopiesPluginName, ec),
            fmt::format("dev mode: --from-game --out still builds the copies ESL from a v5 request (exit {})", exit));
    }

    /// The throttle's wiring in RunKeyed: with the parent's event (Local\HoMBlackFlame_Throttle_<this pid>) set before
    /// the first object, the THROTTLE line is printed and no GPU device is made for the run's BC7 texture (WARP, so on
    /// any machine), and OpenMP drops to one thread; the same run with the event unset makes the device (the pair shows
    /// the throttle, not a run with nothing for the GPU, kept it off). OpenMP's thread count is restored after
    void CheckThrottleRun(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_sourceNif)
    {
        const auto pid = static_cast<std::uint32_t>(::GetCurrentProcessId());
        const auto name = BlackFlame::BuilderSignal::ThrottleEventName(pid);
        const HANDLE event = ::CreateEventW(nullptr, TRUE, FALSE, name.c_str());
        if (!event) {
            a_check.Check(false, fmt::format("throttle run: the event for pid {} was not made (Windows error {})", pid, ::GetLastError()));
            return;
        }
        (void)::ResetEvent(event);  // unset for the control run, whoever made it before
        struct Restore
        {
            HANDLE event;
            int threads;
            ~Restore()
            {
                ::omp_set_num_threads(threads);
                ::CloseHandle(event);
            }
        } restore{ event, ::omp_get_max_threads() };
        const auto run = [&](const Fixture& a_fixture, int& a_exit) {
            KeyedOptions options;
            options.staging = a_fixture.staging;
            options.commitReserve = 0;
            options.memoryProbe = []() { return std::optional<MemoryState>(MemoryState{ 0, UINT64_MAX }); };
            options.parentPid = pid;
            options.gpu = GpuUse::kTestWarp;
            std::ostringstream text;
            auto* const saved = std::cout.rdbuf(text.rdbuf());
            try {
                a_exit = RunKeyed(a_fixture.folder, options);
            } catch (...) {
                std::cout.rdbuf(saved);
                throw;
            }
            std::cout.rdbuf(saved);
            return text.str();
        };
        constexpr std::string_view kThrottled = "THROTTLE the publish window closed";
        constexpr std::string_view kDeviceMade = "GPU   device made";
        // WARP is established on its own, not from the control run: an inverted event check would throttle the control
        // run and leave it without a device, which must FAIL here, never read as "no WARP" (Codex review)
        std::string warpWhat;
        const bool warp = WarpAvailable(warpWhat);
        int controlExit = -1;
        const auto control = run(MakeFixture(a_dir, "throttle-unset", a_sourceNif, Variant::kBc7), controlExit);
        a_check.Check(controlExit == 0 && control.find(kThrottled) == std::string::npos,
            fmt::format("throttle run: with the event unset the run prints no THROTTLE line (exit {})", controlExit));
        if (warp) {
            a_check.Check(control.find("devices made 1") != std::string::npos && control.find(kDeviceMade) != std::string::npos,
                fmt::format("throttle run: with the event unset the run makes a WARP device for its BC7 texture ({})", warpWhat));
        } else {
            std::cout << fmt::format("INFO throttle run: no WARP here ({}): the device side of the throttle is not checked; the "
                                     "THROTTLE line and OpenMP still are\n", warpWhat);
        }
        (void)::SetEvent(event);
        int throttledExit = -1;
        const auto throttled = run(MakeFixture(a_dir, "throttle-set", a_sourceNif, Variant::kBc7), throttledExit);
        const int threads = ::omp_get_max_threads();
        const auto at = throttled.find(kThrottled);
        a_check.Check(throttledExit == 0 && at != std::string::npos && threads == 1,
            fmt::format("throttle run: with the event set the THROTTLE line is printed and OpenMP runs on {} thread(s) (exit {})",
                threads, throttledExit));
        if (warp) {
            const auto summary = throttled.find("gpu    ");
            a_check.Check(throttled.find(kDeviceMade) == std::string::npos && summary != std::string::npos && summary > at &&
                              throttled.find("devices made 0", summary) != std::string::npos &&
                              throttled.find("stopped:", summary) != std::string::npos,
                "throttle run: with the event set the THROTTLE line comes before the first object and no GPU device is made after "
                "it (the summary: devices made 0, stopped)");
        }
    }
}

// =============================================================================
// BuilderLauncher - the builder child: CreateProcessW suspended, a job with kill-on-close and the memory cap, UniqueHandle
// =============================================================================

#include "BuilderLauncher.h"

#include "BuilderControl.h"
#include "BuilderFolderMutex.h"
#include "BuilderSignal.h"
#include "TrackedMutex.h"

#include <atomic>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace BlackFlame::BuilderLauncher
{
    namespace
    {
        constexpr const wchar_t* kLogName = L"builder.log";
        constexpr const wchar_t* kPrevLogName = L"builder.prev.log";
        constexpr std::uint64_t kMiB = 1024ull * 1024ull;

        // review E2 #1: held only for a few field reads and kernel queries, never while setting job limits, and while logging
        // only in Start (its launch lines, so "builder started" precedes the end callback's line); the main thread's
        // ThrottleAfterWindow never waits on it. The handles are set once in Start and never closed (the session is leaked),
        // so copies taken under the lock stay valid outside it
        struct Session
        {
            TrackedMutex mutex;  // with the session's lifetime (review Gemini 2: no static mutex destroyed before a late reader)
            UniqueHandle folderMutex;    // BuilderFolderMutex.h: this game's claim on the request folder, held for its lifetime
            UniqueHandle job;    // never closed by us (kill-on-close fires when the game ends)
            UniqueHandle process;        // kept for the exit code (F4); the end wait below holds it, so never closed either
            UniqueHandle throttleEvent;  // BuilderSignal.h, manual reset; set by ThrottleAfterWindow
            HANDLE endWait = nullptr;    // RegisterWaitForSingleObject's; released by OnBuilderEnded (review E2 #3)
            Launch launch;
            std::optional<std::uint32_t> exitCode;  // once the end is noted (nullopt then: GetExitCodeProcess failed)
            bool ended = false;                     // the end noted
            bool tried = false;
            bool throttled = false;  // the throttle taken (once a session)
            // Set only when ThrottleAfterWindow found the lock busy and could not hand the throttle to a thread-pool thread:
            // the next lock holder that honours it takes it (BuilderControl::TakeOnce). Unset otherwise, so the progress
            // worker's polls never take the lock for it
            std::atomic<bool> throttleRequested{ false };
        };
        Session& TheSession()
        {
            static auto* session = std::make_unique<Session>().release();  // leaked on purpose: the job outlives every static
            return *session;
        }

        // One argument as CommandLineToArgvW reads it back: quoted, a backslash run before a quote (or the closing one)
        // doubled, an inner quote escaped (review Gemini / agy 2: a path ending in '\' would swallow the next arguments)
        std::wstring Quoted(const std::filesystem::path& a_path)
        {
            const auto text = a_path.wstring();
            std::wstring out = L"\"";
            std::size_t backslashes = 0;
            for (const wchar_t c : text) {
                if (c == L'\\') {
                    ++backslashes;
                    continue;
                }
                out.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
                backslashes = 0;
                out.push_back(c);
            }
            out.append(backslashes * 2, L'\\');
            out.push_back(L'"');
            return out;
        }

        std::string Text(const std::filesystem::path& a_path)
        {
            const auto text = a_path.u8string();
            return std::string(text.begin(), text.end());
        }

        std::string LastError(const char* a_what)
        {
            return std::format("{} failed (Windows error {})", a_what, ::GetLastError());
        }

        // The job: kill-on-close and the commit cap
        UniqueHandle MakeJob(std::uint32_t a_jobMemoryMiB, std::string& a_why)
        {
            UniqueHandle job(::CreateJobObjectW(nullptr, nullptr));
            if (!job) {
                a_why = LastError("CreateJobObjectW");
                return {};
            }
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_JOB_MEMORY;
            limits.JobMemoryLimit = static_cast<SIZE_T>(a_jobMemoryMiB * kMiB);
            if (!::SetInformationJobObject(job.Get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
                a_why = LastError("SetInformationJobObject");
                return {};
            }
            return job;
        }

        // The last launch's builder.log as builder.prev.log (an older prev replaced). Not kept: a warning, and builder.log
        // is overwritten as before. What happened, for the "builder started" line
        std::string KeepPreviousLog(const std::filesystem::path& a_folder)
        {
            const auto log = a_folder / kLogName;
            const auto prev = a_folder / kPrevLogName;
            if (::MoveFileExW(log.c_str(), prev.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                return "the previous one kept as builder.prev.log";
            }
            const auto error = ::GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
                return "no previous builder.log";
            }
            logger::warn("BlackFlame: slots: the previous builder.log could not be kept as builder.prev.log (Windows error {}): it "
                         "is overwritten", error);
            return std::format("the previous one NOT kept (Windows error {})", error);
        }

        // builder.log, inheritable (the only handle the child gets)
        UniqueHandle MakeLog(const std::filesystem::path& a_folder, std::string& a_why)
        {
            SECURITY_ATTRIBUTES inherit{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
            UniqueHandle log(::CreateFileW((a_folder / kLogName).c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, &inherit,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
            if (!log) {
                a_why = LastError("creating builder.log");
            }
            return log;
        }

        // The throttle event (BuilderSignal.h), unset, not inherited (the builder opens it by name). Not made: the builder
        // still starts (never throttled from its own side; the job's priority and CPU cap still apply); a_note says why
        UniqueHandle MakeThrottleEvent(std::string& a_note)
        {
            const auto name = BuilderSignal::ThrottleEventName(::GetCurrentProcessId());
            UniqueHandle event(::CreateEventW(nullptr, TRUE, FALSE, name.c_str()));
            if (!event) {
                a_note = std::format("'{}' NOT made: {}", Text(name), LastError("CreateEventW"));
                return {};
            }
            if (::GetLastError() == ERROR_ALREADY_EXISTS) {
                (void)::ResetEvent(event.Get());  // not ours from before (one DLL a process), but it never starts set
                a_note = std::format("'{}' (it already existed: reset)", Text(name));
            } else {
                a_note = std::format("'{}'", Text(name));
            }
            return event;
        }

        Launch StartChecked(const std::filesystem::path& a_exe, const std::filesystem::path& a_requestFolder,
            const std::filesystem::path& a_staging, std::uint32_t a_jobMemoryMiB, Session& a_session, std::string& a_logNote,
            std::string& a_eventNote)
        {
            Launch out;
            std::error_code ec;
            if (a_exe.empty() || !std::filesystem::exists(a_exe, ec)) {
                out.why = std::format("the builder exe '{}' is not there", Text(a_exe));
                return out;
            }
            // review E2 #2 (Codex 2 of defe5ede), round 2: one game process per request folder starts a builder; the claim
            // is a named mutex made before builder.log is rotated and held for the game's lifetime (BuilderFolderMutex.h)
            HANDLE folderMutex = nullptr;
            DWORD claimError = 0;
            const auto claim = BuilderFolderMutex::Acquire(a_requestFolder, folderMutex, claimError);
            if (!BuilderFolderMutex::MayStart(claim)) {
                out.why = std::format("another game process holds this request folder ('{}', mutex '{}', Windows error {}): its "
                                      "builder.log is left as it is and no second builder starts on its staging folder",
                    Text(a_requestFolder), Text(BuilderFolderMutex::MutexName(a_requestFolder)), claimError);
                return out;
            }
            if (claim == BuilderFolderMutex::Claim::kOwned) {
                a_session.folderMutex = UniqueHandle(folderMutex);
            } else {
                logger::warn("BlackFlame: slots: the request folder's mutex '{}' was not made (Windows error {}): a second game on "
                             "the same folder is not excluded this session", Text(BuilderFolderMutex::MutexName(a_requestFolder)),
                    claimError);
            }
            std::filesystem::create_directories(a_staging, ec);
            if (ec) {
                out.why = std::format("the staging folder '{}' could not be made: {}", Text(a_staging), ec.message());
                return out;
            }
            auto job = MakeJob(a_jobMemoryMiB, out.why);
            if (job) {
                a_logNote = KeepPreviousLog(a_requestFolder);
            }
            auto log = job ? MakeLog(a_requestFolder, out.why) : UniqueHandle{};
            if (!job || !log) {
                return out;
            }
            auto throttleEvent = MakeThrottleEvent(a_eventNote);  // before the child starts: it opens the event once, at its start
            if (!throttleEvent) {
                logger::warn("BlackFlame: slots: the builder's throttle event {}; the builder starts without it", a_eventNote);
            }
            // Only the log handle is inherited: an attribute list naming it
            SIZE_T size = 0;
            (void)::InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
            std::vector<std::byte> storage(size);
            auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
            if (!::InitializeProcThreadAttributeList(attributes, 1, 0, &size)) {
                out.why = LastError("InitializeProcThreadAttributeList");
                return out;
            }
            struct AttributeListEnd
            {
                LPPROC_THREAD_ATTRIBUTE_LIST list;
                ~AttributeListEnd() { ::DeleteProcThreadAttributeList(list); }
            } listEnd{ attributes };
            HANDLE inherited = log.Get();
            if (!::UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &inherited, sizeof(inherited), nullptr, nullptr)) {
                out.why = LastError("UpdateProcThreadAttribute");
                return out;
            }
            STARTUPINFOEXW startup{};
            startup.StartupInfo.cb = sizeof(startup);
            startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
            startup.StartupInfo.hStdOutput = log.Get();
            startup.StartupInfo.hStdError = log.Get();
            startup.StartupInfo.hStdInput = nullptr;
            startup.lpAttributeList = attributes;
            std::wstring command = Quoted(a_exe) + L" --from-game " + Quoted(a_requestFolder) + L" --staging " + Quoted(a_staging) +
                                   L" --job-memory " + std::to_wstring(a_jobMemoryMiB) + L" --parent-pid " +
                                   std::to_wstring(::GetCurrentProcessId());
            PROCESS_INFORMATION process{};
            const auto workingFolder = a_exe.parent_path();
            if (!::CreateProcessW(a_exe.c_str(), command.data(), nullptr, nullptr, TRUE,
                    CREATE_SUSPENDED | CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                    workingFolder.c_str(), &startup.StartupInfo, &process)) {
                out.why = LastError("CreateProcessW");
                return out;
            }
            UniqueHandle processHandle(process.hProcess);
            UniqueHandle threadHandle(process.hThread);
            if (!::AssignProcessToJobObject(job.Get(), processHandle.Get())) {
                out.why = LastError("AssignProcessToJobObject");
                ::TerminateProcess(processHandle.Get(), 1);  // never run outside the job: no memory cap, no kill-on-close
                return out;
            }
            if (::ResumeThread(threadHandle.Get()) == static_cast<DWORD>(-1)) {
                out.why = LastError("ResumeThread");
                ::TerminateProcess(processHandle.Get(), 1);
                return out;
            }
            out.started = true;
            out.pid = process.dwProcessId;
            a_session.job = std::move(job);
            a_session.process = std::move(processHandle);
            a_session.throttleEvent = std::move(throttleEvent);
            return out;  // the thread handle closes here; the job and the process live on in the session
        }

        // The builder's end as noted under the lock, logged after it (LogEnd)
        struct EndNote
        {
            std::uint32_t pid = 0;
            std::optional<std::uint32_t> code;  // nullopt: GetExitCodeProcess failed (error)
            std::string error;
        };

        // Under the session's lock: once the process has ended (its handle signalled), its exit code, returned once for
        // LogEnd. A job that shows no process while the handle is not signalled yet is left to the next call (no
        // STILL_ACTIVE guess)
        std::optional<EndNote> NoteEndedLocked(Session& a_session)
        {
            if (a_session.ended || !a_session.process || ::WaitForSingleObject(a_session.process.Get(), 0) != WAIT_OBJECT_0) {
                return std::nullopt;
            }
            a_session.ended = true;
            EndNote note{ a_session.launch.pid, std::nullopt, {} };
            DWORD code = 0;
            if (!::GetExitCodeProcess(a_session.process.Get(), &code)) {
                note.error = LastError("GetExitCodeProcess");
                return note;
            }
            a_session.exitCode = code;
            note.code = code;
            return note;
        }

        // Outside the lock
        void LogEnd(const std::optional<EndNote>& a_note)
        {
            if (!a_note) {
                return;
            }
            if (!a_note->code) {
                logger::warn("BlackFlame: slots: builder (pid {}) ended; its exit code: {}", a_note->pid, a_note->error);
                return;
            }
            const auto text = BuilderControl::ExitCodeText(*a_note->code);
            if (*a_note->code == BuilderControl::kExitOk) {
                logger::info("BlackFlame: slots: builder (pid {}) ended: exit code {}", a_note->pid, text);
            } else {
                logger::warn("BlackFlame: slots: builder (pid {}) ended: exit code {}", a_note->pid, text);
            }
        }

        // What ThrottleAfterWindow does, taken under the lock (TakeThrottleLocked) and run after it (RunThrottle)
        struct ThrottleJob
        {
            enum class What : std::uint8_t
            {
                kNothing,    // not requested, or taken already
                kNoBuilder,  // none this session
                kEnded,      // it already ended
                kRun
            };
            What what = What::kNothing;
            std::uint32_t pid = 0;
            HANDLE job = nullptr;
            HANDLE process = nullptr;
            HANDLE event = nullptr;
            std::optional<EndNote> end;
        };

        // a_pending: the request ThrottleAfterWindow left in throttleRequested (TakeOnce); else asked directly (the main
        // thread's free lock, the thread-pool hand-off). Either way at most once a session
        ThrottleJob TakeThrottleLocked(Session& a_session, bool a_pending)
        {
            ThrottleJob out;
            if (!BuilderControl::TakeThrottle(a_pending, a_session.throttleRequested, a_session.throttled)) {
                return out;
            }
            if (!a_session.launch.started || !a_session.job || !a_session.process) {
                out.what = ThrottleJob::What::kNoBuilder;
                return out;
            }
            out.pid = a_session.launch.pid;
            out.end = NoteEndedLocked(a_session);
            if (a_session.ended) {
                out.what = ThrottleJob::What::kEnded;
                return out;
            }
            out.what = ThrottleJob::What::kRun;
            out.job = a_session.job.Get();
            out.process = a_session.process.Get();
            out.event = a_session.throttleEvent ? a_session.throttleEvent.Get() : nullptr;
            return out;
        }

        // Outside the lock (the job's handle copied under it): the extended limits read, only the priority class added (a
        // set replaces LimitFlags whole: kill-on-close and the memory cap must stay), then read back with the process's own
        // class
        std::string ApplyIdlePriority(HANDLE a_job, HANDLE a_process, bool& a_ok)
        {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            if (!::QueryInformationJobObject(a_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits), nullptr)) {
                a_ok = false;
                return "idle priority NOT set: " + LastError("QueryInformationJobObject(limits)");
            }
            limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PRIORITY_CLASS;
            limits.BasicLimitInformation.PriorityClass = IDLE_PRIORITY_CLASS;
            if (!::SetInformationJobObject(a_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
                a_ok = false;
                return "idle priority NOT set: " + LastError("SetInformationJobObject(limits)");
            }
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION back{};
            if (!::QueryInformationJobObject(a_job, JobObjectExtendedLimitInformation, &back, sizeof(back), nullptr)) {
                a_ok = false;
                return "idle priority set (read back: " + LastError("QueryInformationJobObject(limits)") + ")";
            }
            const DWORD flags = back.BasicLimitInformation.LimitFlags;
            const bool killOnClose = (flags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE) != 0;
            const bool memoryCap = (flags & JOB_OBJECT_LIMIT_JOB_MEMORY) != 0;
            const DWORD jobClass = back.BasicLimitInformation.PriorityClass;
            const DWORD processClass = ::GetPriorityClass(a_process);
            a_ok = killOnClose && memoryCap && jobClass == IDLE_PRIORITY_CLASS;
            return std::format("idle priority set (read back: kill-on-close {}, job memory {} MiB, job priority class 0x{:X}, the "
                               "process's 0x{:X}; idle is 0x{:X})", killOnClose ? "yes" : "NO",
                memoryCap ? static_cast<std::uint64_t>(back.JobMemoryLimit) / kMiB : 0, jobClass, processClass,
                static_cast<DWORD>(IDLE_PRIORITY_CLASS));
        }

        // A hard cap of one logical CPU of the whole machine (BuilderControl.h), read back
        std::string ApplyCpuCap(HANDLE a_job, bool& a_ok)
        {
            DWORD cpus = ::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
            if (cpus == 0) {
                SYSTEM_INFO info{};
                ::GetSystemInfo(&info);
                cpus = info.dwNumberOfProcessors;
            }
            const auto rate = BuilderControl::CpuRateForOneCpu(cpus);
            JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cap{};
            cap.ControlFlags = JOB_OBJECT_CPU_RATE_CONTROL_ENABLE | JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;
            cap.CpuRate = rate;
            if (!::SetInformationJobObject(a_job, JobObjectCpuRateControlInformation, &cap, sizeof(cap))) {
                a_ok = false;
                return std::format("CPU cap {}/{} NOT set: {}", rate, BuilderControl::kCpuRateWholeMachine,
                    LastError("SetInformationJobObject(CPU rate)"));
            }
            JOBOBJECT_CPU_RATE_CONTROL_INFORMATION back{};
            if (!::QueryInformationJobObject(a_job, JobObjectCpuRateControlInformation, &back, sizeof(back), nullptr)) {
                a_ok = false;
                return std::format("CPU hard cap {}/{} set (one of {} logical CPUs; read back: {})", rate,
                    BuilderControl::kCpuRateWholeMachine, cpus, LastError("QueryInformationJobObject(CPU rate)"));
            }
            a_ok = back.ControlFlags == cap.ControlFlags && back.CpuRate == rate;
            return std::format("CPU hard cap {}/{} set (one of {} logical CPUs; read back: flags 0x{:X}, rate {})", rate,
                BuilderControl::kCpuRateWholeMachine, cpus, back.ControlFlags, back.CpuRate);
        }

        // Outside the lock: the job calls and every log line of the throttle
        void RunThrottle(const ThrottleJob& a_job)
        {
            LogEnd(a_job.end);
            switch (a_job.what) {
            case ThrottleJob::What::kNothing:
                return;
            case ThrottleJob::What::kNoBuilder:
                logger::debug("BlackFlame: slots: no builder this session: nothing to throttle");
                return;
            case ThrottleJob::What::kEnded:
                logger::debug("BlackFlame: slots: the builder (pid {}) already ended: nothing to throttle", a_job.pid);
                return;
            case ThrottleJob::What::kRun:
                break;
            }
            bool priorityOk = true;
            bool cpuOk = true;
            const auto priority = ApplyIdlePriority(a_job.job, a_job.process, priorityOk);
            const auto cpu = ApplyCpuCap(a_job.job, cpuOk);
            bool eventOk = true;
            std::string event = "set";
            if (!a_job.event) {
                eventOk = false;
                event = "not there (it was not made at launch)";
            } else if (!::SetEvent(a_job.event)) {
                eventOk = false;
                event = "NOT set: " + LastError("SetEvent");
            }
            const auto line = std::format("BlackFlame: slots: builder (pid {}) throttled after the publish window closed: {}; {}; "
                                          "throttle event {}", a_job.pid, priority, cpu, event);
            if (priorityOk && cpuOk && eventOk) {
                logger::info("{}", line);
            } else {
                logger::warn("{}", line);
            }
        }

        // A thread-pool thread (QueueUserWorkItem): the throttle when the main thread found the lock busy; it may wait
        DWORD WINAPI ThrottleOnPool(LPVOID)
        {
            try {
                ThrottleJob job;
                {
                    std::lock_guard lock(TheSession().mutex);
                    job = TakeThrottleLocked(TheSession(), false);
                }
                RunThrottle(job);
            } catch (const std::exception& e) {
                logger::error("BlackFlame: slots: the handed-off builder throttle threw: {}", e.what());
            } catch (...) {
                logger::error("BlackFlame: slots: the handed-off builder throttle threw an exception of an unknown type");
            }
            return 0;
        }

        // Never on the main thread (the progress worker, the end callback): a request the main thread could not hand off
        void HonourPendingThrottle()
        {
            auto& session = TheSession();
            if (!session.throttleRequested.load()) {
                return;
            }
            ThrottleJob job;
            {
                std::lock_guard lock(session.mutex);
                job = TakeThrottleLocked(session, true);
            }
            RunThrottle(job);
        }

        // RegisterWaitForSingleObject's wait, released without waiting (called from its own callback UnregisterWait
        // reports ERROR_IO_PENDING: the wait is released once the callback returns; not a failure)
        void ReleaseEndWait(HANDLE a_wait)
        {
            if (!a_wait || ::UnregisterWait(a_wait)) {
                return;
            }
            const DWORD error = ::GetLastError();
            if (error != ERROR_IO_PENDING) {
                logger::warn("BlackFlame: slots: the wait on the builder's end was not released: UnregisterWait failed (Windows "
                             "error {})", error);
            }
        }

        // The end seen however late in the session (the progress worker stops polling when the publish window closes); the
        // lock held only to note it, the log line and the wait's release after it
        VOID CALLBACK OnBuilderEnded(PVOID, BOOLEAN)
        {
            try {
                std::optional<EndNote> end;
                HANDLE wait = nullptr;
                {
                    std::lock_guard lock(TheSession().mutex);
                    end = NoteEndedLocked(TheSession());
                    wait = std::exchange(TheSession().endWait, nullptr);
                }
                LogEnd(end);
                ReleaseEndWait(wait);
                HonourPendingThrottle();
            } catch (...) {
            }
        }
    }

    Launch Start(const std::filesystem::path& a_exe, const std::filesystem::path& a_requestFolder,
        const std::filesystem::path& a_staging, std::uint32_t a_jobMemoryMiB)
    {
        auto& session = TheSession();
        Launch launch;
        std::string logNote = "no previous builder.log";
        std::string eventNote;
        std::string waitWhy;
        {
            // StartChecked's warnings and the "builder started" line are logged under the lock: nothing else takes it
            // before the end wait is registered, so a builder that ends at once is logged ended after it started
            std::lock_guard lock(session.mutex);
            if (session.tried) {
                return session.launch;
            }
            session.tried = true;
            session.launch = StartChecked(a_exe, a_requestFolder, a_staging, a_jobMemoryMiB, session, logNote, eventNote);
            if (session.launch.started) {
                logger::info("BlackFlame: slots: builder started (pid {}): '{}' --from-game '{}' --staging '{}' --job-memory {} MiB, "
                             "below normal priority, in a kill-on-close job; its output in builder.log ({}); throttle event {}",
                    session.launch.pid, Text(a_exe), Text(a_requestFolder), Text(a_staging), a_jobMemoryMiB, logNote,
                    session.throttleEvent ? eventNote : std::string("none"));
                // Stored under the lock the callback takes first, so it always finds the wait to release (review E2 #3)
                HANDLE wait = nullptr;
                if (::RegisterWaitForSingleObject(&wait, session.process.Get(), &OnBuilderEnded, nullptr, INFINITE, WT_EXECUTEONLYONCE)) {
                    session.endWait = wait;
                } else {
                    waitWhy = LastError("RegisterWaitForSingleObject");
                }
            }
            launch = session.launch;
        }
        if (!launch.started) {
            logger::warn("BlackFlame: slots: the builder was not started: {}", launch.why);
            return launch;
        }
        if (!waitWhy.empty()) {
            logger::warn("BlackFlame: slots: {}: the builder's end is noted only while the progress worker polls", waitWhy);
        }
        return launch;
    }

    bool Running()
    {
        auto& session = TheSession();
        std::optional<EndNote> end;
        bool running = false;
        {
            std::lock_guard lock(session.mutex);
            if (!session.job) {
                return false;
            }
            JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
            running = ::QueryInformationJobObject(session.job.Get(), JobObjectBasicAccountingInformation, &info, sizeof(info), nullptr) &&
                      info.ActiveProcesses > 0;
            if (!running) {
                end = NoteEndedLocked(session);
            }
        }
        LogEnd(end);
        HonourPendingThrottle();  // the progress worker's thread
        return running;
    }

    std::string Status()
    {
        Launch launch;
        {
            std::lock_guard lock(TheSession().mutex);
            launch = TheSession().launch;
        }
        if (!launch.started) {
            return launch.why.empty() ? std::string("not started") : "not started (" + launch.why + ")";
        }
        if (Running()) {
            return std::format("running (pid {})", launch.pid);
        }
        std::lock_guard lock(TheSession().mutex);
        const auto& session = TheSession();
        if (!session.ended) {
            return "ended (exit code not read yet)";
        }
        return session.exitCode ? "ended (exit " + BuilderControl::ExitCodeText(*session.exitCode) + ")" :
                                  std::string("ended (exit code not readable)");
    }

    void ThrottleAfterWindow()
    {
        // review E2 #1 (Codex 1 of defe5ede): the main thread at kPreLoadGame never waits on the session lock. Free: the
        // job is taken under it and run after it, here. Busy (the end callback or the progress worker in its few calls):
        // handed to a thread-pool thread, which may wait. That refused: the request is published (throttleRequested) for
        // the next lock holder; only then, so a throttle taken here or by the pool thread leaves no flag for later polls
        auto& session = TheSession();
        ThrottleJob job;
        DWORD submitError = 0;
        const auto how = BuilderControl::RunOrHandOff(
            session.mutex,
            [&](std::unique_lock<TrackedMutex>& a_lock) {
                job = TakeThrottleLocked(session, false);
                a_lock.unlock();
            },
            [&]() {
                if (::QueueUserWorkItem(&ThrottleOnPool, nullptr, WT_EXECUTEDEFAULT)) {
                    return true;
                }
                submitError = ::GetLastError();
                return false;
            });
        switch (how) {
        case BuilderControl::HandOff::kRanHere:
            RunThrottle(job);
            break;
        case BuilderControl::HandOff::kHandedOff:
            logger::info("BlackFlame: slots: the builder's session lock was busy: its throttle is handed to a thread-pool thread");
            break;
        case BuilderControl::HandOff::kPending:
            session.throttleRequested.store(true);
            logger::warn("BlackFlame: slots: the builder's session lock was busy and the hand-off to a thread-pool thread failed "
                         "(Windows error {}): the throttle waits for the next lock holder (the progress worker if it still "
                         "polls, else only the builder's end, when it no longer matters): in practice none this session",
                submitError);
            break;
        }
    }
}

// =============================================================================
// BuilderLauncher - the builder child: CreateProcessW suspended, a job with kill-on-close and the memory cap, UniqueHandle
// =============================================================================

#include "BuilderLauncher.h"

#include "TrackedMutex.h"

#include <format>
#include <memory>
#include <mutex>
#include <vector>

namespace BlackFlame::BuilderLauncher
{
    namespace
    {
        constexpr const wchar_t* kLogName = L"builder.log";
        constexpr std::uint64_t kMiB = 1024ull * 1024ull;

        struct Session
        {
            UniqueHandle job;  // never closed by us (kill-on-close fires when the game ends)
            Launch launch;
            bool tried = false;
        };
        Session& TheSession()
        {
            static auto* session = std::make_unique<Session>().release();  // leaked on purpose: the job outlives every static
            return *session;
        }
        TrackedMutex g_mutex;

        std::wstring Quoted(const std::filesystem::path& a_path)
        {
            return L"\"" + a_path.wstring() + L"\"";
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

        Launch StartChecked(const std::filesystem::path& a_exe, const std::filesystem::path& a_requestFolder,
            const std::filesystem::path& a_staging, std::uint32_t a_jobMemoryMiB, UniqueHandle& a_jobOut)
        {
            Launch out;
            std::error_code ec;
            if (a_exe.empty() || !std::filesystem::exists(a_exe, ec)) {
                out.why = std::format("the builder exe '{}' is not there", Text(a_exe));
                return out;
            }
            std::filesystem::create_directories(a_staging, ec);
            auto job = MakeJob(a_jobMemoryMiB, out.why);
            auto log = job ? MakeLog(a_requestFolder, out.why) : UniqueHandle{};
            if (!job || !log) {
                return out;
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
            a_jobOut = std::move(job);
            return out;  // the process and thread handles close here; the job lives on in the session
        }
    }

    Launch Start(const std::filesystem::path& a_exe, const std::filesystem::path& a_requestFolder,
        const std::filesystem::path& a_staging, std::uint32_t a_jobMemoryMiB)
    {
        std::lock_guard lock(g_mutex);
        auto& session = TheSession();
        if (session.tried) {
            return session.launch;
        }
        session.tried = true;
        session.launch = StartChecked(a_exe, a_requestFolder, a_staging, a_jobMemoryMiB, session.job);
        if (session.launch.started) {
            logger::info("BlackFlame: slots: builder started (pid {}): '{}' --from-game '{}' --staging '{}' --job-memory {} MiB, "
                         "below normal priority, in a kill-on-close job; its output in builder.log", session.launch.pid, Text(a_exe),
                Text(a_requestFolder), Text(a_staging), a_jobMemoryMiB);
        } else {
            logger::warn("BlackFlame: slots: the builder was not started: {}", session.launch.why);
        }
        return session.launch;
    }

    bool Running() noexcept
    {
        std::lock_guard lock(g_mutex);
        auto& session = TheSession();
        if (!session.job) {
            return false;
        }
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
        return ::QueryInformationJobObject(session.job.Get(), JobObjectBasicAccountingInformation, &info, sizeof(info), nullptr) &&
               info.ActiveProcesses > 0;
    }

    std::string Status()
    {
        Launch launch;
        {
            std::lock_guard lock(g_mutex);
            launch = TheSession().launch;
        }
        if (!launch.started) {
            return launch.why.empty() ? std::string("not started") : "not started (" + launch.why + ")";
        }
        return Running() ? std::format("running (pid {})", launch.pid) : std::string("ended");
    }
}

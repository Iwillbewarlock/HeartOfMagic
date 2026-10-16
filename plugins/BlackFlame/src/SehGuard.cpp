#include "SehGuard.h"
#include "TrackedMutex.h"

#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <Windows.h>

#include <atomic>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace BlackFlame
{
    namespace
    {
        struct SehInfo
        {
            DWORD code = 0;
            void* address = nullptr;
        };

        // A step that faulted once is off for the session: its work may have stopped half done
        struct StepState
        {
            int faults = 0;
            std::uint64_t skipped = 0;
        };

        constexpr std::uint64_t kSkipLogFactor = 10;  // skipped calls logged at 1, 10, 100, ...

        TrackedMutex g_mutex;
        std::atomic<bool> g_anyFault{ false };  // no lookup at all until some step faulted

        // Guarded by g_mutex; keyed by the step name's content (one name at several call sites is one
        // step). Heap-held like the layers' lists, so nothing runs at DLL unload.
        std::unordered_map<std::string, StepState>& Steps()
        {
            static auto* steps = std::make_unique<std::unordered_map<std::string, StepState>>().release();
            return *steps;
        }

        // Only faults that mean "this code read or ran something it should not have"; everything else
        // (a C++ exception, a stack overflow, a breakpoint, a debugger's thread-name exception) goes on
        // to the handlers above
        bool IsCaughtCode(DWORD a_code)
        {
            switch (a_code) {
            case EXCEPTION_ACCESS_VIOLATION:
            case EXCEPTION_IN_PAGE_ERROR:
            case EXCEPTION_ILLEGAL_INSTRUCTION:
            case EXCEPTION_INT_DIVIDE_BY_ZERO:
            case EXCEPTION_DATATYPE_MISALIGNMENT:
                return true;
            default:
                return false;
            }
        }

        int Filter(EXCEPTION_POINTERS* a_pointers, SehInfo* a_out)
        {
            if (!a_pointers || !a_pointers->ExceptionRecord ||
                !IsCaughtCode(a_pointers->ExceptionRecord->ExceptionCode)) {
                return EXCEPTION_CONTINUE_SEARCH;
            }
            a_out->code = a_pointers->ExceptionRecord->ExceptionCode;
            a_out->address = a_pointers->ExceptionRecord->ExceptionAddress;
            return EXCEPTION_EXECUTE_HANDLER;
        }

        // No C++ objects in this frame: __try needs that
        bool RunRaw(SehBody a_body, void* a_context, SehInfo* a_info)
        {
            __try {
                a_body(a_context);
                return true;
            } __except (Filter(GetExceptionInformation(), a_info)) {
                return false;
            }
        }

        std::string ModuleOffset(void* a_address)
        {
            HMODULE module = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    static_cast<LPCWSTR>(a_address), &module) ||
                !module) {
                return std::format("0x{:X} (no module)", reinterpret_cast<std::uintptr_t>(a_address));
            }
            char path[MAX_PATH]{};
            GetModuleFileNameA(module, path, MAX_PATH);
            std::string name(path);
            const auto slash = name.find_last_of("\\/");
            if (slash != std::string::npos) {
                name.erase(0, slash + 1);
            }
            return std::format("{}+0x{:X}", name,
                reinterpret_cast<std::uintptr_t>(a_address) - reinterpret_cast<std::uintptr_t>(module));
        }

        // True when the step is off (a call skipped, counted and logged at 1, 10, 100, ...)
        bool SkipDisabled(const char* a_step)
        {
            if (!g_anyFault.load()) {
                return false;
            }
            std::uint64_t skipped = 0;
            {
                std::lock_guard lock(g_mutex);
                const auto it = Steps().find(a_step);
                if (it == Steps().end()) {
                    return false;
                }
                skipped = ++it->second.skipped;
            }
            for (std::uint64_t mark = 1; mark <= skipped; mark *= kSkipLogFactor) {
                if (mark == skipped) {
                    logger::warn("BlackFlame: {}: off after its fault, {} call(s) skipped so far", a_step, skipped);
                    break;
                }
            }
            return true;
        }

        // A step that keeps running after a fault (a restore: one bad property must not leave the rest
        // grey) is counted under its own table, never switched off
        std::unordered_map<std::string, int>& KeptFaults()
        {
            static auto* kept = std::make_unique<std::unordered_map<std::string, int>>().release();
            return *kept;
        }

        void Report(const char* a_step, const SehInfo& a_info, SehPolicy a_policy)
        {
            int faults = 0;
            {
                std::lock_guard lock(g_mutex);
                faults = a_policy == SehPolicy::kDisableStep ? ++Steps()[a_step].faults : ++KeptFaults()[a_step];
            }
            if (a_policy == SehPolicy::kDisableStep) {
                g_anyFault.store(true);
            }
            // A kept step can fault again and again: its lines are capped like the skip lines (1, 10, 100...)
            if (a_policy == SehPolicy::kKeepStep) {
                bool due = false;
                for (std::uint64_t mark = 1; mark <= static_cast<std::uint64_t>(faults); mark *= kSkipLogFactor) {
                    due = due || mark == static_cast<std::uint64_t>(faults);
                }
                if (!due) {
                    return;
                }
            }
            logger::error("BlackFlame: {}: hardware exception 0x{:08X} at {} (faultThread={}), fault {} of this step; {}",
                a_step, a_info.code, ModuleOffset(a_info.address), GetCurrentThreadId(), faults,
                a_policy == SehPolicy::kDisableStep ? "the step is off for the rest of the session" :
                                                      "the step stays on (only this item was skipped)");
        }
    }

    bool RunSehGuarded(const char* a_step, SehBody a_body, void* a_context, SehPolicy a_policy)
    {
        if (a_policy == SehPolicy::kDisableStep && SkipDisabled(a_step)) {
            return false;
        }
        SehInfo info;
        if (RunRaw(a_body, a_context, &info)) {
            return true;
        }
        Report(a_step, info, a_policy);
        return false;
    }
}

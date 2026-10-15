#include "SehGuard.h"

#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <Windows.h>

#include <format>
#include <mutex>
#include <string>
#include <unordered_set>

namespace BlackFlame
{
    namespace
    {
        struct SehInfo
        {
            DWORD code = 0;
            void* address = nullptr;
        };

        int Filter(EXCEPTION_POINTERS* a_pointers, SehInfo* a_out)
        {
            if (!a_pointers || !a_pointers->ExceptionRecord) {
                return EXCEPTION_CONTINUE_SEARCH;
            }
            // A C++ exception (MSVC's 0xE06D7363) is left to the C++ handlers
            constexpr DWORD kCppException = 0xE06D7363;
            if (a_pointers->ExceptionRecord->ExceptionCode == kCppException) {
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

        void Report(const char* a_step, const SehInfo& a_info)
        {
            static std::mutex mutex;
            static std::unordered_set<std::string> reported;
            {
                std::lock_guard lock(mutex);
                if (!reported.insert(a_step).second) {
                    return;  // once per step
                }
            }
            logger::error("BlackFlame: {}: hardware exception 0x{:08X} at {} (thread {}); this step is skipped where it "
                          "happens, further ones are not logged",
                a_step, a_info.code, ModuleOffset(a_info.address), GetCurrentThreadId());
        }
    }

    bool RunSehGuarded(const char* a_step, SehBody a_body, void* a_context) noexcept
    {
        SehInfo info;
        if (RunRaw(a_body, a_context, &info)) {
            return true;
        }
        try {
            Report(a_step, info);
        } catch (...) {
        }
        return false;
    }
}

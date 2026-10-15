#include "SelfTestLog.h"

#include <format>
#include <fstream>

namespace BlackFlame::SelfTestLog
{
    namespace
    {
        std::ofstream g_file;
        int g_pass = 0;
        int g_fail = 0;
        int g_info = 0;

        void Write(const char* a_kind, const char* a_id, const std::string& a_detail)
        {
            const auto line = std::format("[SELFTEST] {} {} {}", a_kind, a_id, a_detail);
            if (std::string_view(a_kind) == "FAIL") {
                logger::warn("{}", line);
            } else {
                logger::info("{}", line);
            }
            if (g_file) {
                g_file << line << '\n';
                g_file.flush();
            }
        }
    }

    void Open(bool a_append)
    {
        g_pass = 0;
        g_fail = 0;
        g_info = 0;
        auto dir = logger::log_directory();
        if (!dir) {
            logger::warn("BlackFlame: no SKSE log folder - self-test results go to the main log only");
            return;
        }
        const auto path = *dir / kFileName;
        g_file.open(path, std::ios::out | (a_append ? std::ios::app : std::ios::trunc));
        if (!g_file) {
            logger::warn("BlackFlame: could not open the self-test file - results go to the main log only");
        }
    }

    void Pass(const char* a_id, const std::string& a_detail)
    {
        ++g_pass;
        Write("PASS", a_id, a_detail);
    }

    void Fail(const char* a_id, const std::string& a_detail)
    {
        ++g_fail;
        Write("FAIL", a_id, a_detail);
    }

    void Info(const char* a_id, const std::string& a_detail)
    {
        ++g_info;
        Write("INFO", a_id, a_detail);
    }

    void Check(const char* a_id, bool a_ok, const std::string& a_detail)
    {
        if (a_ok) {
            Pass(a_id, a_detail);
        } else {
            Fail(a_id, a_detail);
        }
    }

    void Done(const char* a_label)
    {
        const auto line = std::format("[SELFTEST] DONE{}{} pass={} fail={} info={}", a_label ? " " : "", a_label ? a_label : "",
            g_pass, g_fail, g_info);
        logger::info("{}", line);
        if (g_file) {
            g_file << line << '\n';
            g_file.close();
        }
        FlushLog();  // info lines are buffered (plugins/Common.h): the results reach disk now
    }
}

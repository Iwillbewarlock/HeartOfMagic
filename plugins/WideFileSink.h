#pragma once

#include <spdlog/sinks/base_sink.h>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <share.h>
#include <string>

/**
 * A log file opened by its wide (UTF-16) path.
 *
 * CommonLib's logger::init() hands spdlog path->string(), which converts the
 * Documents path to the system's ANSI code page and throws when that page
 * cannot hold it (a user folder named in Korean on an English Windows, an
 * emoji anywhere in it). spdlog here is built without SPDLOG_WCHAR_FILENAMES,
 * so its own file sinks cannot take a wide path either. This one opens the
 * file with _wfsopen and does nothing else; SetupLog (Common.h) falls back to
 * it when init() throws.
 */
class WideFileSink final : public spdlog::sinks::base_sink<std::mutex>
{
public:
    /** Truncates the file, like CommonLib's sink. Throws spdlog::spdlog_ex when it cannot open it. */
    explicit WideFileSink(const std::filesystem::path& path)
    {
        m_file.reset(_wfsopen(path.c_str(), L"wb", _SH_DENYWR));
        if (!m_file) {
            throw spdlog::spdlog_ex("WideFileSink: cannot open the log file");
        }
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);
        std::fwrite(formatted.data(), 1, formatted.size(), m_file.get());
    }

    void flush_() override { std::fflush(m_file.get()); }

private:
    struct FileCloser
    {
        void operator()(std::FILE* file) const noexcept
        {
            if (file) {
                std::fclose(file);
            }
        }
    };
    std::unique_ptr<std::FILE, FileCloser> m_file;
};

#pragma once

#include "AsciiText.h"
#include "EncodingUtils.h"

#include <cstdint>
#include <format>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

/**
 * Plugin file names in persistentIds ("Plugin.esp|0x000D62").
 *
 * The game keeps a plugin's file name in the system's ANSI code page. The key
 * holds its UTF-8 form (EncodingUtils::SanitizeToUTF8), so it can go into JSON;
 * an ASCII or already-UTF-8 name is byte for byte what it always was. Finding
 * the file again takes the game's own lookup first (it compares the raw bytes,
 * which is every ASCII name) and, for a name with a non-ASCII byte it misses,
 * the converted names of the loaded files.
 *
 * No game types: SpellScannerFormId.cpp hands in the game's lookup and file
 * list, and an offline check hands in its own.
 */
namespace PluginNames
{
    /// CP_ACP, the system's ANSI code page (not including <windows.h> here)
    inline constexpr unsigned int kSystemCodePage = 0;

    [[nodiscard]] inline std::string MakePersistentId(const char* rawFileName, std::uint32_t localFormId,
                                                      unsigned int codePage = kSystemCodePage)
    {
        return std::format("{}|0x{:06X}", EncodingUtils::SanitizeToUTF8(rawFileName, codePage), localFormId);
    }

    /**
     * Finds files by the name a persistentId holds, remembering every answer
     * (a miss too). One lives for one pass over a tree: a tree of hundreds of
     * nodes from one plugin converts the file list once, not once per node.
     *
     * @tparam Handle  what a file is to the caller (const RE::TESFile*); a
     *                 value-initialised Handle means "not loaded"
     */
    template <class Handle>
    class Lookup
    {
    public:
        explicit Lookup(unsigned int codePage = kSystemCodePage) : m_codePage(codePage) {}

        /**
         * @param rawLookup  Handle(const std::string&): the game's lookup by raw bytes
         * @param forEachFile  void(visit) calling visit(const char* rawName, Handle) for each loaded
         *                     file; called at most once per Lookup
         */
        template <class RawLookup, class ForEachFile>
        Handle Find(const std::string& name, RawLookup&& rawLookup, ForEachFile&& forEachFile)
        {
            if (const auto known = m_found.find(name); known != m_found.end()) {
                return known->second;
            }
            Handle found = rawLookup(name);
            if (!found && AsciiText::HasNonAscii(name)) {
                if (!m_convertedReady) {
                    forEachFile([this](const char* rawName, Handle file) {
                        if (rawName && AsciiText::HasNonAscii(rawName)) {
                            m_converted.emplace_back(EncodingUtils::SanitizeToUTF8(rawName, m_codePage), file);
                        }
                    });
                    m_convertedReady = true;
                    ++m_fileListReads;
                }
                for (const auto& [converted, file] : m_converted) {
                    if (AsciiText::EqualsIgnoreCase(converted, name)) {
                        found = file;
                        break;
                    }
                }
            }
            m_found.emplace(name, found);
            return found;
        }

        /// How often the file list was read (0 or 1) - for checks
        [[nodiscard]] int FileListReads() const noexcept { return m_fileListReads; }

    private:
        unsigned int m_codePage;
        std::unordered_map<std::string, Handle> m_found;
        std::vector<std::pair<std::string, Handle>> m_converted;
        bool m_convertedReady = false;
        int m_fileListReads = 0;
    };
}

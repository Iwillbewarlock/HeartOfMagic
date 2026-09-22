#pragma once

#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>

/**
 * Writing a file without losing the old one.
 *
 * Every save in this plugin used to open the real file and truncate it, so a
 * crash or a power cut partway through left a half-written spell_tree.json and
 * the player's whole generated tree was gone. The content is written to a
 * neighbouring temp file first, flushed, closed, and only then moved over the
 * target - a move being the one step the filesystem does in one piece. The
 * previous file is kept as <name>.bak, which is what a player is told to
 * rename when a save does go wrong.
 */
namespace FileUtils
{
    /**
     * @param path      the file to end up with
     * @param content   what it should contain
     * @param keepBackup  move the existing file to <name>.bak first
     * @return true when `path` now holds `content`
     */
    inline bool WriteAtomically(const std::filesystem::path& path,
                                std::string_view content,
                                bool keepBackup = true)
    {
        std::error_code ec;
        auto temp = path;
        temp += ".tmp";

        {
            std::ofstream file(temp, std::ios::binary | std::ios::trunc);
            if (!file.is_open()) {
                logger::error("FileUtils: cannot open {} for writing", temp.string());
                return false;
            }
            file.write(content.data(), static_cast<std::streamsize>(content.size()));
            file.flush();
            if (file.fail()) {
                logger::error("FileUtils: failed while writing {}", temp.string());
                file.close();
                std::filesystem::remove(temp, ec);
                return false;
            }
        }  // closed here: the move below must not race the stream

        auto backup = path;
        backup += ".bak";
        bool movedToBackup = false;

        if (keepBackup && std::filesystem::exists(path, ec)) {
            std::filesystem::remove(backup, ec);
            ec.clear();
            std::filesystem::rename(path, backup, ec);
            movedToBackup = !ec;
            if (ec) {
                // Not fatal: the new file is still worth having
                logger::warn("FileUtils: could not keep a backup of {}: {}", path.string(), ec.message());
                ec.clear();
            }
        }

        std::filesystem::rename(temp, path, ec);
        if (ec) {
            // Windows will not rename onto an existing file; take it out of the way
            ec.clear();
            std::filesystem::remove(path, ec);
            ec.clear();
            std::filesystem::rename(temp, path, ec);
        }
        if (ec) {
            logger::error("FileUtils: could not move {} into place: {}", temp.string(), ec.message());
            std::filesystem::remove(temp, ec);
            ec.clear();
            // Put the old file back under its own name. Without this the caller
            // is told the save failed while the file it names is gone, and the
            // player has to know to rename a .bak by hand to get it back.
            if (movedToBackup) {
                std::filesystem::rename(backup, path, ec);
                if (ec) {
                    logger::error("FileUtils: and the previous file is left at {}", backup.string());
                } else {
                    logger::info("FileUtils: the previous {} is back in place", path.filename().string());
                }
            }
            return false;
        }
        return true;
    }
}

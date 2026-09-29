#pragma once

#include "PathText.h"

#include <filesystem>
#include <fstream>
#include <string>
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
    /// Where WriteAtomically keeps the previous file: <name>.bak
    inline std::filesystem::path BackupPath(const std::filesystem::path& path)
    {
        auto backup = path;
        backup += ".bak";
        return backup;
    }

    /**
     * @param path      the file to end up with
     * @param content   what it should contain
     * @param keepBackup  move the existing file to <name>.bak first (the old
     *                    .bak is removed); false leaves any .bak as it is
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
                logger::error("FileUtils: cannot open {} for writing", PathText::Utf8(temp));
                return false;
            }
            file.write(content.data(), static_cast<std::streamsize>(content.size()));
            file.flush();
            if (file.fail()) {
                logger::error("FileUtils: failed while writing {}", PathText::Utf8(temp));
                file.close();
                std::filesystem::remove(temp, ec);
                return false;
            }
        }  // closed here: the move below must not race the stream

        const auto backup = BackupPath(path);
        bool movedToBackup = false;

        if (keepBackup && std::filesystem::exists(path, ec)) {
            std::filesystem::remove(backup, ec);
            ec.clear();
            std::filesystem::rename(path, backup, ec);
            movedToBackup = !ec;
            if (ec) {
                // Not fatal: the new file is still worth having
                logger::warn("FileUtils: could not keep a backup of {}: {}", PathText::Utf8(path), ec.message());
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
            logger::error("FileUtils: could not move {} into place: {}", PathText::Utf8(temp), ec.message());
            std::filesystem::remove(temp, ec);
            ec.clear();
            // Put the old file back under its own name. Without this the caller
            // is told the save failed while the file it names is gone, and the
            // player has to know to rename a .bak by hand to get it back.
            if (movedToBackup) {
                std::filesystem::rename(backup, path, ec);
                if (ec) {
                    logger::error("FileUtils: and the previous file is left at {}", PathText::Utf8(backup));
                } else {
                    logger::info("FileUtils: the previous {} is back in place", PathText::Utf8(path.filename()));
                }
            }
            return false;
        }
        return true;
    }

    /// How many unreadable copies of one file MoveAside keeps (.broken, .broken-2 ...)
    inline constexpr int kMaxAsideCopies = 20;

    /**
     * Move a file that cannot be read out of the way before it is replaced.
     *
     * A config.json that did not parse used to be written over with the
     * defaults, and whatever the player had set was gone. It is kept as
     * <name><suffix> - or <name><suffix>-2, -3 ... when an earlier one is
     * there - for the player (or a bug report) to look at.
     *
     * @return the path it was moved to; empty when it could not be moved,
     *         in which case the caller must leave the file where it is
     */
    inline std::filesystem::path MoveAside(const std::filesystem::path& path,
                                           std::string_view suffix = ".broken")
    {
        std::error_code ec;
        for (int copy = 1; copy <= kMaxAsideCopies; ++copy) {
            auto target = path;
            target += std::string(suffix);
            if (copy > 1) {
                target += "-" + std::to_string(copy);
            }
            ec.clear();
            if (std::filesystem::exists(target, ec) || ec) {
                continue;
            }
            std::filesystem::rename(path, target, ec);
            if (!ec) {
                return target;
            }
            logger::error("FileUtils: could not move {} aside to {}: {}",
                PathText::Utf8(path), PathText::Utf8(target.filename()), ec.message());
            return {};
        }
        logger::error("FileUtils: {} not moved aside - {} earlier copies are already there",
            PathText::Utf8(path), kMaxAsideCopies);
        return {};
    }
}

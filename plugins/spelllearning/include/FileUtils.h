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
 *
 * The file is there at every moment: the .bak is a copy, not the file moved
 * away, and the temp file is moved onto the file, replacing it (MSVC's
 * std::filesystem::rename replaces an existing file: tools/file-utils-check).
 * The .bak used to be made by moving the file, so a crash between that move
 * and the next left no file at all, and the tree graft's journal took a
 * missing tree for one it had not written.
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

    /// Where WriteAtomically writes the content before it moves it onto the file: <name>.tmp
    inline std::filesystem::path TempPath(const std::filesystem::path& path)
    {
        auto temp = path;
        temp += ".tmp";
        return temp;
    }

    /**
     * @param path      the file to end up with
     * @param content   what it should contain
     * @param keepBackup  copy the existing file to <name>.bak first (over the
     *                    old .bak); false leaves any .bak as it is
     * @return true when `path` now holds `content`; false leaves `path` as it was
     */
    inline bool WriteAtomically(const std::filesystem::path& path,
                                std::string_view content,
                                bool keepBackup = true)
    {
        std::error_code ec;
        const auto temp = TempPath(path);

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

        // A copy: the file itself stays where it is until the move below replaces it
        if (keepBackup && std::filesystem::exists(path, ec)) {
            std::filesystem::copy_file(path, BackupPath(path), std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                // Not fatal: the new file is still worth having
                logger::warn("FileUtils: could not keep a backup of {}: {}", PathText::Utf8(path), ec.message());
            }
        }
        ec.clear();

        // Replaces the file in one step (MoveFileExW with MOVEFILE_REPLACE_EXISTING in MSVC's
        // STL); never remove-then-move, which leaves a moment with no file
        std::filesystem::rename(temp, path, ec);
        if (ec) {
            logger::error("FileUtils: could not move {} into place: {} - {} is left as it was", PathText::Utf8(temp),
                ec.message(), PathText::Utf8(path.filename()));
            std::filesystem::remove(temp, ec);
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

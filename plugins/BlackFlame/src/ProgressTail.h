#pragma once

// =============================================================================
// PROGRESS TAIL - where the reader of progress.jsonl goes on from (no game types; BuildProgress.cpp uses it)
// =============================================================================
// The builder appends one line per finished object; at its start it compacts the file by writing a new one and renaming it
// over the old with POSIX semantics (the rename succeeds while this reader holds the old file open with FILE_SHARE_DELETE),
// and it drops a cut last line before it appends. So each poll the reader opens the path again and:
//   Begin  a file id other than the last one (the compaction) or a file shorter than the offset: read from 0, the old offset
//          and any kept bytes forgotten (the table is by key, so lines read twice change nothing)
//   Take   the bytes read from that offset: the complete lines are returned, the bytes after the last '\n' kept
//   End    at the end of the file a cut last line is not kept: the offset steps back to its start, so the next poll reads
//          it again whole (or the line the builder wrote in its place) and never fuses a fragment with a later line
// C++17: tools/blackflame-cosave-check tests it.

#include <cstdint>
#include <string>
#include <string_view>

namespace BlackFlame::ProgressTail
{
    struct Tail
    {
        std::string pending;      // bytes after the last '\n' of this read
        std::uint64_t offset = 0; // the next read starts here
        std::uint64_t fileId = 0;
        bool haveFile = false;
    };

    /// The file has a_fileId and a_size now: the offset to read from; a_reset tells that the file is another one (or shorter)
    [[nodiscard]] inline std::uint64_t Begin(Tail& a_tail, std::uint64_t a_fileId, std::uint64_t a_size, bool& a_reset)
    {
        a_reset = a_tail.haveFile && (a_fileId != a_tail.fileId || a_size < a_tail.offset);
        if (a_reset) {
            a_tail.offset = 0;
            a_tail.pending.clear();
        }
        a_tail.haveFile = true;
        a_tail.fileId = a_fileId;
        return a_tail.offset;
    }

    /// a_bytes read at the offset: the complete lines (ending with '\n'); the rest is kept for End
    [[nodiscard]] inline std::string Take(Tail& a_tail, std::string_view a_bytes)
    {
        a_tail.offset += a_bytes.size();
        a_tail.pending.append(a_bytes);
        const auto end = a_tail.pending.rfind('\n');
        if (end == std::string::npos) {
            return {};
        }
        std::string lines = a_tail.pending.substr(0, end + 1);
        a_tail.pending.erase(0, end + 1);
        return lines;
    }

    /// The read reached the end of the file: a cut last line is read again from its start at the next poll
    inline void End(Tail& a_tail)
    {
        a_tail.offset -= a_tail.pending.size();
        a_tail.pending.clear();
    }
}

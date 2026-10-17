#pragma once

// =============================================================================
// PROGRESS TAIL TESTS - plugins/BlackFlame/src/ProgressTail.h on simulated progress.jsonl histories
// =============================================================================
// Each poll: the file's id and its whole contents now; the reader reads from where ProgressTail says (as BuildProgress does).

#include "../../plugins/BlackFlame/src/ProgressTail.h"

#include <cstdint>
#include <string>

namespace ProgressTailTests
{
    namespace PT = BlackFlame::ProgressTail;

    // One poll of a file with a_id holding a_contents: the complete lines read
    inline std::string Poll(PT::Tail& a_tail, std::uint64_t a_id, const std::string& a_contents, bool* a_reset = nullptr)
    {
        bool reset = false;
        const auto from = PT::Begin(a_tail, a_id, a_contents.size(), reset);
        if (a_reset) {
            *a_reset = reset;
        }
        std::string lines;
        if (from < a_contents.size()) {
            const std::string_view rest = std::string_view(a_contents).substr(static_cast<std::size_t>(from));
            const auto half = rest.size() / 2;  // in two reads, as the reader's chunks may cut anywhere
            lines += PT::Take(a_tail, rest.substr(0, half));
            lines += PT::Take(a_tail, rest.substr(half));
        }
        PT::End(a_tail);
        return lines;
    }

    template <class Check>
    void Run(Check&& a_check)
    {
        PT::Tail tail;
        const auto first = Poll(tail, 1, "{a}\n{b");
        const auto second = Poll(tail, 1, "{a}\n{b}\n{c}\n");
        a_check(first == "{a}\n" && second == "{b}\n{c}\n" && tail.offset == 12,
            "progress tail: a cut last line waits and is read whole once complete; lines are never read twice on one file");

        PT::Tail dropped;
        (void)Poll(dropped, 1, "{a}\n{bb");
        const auto replaced = Poll(dropped, 1, "{a}\n{d}\n");
        a_check(replaced == "{d}\n", "progress tail: a cut line the builder dropped and replaced is not fused with the new line (OCR DC6)");

        PT::Tail compacted;
        (void)Poll(compacted, 1, "{a}\n{b}\n{c}\n");
        bool reset = false;
        const auto after = Poll(compacted, 2, "{b}\n{c}\n{e}\n", &reset);
        a_check(reset && after == "{b}\n{c}\n{e}\n",
            "progress tail: a new file id (the builder's compaction by a POSIX rename) is read from its start, the old offset dropped");

        PT::Tail shrunk;
        (void)Poll(shrunk, 1, "{a}\n{b}\n{c}\n");
        bool shrinkReset = false;
        const auto again = Poll(shrunk, 1, "{x}\n", &shrinkReset);
        const auto none = Poll(shrunk, 1, "{x}\n");
        a_check(shrinkReset && again == "{x}\n" && none.empty(), "progress tail: a file shorter than the offset (same id) is read from 0");
    }
}

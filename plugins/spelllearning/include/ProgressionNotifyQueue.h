#pragma once

#include <utility>
#include <vector>

// =============================================================================
// ProgressionNotifyQueue - the hold-and-send rule behind ProgressionNotify
//
// No game types, so tools/progression-notify-check tests this very code.
// Open/Close nest: while any is open, Post holds the event; the outermost Close
// sends what was held, in the order it was posted. An event posted while those
// are being sent (a receiver that changes state again) is sent after them, so
// the order is still the posting order. Not thread-safe: one per thread.
// send must not throw (ProgressionNotify catches inside it). A receiver that
// posts again on every notice would keep Close sending: none does (the panel's
// replies come back as later game-thread tasks).
// =============================================================================

template <class Event>
class ProgressionNotifyQueue
{
public:
    template <class Send>
    void Post(Event event, Send&& send)
    {
        if (m_depth == 0 && !m_sending) {
            send(event);
            return;
        }
        m_held.push_back(std::move(event));
    }

    void Open() { ++m_depth; }

    template <class Send>
    void Close(Send&& send)
    {
        if (m_depth > 0) --m_depth;
        if (m_depth != 0 || m_sending) return;
        m_sending = true;
        while (!m_held.empty()) {
            auto batch = std::move(m_held);
            m_held.clear();
            for (const auto& event : batch) send(event);
        }
        m_sending = false;
    }

    int Depth() const { return m_depth; }
    std::size_t Held() const { return m_held.size(); }

private:
    int m_depth = 0;
    bool m_sending = false;
    std::vector<Event> m_held;
};

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
        // Two buffers that swap: the held events go out of m_out while a receiver's posts
        // land in m_held (m_out's emptied buffer); both keep their capacity, so a notice
        // after the first allocates nothing. A Close inside a send returns above (m_sending)
        while (!m_held.empty()) {
            m_out.swap(m_held);
            for (const auto& event : m_out) send(event);
            m_out.clear();
        }
        m_sending = false;
    }

    int Depth() const { return m_depth; }
    std::size_t Held() const { return m_held.size(); }

private:
    int m_depth = 0;
    bool m_sending = false;
    std::vector<Event> m_held;
    std::vector<Event> m_out;   // the batch being sent (Close)
};

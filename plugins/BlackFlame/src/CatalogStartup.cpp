#include "Common.h"
#include "CatalogStartup.h"
#include "CatalogFile.h"
#include "CopyFreshness.h"
#include "MainThreadQueue.h"
#include "MenuWatch.h"

#include <chrono>
#include <utility>

namespace BlackFlame::CatalogStartup
{
    namespace
    {
        using Clock = std::chrono::steady_clock;
        constexpr auto kPollInterval = std::chrono::milliseconds(500);
        std::function<void()> g_ready;
        Clock::time_point g_nextRead;
        bool g_started = false;

        void Cancel(const char* a_reason)
        {
            g_ready = {};
            MarkFreshnessUnchecked(a_reason);
            logger::warn("BlackFlame: catalog startup cancelled: {}. Runtime hooks and copies stay off this session; "
                         "finish the HoM scan and restart at the main menu.", a_reason);
        }

        void Resume()
        {
            auto ready = std::exchange(g_ready, {});  // consume before invoking: never initialize twice
            logger::info("BlackFlame: catalog ready; initializing fire sets, private copies and runtime hooks once");
            ready();
        }

        void Poll();

        void QueuePoll()
        {
            if (!MainQueue::Post("BlackFlameCatalogStartup", Poll)) {
                Cancel("catalog wait could not queue a main-thread task");
            }
        }

        void Poll()
        {
            if (!g_ready) {
                return;
            }
            if (!MenuWatch::PublishOpen()) {
                Cancel("the first game load started before the catalog was ready");
                return;
            }
            if (!MenuWatch::DrainMayPublish()) {
                if (MenuWatch::ProofPending()) {
                    QueuePoll();
                } else {
                    Cancel("the main-thread publication proof was refused");
                }
                return;
            }
            const auto now = Clock::now();
            if (now >= g_nextRead) {
                g_nextRead = now + kPollInterval;
                if (ReadCatalogFile()) {
                    // Recheck immediately before the initializer, including slot fill and IED's ARMA swap.
                    if (MenuWatch::DrainMayPublish()) {
                        Resume();
                    } else {
                        Cancel("the publication window closed while reading the catalog");
                    }
                    return;
                }
            }
            QueuePoll();
        }
    }

    void Begin(std::function<void()> a_ready)
    {
        if (g_started || !a_ready) {
            return;
        }
        g_started = true;
        g_ready = std::move(a_ready);
        if (!MenuWatch::PublishOpen()) {
            Cancel("the publication window is already closed");
        } else if (ReadCatalogFile()) {
            Resume();  // existing catalog: preserve the synchronous DataLoaded W1 route
        } else if (!MainQueue::Installed()) {
            Cancel("no main-thread queue is available to wait for the HoM scan");
        } else {
            MarkFreshnessUnchecked("waiting for the HoM spell catalog before creating copies");
            logger::info("BlackFlame: waiting for a valid HoM catalog at '{}'; stay at the main menu while HoM scans", kCatalogPath);
            QueuePoll();
        }
    }
}

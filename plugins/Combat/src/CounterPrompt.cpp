#include "Common.h"
#include "CounterPrompt.h"
#include "CombatConfig.h"
#include "CounterKey.h"
#include "IncomingWarning.h"
#include "WardSupport.h"

#include <chrono>
#include <mutex>
#include <thread>

// The render-thread queue below follows CIGAR by kkww101010 (GPL-3.0,
// github.com/kkw1010-dev/HKT, src/Prompt.cpp): SkyPrompt draws inside the
// frame's Present call, and a prompt sent or removed from another thread while
// it draws crashed SkyPrompt 2.4.0 (CIGAR's report, 2026-09-28). So every
// SkyPrompt call waits in a queue that a hook on that Present call drains.

namespace SLCombat
{
    namespace
    {
        constexpr SkyPromptAPI::EventID kCounterEvent = 1;
        constexpr RE::FormID kPlayerRef = 0x14;  ///< SkyPrompt draws an actor's prompt beside the head

        // Progress ring: |integer part| = speed (1 = a full circle in 10 s), fraction = start,
        // positive = running down (SkyPrompt docs, "Progress Circle")
        constexpr float kRingSecondsPerCircle = 10.0f;
        constexpr float kRingMaxStart = 0.99f;
        constexpr float kRingMinStart = 0.01f;

        // No threat report for this long: the prompt comes down
        constexpr auto kHideCheck = std::chrono::milliseconds(100);
        constexpr float kNoThreat = 1e9f;

        // Without a Present call for this long, the queue is delivered from the game thread
        constexpr auto kPresentSilence = std::chrono::seconds(5);

        using Clock = std::chrono::steady_clock;

        // --- threat collapse (any thread) --------------------------------------
        std::atomic<float> s_nearest{ kNoThreat };
        std::atomic<bool> s_showQueued{ false };
        std::atomic<bool> s_hideCheckQueued{ false };

        // --- render-thread queue ---------------------------------------------
        struct Command
        {
            bool send = false;
            float progress = 0.0f;
            std::string text;
            CounterPrompt::KeyList keys;
        };

        constexpr SkyPromptAPI::ActionID kCounterAction = 0;

        std::mutex s_queueLock;
        std::vector<Command> s_queue;
        std::mutex s_deliverLock;  ///< held while a batch is delivered
        std::atomic<Clock::rep> s_lastPresent{ 0 };
        bool s_hookInstalled = false;

        // A hand holds a ward (the longer window), read by ProcessEvent on SkyPrompt's thread
        std::atomic<bool> s_wardInHand{ false };

        void Deliver(Command& a_command);

        void DeliverLocked()
        {
            std::vector<Command> batch;
            {
                std::scoped_lock guard(s_queueLock);
                batch.swap(s_queue);
            }
            for (auto& command : batch) {
                Deliver(command);
            }
        }

        bool PresentAlive()
        {
            auto last = s_lastPresent.load();
            return s_hookInstalled && last != 0 &&
                   Clock::now() - Clock::time_point(Clock::duration(last)) < kPresentSilence;
        }

        void Enqueue(Command&& a_command)
        {
            {
                std::scoped_lock guard(s_queueLock);
                s_queue.push_back(std::move(a_command));
            }
            if (PresentAlive()) {
                return;
            }
            if (std::unique_lock deliver(s_deliverLock, std::try_to_lock); deliver.owns_lock()) {
                DeliverLocked();
            }
        }

        // BSGraphics::Renderer::End's call to Present, where SkyPrompt draws (CIGAR: same site)
        struct PresentHook
        {
            static void Thunk(std::uint32_t a_timer)
            {
                s_lastPresent = Clock::now().time_since_epoch().count();
                std::scoped_lock deliver(s_deliverLock);
                DeliverLocked();
                Original(a_timer);
            }
            static inline REL::Relocation<decltype(Thunk)> Original;
        };

        void InstallRenderHook()
        {
            constexpr std::uint8_t kCallOpcode = 0xE8;
            const REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(75461, 77246) };
            const auto site = target.address() + REL::Relocate(0x9, 0x9, 0x15);
            if (*reinterpret_cast<const std::uint8_t*>(site) != kCallOpcode) {
                logger::error("SL_Combat: no call at the Present site - SkyPrompt calls go out from the game thread");
                return;
            }
            PresentHook::Original = SKSE::GetTrampoline().write_call<5>(site, PresentHook::Thunk);
            s_hookInstalled = true;
            logger::info("SL_Combat: Present hooked for SkyPrompt calls");
        }

        float RingProgress(float a_seconds)
        {
            float speed = std::max(1.0f, std::floor(kRingSecondsPerCircle / std::max(a_seconds, 0.01f)));
            float start = std::clamp(a_seconds * speed / kRingSecondsPerCircle, kRingMinStart, kRingMaxStart);
            return speed + start;
        }

        bool WardInHand(RE::PlayerCharacter* a_player)
        {
            for (bool left : { true, false }) {
                auto* form = a_player->GetEquippedObject(left);
                if (IsWardSpell(form ? form->As<RE::SpellItem>() : nullptr)) {
                    return true;
                }
            }
            return false;
        }

        void AddTaskLater(std::chrono::milliseconds a_delay, std::function<void()> a_task)
        {
            std::thread([a_delay, task = std::move(a_task)]() mutable {
                std::this_thread::sleep_for(a_delay);
                if (auto* tasks = SKSE::GetTaskInterface()) {
                    tasks->AddTask(std::move(task));
                }
            }).detach();
        }

        void Deliver(Command& a_command)
        {
            CounterPrompt::GetSingleton()->Publish(a_command.send, a_command.progress,
                std::move(a_command.text), std::move(a_command.keys));
        }
    }

    CounterPrompt* CounterPrompt::GetSingleton()
    {
        static CounterPrompt instance;
        return &instance;
    }

    bool CounterPrompt::Initialize()
    {
        auto* self = GetSingleton();
        self->m_client = SkyPromptAPI::RequestClientID();
        logger::info("SL_Combat: SkyPrompt client id {} (API {}.{})", self->m_client, SkyPromptAPI::MAJOR, SkyPromptAPI::MINOR);
        if (self->m_client == 0) {
            return false;
        }
        InstallRenderHook();
        return true;
    }

    void CounterPrompt::OnThreat(float a_secondsToHit)
    {
        if (GetSingleton()->m_client == 0) {
            return;
        }
        float current = s_nearest.load();
        while (a_secondsToHit < current && !s_nearest.compare_exchange_weak(current, a_secondsToHit)) {
        }
        if (s_showQueued.exchange(true)) {
            return;  // this frame's update is already on its way
        }
        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([]() {
                float seconds = s_nearest.exchange(kNoThreat);
                s_showQueued.store(false);
                if (seconds < kNoThreat) {
                    GetSingleton()->Show(seconds);
                }
            });
        } else {
            s_showQueued.store(false);
        }
    }

    // =========================================================================
    // Game thread
    // =========================================================================

    void CounterPrompt::Show(float a_secondsToHit)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }
        ScheduleHideCheck();
        s_wardInHand.store(WardInHand(player));
        if (m_shown) {
            return;  // already up; the ring runs on its own
        }

        // One prompt on the counter key, whatever the hands hold
        Command command;
        command.send = true;
        command.progress = RingProgress(a_secondsToHit);
        command.text = CombatConfig::Get().promptText;
        command.keys = CounterKey::PromptKeys();
        if (command.keys.empty()) {
            return;  // no key set and nothing bound to block
        }
        m_shown = true;
        logger::info("counter.prompt show key='{}' bindings={} in={:.2f}s",
            CounterKey::Name(CombatConfig::Get().counterKey), command.keys.size(), a_secondsToHit);
        Enqueue(std::move(command));
    }

    void CounterPrompt::Hide()
    {
        if (!m_shown) {
            return;
        }
        m_shown = false;
        Command command;
        command.send = false;
        Enqueue(std::move(command));
    }

    void CounterPrompt::ScheduleHideCheck()
    {
        if (s_hideCheckQueued.exchange(true)) {
            return;
        }
        AddTaskLater(kHideCheck, []() {
            s_hideCheckQueued.store(false);
            auto* self = GetSingleton();
            if (IncomingWarning::NearestThreatSeconds() < 0.0f) {
                self->Hide();
            } else if (self->m_shown) {
                self->ScheduleHideCheck();
            }
        });
    }

    // =========================================================================
    // Render thread (Publish) and SkyPrompt's thread (ProcessEvent)
    // =========================================================================

    void CounterPrompt::Publish(bool a_send, float a_progress, std::string a_text, KeyList a_keys)
    {
        if (!a_send) {
            SkyPromptAPI::RemovePrompt(this, m_client);
            m_promptCount = 0;
            return;
        }
        // SkyPrompt keeps string_views into the text: the old one lives one more round
        m_previousText = std::move(m_text);
        m_text = std::move(a_text);
        m_keys = std::move(a_keys);
        std::span<const std::pair<RE::INPUT_DEVICE, SkyPromptAPI::ButtonID>> keys(m_keys.data(), m_keys.size());
        m_prompt = SkyPromptAPI::Prompt(m_text, kCounterEvent, kCounterAction, SkyPromptAPI::kSinglePress, kPlayerRef,
            keys, 0xFFFFFFFF, a_progress);
        m_promptCount = 1;
        if (!SkyPromptAPI::SendPrompt(this, m_client)) {
            logger::info("counter.prompt SendPrompt returned false (already queued or refused)");
        }
    }

    std::span<const SkyPromptAPI::Prompt> CounterPrompt::GetPrompts() const
    {
        return { &m_prompt, m_promptCount };
    }

    void CounterPrompt::ProcessEvent(SkyPromptAPI::PromptEvent a_event) const
    {
        if (a_event.prompt.actionID != kCounterAction) {
            return;
        }
        auto* input = CounterInput::GetSingleton();
        switch (a_event.type) {
        case SkyPromptAPI::kDown:
            // The press time is taken now, on SkyPrompt's thread; only atomics are touched
            input->RecordPress(s_wardInHand.load());
            input->SetHeld(true);
            logger::info("counter.prompt press");
            break;
        case SkyPromptAPI::kUp:
            input->SetHeld(false);
            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask([]() { WardSupport::ReleaseHold(); });
            }
            break;
        default:
            break;
        }
    }
}

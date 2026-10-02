#include "Common.h"
#include "CombatMenu.h"
#include "CombatConfig.h"
#include "CounterKey.h"
#include "TestSpawn.h"
#include "WardSupport.h"

// The vendored header trips a few MSVC warnings in its own code
#pragma warning(push)
#pragma warning(disable : 4099 5054)
#include "SKSEMenuFramework/SKSEMenuFramework.h"
#pragma warning(pop)

#include <mutex>

namespace SLCombat
{
    namespace
    {
        constexpr const char* kSection = "Heart of Magic Combat";
        constexpr const char* kItem = "Counter";
        constexpr std::size_t kTextBuffer = 64;

        bool s_registered = false;
        bool s_counterActive = false;   ///< the hooks were installed at load
        bool s_loaded = false;          ///< render thread: the working copy holds the live values
        CombatConfig s_edit;            ///< render thread: what the page edits
        std::array<char, kTextBuffer> s_promptText{};
        std::array<char, kTextBuffer> s_hudText{};
        std::string s_status;           ///< last apply / revert result

        // Shown on the page; filled on the game thread after load
        std::mutex s_infoLock;
        std::string s_wardName = "-";

        void CopyText(std::array<char, kTextBuffer>& a_out, const std::string& a_in)
        {
            a_out.fill('\0');
            std::copy_n(a_in.begin(), std::min(a_in.size(), a_out.size() - 1), a_out.begin());
        }

        void LoadWorkingCopy()
        {
            s_edit = CombatConfig::Get();  // a plain copy; small and only read on the game thread otherwise
            CopyText(s_promptText, s_edit.promptText);
            CopyText(s_hudText, s_edit.hudText);
            s_loaded = true;
        }

        void Apply()
        {
            CombatConfig copy = s_edit;
            copy.promptText = s_promptText.data();
            copy.hudText = s_hudText.data();
            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask([copy]() {
                    CombatConfig::Get() = copy;
                    bool saved = CombatConfig::Get().Save();
                    logger::info("SL_Combat: settings applied from the menu (saved={})", saved);
                    FlushLog();
                });
                s_status = "Applied and saved.";
            }
        }

        void RefreshInfo()
        {
            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask([]() {
                    auto* ward = WardSupport::BestKnownWard(RE::PlayerCharacter::GetSingleton());
                    std::scoped_lock lock(s_infoLock);
                    s_wardName = ward ? ward->GetName() : "none known";
                });
            }
        }

        void Hint(const char* a_text)
        {
            ImGui::TextDisabled("%s", a_text);
        }

        void RenderCounterKey(CombatConfig& a_config)
        {
            ImGui::Text("Counter key: %s", CounterKey::Name(a_config.counterKey).c_str());
            if (ImGui::Button("Block key")) {
                a_config.counterKey = CounterKey::kGameBlockKey;
            }
            using Mouse = RE::BSWin32MouseDevice::Keys;
            constexpr std::array kMouseChoices{ Mouse::kMiddleButton, Mouse::kButton3, Mouse::kButton4 };
            for (auto button : kMouseChoices) {
                int code = CounterKey::kMouseOffset + static_cast<int>(button);
                ImGui::SameLine();
                if (ImGui::Button(CounterKey::Name(code).c_str())) {
                    a_config.counterKey = code;
                }
            }
            if (ImGui::InputInt("Key code", &a_config.counterKey, 0, 0) && !CounterKey::Valid(a_config.counterKey)) {
                a_config.counterKey = CounterKey::kGameBlockKey;
            }
            Hint("-1 = the game's block key. Codes as in SkyUI menus: keyboard scan code (G = 34),\n"
                 "256 + mouse button (258 middle, 259 / 260 side buttons). A gamepad always uses block.");
        }

        void PostSpawn(TestMage a_kind)
        {
            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask([a_kind]() { TestSpawn::Spawn(a_kind); });
            }
        }

        void RenderTestTools(CombatConfig& a_config)
        {
            ImGui::SeparatorText("Test");
            ImGui::Checkbox("Test tools", &a_config.testTools);
            if (!a_config.testTools) {
                Hint("Hostile mages to practise on. Applies at once; Apply and save keeps it.");
                return;
            }
            Hint("Vanilla leveled warlocks, about 10 m in front of you. They attack on sight.");
            if (ImGui::Button("Spawn fire mage")) {
                PostSpawn(TestMage::kFire);
            }
            ImGui::SameLine();
            if (ImGui::Button("Spawn frost mage")) {
                PostSpawn(TestMage::kFrost);
            }
            ImGui::SameLine();
            if (ImGui::Button("Spawn shock mage")) {
                PostSpawn(TestMage::kShock);
            }
            Hint("Fire and frost cast missiles (counterable); shock casts a beam (no prompt expected).");
            ImGui::Text("Spawned: %zu", TestSpawn::Count());
            ImGui::SameLine();
            if (ImGui::Button("Remove all")) {
                if (auto* tasks = SKSE::GetTaskInterface()) {
                    tasks->AddTask([]() { TestSpawn::RemoveAll(); });
                }
            }
        }
    }

    void CombatMenu::Register(bool a_counterActive)
    {
        s_counterActive = a_counterActive;
        if (s_registered || !SKSEMenuFramework::IsInstalled()) {
            logger::info("SL_Combat: SKSE Menu Framework {} - no settings page", s_registered ? "page already added" : "not installed");
            return;
        }
        SKSEMenuFramework::SetSection(kSection);
        SKSEMenuFramework::AddSectionItem(kItem, Render);
        s_registered = true;
        logger::info("SL_Combat: settings page added to SKSE Menu Framework ({}/{})", kSection, kItem);
    }

    void __stdcall CombatMenu::Render()
    {
        if (!s_loaded) {
            LoadWorkingCopy();
            RefreshInfo();
        }
        auto& c = s_edit;

        // --- status -----------------------------------------------------------
        ImGui::SeparatorText("Status");
        ImGui::Text("Counter: %s", s_counterActive ? "active" : "off (SkyPrompt missing or disabled at load)");
        ImGui::Text("360 Ward: %s", WardSupport::AllAngles() ? "loaded - every direction" : "not loaded - front only");
        ImGui::Text("Dual-cast perk: %s", WardSupport::DualCastPerkName().c_str());
        {
            std::scoped_lock lock(s_infoLock);
            ImGui::Text("Ward the counter raises: %s", s_wardName.c_str());
        }
        if (ImGui::Button("Refresh")) {
            RefreshInfo();
        }

        // --- counter ----------------------------------------------------------
        ImGui::SeparatorText("Counter");
        ImGui::Checkbox("Counter on", &c.enableCounter);
        Hint("Turning it on takes a game restart if it was off at load.");
        RenderCounterKey(c);
        ImGui::SliderFloat("Success window (s)", &c.counterWindow, 0.1f, 0.5f, "%.2f");
        ImGui::SliderFloat("Extra window, ward in hand (s)", &c.wardBonusWindow, 0.0f, 0.3f, "%.2f");
        ImGui::SliderFloat("Front angle (deg, without 360 Ward)", &c.frontAngle, 15.0f, 180.0f, "%.0f");
        ImGui::Checkbox("Every direction with 360 Ward", &c.sphereWardAllAngles);
        ImGui::Checkbox("Needs a known ward", &c.requireKnownWard);
        ImGui::Checkbox("Needs a spell in hand", &c.requireSpellInHand);
        Hint("Off: a two-hander, a bow or weapons alone can counter too.");
        ImGui::SliderFloat("Prompt appears (s before impact)", &c.warnLeadSeconds, 0.3f, 1.5f, "%.2f");
        ImGui::SliderFloat("Ignore misses wider than", &c.warnRadius, 40.0f, 300.0f, "%.0f");
        ImGui::InputText("Prompt text", s_promptText.data(), s_promptText.size());
        int result = static_cast<int>(c.counterResult);
        const char* results[] = { "Ward takes the hit", "Absorb the spell", "Reflect the spell" };
        if (ImGui::Combo("On a counter", &result, results, 3)) {
            c.counterResult = static_cast<CounterResult>(result);
        }

        // --- ward -------------------------------------------------------------
        ImGui::SeparatorText("Ward");
        ImGui::SliderFloat("Ward stays after a counter (s, free)", &c.wardLingerSeconds, 0.0f, 3.0f, "%.1f");
        ImGui::Checkbox("Hold the counter key to keep the ward up", &c.holdWard);
        Hint("Spells in both hands and the Restoration dual-casting perk: the ward is dual-cast.");
        ImGui::SliderFloat("Ward refresh (s)", &c.holdRefreshSeconds, 0.1f, 0.5f, "%.2f");

        // --- reward -----------------------------------------------------------
        ImGui::SeparatorText("Reward");
        ImGui::SliderFloat("Slow motion speed (1 = off)", &c.slowMoScale, 0.2f, 1.0f, "%.2f");
        ImGui::SliderFloat("Slow motion length (s)", &c.slowMoDuration, 0.0f, 1.0f, "%.2f");
        ImGui::Checkbox("Next cast free", &c.freeNextCast);
        ImGui::SliderFloat("Free cast window (s)", &c.freeCastWindow, 0.0f, 10.0f, "%.1f");
        ImGui::SliderFloat("Learning XP per counter", &c.xpPerCounter, 0.0f, 50.0f, "%.0f");
        ImGui::InputText("Corner message", s_hudText.data(), s_hudText.size());
        Hint("Empty = no message.");

        RenderTestTools(c);

        // --- buttons ----------------------------------------------------------
        ImGui::Separator();
        if (ImGui::Button("Apply and save")) {
            Apply();
        }
        ImGui::SameLine();
        if (ImGui::Button("Revert")) {
            LoadWorkingCopy();
            s_status = "Reverted to the live settings.";
        }
        ImGui::SameLine();
        if (ImGui::Button("Defaults")) {
            bool active = c.enableCounter;
            s_edit = CombatConfig{};
            s_edit.enableCounter = active;
            CopyText(s_promptText, s_edit.promptText);
            CopyText(s_hudText, s_edit.hudText);
            s_status = "Defaults loaded - press Apply and save to keep them.";
        }
        if (!s_status.empty()) {
            Hint(s_status.c_str());
        }
    }
}

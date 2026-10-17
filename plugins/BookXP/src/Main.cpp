// =============================================================================
// SL_BookXP - SpellLearning C++ API Test Plugin
// =============================================================================
// Grants spell learning XP when the player reads normal books.
// Demonstrates the SpellLearning C++ API:
//   1. Register for API broadcast from SpellLearning
//   2. Receive ISpellLearningAPI at kPostPostLoad
//   3. Use API to register XP source and grant XP
//
// No ESP required - pure SKSE DLL plugin.
// =============================================================================

#include "Common.h"

#include "SpellLearningAPI.h"

static constexpr float XP_PER_BOOK = 15.0f;
static constexpr const char* SOURCE_ID = "book_reading";
static constexpr const char* SOURCE_DISPLAY = "Book Reading";

// Cached API pointer (set when SpellLearning broadcasts at kPostPostLoad)
static SpellLearning::ISpellLearningAPI* g_api = nullptr;

// Track whether we've registered our source (do it once at kDataLoaded)
static bool g_sourceRegistered = false;

// =============================================================================
// BOOK MENU WATCHER
// =============================================================================
// Detects when the player opens any book via BookMenu.
// Normal books always open BookMenu. Spell tomes are intercepted by
// SpellLearning's SpellTomeHook before reaching BookMenu, so we only
// see non-spell-tome books here.

class BookMenuWatcher : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
    static BookMenuWatcher* GetSingleton()
    {
        static BookMenuWatcher instance;
        return &instance;
    }

    RE::BSEventNotifyControl ProcessEvent(
        const RE::MenuOpenCloseEvent* a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
    {
        if (!a_event || !a_event->opening) {
            return RE::BSEventNotifyControl::kContinue;
        }

        // "Book Menu" is the vanilla menu name when opening a book
        if (a_event->menuName != RE::BookMenu::MENU_NAME) {
            return RE::BSEventNotifyControl::kContinue;
        }

        logger::info("SL_BookXP: BookMenu opened - book read detected!");

        if (g_api) {
            GrantXPViaAPI();
        } else {
            logger::warn("SL_BookXP: API not available, cannot grant XP");
        }

        return RE::BSEventNotifyControl::kContinue;
    }

private:
    BookMenuWatcher() = default;

    // Whole numbers, or one decimal below 10 so a small grant does not read as 0 or 1 ("7.5", "3")
    static std::string FormatXP(float xp)
    {
        static constexpr float kWholeFrom = 10.0f;
        if (xp >= kWholeFrom) return fmt::format("{:.0f}", xp);
        auto text = fmt::format("{:.1f}", xp);
        if (text.ends_with(".0")) text.resize(text.size() - 2);
        return text;
    }

    // Full API path - query targets and grant XP with source tracking
    static void GrantXPViaAPI()
    {
        logger::info("SL_BookXP: Granting XP via API");

        const char* schools[] = {
            "Alteration", "Conjuration", "Destruction", "Illusion", "Restoration"
        };

        int granted = 0;
        float leastGranted = 0.0f;
        float mostGranted = 0.0f;
        for (const char* school : schools) {
            uint32_t targetId = g_api->GetLearningTarget(school);
            if (targetId != 0) {
                logger::info("SL_BookXP: Found learning target {:08X} for {}", targetId, school);
                float actual = g_api->AddSourcedXP(targetId, XP_PER_BOOK, SOURCE_ID);
                if (actual > 0.0f) {
                    logger::info("SL_BookXP: Granted {:.1f} XP to {:08X} ({})",
                        actual, targetId, school);
                    leastGranted = granted == 0 ? actual : (std::min)(leastGranted, actual);
                    mostGranted = granted == 0 ? actual : (std::max)(mostGranted, actual);
                    granted++;
                } else {
                    logger::info("SL_BookXP: XP capped for {:08X} ({}) - returned {:.1f}",
                        targetId, school, actual);
                }
            }
        }

        if (granted > 0) {
            // The XP the spells really got - after the Overall XP Multiplier, this source's
            // Multiplier and Cap - not XP_PER_BOOK, which the corner showed before 1.5.1
            auto xpText = FormatXP(leastGranted);
            if (FormatXP(mostGranted) != xpText) {
                xpText += "-" + FormatXP(mostGranted);
            }
            auto countText = std::to_string(granted);
            constexpr const char* kEnglish = "[BookXP] +{{xp}} XP to {{count}} spell(s)";
            // Heart of Magic 1.2.1+ shows it in the panel's language and honours
            // Settings > XP Alerts (hud.bookXpGranted is an XP alert); an older one gets the English text
            if (g_api->GetAPIVersion() >= 2) {
                const char* params[] = { "xp", xpText.c_str(), "count", countText.c_str() };
                g_api->ShowNotification("hud.bookXpGranted", kEnglish, params, 2);
            } else {
                auto msg = fmt::format("[BookXP] +{} XP to {} spell(s)", xpText, countText);
                RE::SendHUDMessage::ShowHUDMessage(msg.c_str());
            }
            logger::info("SL_BookXP: +{} XP to {} spell(s)", xpText, countText);
        } else {
            logger::info("SL_BookXP: No active learning targets");
        }
    }
};

// =============================================================================
// SKSE MESSAGE HANDLERS
// =============================================================================

void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
{
    if (!a_msg) return;

    switch (a_msg->type) {
        case SKSE::MessagingInterface::kDataLoaded: {
            logger::info("SL_BookXP: kDataLoaded");

            // Register our XP source via the API (creates UI controls in settings)
            if (g_api && !g_sourceRegistered) {
                bool ok = g_api->RegisterXPSource(SOURCE_ID, SOURCE_DISPLAY);
                logger::info("SL_BookXP: RegisterXPSource('{}', '{}') = {}",
                    SOURCE_ID, SOURCE_DISPLAY, ok);
                g_sourceRegistered = true;
            } else if (!g_api) {
                logger::warn("SL_BookXP: API not available at kDataLoaded - source not registered");
            }

            // Register for BookMenu events
            auto* ui = RE::UI::GetSingleton();
            if (ui) {
                ui->AddEventSink<RE::MenuOpenCloseEvent>(BookMenuWatcher::GetSingleton());
                logger::info("SL_BookXP: BookMenu event sink registered - listening for book reads");
            } else {
                logger::error("SL_BookXP: Failed to get UI singleton!");
            }
            break;
        }
    }
}

// Receive API broadcast from SpellLearning (sent at kPostPostLoad)
void OnSpellLearningMessage(SKSE::MessagingInterface::Message* a_msg)
{
    if (!a_msg) return;

    if (a_msg->type == SpellLearning::kMessageType_APIReady && a_msg->data) {
        g_api = static_cast<SpellLearning::ISpellLearningAPI*>(a_msg->data);
        logger::info("SL_BookXP: Received SpellLearning API v{} - full access available!",
            g_api->GetAPIVersion());
    } else {
        logger::info("SL_BookXP: Received message from SpellLearning (type=0x{:X}, data={})",
            a_msg->type, a_msg->data ? "valid" : "null");
    }
}

// =============================================================================
// SKSE PLUGIN ENTRY
// =============================================================================

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse, false);
    SetupLog("SpellLearning C++ API Test Plugin");

    auto messaging = SKSE::GetMessagingInterface();

    // Listen for SKSE lifecycle events (kDataLoaded)
    messaging->RegisterListener(OnSKSEMessage);
    logger::info("SL_BookXP: SKSE message listener registered");

    // Listen for SpellLearning API broadcast (arrives at kPostPostLoad)
    messaging->RegisterListener("SpellLearning", OnSpellLearningMessage);
    logger::info("SL_BookXP: Listening for SpellLearning API broadcast");

    logger::info("SL_BookXP: Initialization complete");
    return true;
}

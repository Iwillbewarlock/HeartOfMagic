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

#include <mutex>
#include <unordered_set>
#include <vector>

static constexpr float XP_PER_BOOK = 15.0f;
static constexpr const char* SOURCE_ID = "book_reading";
static constexpr const char* SOURCE_DISPLAY = "Book Reading";

// Cached API pointer (set when SpellLearning broadcasts at kPostPostLoad)
static SpellLearning::ISpellLearningAPI* g_api = nullptr;

// Track whether we've registered our source (do it once at kDataLoaded)
static bool g_sourceRegistered = false;

// =============================================================================
// READ BOOKS
// =============================================================================
// XP comes once per book (user report 2026-10-30: re-reading the same book or note gave XP every time). The base
// forms that gave it are kept in the save (SKSE co-save); a book counts once however many copies there are. A save
// from before this - or before BookXP was installed - has no list: at its load the books the game itself marks read
// (TESObjectBOOK::IsRead) are added, so they give nothing again. That mark is not used at the moment of reading: the
// game may set it before the book menu opens, which would make every first read look like a second one.

namespace ReadBooks
{
    inline constexpr std::uint32_t kUniqueID = 'SLBX';
    inline constexpr std::uint32_t kRecord = 'READ';
    inline constexpr std::uint32_t kVersion = 1;

    std::mutex g_lock;
    std::unordered_set<RE::FormID> g_read;

    // true the first time a book is seen (and from then on it is in the list)
    bool MarkRead(RE::FormID a_book)
    {
        const std::scoped_lock lock(g_lock);
        return g_read.insert(a_book).second;
    }

    void OnSave(SKSE::SerializationInterface* a_intfc)
    {
        std::vector<RE::FormID> books;
        {
            const std::scoped_lock lock(g_lock);
            books.assign(g_read.begin(), g_read.end());
        }
        if (!a_intfc->OpenRecord(kRecord, kVersion)) {
            logger::error("SL_BookXP: could not open the read-books record - {} book(s) not saved", books.size());
            return;
        }
        const auto count = static_cast<std::uint32_t>(books.size());
        bool ok = a_intfc->WriteRecordData(count);
        for (const auto book : books) {
            if (!ok) break;
            ok = a_intfc->WriteRecordData(book);
        }
        if (!ok) {
            logger::error("SL_BookXP: writing the read-books record failed - the next load may give XP again for some books");
            return;
        }
        logger::info("SL_BookXP: saved {} read book(s)", count);
    }

    void OnLoad(SKSE::SerializationInterface* a_intfc)
    {
        std::unordered_set<RE::FormID> loaded;
        std::size_t dropped = 0;
        std::uint32_t type = 0, version = 0, length = 0;
        while (a_intfc->GetNextRecordInfo(type, version, length)) {
            if (type != kRecord || version != kVersion) {
                logger::warn("SL_BookXP: unknown co-save record {:08X} v{} ({} bytes) skipped", type, version, length);
                continue;
            }
            std::uint32_t count = 0;
            if (!a_intfc->ReadRecordData(count)) {
                logger::error("SL_BookXP: the read-books record has no count - skipped");
                continue;
            }
            for (std::uint32_t i = 0; i < count; ++i) {
                RE::FormID saved = 0, now = 0;
                if (!a_intfc->ReadRecordData(saved)) {
                    logger::error("SL_BookXP: the read-books record ends after {} of {} book(s)", i, count);
                    break;
                }
                // a book of a plugin removed since that save is dropped; one whose load order moved is renumbered
                if (a_intfc->ResolveFormID(saved, now)) {
                    loaded.insert(now);
                } else {
                    ++dropped;
                }
            }
        }
        const auto kept = loaded.size();
        {
            const std::scoped_lock lock(g_lock);
            g_read = std::move(loaded);
        }
        logger::info("SL_BookXP: loaded the read-books list ({} book(s), {} of removed plugins dropped)", kept, dropped);
    }

    void OnRevert(SKSE::SerializationInterface*)
    {
        const std::scoped_lock lock(g_lock);
        g_read.clear();
    }

    // After every save load: the books the game marks read join the list (a save from before this has none of its own)
    void AddBooksTheGameMarksRead()
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        if (!data) return;
        std::size_t added = 0;
        {
            const std::scoped_lock lock(g_lock);
            for (auto* book : data->GetFormArray<RE::TESObjectBOOK>()) {
                if (book && book->IsRead() && g_read.insert(book->GetFormID()).second) ++added;
            }
        }
        logger::info("SL_BookXP: {} book(s) the game marks read joined the read-books list", added);
    }
}

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

        if (!g_api) {
            logger::warn("SL_BookXP: BookMenu opened - API not available, cannot grant XP");
            return RE::BSEventNotifyControl::kContinue;
        }

        // The book on screen; when it cannot be told, XP is given as before the once-per-book check rather than none
        if (auto* book = RE::BookMenu::GetTargetForm()) {
            const char* name = book->GetName();
            if (!ReadBooks::MarkRead(book->GetFormID())) {
                logger::info("SL_BookXP: BookMenu opened - {:08X} '{}' was read before, no XP", book->GetFormID(), name ? name : "");
                return RE::BSEventNotifyControl::kContinue;
            }
            logger::info("SL_BookXP: BookMenu opened - {:08X} '{}' read for the first time", book->GetFormID(), name ? name : "");
        } else {
            logger::warn("SL_BookXP: BookMenu opened but the book is not known - XP given without the once-per-book check");
        }

        GrantXPViaAPI();
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
        case SKSE::MessagingInterface::kPostLoadGame:
            ReadBooks::AddBooksTheGameMarksRead();
            FlushLog();
            break;
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

    // The books that already gave XP, kept in the save
    if (auto* serialization = SKSE::GetSerializationInterface()) {
        serialization->SetUniqueID(ReadBooks::kUniqueID);
        serialization->SetSaveCallback(ReadBooks::OnSave);
        serialization->SetLoadCallback(ReadBooks::OnLoad);
        serialization->SetRevertCallback(ReadBooks::OnRevert);
    } else {
        logger::error("SL_BookXP: no SKSE serialization interface - the read-books list is not kept in the save");
    }

    logger::info("SL_BookXP: Initialization complete");
    return true;
}

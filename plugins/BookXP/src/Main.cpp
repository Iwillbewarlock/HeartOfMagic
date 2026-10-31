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

// A skill book of a magic school gives that school's learning target the larger amount; any other book a little to
// every target (user 2026-10-31: books with nothing to do with magic gave the full 15 before)
static constexpr float XP_PER_MAGIC_SKILL_BOOK = 45.0f;
static constexpr float XP_PER_OTHER_BOOK = 5.0f;
static constexpr const char* SOURCE_ID = "book_reading";
static constexpr const char* SOURCE_DISPLAY = "Book Reading";

// Cached API pointer (set when SpellLearning broadcasts at kPostPostLoad)
static SpellLearning::ISpellLearningAPI* g_api = nullptr;

// Track whether we've registered our source (do it once at kDataLoaded)
static bool g_sourceRegistered = false;

// =============================================================================
// READ BOOKS
// =============================================================================
// XP comes once per book (user report 2026-10-30: re-reading the same book or note gave XP every time). A book joins
// the list when it gives XP - read with no learning target it gives none and can still give it later. The list holds
// base forms (copies of a book count as one) and is kept in the save (SKSE co-save). A save without the list - from
// before this, or before BookXP was installed - gets the books the game itself marks read (TESObjectBOOK::IsRead) at
// its load, so they give nothing again. That mark is used only then: the game sets it on every read, XP or not, and
// may set it before the book menu opens.

namespace ReadBooks
{
    inline constexpr std::uint32_t kUniqueID = 'SLBX';
    inline constexpr std::uint32_t kRecord = 'READ';
    inline constexpr std::uint32_t kVersion = 1;

    std::mutex g_lock;
    std::unordered_set<RE::FormID> g_read;
    bool g_haveList = false;  // this save had its own list (OnLoad read the record)

    bool GaveXP(RE::FormID a_book)
    {
        const std::scoped_lock lock(g_lock);
        return g_read.contains(a_book);
    }

    void Remember(RE::FormID a_book)
    {
        const std::scoped_lock lock(g_lock);
        g_read.insert(a_book);
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
        bool haveList = false;
        std::uint32_t type = 0, version = 0, length = 0;
        while (a_intfc->GetNextRecordInfo(type, version, length)) {
            if (type != kRecord || version != kVersion) {
                logger::warn("SL_BookXP: unknown co-save record {:08X} v{} ({} bytes) skipped", type, version, length);
                continue;
            }
            // ReadRecordData returns the bytes read: a value cut short is not taken
            std::uint32_t count = 0;
            if (a_intfc->ReadRecordData(count) != sizeof(count)) {
                logger::error("SL_BookXP: the read-books record has no count - skipped");
                continue;
            }
            haveList = true;
            for (std::uint32_t i = 0; i < count; ++i) {
                RE::FormID saved = 0, now = 0;
                if (a_intfc->ReadRecordData(saved) != sizeof(saved)) {
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
            g_haveList = haveList;
        }
        logger::info("SL_BookXP: loaded the read-books list ({} book(s), {} of removed plugins dropped)", kept, dropped);
    }

    void OnRevert(SKSE::SerializationInterface*)
    {
        const std::scoped_lock lock(g_lock);
        g_read.clear();
        g_haveList = false;
    }

    // After a save load that worked: a save without its own list takes the books the game marks read
    void AfterLoad(bool a_loaded)
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        if (!a_loaded || !data) return;
        std::size_t added = 0;
        {
            const std::scoped_lock lock(g_lock);
            if (g_haveList) return;
            for (auto* book : data->GetFormArray<RE::TESObjectBOOK>()) {
                if (book && book->IsRead() && g_read.insert(book->GetFormID()).second) ++added;
            }
            g_haveList = true;
        }
        logger::info("SL_BookXP: a save without a read-books list - {} book(s) the game marks read give no XP", added);
    }
}

// =============================================================================
// BOOK MENU WATCHER
// =============================================================================
// Detects when the player opens any book via BookMenu.
// Normal books always open BookMenu. Spell tomes read from the inventory are
// intercepted by SpellLearning's SpellTomeHook; one that still opens BookMenu
// (if any path does) is skipped below.

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

        HandleBook(RE::BookMenu::GetTargetForm());
        FlushLog();  // info lines wait in the buffer until a save or load otherwise, and quitting loses them
        return RE::BSEventNotifyControl::kContinue;
    }

private:
    BookMenuWatcher() = default;

    // The school whose skill this book raises, nullptr for any other book
    static const char* MagicSchoolTaught(const RE::TESObjectBOOK* a_book)
    {
        if (!a_book->TeachesSkill()) return nullptr;
        switch (a_book->GetSkill()) {
            case RE::ActorValue::kAlteration: return "Alteration";
            case RE::ActorValue::kConjuration: return "Conjuration";
            case RE::ActorValue::kDestruction: return "Destruction";
            case RE::ActorValue::kIllusion: return "Illusion";
            case RE::ActorValue::kRestoration: return "Restoration";
            default: return nullptr;
        }
    }

    // The book on screen; when it cannot be told, XP is given as for an ordinary book, without the once-per-book check
    static void HandleBook(RE::TESObjectBOOK* book)
    {
        const char* school = nullptr;
        if (book) {
            const char* name = book->GetName();
            const auto id = book->GetFormID();
            // a spell tome's XP is Heart of Magic's own (its tome hook), not a read book's
            if (book->TeachesSpell()) {
                logger::info("SL_BookXP: BookMenu opened - {:08X} '{}' is a spell tome, no book XP", id, name ? name : "");
                return;
            }
            if (ReadBooks::GaveXP(id)) {
                logger::info("SL_BookXP: BookMenu opened - {:08X} '{}' gave XP before, none now", id, name ? name : "");
                return;
            }
            school = MagicSchoolTaught(book);
            logger::info("SL_BookXP: BookMenu opened - {:08X} '{}' has not given XP yet ({})", id, name ? name : "",
                school ? fmt::format("a {} skill book", school) : std::string("not a magic skill book"));
        } else {
            logger::warn("SL_BookXP: BookMenu opened but the book is not known - XP given without the once-per-book check");
        }

        // a book joins the list only when a spell got XP from it: read with no fitting learning target it can give it later
        const int granted = school ? GrantXPViaAPI(XP_PER_MAGIC_SKILL_BOOK, school) : GrantXPViaAPI(XP_PER_OTHER_BOOK, nullptr);
        if (granted > 0 && book) ReadBooks::Remember(book->GetFormID());
    }

    // Whole numbers, or one decimal below 10 so a small grant does not read as 0 or 1 ("7.5", "3")
    static std::string FormatXP(float xp)
    {
        static constexpr float kWholeFrom = 10.0f;
        if (xp >= kWholeFrom) return fmt::format("{:.0f}", xp);
        auto text = fmt::format("{:.1f}", xp);
        if (text.ends_with(".0")) text.resize(text.size() - 2);
        return text;
    }

    // Full API path - query targets and grant XP with source tracking; returns how many spells got XP.
    // a_onlySchool: that school's target alone (a magic skill book); nullptr: every school's target
    static int GrantXPViaAPI(float a_xp, const char* a_onlySchool)
    {
        logger::info("SL_BookXP: Granting {:.0f} XP via API to {}", a_xp, a_onlySchool ? a_onlySchool : "every learning target");

        const char* schools[] = {
            "Alteration", "Conjuration", "Destruction", "Illusion", "Restoration"
        };

        int granted = 0;
        float leastGranted = 0.0f;
        float mostGranted = 0.0f;
        for (const char* school : schools) {
            if (a_onlySchool && std::string_view(school) != a_onlySchool) continue;
            uint32_t targetId = g_api->GetLearningTarget(school);
            if (targetId != 0) {
                logger::info("SL_BookXP: Found learning target {:08X} for {}", targetId, school);
                float actual = g_api->AddSourcedXP(targetId, a_xp, SOURCE_ID);
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
            // Multiplier and Cap - not the base amount, which the corner showed before 1.5.1
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
        return granted;
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
            // data: whether the load worked
            ReadBooks::AfterLoad(static_cast<bool>(a_msg->data));
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

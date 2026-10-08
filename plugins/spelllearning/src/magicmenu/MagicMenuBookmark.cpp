#include "magicmenu/MagicMenuBookmark.h"
#include "magicmenu/MagicMenuInternal.h"
#include "uimanager/UIManager.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>

// =============================================================================
// MAGIC MENU BOOKMARK - see MagicMenuBookmark.h
// The menu's open/close events, finding the item card (spell description box)
// in the menu's Scaleform movie, and the open-panel / back-to-menu round trip.
// =============================================================================

namespace MagicMenuBookmark
{
    namespace
    {
        constexpr const char* kPathsFile = "Data/SKSE/Plugins/SpellLearning/magicmenu_paths.json";
        constexpr const char* kMenuRoot = "_root.Menu_mc";
        constexpr const char* kCardHint = "itemcard";  // a card the paths file does not list: a member named like this
        constexpr int kLogDepth = 2;                   // spec 4.2: the menu root's children, two levels
        constexpr int kSearchDepth = 3;
        constexpr int kInjectRetries = 5;              // UI tasks to wait for the card clip after the open event

        std::vector<std::string> g_paths;
        std::vector<std::string> g_titleFields;

        std::atomic<bool> g_openPanelOnClose{ false };  // clicked: open the panel once the menu has closed
        std::atomic<bool> g_returnToMenu{ false };      // the panel was opened from the bookmark

        void ReadStrings(const nlohmann::json& a_json, const char* a_key, std::vector<std::string>& a_out)
        {
            if (!a_json.contains(a_key) || !a_json[a_key].is_array()) return;
            for (const auto& value : a_json[a_key]) {
                if (value.is_string()) a_out.push_back(value.get<std::string>());
            }
        }

        void ReadPaths()
        {
            std::ifstream file(kPathsFile, std::ios::binary);
            const auto json = nlohmann::json::parse(file, nullptr, false);
            if (json.is_discarded() || !json.is_object()) {
                logger::warn("MagicMenuBookmark: {} missing or unreadable - only the card search is left", kPathsFile);
                return;
            }
            ReadStrings(json, "cardPaths", g_paths);
            ReadStrings(json, "titleFields", g_titleFields);
            logger::info("MagicMenuBookmark: {} card paths, {} title fields", g_paths.size(), g_titleFields.size());
        }

        void LogTree(const RE::GFxValue& a_node, const std::string& a_name, int a_depth)
        {
            RE::GFxValue width, height;
            a_node.GetMember("_width", &width);
            a_node.GetMember("_height", &height);
            logger::info("MagicMenuBookmark:   {}{} ({:.0f}x{:.0f})", std::string(static_cast<std::size_t>((kLogDepth - a_depth) * 2), ' '),
                a_name, width.IsNumber() ? width.GetNumber() : 0.0, height.IsNumber() ? height.GetNumber() : 0.0);
            if (a_depth == 0) return;
            a_node.VisitMembers([&](const char* a_member, const RE::GFxValue& a_value) {
                if (a_value.IsDisplayObject()) LogTree(a_value, a_name + "." + a_member, a_depth - 1);
            });
        }

        // A card no listed path reaches (another menu mod): the first display object below
        // Menu_mc whose name has "itemcard" in it, breadth first. Logged, so its path can go in the file.
        std::optional<RE::GFxValue> SearchCard(RE::GFxMovieView* a_movie)
        {
            RE::GFxValue root;
            if (!a_movie->GetVariable(&root, kMenuRoot) || !root.IsDisplayObject()) return std::nullopt;
            std::vector<std::pair<std::string, RE::GFxValue>> level{ { kMenuRoot, root } };
            for (int depth = 0; depth < kSearchDepth && !level.empty(); ++depth) {
                std::vector<std::pair<std::string, RE::GFxValue>> next;
                for (auto& [path, node] : level) {
                    node.VisitMembers([&](const char* a_member, const RE::GFxValue& a_value) {
                        if (a_value.IsDisplayObject()) next.emplace_back(path + "." + a_member, a_value);
                    });
                }
                for (auto& [path, node] : next) {
                    std::string name = path.substr(path.rfind('.') + 1);
                    std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (name.find(kCardHint) != std::string::npos) {
                        logger::info("MagicMenuBookmark: card found by search at {} - add it to magicmenu_paths.json", path);
                        return node;
                    }
                }
                level = std::move(next);
            }
            return std::nullopt;
        }

        std::optional<RE::GFxValue> FindCard(RE::GFxMovieView* a_movie)
        {
            for (const auto& path : g_paths) {
                RE::GFxValue card;
                if (a_movie->GetVariable(&card, path.c_str()) && card.IsDisplayObject()) {
                    logger::info("MagicMenuBookmark: card at {}", path);
                    return card;
                }
            }
            return SearchCard(a_movie);
        }

        void Inject(int a_triesLeft)
        {
            auto* movie = OpenMagicMenuMovie();
            if (!movie) return;  // closed meanwhile
            if (auto card = FindCard(movie)) {
                RE::GFxValue tab, watch;
                if ((card->GetMember("hom_bookmark", &tab) && tab.IsDisplayObject()) ||
                    (card->GetMember("hom_watch", &watch) && watch.IsDisplayObject())) {
                    return;  // already on this movie
                }
                logger::info("MagicMenuBookmark: card ready after {} UI tasks", kInjectRetries - a_triesLeft + 1);
                LogTree(*card, "card", kLogDepth);
                if (UIManager::MagicMenuBookmark()) Tab::Attach(movie, *card);
                SpellInfo::Attach(movie, *card);
                return;
            }
            if (a_triesLeft > 0) {
                SKSE::GetTaskInterface()->AddUITask([a_triesLeft] { Inject(a_triesLeft - 1); });
                return;
            }
            logger::warn("MagicMenuBookmark: card not found - no bookmark. The menu root's children:");
            RE::GFxValue root;
            if (movie->GetVariable(&root, "_root")) LogTree(root, "_root", kLogDepth);
        }

        class MenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
        {
        public:
            static MenuSink* GetSingleton()
            {
                static MenuSink singleton;
                return &singleton;
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
                RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
            {
                if (!a_event || a_event->menuName != RE::MagicMenu::MENU_NAME) return RE::BSEventNotifyControl::kContinue;
                logger::info("MagicMenuBookmark: magic menu {}", a_event->opening ? "opened" : "closed");
                if (a_event->opening) {
                    SKSE::GetTaskInterface()->AddUITask([] { Inject(kInjectRetries); });
                } else if (g_openPanelOnClose.exchange(false)) {
                    SKSE::GetTaskInterface()->AddTask([] {
                        g_returnToMenu = true;
                        UIManager::GetSingleton()->ShowPanel();
                    });
                }
                return RE::BSEventNotifyControl::kContinue;
            }

            MenuSink(const MenuSink&) = delete;
            MenuSink& operator=(const MenuSink&) = delete;

        private:
            MenuSink() = default;
        };
    }

    const std::vector<std::string>& TitleFields() { return g_titleFields; }

    RE::GFxMovieView* OpenMagicMenuMovie()
    {
        auto* ui = RE::UI::GetSingleton();
        auto menu = ui ? ui->GetMenu<RE::MagicMenu>() : nullptr;
        return menu ? menu->uiMovie.get() : nullptr;
    }

    void RequestPanel()
    {
        logger::info("MagicMenuBookmark: bookmark clicked");
        g_openPanelOnClose = true;
        RE::UIMessageQueue::GetSingleton()->AddMessage(RE::MagicMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
    }

    bool TakeReturnFlag() { return g_returnToMenu.exchange(false); }

    void ClearReturnFlag()
    {
        g_returnToMenu = false;
        g_openPanelOnClose = false;
    }

    void Install()
    {
        ReadPaths();
        RE::UI::GetSingleton()->AddEventSink<RE::MenuOpenCloseEvent>(MenuSink::GetSingleton());
        logger::info("MagicMenuBookmark: installed");
    }
}

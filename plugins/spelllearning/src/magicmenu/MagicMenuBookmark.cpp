#include "magicmenu/MagicMenuBookmark.h"
#include "magicmenu/MagicMenuInternal.h"
#include "magicmenu/MagicMenuDraw.h"
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
        constexpr int kFindFrames = 300;               // frames to wait for the card (and its width) after the menu opens
        constexpr int kSearchEvery = 30;               // the name search walks the whole menu: every this many frames

        std::vector<std::string> g_paths;
        std::vector<std::string> g_titleFields;
        std::string g_cardPath;        // where FindCard found the card last, for the log
        std::string g_cardVar;         // the same, as a path GetVariable takes
        bool g_cardLogged = false;     // the card's children are logged once per game session

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
                        g_cardVar = path;
                        g_cardPath = path + " (found by search - add it to magicmenu_paths.json)";
                        return node;
                    }
                }
                level = std::move(next);
            }
            return std::nullopt;
        }

        bool g_cardWithoutWidth = false;  // this open: the card was there but had no width yet

        std::optional<RE::GFxValue> FindCard(RE::GFxMovieView* a_movie, bool a_search)
        {
            for (const auto& path : g_paths) {
                RE::GFxValue card;
                if (a_movie->GetVariable(&card, path.c_str()) && card.IsDisplayObject()) {
                    g_cardPath = g_cardVar = path;
                    return card;
                }
            }
            return a_search ? SearchCard(a_movie) : std::nullopt;
        }

        constexpr std::array<const char*, 7> kFollowed{ "_x", "_y", "_xscale", "_yscale", "_rotation", "_visible", "_alpha" };

        void Follow(RE::GFxValue& a_card, RE::GFxValue& a_overlay)
        {
            for (const auto* member : kFollowed) {
                RE::GFxValue value;
                if (a_card.GetMember(member, &value)) a_overlay.SetMember(member, value);
            }
        }

        // The overlay's frame: follow the card (it moves and fades with the menu)
        class Sync final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params& a_params) override
            {
                auto* movie = OpenMagicMenuMovie();
                auto card = movie ? CurrentCard(movie) : std::nullopt;
                if (card && a_params.thisPtr) Follow(*card, *a_params.thisPtr);
            }
        };
        RE::GPtr<Sync> g_sync;

        // Attaches once the card is there and has a width; true when done (or already done on this movie)
        bool TryAttach(RE::GFxMovieView* a_movie, int a_frame)
        {
            auto card = FindCard(a_movie, a_frame % kSearchEvery == 0);
            if (!card) return false;
            if (Bounds(*card).Width() <= 0) {
                g_cardWithoutWidth = true;
                return false;
            }
            RE::GFxValue parent, existing;
            if (!card->GetMember("_parent", &parent) || !parent.IsDisplayObject()) return false;
            if (parent.GetMember("hom_overlay", &existing) && existing.IsDisplayObject()) return true;  // already on this movie
            logger::info("MagicMenuBookmark: card at {} after {} frames ({:.0f} wide inside; its own button handlers: onPress {}, onRelease {})",
                g_cardPath, a_frame, Bounds(*card).Width(), card->HasMember("onPress"), card->HasMember("onRelease"));
            if (!g_cardLogged) {
                g_cardLogged = true;
                LogTree(*card, "card", kLogDepth);
            }
            auto overlay = MagicMenuDraw::Clip(parent, "hom_overlay");
            if (!overlay.IsDisplayObject()) return true;
            Follow(*card, overlay);
            if (!g_sync) g_sync = RE::make_gptr<Sync>();
            RE::GFxValue fn;
            a_movie->CreateFunction(&fn, g_sync.get());
            overlay.SetMember("onEnterFrame", fn);
            const auto box = Bounds(*card);
            if (UIManager::MagicMenuBookmark() && UIManager::GetSingleton()->IsInitialized()) Tab::Attach(a_movie, overlay, box);
            SpellInfo::Attach(a_movie, overlay, *card);
            return true;
        }

        void LogNotFound(RE::GFxMovieView* a_movie)
        {
            if (g_cardWithoutWidth) {
                logger::warn("MagicMenuBookmark: card at {} but it had no width for {} frames - no bookmark", g_cardPath, kFindFrames);
                return;
            }
            logger::warn("MagicMenuBookmark: card not found in {} frames - no bookmark. The menu root's children:", kFindFrames);
            RE::GFxValue root;
            if (a_movie->GetVariable(&root, "_root")) LogTree(root, "_root", kLogDepth);
        }

        // The card may not exist (or be empty) when the open event comes: an empty clip on _root
        // tries again every frame of the movie itself, up to kFindFrames, then removes itself
        class Finder final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params& a_params) override
            {
                auto* movie = OpenMagicMenuMovie();
                if (!movie || !a_params.thisPtr) return;
                const bool done = TryAttach(movie, ++m_frames);
                if (!done && m_frames < kFindFrames) return;
                if (!done) LogNotFound(movie);
                a_params.thisPtr->Invoke("removeMovieClip");
            }
            void Reset() { m_frames = 0; }

        private:
            int m_frames = 0;
        };
        RE::GPtr<Finder> g_finder;

        void Inject()
        {
            auto* movie = OpenMagicMenuMovie();
            g_cardWithoutWidth = false;
            if (!movie || TryAttach(movie, 0)) return;
            RE::GFxValue root, fn;
            if (!movie->GetVariable(&root, "_root") || !root.IsDisplayObject()) return;
            if (!g_finder) g_finder = RE::make_gptr<Finder>();
            g_finder->Reset();
            auto finder = MagicMenuDraw::Clip(root, "hom_finder");
            movie->CreateFunction(&fn, g_finder.get());
            finder.SetMember("onEnterFrame", fn);
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
                    g_openPanelOnClose = false;  // a click whose close never came does not open the panel later
                    SKSE::GetTaskInterface()->AddUITask([] { Inject(); });
                } else if (g_openPanelOnClose.exchange(false)) {
                    SKSE::GetTaskInterface()->AddTask([] {
                        auto* ui = UIManager::GetSingleton();
                        ui->ShowPanel();
                        g_returnToMenu = ui->IsPanelVisible();  // not opened (not initialized): nothing to return from
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

    CardBounds Bounds(RE::GFxValue& a_card)
    {
        // The visible box first: getBounds of the whole card also counts hidden parts outside it
        // (weapon labels, buttons), which put the bookmark above the box in the first game run
        RE::GFxValue background, x, y, w, h;
        if (a_card.GetMember("background", &background) && background.IsDisplayObject() && background.GetMember("_x", &x) &&
            background.GetMember("_y", &y) && background.GetMember("_width", &w) && background.GetMember("_height", &h) &&
            x.IsNumber() && y.IsNumber() && w.IsNumber() && h.IsNumber() && w.GetNumber() > 0) {
            return { x.GetNumber(), x.GetNumber() + w.GetNumber(), y.GetNumber(), y.GetNumber() + h.GetNumber() };
        }
        // getBounds(card) is in the card's own coordinates, where its children's _x/_y are;
        // _width is in the parent's and carries the card's own scale
        RE::GFxValue bounds;
        const std::array<RE::GFxValue, 1> args{ a_card };
        if (!a_card.Invoke("getBounds", &bounds, args) || !bounds.IsObject()) return {};
        auto read = [&](const char* a_name) {
            RE::GFxValue value;
            return bounds.GetMember(a_name, &value) && value.IsNumber() ? value.GetNumber() : 0.0;
        };
        return { read("xMin"), read("xMax"), read("yMin"), read("yMax") };
    }

    std::optional<RE::GFxValue> CurrentCard(RE::GFxMovieView* a_movie)
    {
        RE::GFxValue card;
        if (g_cardVar.empty() || !a_movie->GetVariable(&card, g_cardVar.c_str()) || !card.IsDisplayObject()) return std::nullopt;
        return card;
    }

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

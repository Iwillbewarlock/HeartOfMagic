#include "magicmenu/MagicMenuBookmark.h"

#include <array>
#include <fstream>
#include <nlohmann/json.hpp>

// =============================================================================
// MAGIC MENU BOOKMARK - see MagicMenuBookmark.h
// Step 1 (plan Task 1): find the item card through magicmenu_paths.json and log
// what the bookmark will need - the card's children, the selected spell, and
// whether a picture loads into a clip.
// =============================================================================

namespace MagicMenuBookmark
{
    namespace
    {
        constexpr const char* kPathsFile = "Data/SKSE/Plugins/SpellLearning/magicmenu_paths.json";
        constexpr int kLogDepth = 2;       // spec 4.2: the menu root's children, two levels
        constexpr int kInjectRetries = 5;  // UI tasks to wait for the card clip after the open event

        // Picture paths tried by the image probe, relative to Data/Interface (loadMovie) and Data (img://)
        constexpr std::array<const char*, 2> kProbeImages{ "HeartOfMagic/bookmark.dds",
                                                           "img://Interface/HeartOfMagic/bookmark.dds" };

        std::vector<std::string> g_paths;

        void ReadPaths()
        {
            std::ifstream file(kPathsFile, std::ios::binary);
            auto json = nlohmann::json::parse(file, nullptr, false);
            if (json.is_discarded() || !json.contains("cardPaths") || !json["cardPaths"].is_array()) {
                logger::warn("MagicMenuBookmark: {} missing or unreadable - no bookmark", kPathsFile);
                return;
            }
            for (const auto& path : json["cardPaths"]) {
                if (path.is_string()) g_paths.push_back(path.get<std::string>());
            }
            logger::info("MagicMenuBookmark: {} card paths", g_paths.size());
        }

        RE::GFxMovieView* OpenMagicMenuMovie()
        {
            auto* ui = RE::UI::GetSingleton();
            auto menu = ui ? ui->GetMenu<RE::MagicMenu>() : nullptr;
            return menu ? menu->uiMovie.get() : nullptr;
        }

        std::optional<RE::GFxValue> FindCard(RE::GFxMovieView* a_movie, bool a_log)
        {
            for (const auto& path : g_paths) {
                RE::GFxValue card;
                if (a_movie->GetVariable(&card, path.c_str()) && card.IsDisplayObject()) {
                    if (a_log) logger::info("MagicMenuBookmark: card at {}", path);
                    return card;
                }
            }
            return std::nullopt;
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

        void LogSelected()
        {
            auto menu = RE::UI::GetSingleton()->GetMenu<RE::MagicMenu>();
            auto* list = menu ? menu->GetRuntimeData().itemList : nullptr;
            auto* item = list ? list->GetSelectedItem() : nullptr;
            auto* form = item ? item->data.baseForm : nullptr;
            logger::info("MagicMenuBookmark: selected {} ({:08X})", form ? form->GetName() : "(none)", form ? form->GetFormID() : 0);
        }

        // One scratch clip per picture path; a loaded picture has a width a frame later
        void ProbeImages(RE::GFxValue& a_card)
        {
            for (std::size_t i = 0; i < kProbeImages.size(); ++i) {
                RE::GFxValue probe;
                const auto name = std::format("hom_probe{}", i);
                if (!a_card.CreateEmptyMovieClip(&probe, name.c_str(), -1)) {
                    logger::info("MagicMenuBookmark: image probe {} - no clip", i);
                    continue;
                }
                probe.SetMember("_alpha", 0.0);
                std::array<RE::GFxValue, 1> args{ RE::GFxValue(kProbeImages[i]) };
                probe.Invoke("loadMovie", args);
            }
            SKSE::GetTaskInterface()->AddUITask([] {
                auto* movie = OpenMagicMenuMovie();
                auto card = movie ? FindCard(movie, false) : std::nullopt;
                if (!card) return;
                for (std::size_t i = 0; i < kProbeImages.size(); ++i) {
                    RE::GFxValue probe, width;
                    const auto name = std::format("hom_probe{}", i);
                    if (card->GetMember(name.c_str(), &probe) && probe.GetMember("_width", &width))
                        logger::info("MagicMenuBookmark: image probe {} width {}", kProbeImages[i], width.GetNumber());
                }
            });
        }

        void Diagnose(int a_triesLeft)
        {
            auto* movie = OpenMagicMenuMovie();
            if (!movie) return;  // closed meanwhile
            if (auto card = FindCard(movie, true)) {
                logger::info("MagicMenuBookmark: found after {} UI tasks", kInjectRetries - a_triesLeft + 1);
                LogTree(*card, "card", kLogDepth);
                LogSelected();
                ProbeImages(*card);
                return;
            }
            if (a_triesLeft > 0) {
                SKSE::GetTaskInterface()->AddUITask([a_triesLeft] { Diagnose(a_triesLeft - 1); });
                return;
            }
            logger::warn("MagicMenuBookmark: card not found - the menu root's children:");
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
                if (a_event && a_event->menuName == RE::MagicMenu::MENU_NAME) {
                    logger::info("MagicMenuBookmark: magic menu {}", a_event->opening ? "opened" : "closed");
                    if (a_event->opening) SKSE::GetTaskInterface()->AddUITask([] { Diagnose(kInjectRetries); });
                }
                return RE::BSEventNotifyControl::kContinue;
            }

            MenuSink(const MenuSink&) = delete;
            MenuSink& operator=(const MenuSink&) = delete;

        private:
            MenuSink() = default;
        };
    }

    void Install()
    {
        ReadPaths();
        RE::UI::GetSingleton()->AddEventSink<RE::MenuOpenCloseEvent>(MenuSink::GetSingleton());
        logger::info("MagicMenuBookmark: installed");
    }
}

#include "magicmenu/MagicMenuBookmark.h"
#include "magicmenu/MagicMenuInternal.h"
#include "magicmenu/MagicMenuDraw.h"
#include "uimanager/UIManager.h"

#include <atomic>

// =============================================================================
// MAGIC MENU BOOKMARK - see MagicMenuBookmark.h
// The menu's open/close events, the widget at the top right of the menu's
// visible area (the leather clip, and under it the selected spell's box), and
// the open-panel / back-to-menu round trip.
// =============================================================================

namespace MagicMenuBookmark
{
    namespace
    {
        // Widget layout in units of a 720-high screen (the movie's visible height / 720)
        constexpr double kRefHeight = 720.0;
        constexpr double kMargin = 24.0;     // from the visible area's top and right edges
        constexpr double kWidgetW = 230.0;   // the spell box; the clip sits at its right end, above it
        constexpr double kClipW = 70.0;
        constexpr double kClipGap = 6.0;     // between the clip and the box

        std::atomic<bool> g_openPanelOnClose{ false };  // clicked: open the panel once the menu has closed
        std::atomic<bool> g_returnToMenu{ false };      // the panel was opened from the bookmark

        void AttachWidget()
        {
            auto* movie = OpenMagicMenuMovie();
            if (!movie) return;  // closed meanwhile
            const bool bookmark = UIManager::MagicMenuBookmark() && UIManager::GetSingleton()->IsInitialized();
            if (!bookmark && !UIManager::MagicMenuSpellInfo()) return;  // both switched off: add nothing
            RE::GFxValue root, existing;
            if (!movie->GetVariable(&root, "_root") || !root.IsDisplayObject()) return;
            if (root.GetMember("hom_widget", &existing) && existing.IsDisplayObject()) return;  // already on this movie

            const auto area = movie->GetVisibleFrameRect();
            const double unit = (area.bottom - area.top) / kRefHeight;
            if (unit <= 0) {
                logger::warn("MagicMenuBookmark: the menu has no visible area - no widget");
                return;
            }
            auto widget = MagicMenuDraw::Clip(root, "hom_widget");
            if (!widget.IsDisplayObject()) return;
            const double x = area.right - (kMargin + kWidgetW) * unit, y = area.top + kMargin * unit;
            widget.SetMember("_x", x);
            widget.SetMember("_y", y);

            const double clipW = kClipW * unit;
            if (bookmark) Tab::Attach(movie, widget, (kWidgetW - kClipW) * unit, 0, clipW);
            SpellInfo::Attach(movie, widget, 0, Tab::Height(clipW) + kClipGap * unit, kWidgetW * unit, unit);
            logger::info("MagicMenuBookmark: widget at x {:.0f} y {:.0f} (visible area x {:.0f}..{:.0f} y {:.0f}..{:.0f})", x, y,
                area.left, area.right, area.top, area.bottom);
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
                    SKSE::GetTaskInterface()->AddUITask([] { AttachWidget(); });
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
        RE::UI::GetSingleton()->AddEventSink<RE::MenuOpenCloseEvent>(MenuSink::GetSingleton());
        logger::info("MagicMenuBookmark: installed");
    }
}

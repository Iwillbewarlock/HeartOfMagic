#include "magicmenu/MagicMenuBookmark.h"
#include "magicmenu/MagicMenuInternal.h"
#include "magicmenu/MagicMenuDraw.h"
#include "uimanager/UIManager.h"

#include <array>
#include <atomic>

// =============================================================================
// MAGIC MENU BOOKMARK - see MagicMenuBookmark.h
// The menu's open/close events, the widget that follows the item card's visible
// top-right corner (the leather clip folded over it, and above the card, left of
// the clip, the selected spell's box), and
// the open-panel / back-to-menu round trip.
// =============================================================================

namespace MagicMenuBookmark
{
    namespace
    {
        // Widget layout in units of a 720-high screen (the movie's visible height / 720). The widget's
        // origin is the item card's visible top-right corner; the clip folds over it, the spell box sits
        // just above the card, left of the clip.
        constexpr double kRefHeight = 720.0;
        constexpr double kClipW = 54.0;      // its flap stays above the description's first line (game run 3, 2026-10-09)
        constexpr double kClipInset = 10.0;  // the clip's right edge, left of the card's right edge
        constexpr double kBoxW = 230.0, kGap = 6.0;  // the box's height: SpellInfo::kBoxH

        // The item card (SkyUI's ItemMenu, then vanilla's names) and the part of its "background" that is
        // the visible box: in game run 2 (2026-10-09) the background's bounds stood about 40 card units
        // right of and 36 above the black box the player sees (its shadow/frame margin)
        constexpr std::array<const char*, 2> kCardPaths{ "_root.Menu_mc.itemCardFadeHolder.ItemCard_mc",
                                                         "_root.Menu_mc.ItemCardFadeHolder_mc.ItemCard_mc" };
        constexpr double kBoxInsetRight = 40.0, kBoxInsetTop = 36.0;
        constexpr int kMaxParents = 16;
        constexpr double kMinAlpha = 0.05;     // below this the card counts as hidden
        constexpr int kMissingFramesWarn = 120;

        std::atomic<bool> g_openPanelOnClose{ false };  // clicked: open the panel once the menu has closed
        std::atomic<bool> g_returnToMenu{ false };      // the panel was opened from the bookmark
        bool g_layoutLogged = false;                    // the first placement each session is logged

        std::optional<RE::GFxValue> FindCard(RE::GFxMovieView* a_movie)
        {
            for (const auto* path : kCardPaths) {
                RE::GFxValue card;
                if (a_movie->GetVariable(&card, path) && card.IsDisplayObject()) return card;
            }
            return std::nullopt;
        }

        // A point of a clip in _root's coordinates
        std::pair<double, double> ToRoot(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, RE::GFxValue& a_root, double a_x, double a_y)
        {
            RE::GFxValue point, x, y;
            a_movie->CreateObject(&point);
            point.SetMember("x", a_x);
            point.SetMember("y", a_y);
            a_clip.Invoke("localToGlobal", nullptr, &point, 1);
            a_root.Invoke("globalToLocal", nullptr, &point, 1);
            point.GetMember("x", &x);
            point.GetMember("y", &y);
            return { x.IsNumber() ? x.GetNumber() : 0.0, y.IsNumber() ? y.GetNumber() : 0.0 };
        }

        // The card's visible top-right corner in _root, and whether the card is shown (its fade with
        // its parents'); nullopt without a card or its background
        struct Corner { double x, y, alpha; bool visible; };
        std::optional<Corner> CardCorner(RE::GFxMovieView* a_movie, RE::GFxValue& a_root)
        {
            auto card = FindCard(a_movie);
            RE::GFxValue background, bounds, xMax, yMin;
            if (!card || !card->GetMember("background", &background) || !background.IsDisplayObject()) return std::nullopt;
            const std::array<RE::GFxValue, 1> args{ *card };
            if (!background.Invoke("getBounds", &bounds, args) || !bounds.IsObject() || !bounds.GetMember("xMax", &xMax) ||
                !bounds.GetMember("yMin", &yMin) || !xMax.IsNumber() || !yMin.IsNumber()) {
                return std::nullopt;
            }
            const auto [x, y] = ToRoot(a_movie, *card, a_root, xMax.GetNumber() - kBoxInsetRight, yMin.GetNumber() + kBoxInsetTop);
            double alpha = 1.0;
            bool visible = true;
            RE::GFxValue clip = *card;
            for (int depth = 0; depth < kMaxParents && clip.IsDisplayObject(); ++depth) {
                RE::GFxValue a, v, parent;
                if (clip.GetMember("_alpha", &a) && a.IsNumber()) alpha *= a.GetNumber() / 100.0;
                if (clip.GetMember("_visible", &v) && v.IsBool()) visible = visible && v.GetBool();
                if (!clip.GetMember("_parent", &parent) || !parent.IsDisplayObject() || parent == a_root) break;
                clip = parent;
            }
            if (!g_layoutLogged) {
                g_layoutLogged = true;
                RE::GFxValue xMin, yMax;
                bounds.GetMember("xMin", &xMin);
                bounds.GetMember("yMax", &yMax);
                const auto [ax, ay] = ToRoot(a_movie, *card, a_root, xMin.GetNumber(), yMin.GetNumber());
                const auto [bx, by] = ToRoot(a_movie, *card, a_root, xMax.GetNumber(), yMax.GetNumber());
                logger::info("MagicMenuBookmark: card background in _root x {:.0f}..{:.0f} y {:.0f}..{:.0f}, widget corner {:.0f},{:.0f}",
                    ax, bx, ay, by, x, y);
            }
            return Corner{ x, y, alpha, visible };
        }

        // The widget's frame: follow the card's corner (the menu slides and fades the card)
        class Follow final : public RE::GFxFunctionHandler
        {
            int m_missing = 0;  // frames in a row without the card (one warning per long gap)

        public:
            void Call(Params& a_params) override
            {
                auto* movie = OpenMagicMenuMovie();
                RE::GFxValue root;
                if (!movie || !a_params.thisPtr || !movie->GetVariable(&root, "_root")) return;
                const auto corner = CardCorner(movie, root);
                auto& widget = *a_params.thisPtr;
                // _alpha 0 still takes the mouse in AS2: a card faded out hides the widget too
                const bool shown = corner.has_value() && corner->visible && corner->alpha > kMinAlpha;
                widget.SetMember("_visible", shown);
                if (!shown) Tab::Unhover(widget);
                if (!corner) {
                    if (++m_missing == kMissingFramesWarn) {
                        logger::warn("MagicMenuBookmark: no item card (or its background) for {} frames - widget hidden", kMissingFramesWarn);
                    }
                    return;
                }
                m_missing = 0;
                widget.SetMember("_x", corner->x);
                widget.SetMember("_y", corner->y);
                widget.SetMember("_alpha", corner->alpha * 100.0);
            }
        };
        RE::GPtr<Follow> g_follow;

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
            widget.SetMember("_visible", false);  // until the first frame finds the card
            if (!g_follow) g_follow = RE::make_gptr<Follow>();
            RE::GFxValue fn;
            movie->CreateFunction(&fn, g_follow.get());
            widget.SetMember("onEnterFrame", fn);

            const double clipW = kClipW * unit, clipX = -(kClipInset * unit + clipW);
            if (bookmark) Tab::Attach(movie, widget, clipX, -Tab::FoldHeight(clipW), clipW);
            SpellInfo::Attach(movie, widget, clipX - (kGap + kBoxW) * unit, -(SpellInfo::kBoxH + kGap) * unit, kBoxW * unit, unit);
            logger::info("MagicMenuBookmark: widget attached (visible area x {:.0f}..{:.0f} y {:.0f}..{:.0f})", area.left, area.right,
                area.top, area.bottom);
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

#include "magicmenu/MagicMenuInternal.h"
#include "magicmenu/MagicMenuDraw.h"

#include <chrono>
#include <cmath>
#include <numbers>

// =============================================================================
// MAGIC MENU BOOKMARK - the bookmark itself (spec 2.1, plan "책갈피 그림"):
// a leather clip folded over the item card's top-right corner, "Heart of Magic"
// on it, rising a little and glowing gold while the mouse is over it.
// Two pictures (Interface/HeartOfMagic/bookmark*.dds) when the menu can load
// them; until then, and for good when it cannot, the same clip drawn with lines.
// =============================================================================

namespace MagicMenuBookmark::Tab
{
    namespace
    {
        // Sizes at a 600-wide card, scaled to the card's width. y=0 is the card's top border:
        // the fold rises kFoldH above it, the front flap hangs kFlapH into the card.
        constexpr double kRefCardWidth = 600.0;
        constexpr double kTabW = 80.0, kFoldH = 6.0, kFlapH = 40.0, kCorner = 6.0, kRightInset = 16.0, kHoverRise = 4.0;
        constexpr std::uint32_t kLeather = 0xA0643A, kLeatherEdge = 0x6E4024, kStitch = 0xE8DCC8, kInk = 0x5E331B;
        constexpr std::uint32_t kGold = 0xFFD27A;
        constexpr double kStitchY = 5.0, kStitchInset = 5.0, kDash = 4.0;
        constexpr double kLabelY = 14.0, kLabelH = 18.0, kLabelSize = 11.0;

        struct Pt { double x, y; };
        // The outline is one table so a new design only changes it
        constexpr std::array<Pt, 7> kOutline{ { { 0, -kFoldH }, { kTabW, -kFoldH }, { kTabW, kFlapH - kCorner },
                                                { kTabW - kCorner, kFlapH }, { kCorner, kFlapH }, { 0, kFlapH - kCorner },
                                                { 0, -kFoldH } } };

        // The pictures: 256x128, the clip spans the middle kPicContentW of the width (transparent
        // margins) and the card's top border sits kPicBorderY down from the top
        constexpr const char* kPicture = "HeartOfMagic/bookmark.dds";
        constexpr const char* kGlowPicture = "HeartOfMagic/bookmark_glow.dds";
        constexpr double kPicContentW = 0.8375, kPicPadX = 0.08125, kPicBorderY = 0.13125;
        constexpr int kPictureWaitFrames = 60;  // frames to wait for both pictures before settling on the lines

        // Gold glow while hovered: fade in over kGlowFadeMs, then breathe between kGlowLow and 100 every kGlowPeriodMs
        constexpr double kGlowFadeMs = 250.0, kGlowPeriodMs = 2000.0, kGlowLow = 60.0;

        // One bookmark exists at a time (one magic menu); Attach resets this
        struct State
        {
            double scale = 1.0;
            bool hover = false;
            bool pictures = false;  // both pictures loaded and fitted
            int frames = 0;
            double phase = 0.0;
            std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
        } g_state;

        RE::GFxValue Child(RE::GFxValue& a_parent, const char* a_name)
        {
            RE::GFxValue child;
            a_parent.GetMember(a_name, &child);
            return child;
        }

        double Number(RE::GFxValue& a_clip, const char* a_member)
        {
            RE::GFxValue value;
            return a_clip.GetMember(a_member, &value) && value.IsNumber() ? value.GetNumber() : 0.0;
        }

        void DrawLines(RE::GFxMovieView* a_movie, RE::GFxValue& a_vec, double s)
        {
            MagicMenuDraw::LineStyle(a_vec, 1.0, kLeatherEdge);
            MagicMenuDraw::BeginFill(a_vec, kLeather, 100.0);
            MagicMenuDraw::MoveTo(a_vec, kOutline[0].x * s, kOutline[0].y * s);
            for (std::size_t i = 1; i < kOutline.size(); ++i) MagicMenuDraw::LineTo(a_vec, kOutline[i].x * s, kOutline[i].y * s);
            MagicMenuDraw::EndFill(a_vec);
            MagicMenuDraw::LineStyle(a_vec, 1.0, kStitch);  // the stitch line under the fold
            for (double x = kStitchInset; x + kDash <= kTabW - kStitchInset; x += 2 * kDash) {
                MagicMenuDraw::MoveTo(a_vec, x * s, kStitchY * s);
                MagicMenuDraw::LineTo(a_vec, (x + kDash) * s, kStitchY * s);
            }
            MagicMenuDraw::Text(a_movie, a_vec, "hom_label", 0, kLabelY * s, kTabW * s, kLabelH * s, "Heart of Magic", kInk,
                kLabelSize * s, "center");
            auto glow = MagicMenuDraw::Text(a_movie, a_vec, "hom_glowText", 0, kLabelY * s, kTabW * s, kLabelH * s,
                "Heart of Magic", kGold, kLabelSize * s, "center");
            glow.SetMember("_alpha", 0.0);
            const bool filtered = MagicMenuDraw::Glow(a_movie, glow, kGold, 6.0, 2.0);
            logger::info("MagicMenuBookmark: drawn bookmark, glow filter {}", filtered ? "on" : "not supported (gold letters only)");
        }

        void LoadPicture(RE::GFxValue& a_tab, const char* a_name, const char* a_path)
        {
            auto pic = MagicMenuDraw::Clip(a_tab, a_name);
            pic.SetMember("_visible", false);
            const std::array<RE::GFxValue, 1> args{ RE::GFxValue(a_path) };
            pic.Invoke("loadMovie", args);
        }

        // Both pictures loaded (a width): size and place them over the clip, hide the lines
        void FitPictures(RE::GFxValue& a_tab)
        {
            auto pic = Child(a_tab, "hom_pic");
            auto glow = Child(a_tab, "hom_picGlow");
            if (Number(pic, "_width") <= 0 || Number(glow, "_width") <= 0) return;
            const double w = kTabW * g_state.scale / kPicContentW, h = w / 2;
            for (auto* clip : { &pic, &glow }) {
                clip->SetMember("_width", w);
                clip->SetMember("_height", h);
                clip->SetMember("_x", -kPicPadX * w);
                clip->SetMember("_y", -kPicBorderY * h);
                clip->SetMember("_visible", true);
            }
            glow.SetMember("_alpha", 0.0);
            Child(a_tab, "hom_vec").SetMember("_visible", false);
            g_state.pictures = true;
            logger::info("MagicMenuBookmark: bookmark pictures loaded ({} frames)", g_state.frames);
        }

        void StepGlow(RE::GFxValue& a_tab)
        {
            const auto now = std::chrono::steady_clock::now();
            const double dt = std::chrono::duration<double, std::milli>(now - g_state.last).count();
            g_state.last = now;
            RE::GFxValue glow = g_state.pictures ? Child(a_tab, "hom_picGlow") : Child(a_tab, "hom_vec");
            if (!g_state.pictures) glow = Child(glow, "hom_glowText");
            if (!glow.IsDisplayObject()) return;
            const double step = 100.0 * std::min(dt, kGlowFadeMs) / kGlowFadeMs;
            double alpha = Number(glow, "_alpha");
            if (g_state.hover) {
                g_state.phase = std::fmod(g_state.phase + dt / kGlowPeriodMs, 1.0);
                const double breath = kGlowLow + (100.0 - kGlowLow) * (0.5 + 0.5 * std::cos(2 * std::numbers::pi * g_state.phase));
                alpha = alpha < breath ? std::min(alpha + step, breath) : breath;
            } else {
                g_state.phase = 0.0;
                alpha = std::max(alpha - step, 0.0);
            }
            glow.SetMember("_alpha", alpha);
        }

        class Handler final : public RE::GFxFunctionHandler
        {
        public:
            enum class Kind { kClick, kRollOver, kRollOut, kTick };
            explicit Handler(Kind a_kind) : m_kind(a_kind) {}

            void Call(Params& a_params) override
            {
                auto* tab = a_params.thisPtr;
                if (!tab) return;
                switch (m_kind) {
                case Kind::kClick:
                    RequestPanel();
                    break;
                case Kind::kRollOver:
                case Kind::kRollOut:
                    {
                        const bool over = m_kind == Kind::kRollOver;
                        if (over == g_state.hover) break;
                        g_state.hover = over;
                        tab->SetMember("_y", Number(*tab, "_y") + (over ? -kHoverRise : kHoverRise) * g_state.scale);
                        break;
                    }
                case Kind::kTick:
                    if (!g_state.pictures && g_state.frames <= kPictureWaitFrames) {
                        FitPictures(*tab);
                        if (++g_state.frames > kPictureWaitFrames && !g_state.pictures)
                            logger::info("MagicMenuBookmark: bookmark pictures did not load - the drawn clip stays");
                    }
                    StepGlow(*tab);
                    break;
                }
            }

        private:
            Kind m_kind;
        };

        // Refcounted Scaleform objects kept for the plugin's life; each new menu movie wraps them again
        RE::GPtr<Handler> g_click, g_rollOver, g_rollOut, g_tick;

        void SetHandler(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, const char* a_event, Handler* a_handler)
        {
            RE::GFxValue fn;
            a_movie->CreateFunction(&fn, a_handler);
            a_clip.SetMember(a_event, fn);
        }
    }

    void Attach(RE::GFxMovieView* a_movie, RE::GFxValue& a_card)
    {
        if (!g_click) {
            g_click = RE::make_gptr<Handler>(Handler::Kind::kClick);
            g_rollOver = RE::make_gptr<Handler>(Handler::Kind::kRollOver);
            g_rollOut = RE::make_gptr<Handler>(Handler::Kind::kRollOut);
            g_tick = RE::make_gptr<Handler>(Handler::Kind::kTick);
        }
        const double cardWidth = Number(a_card, "_width");
        if (cardWidth <= 0) {
            logger::warn("MagicMenuBookmark: the card has no width - no bookmark");
            return;
        }
        g_state = State{};
        g_state.scale = cardWidth / kRefCardWidth;

        auto tab = MagicMenuDraw::Clip(a_card, "hom_bookmark");
        if (!tab.IsDisplayObject()) return;
        auto vec = MagicMenuDraw::Clip(tab, "hom_vec");
        DrawLines(a_movie, vec, g_state.scale);
        LoadPicture(tab, "hom_pic", kPicture);
        LoadPicture(tab, "hom_picGlow", kGlowPicture);
        tab.SetMember("_x", cardWidth - (kTabW + kRightInset) * g_state.scale);

        SetHandler(a_movie, tab, "onRelease", g_click.get());
        SetHandler(a_movie, tab, "onRollOver", g_rollOver.get());
        SetHandler(a_movie, tab, "onRollOut", g_rollOut.get());
        SetHandler(a_movie, tab, "onReleaseOutside", g_rollOut.get());
        SetHandler(a_movie, tab, "onEnterFrame", g_tick.get());
        logger::info("MagicMenuBookmark: bookmark attached (card {:.0f} wide, scale {:.2f})", cardWidth, g_state.scale);
    }
}

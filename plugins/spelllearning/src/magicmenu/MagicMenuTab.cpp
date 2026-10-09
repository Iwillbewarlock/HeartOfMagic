#include "magicmenu/MagicMenuInternal.h"
#include "magicmenu/MagicMenuDraw.h"

#include <chrono>
#include <cmath>
#include <numbers>

// =============================================================================
// MAGIC MENU BOOKMARK - the bookmark itself (spec 2.1, plan "책갈피 그림"):
// a leather clip with "Heart / of / Magic" on it at the top of the magic menu
// widget, rising a little and glowing gold while the mouse is over it.
// Two pictures (Interface/HeartOfMagic/bookmark*.dds) when the menu can load
// them; until then, and for good when it cannot, the same clip drawn with lines.
// =============================================================================

namespace MagicMenuBookmark::Tab
{
    namespace
    {
        // Sizes for a clip kTabW wide, scaled to the width the widget gives it. y=0 is the line
        // under the fold (where a page edge would sit): the fold rises kFoldH above it, the flap hangs kFlapH.
        constexpr double kTabW = 90.0, kFoldH = 7.0, kFlapH = 112.0, kCorner = 8.0, kHoverRise = 4.0;
        constexpr std::uint32_t kLeather = 0x56463A, kLeatherEdge = 0x3A2E25, kStitch = 0x8C8070, kInk = 0x2E241C;  // dark worn leather (2026-10-09 picture)
        constexpr std::uint32_t kGold = 0xFFD27A;
        constexpr double kStitchY = 16.0, kStitchInset = 6.0, kDash = 5.0;
        constexpr double kEdgeLine = 1.5, kStitchLine = 1.2;  // line widths at kTabW, scaled with the clip
        constexpr double kLabelY = 35.0, kLabelH = 54.0, kLabelSize = 15.0;
        constexpr const char* kLabel = "Heart\rof\rMagic";  // three lines, as on the pictures

        struct Pt { double x, y; };
        // The outline is one table so a new design only changes it
        constexpr std::array<Pt, 7> kOutline{ { { 0, -kFoldH }, { kTabW, -kFoldH }, { kTabW, kFlapH - kCorner },
                                                { kTabW - kCorner, kFlapH }, { kCorner, kFlapH }, { 0, kFlapH - kCorner },
                                                { 0, -kFoldH } } };

        // The pictures: 256x256 (square), the clip spans the middle kPicContentW of the width
        // (transparent margins) and the card's top border sits kPicBorderY down from the top (just under
        // the fold's top). loadMovie takes them relative to Data/Interface (game run 2026-10-08)
        constexpr const char* kPicture = "HeartOfMagic/bookmark.dds";
        constexpr const char* kGlowPicture = "HeartOfMagic/bookmark_glow.dds";
        constexpr double kPicContentW = 0.7102, kPicPadX = 0.1449, kPicBorderY = 0.0875;
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
            MagicMenuDraw::LineStyle(a_vec, kEdgeLine * s, kLeatherEdge);
            MagicMenuDraw::BeginFill(a_vec, kLeather, 100.0);
            MagicMenuDraw::MoveTo(a_vec, kOutline[0].x * s, kOutline[0].y * s);
            for (std::size_t i = 1; i < kOutline.size(); ++i) MagicMenuDraw::LineTo(a_vec, kOutline[i].x * s, kOutline[i].y * s);
            MagicMenuDraw::EndFill(a_vec);
            MagicMenuDraw::LineStyle(a_vec, kStitchLine * s, kStitch);  // the stitch line under the fold
            for (double x = kStitchInset; x + kDash <= kTabW - kStitchInset; x += 2 * kDash) {
                MagicMenuDraw::MoveTo(a_vec, x * s, kStitchY * s);
                MagicMenuDraw::LineTo(a_vec, (x + kDash) * s, kStitchY * s);
            }
            MagicMenuDraw::Text(a_movie, a_vec, "hom_label", 0, kLabelY * s, kTabW * s, kLabelH * s, kLabel, kInk,
                kLabelSize * s, "center");
            auto glow = MagicMenuDraw::Text(a_movie, a_vec, "hom_glowText", 0, kLabelY * s, kTabW * s, kLabelH * s,
                kLabel, kGold, kLabelSize * s, "center");
            glow.SetMember("_alpha", 0.0);  // the game's movie has no GlowFilter (2026-10-08): gold letters only
        }

        // A hidden holder with the picture loaded into its child "img": loadMovie resets the clip it
        // loads into, so the hiding stays on the holder
        void LoadPicture(RE::GFxValue& a_tab, const char* a_name, const char* a_path)
        {
            auto holder = MagicMenuDraw::Clip(a_tab, a_name);
            holder.SetMember("_visible", false);
            auto img = MagicMenuDraw::Clip(holder, "img");
            const std::array<RE::GFxValue, 1> args{ RE::GFxValue(a_path) };
            img.Invoke("loadMovie", args);
        }

        // Both pictures loaded (a width): size and place them over the clip, hide the lines
        void FitPictures(RE::GFxValue& a_tab)
        {
            auto pic = Child(a_tab, "hom_pic");
            auto glow = Child(a_tab, "hom_picGlow");
            auto picImg = Child(pic, "img");
            auto glowImg = Child(glow, "img");
            if (Number(picImg, "_width") <= 0 || Number(glowImg, "_width") <= 0) return;
            const double w = kTabW * g_state.scale / kPicContentW, h = w;  // square pictures
            for (auto* img : { &picImg, &glowImg }) {
                img->SetMember("_width", w);
                img->SetMember("_height", h);
                img->SetMember("_x", -kPicPadX * w);
                img->SetMember("_y", -kPicBorderY * h);
            }
            pic.SetMember("_visible", true);
            glow.SetMember("_visible", true);
            glow.SetMember("_alpha", 0.0);
            Child(a_tab, "hom_vec").SetMember("_visible", false);
            g_state.pictures = true;
            logger::debug("MagicMenuBookmark: bookmark pictures loaded ({} frames)", g_state.frames);
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

    double FoldHeight(double a_width)
    {
        return kFoldH * a_width / kTabW;
    }

    void Attach(RE::GFxMovieView* a_movie, RE::GFxValue& a_parent, double a_x, double a_y, double a_width)
    {
        if (!g_click) {
            g_click = RE::make_gptr<Handler>(Handler::Kind::kClick);
            g_rollOver = RE::make_gptr<Handler>(Handler::Kind::kRollOver);
            g_rollOut = RE::make_gptr<Handler>(Handler::Kind::kRollOut);
            g_tick = RE::make_gptr<Handler>(Handler::Kind::kTick);
        }
        g_state = State{};
        g_state.scale = a_width / kTabW;

        auto tab = MagicMenuDraw::Clip(a_parent, "hom_bookmark");
        if (!tab.IsDisplayObject()) return;
        auto vec = MagicMenuDraw::Clip(tab, "hom_vec");
        DrawLines(a_movie, vec, g_state.scale);
        LoadPicture(tab, "hom_pic", kPicture);
        LoadPicture(tab, "hom_picGlow", kGlowPicture);
        // Only the leather takes the mouse, not the pictures' transparent margins: the drawn clip
        // (the same outline, hidden once the pictures show) is the hit area
        tab.SetMember("hitArea", vec);
        tab.SetMember("_x", a_x);
        tab.SetMember("_y", a_y + kFoldH * g_state.scale);

        SetHandler(a_movie, tab, "onRelease", g_click.get());
        SetHandler(a_movie, tab, "onRollOver", g_rollOver.get());
        SetHandler(a_movie, tab, "onRollOut", g_rollOut.get());
        SetHandler(a_movie, tab, "onReleaseOutside", g_rollOut.get());
        SetHandler(a_movie, tab, "onEnterFrame", g_tick.get());
        logger::info("MagicMenuBookmark: bookmark attached ({:.0f} wide)", a_width);
    }

    void Unhover(RE::GFxValue& a_widget)
    {
        if (!g_state.hover) return;
        g_state.hover = false;  // a widget hidden under the mouse gets no roll-out
        RE::GFxValue tab;
        if (a_widget.GetMember("hom_bookmark", &tab) && tab.IsDisplayObject()) {
            tab.SetMember("_y", Number(tab, "_y") + kHoverRise * g_state.scale);
        }
    }
}

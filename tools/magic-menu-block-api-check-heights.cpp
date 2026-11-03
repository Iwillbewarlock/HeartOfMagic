// magic-menu-block-api-check, the opening height and the column's room (2026-11-03, progress-next-pick plan): BlockRulesV3.h's
// OpeningHeight / RememberHeight / JudgeOpen - a block that asks for its height again after its first draw opens the next menu at
// the height it last asked for this game session, never above its registered one, the layout mode still at the registered one
// (BlockRules::ApplyFits), a block that does not ask again put back - and ColumnRoom, Context::columnRoom, at four screen shapes
// and any scroll. See magic-menu-block-api-check.cpp for the tool's usage.

#include "magic-menu-block-api-check.h"

#include "BlockRules.h"
#include "BlockRulesV3.h"

#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>

namespace BlockApiCheck
{
    namespace
    {
        constexpr double kRegistered = 157.0;
        constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

        bool Same(const std::optional<double>& a_got, double a_want)
        {
            return a_got && std::abs(*a_got - a_want) <= BlockRules::kHeightTolerance;
        }

        // One block through menus as the host runs it (Widget.cpp AttachWidget / DrawAddons / TakeAddonHeights): the session's
        // remembered height, the menu's fit height, the first draw's check
        struct Sim
        {
            std::map<std::string, BlockRules::SessionHeight> session;
            std::optional<double> fit;  // g_menu.fitHeight for the block (nullopt: its registered height)
            bool seeded = false;
            std::optional<BlockRules::OpenCheck> check;
            std::uint64_t frame = 0;

            void Open()
            {
                const auto it = session.find("A.b");
                fit = BlockRules::OpeningHeight(it != session.end() ? std::optional{ it->second } : std::nullopt, kRegistered);
                seeded = fit.has_value();
                check.reset();
                frame = 0;
            }

            void FirstDraw() { check = BlockRules::OpenCheck{ frame, false }; }

            // a height request taken at a height pass of this frame
            void Ask(double a_height)
            {
                if (check) check->asked = true;
                if (BlockRules::JudgeHeight(a_height, kRegistered, fit) == BlockRules::HeightVerdict::kApply) {
                    fit = a_height;
                    BlockRules::RememberHeight(session, "A.b", a_height, kRegistered);
                }
            }

            // the next frame's first height pass
            BlockRules::OpenVerdict NextFrame()
            {
                ++frame;
                if (!check) return BlockRules::OpenVerdict::kKeep;
                const auto verdict = BlockRules::JudgeOpen(*check, frame, seeded);
                if (verdict == BlockRules::OpenVerdict::kWait) return verdict;
                session["A.b"].reasks = check->asked;
                if (verdict == BlockRules::OpenVerdict::kRevert) fit.reset();
                check.reset();
                return verdict;
            }
        };

        void OpeningRules()
        {
            std::printf("opening height (2026-11-03): OpeningHeight, RememberHeight, JudgeOpen\n");
            using BlockRules::OpeningHeight;
            using BlockRules::SessionHeight;
            Expect(!OpeningHeight(std::nullopt, kRegistered), "nothing remembered: the registered height");
            Expect(Same(OpeningHeight(SessionHeight{ 80.0, true }, kRegistered), 80.0), "80 remembered, asked again last menu: opens at 80");
            Expect(!OpeningHeight(SessionHeight{ 80.0, false }, kRegistered), "80 remembered but no ask after the first draw: the registered height");
            Expect(!OpeningHeight(SessionHeight{ 200.0, true }, kRegistered) && !OpeningHeight(SessionHeight{ kRegistered + 0.02, true }, kRegistered),
                "above the registered height (200, 157.02): refused - the registered height");
            Expect(Same(OpeningHeight(SessionHeight{ kRegistered + 0.005, true }, kRegistered), kRegistered + 0.005),
                "within the tolerance of the registered height: kept");
            Expect(!OpeningHeight(SessionHeight{ kNaN, true }, kRegistered) && !OpeningHeight(SessionHeight{ 0.0, true }, kRegistered) &&
                       !OpeningHeight(SessionHeight{ -5.0, true }, kRegistered),
                "NaN, 0, -5: the registered height");

            std::map<std::string, SessionHeight> session{ { "A.b", { 90.0, true } } };
            const bool stored = BlockRules::RememberHeight(session, "A.b", 80.0, kRegistered);
            Expect(stored && session["A.b"].height == 80.0 && session["A.b"].reasks, "RememberHeight 80: stored, its reasks flag kept");
            const bool tall = BlockRules::RememberHeight(session, "A.b", 200.0, kRegistered), nan = BlockRules::RememberHeight(session, "A.b", kNaN, kRegistered);
            Expect(!tall && !nan && session["A.b"].height == 80.0, "RememberHeight 200 or NaN: refused, 80 stays");
            const bool fresh = BlockRules::RememberHeight(session, "A.new", 60.0, kRegistered);
            Expect(fresh && session["A.new"].height == 60.0 && !session["A.new"].reasks, "a block's first remembered height: reasks false until judged");

            using BlockRules::OpenVerdict;
            Expect(BlockRules::JudgeOpen({ 5, false }, 5, true) == OpenVerdict::kWait && BlockRules::JudgeOpen({ 5, true }, 5, true) == OpenVerdict::kWait,
                "JudgeOpen in the frame of the first draw: wait (its request may come at the frame's second height pass)");
            Expect(BlockRules::JudgeOpen({ 5, true }, 6, true) == OpenVerdict::kKeep && BlockRules::JudgeOpen({ 5, true }, 6, false) == OpenVerdict::kKeep,
                "asked again: keep");
            Expect(BlockRules::JudgeOpen({ 5, false }, 6, true) == OpenVerdict::kRevert, "opened at a remembered height, no ask: back to the registered height");
            Expect(BlockRules::JudgeOpen({ 5, false }, 6, false) == OpenVerdict::kKeep, "opened at the registered height, no ask: keep");
        }

        void LayoutModeAndFill()
        {
            std::printf("opening height: the layout mode shows the registered height (ApplyFits)\n");
            const std::map<std::string, double> fits{ { "A.b", 80.0 } };
            std::map<std::string, double> heights{ { "A.b", kRegistered } }, inMode = heights;
            std::set<std::string> fill{ "A.b" }, fillInMode = fill;
            BlockRules::ApplyFits(heights, fill, fits, false);
            BlockRules::ApplyFits(inMode, fillInMode, fits, true);
            Expect(heights.at("A.b") == 80.0 && fill.empty(), "an opening height of 80 outside the layout mode: laid out at 80, out of the fill");
            Expect(inMode.at("A.b") == kRegistered && fillInMode.contains("A.b"), "in the layout mode: the registered 157 (the fill kept)");
        }

        void Menus()
        {
            std::printf("opening height: menus of one game session\n");
            using BlockRules::OpenVerdict;
            // a block that asks at every first draw (the HoM progress card, SR's block)
            Sim asks;
            asks.Open();
            asks.FirstDraw();
            asks.Ask(80.0);
            Expect(asks.NextFrame() == OpenVerdict::kKeep && asks.fit == 80.0 && asks.session["A.b"].reasks, "menu 1: registered, asks 80 at its first draw");
            asks.Ask(120.0);  // later in the menu: content grew
            asks.Open();
            Expect(asks.seeded && asks.fit == 120.0, "menu 2 opens at 120, the last height asked in menu 1 (no jump)");
            asks.FirstDraw();
            asks.Ask(120.0);  // the same height: unchanged, but asked
            Expect(asks.NextFrame() == OpenVerdict::kKeep && asks.fit == 120.0, "menu 2: asks 120 again - kept");
            // a block that stops asking (an older add-on, or one that keeps its height): put back once, then the registered height
            asks.Open();
            asks.FirstDraw();
            Expect(asks.NextFrame() == OpenVerdict::kRevert && !asks.fit && !asks.session["A.b"].reasks, "menu 3 opened at 120, no ask: back to 157");
            asks.Open();
            Expect(!asks.seeded && !asks.fit, "menu 4: the registered height");
            // the example block of the guide: asks only on a key (a compact toggle), never at its first draw
            Sim toggle;
            toggle.Open();
            toggle.FirstDraw();
            Expect(toggle.NextFrame() == OpenVerdict::kKeep, "toggle block, menu 1: no ask at the first draw");
            toggle.Ask(64.0);  // H pressed: compact
            Expect(toggle.fit == 64.0 && toggle.session["A.b"].height == 64.0 && !toggle.session["A.b"].reasks, "H: 64, remembered but not re-asked");
            toggle.Open();
            Expect(!toggle.seeded && !toggle.fit, "toggle block, menu 2: the registered height (its own state starts over at each open, as before)");
            // a request that is refused (above the registered height) is not remembered
            Sim tall;
            tall.Open();
            tall.FirstDraw();
            tall.Ask(300.0);
            Expect(tall.NextFrame() == OpenVerdict::kKeep && !tall.fit && !tall.session["A.b"].height, "a request of 300 (> 157): refused, nothing remembered");
            tall.Open();
            Expect(!tall.seeded, "next menu: the registered height");
        }

        void ColumnRooms()
        {
            std::printf("Context::columnRoom (ColumnRoom): screen shapes and scroll\n");
            // the default columns' area 67..623 (the ini's E2 rects); the progress card's content clip at 306 + its pad 4
            constexpr double kAreaBottom = 623.0, kContentTop = 310.0;
            struct Shape
            {
                const char* name;
                double visibleBottom;  // GetVisibleFrameRect's bottom on a 1280 x 720 stage
            };
            for (const Shape shape : { Shape{ "16:9", 720.0 }, Shape{ "16:10", 760.0 }, Shape{ "21:9", 720.0 }, Shape{ "4:3", 840.0 } }) {
                const double room = BlockRules::ColumnRoom(kContentTop, 0.0, kAreaBottom, shape.visibleBottom);
                Expect(room == 313.0, Text("%s (visible bottom %.0f): room 313 under the card's content top (the area ends at 623 first)", shape.name,
                                          shape.visibleBottom));
            }
            Expect(BlockRules::ColumnRoom(kContentTop - 100.0, 100.0, kAreaBottom, 720.0) == 313.0 &&
                       BlockRules::ColumnRoom(kContentTop - 250.0, 250.0, kAreaBottom, 720.0) == 313.0,
                "scrolled 100 or 250: the same 313 (the room does not follow the scroll)");
            Expect(BlockRules::ColumnRoom(kContentTop, 0.0, 800.0, 720.0) == 410.0, "an ini area to 800 on 16:9: cut at the visible bottom 720 (410)");
            Expect(BlockRules::ColumnRoom(700.0, 0.0, kAreaBottom, 720.0) == -77.0, "a clip starting below the area's bottom: negative room");
        }
    }

    void V3OpeningHeights()
    {
        OpeningRules();
        LayoutModeAndFill();
        Menus();
        ColumnRooms();
    }
}

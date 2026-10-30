#include "Common.h"

#include "KeyHints.h"

#include "Draw.h"
#include "Settings.h"
#include "WidgetStrings.h"

#include <array>
#include <atomic>
#include <format>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// SkyUI's source read for this (github schlangster/skyui master): ButtonPanel.as (buttons made at construction :41-45, clearButtons
// :69-78, addButton :80-93 - undefined when none is free -, updateButtons / doUpdateButtons :95-128, laying out the shown ones with
// a label), MappedButton.as (setButtonData :91-100: {text, controls}; a control's keyCode is the icon's frame :125-151),
// BottomBar.as (buttonPanel :31, positionElements :49-54), ItemMenu.as (navPanel = bottomBar.buttonPanel :73, _platform :19 set by
// SetPlatform :170-196, 0 = PC), MagicMenu.as (updateBottomBar :193-219: clearButtons, SkyUI's buttons, updateButtons(true); its
// callers :107, :157, :166, :176 and ItemMenu's setConfig :166). STB Hotkey System's BottomBarHint.cpp (github STB-Team) wraps the
// same method on the menu object and adds {text, controls: {keyCode}} buttons after it; its wrap and ours chain either way round.

namespace KeyHints
{
    namespace
    {
        constexpr const char* kMenuPath = "_root.Menu_mc";
        constexpr const char* kMethod = "updateBottomBar";
        constexpr const char* kSavedMethod = "mmx_savedUpdateBottomBar";  // the wrapped original, kept on Menu_mc (it dies with the movie)
        constexpr int kInstallFrames = 120;          // frames the wrap is tried for at a menu's start (Menu_mc is up by the first)
        constexpr std::size_t kOthersMost = 5;       // SkyUI's most buttons in the magic menu (3, MagicMenu.as:197-216) + STB's 2
        constexpr std::size_t kMaxGrown = 8;         // buttons we add to one panel at most
        constexpr int kMaxRepairs = 8;               // per menu: our buttons found gone without the wrap (logged, put back)
        constexpr int kMaxLogs = 8;                  // per menu: change and drop lines
        constexpr int kMaxSetLogs = 16;              // per session: 'SLMK' lines
        constexpr int kMaxChecks = 4;                // per menu: SelfTest L10 read-backs

        // the add-ons' hints (any thread sets them, the UI thread reads them)
        std::mutex g_hintLock;
        BlockRules::KeyHintList g_hints;

        std::atomic<bool> g_closed{ true };
        std::atomic<int> g_hookCalls{ 0 }, g_dropped{ 0 };

        // this menu's state (UI thread)
        struct MenuState
        {
            bool installed = false;
            int installLeft = kInstallFrames;
            bool broken = false;                       // the panel's members did not read: no more tries this menu
            std::vector<BlockRules::HintButton> want;  // this frame's buttons (Frame), what the wrap adds
            bool up = false;                           // our buttons are in the panel (ours, from index first)
            std::size_t first = 0;
            std::vector<BlockRules::HintButton> ours;      // the buttons in the panel (a prefix of appended when the panel was full)
            std::vector<BlockRules::HintButton> appended;  // the want they were added for (a full panel is not tried again each frame)
            std::size_t grown = 0;
            bool seen = false;                         // the wrap ran this menu; its last arguments (a bool, or none) follow
            std::optional<bool> lastArg;
            bool argPlain = true;                      // the last arguments can be passed again (a bool or none)
            bool relayout = false;                     // lay the panel out again next frame (a button made this frame loads late)
            int repairs = 0, logs = 0;
            bool checkDue = false;                     // SelfTest L10: read our buttons back next frame
            int checks = 0;
            bool gamepadLogged = false, platformWarned = false;
        };
        MenuState g_menu;

        std::optional<double> Number(const RE::GFxValue& a_object, const char* a_member)
        {
            RE::GFxValue value;
            if (!a_object.GetMember(a_member, &value) || !value.IsNumber()) return std::nullopt;
            return value.GetNumber();
        }

        std::string Label(const RE::GFxValue& a_button)
        {
            RE::GFxValue value;  // CLIK Button keeps the label in _label (its label setter, Button.as:81-83; clearButtons sets "")
            return a_button.GetMember("_label", &value) && value.IsString() && value.GetString() ? value.GetString() : std::string{};
        }

        bool Panel(RE::GFxMovieView* a_movie, RE::GFxValue& a_menu, RE::GFxValue& a_panel)
        {
            RE::GFxValue bar;
            return a_movie->GetVariable(&a_menu, kMenuPath) && a_menu.IsObject() && a_menu.GetMember("bottomBar", &bar) && bar.IsObject() &&
                   bar.GetMember("buttonPanel", &a_panel) && a_panel.IsDisplayObject();
        }

        // The panel's buttons array and how many are in use (ButtonPanel's _buttonCount); nullopt when either does not read
        std::optional<std::size_t> InUse(RE::GFxValue& a_panel, RE::GFxValue& a_buttons)
        {
            const auto count = Number(a_panel, "_buttonCount");
            if (!count || *count < 0 || !a_panel.GetMember("buttons", &a_buttons) || !a_buttons.IsArray()) return std::nullopt;
            return static_cast<std::size_t>(*count);
        }

        // SkyUI's gamepad buttons are up: ItemMenu._platform not 0 (unreadable: the keyboard, warned once a menu)
        bool Gamepad(const RE::GFxValue& a_menu)
        {
            const auto platform = Number(a_menu, "_platform");
            if (!platform && !std::exchange(g_menu.platformWarned, true)) {
                logger::warn("KeyHints: {}._platform does not read - the key hints are shown as on the keyboard", kMenuPath);
            }
            return platform && *platform != 0.0;
        }

        // One more button the way ButtonPanel's constructor makes them (ButtonPanel.as:41-45): attachMovie(buttonRenderer, "button<n>",
        // the next depth, buttonInitializer), hidden, pushed on buttons. Its CLIK init (onLoad) is run now when the engine has not
        // yet, so a label set this frame is measured; the platform as the panel's others have it (setPlatform)
        bool GrowOne(RE::GFxMovie* a_movie, RE::GFxValue& a_menu, RE::GFxValue& a_panel, RE::GFxValue& a_buttons)
        {
            RE::GFxValue renderer, initializer, taken, name, button, loaded;
            const auto length = a_buttons.GetArraySize();
            const std::string id = std::format("button{}", length);
            if (!a_panel.GetMember("buttonRenderer", &renderer) || !renderer.IsString() || (a_panel.GetMember(id.c_str(), &taken) && !taken.IsUndefined())) return false;
            a_movie->CreateString(&name, id.c_str());
            const bool init = a_panel.GetMember("buttonInitializer", &initializer) && initializer.IsObject();
            const std::array<RE::GFxValue, 4> args{ renderer, name, RE::GFxValue{ Draw::NextDepth(a_panel) }, initializer };
            if (!a_panel.Invoke("attachMovie", &button, args.data(), init ? 4 : 3) || !button.IsDisplayObject()) return false;
            button.SetMember("_visible", false);
            if (!button.GetMember("initialized", &loaded) || !loaded.IsBool() || !loaded.GetBool()) button.Invoke("onLoad");
            if (const auto platform = Number(a_menu, "_platform")) button.Invoke("setPlatform", std::array<RE::GFxValue, 1>{ RE::GFxValue{ *platform } });
            if (!a_buttons.PushBack(button)) return false;
            ++g_menu.grown;
            return true;
        }

        void Grow(RE::GFxMovie* a_movie, RE::GFxValue& a_menu, RE::GFxValue& a_panel, RE::GFxValue& a_buttons, std::size_t a_count)
        {
            std::size_t made = 0;
            while (made < a_count && GrowOne(a_movie, a_menu, a_panel, a_buttons)) ++made;
            if (made < a_count) logger::warn("KeyHints: the key-hint panel took {} of {} more button(s) (attachMovie failed)", made, a_count);
            if (made > 0) logger::info("KeyHints: the key-hint panel has {} buttons now ({} made by us this menu)", a_buttons.GetArraySize(), g_menu.grown);
        }

        // Our recorded buttons still in the panel: in use, at their indices, with their labels
        bool Intact(RE::GFxValue& a_panel)
        {
            RE::GFxValue buttons, button;
            const auto used = InUse(a_panel, buttons);
            if (!used || g_menu.first + g_menu.ours.size() > *used) return g_menu.ours.empty();
            for (std::size_t i = 0; i < g_menu.ours.size(); ++i) {
                if (!buttons.GetElement(static_cast<std::uint32_t>(g_menu.first + i), &button) || Label(button) != g_menu.ours[i].label) return false;
            }
            return true;
        }

        // g_menu.want added after the panel's buttons in use (the wrap, after SkyUI's own; or Frame), each shown or hidden as wanted,
        // the panel laid out at once (updateButtons(true), as SkyUI does)
        void Append(RE::GFxMovie* a_movie, RE::GFxValue& a_menu, RE::GFxValue& a_panel)
        {
            g_menu.up = false;
            g_menu.ours.clear();
            g_menu.appended = g_menu.want;
            RE::GFxValue buttons;
            const auto used = InUse(a_panel, buttons);
            if (!used) {
                g_menu.broken = true;
                logger::warn("KeyHints: the key-hint panel's _buttonCount / buttons do not read (not SkyUI's ButtonPanel?) - no key hints this menu");
                return;
            }
            g_menu.first = *used;
            g_menu.up = true;
            if (g_menu.want.empty()) return;
            Grow(a_movie, a_menu, a_panel, buttons, BlockRules::ButtonsToGrow(buttons.GetArraySize(), *used, g_menu.want.size(), kMaxGrown - g_menu.grown));
            for (const auto& want : g_menu.want) {
                RE::GFxValue data, controls, text, added, loaded;
                a_movie->CreateObject(&data);
                a_movie->CreateObject(&controls);
                a_movie->CreateString(&text, want.label.c_str());
                controls.SetMember("keyCode", RE::GFxValue{ static_cast<double>(want.scanCode) });
                data.SetMember("text", text);
                data.SetMember("controls", controls);
                if (!a_panel.Invoke("addButton", &added, &data, 1) || !added.IsDisplayObject()) {  // undefined: no free button
                    g_dropped += static_cast<int>(g_menu.want.size() - g_menu.ours.size());
                    if (g_menu.logs++ < kMaxLogs) {
                        logger::warn("KeyHints: the key-hint panel has no free button for {} of our {} hint(s) - dropped this time", g_menu.want.size() - g_menu.ours.size(),
                            g_menu.want.size());
                    }
                    break;
                }
                added.SetMember("_visible", want.shown);
                if (!added.GetMember("initialized", &loaded) || !loaded.IsBool() || !loaded.GetBool()) g_menu.relayout = true;
                g_menu.ours.push_back(want);
            }
            a_panel.Invoke("updateButtons", std::array<RE::GFxValue, 1>{ RE::GFxValue{ true } });
            g_menu.checkDue = !g_menu.ours.empty();
        }

        // SelfTest L10 (bSelfTest), the frame after our buttons were added or shown / hidden: each at its index with its label,
        // _visible as wanted, and every shown one laid out right of the shown buttons before it (doUpdateButtons' order)
        void Check(RE::GFxValue& a_panel)
        {
            if (!Settings::Get().selfTest || !std::exchange(g_menu.checkDue, false) || g_menu.checks >= kMaxChecks) return;
            ++g_menu.checks;
            RE::GFxValue buttons, button, visible;
            const auto used = InUse(a_panel, buttons);
            std::string why;
            double lastX = -std::numeric_limits<double>::infinity();
            int shownCount = 0;
            for (std::size_t i = 0; used && i < g_menu.first + g_menu.ours.size(); ++i) {
                if (!buttons.GetElement(static_cast<std::uint32_t>(i), &button)) {
                    why += std::format("button {} does not read; ", i);
                    continue;
                }
                const bool shown = button.GetMember("_visible", &visible) && visible.IsBool() && visible.GetBool();
                const auto x = Number(button, "_x");
                const bool ours = i >= g_menu.first;
                if (ours) {
                    const auto& want = g_menu.ours[i - g_menu.first];
                    if (Label(button) != want.label) why += std::format("button {} reads \"{}\", not \"{}\"; ", i, Label(button), want.label);
                    if (shown != want.shown) why += std::format("button {} {} (want {}); ", i, shown ? "shown" : "hidden", want.shown ? "shown" : "hidden");
                    if (shown && (!x || !(*x > lastX))) why += std::format("button {} at x {} not right of {:.1f}; ", i, x ? std::format("{:.1f}", *x) : "unread", lastX);
                    shownCount += shown ? 1 : 0;
                }
                if (shown && x) lastX = std::max(lastX, *x);
            }
            if (!used) why = "the panel's _buttonCount / buttons do not read";
            logger::info("SelfTest: L10 key hints {} - {} of ours ({} shown) from button {}{}{}", why.empty() ? "PASS" : "FAIL", g_menu.ours.size(), shownCount,
                g_menu.first, g_menu.grown ? std::format(", {} button(s) made by us", g_menu.grown) : "", why.empty() ? "" : " - " + why);
            FlushLog();
        }

        // Our buttons taken off the panel's end as clearButtons does it (hidden, no label, _x 0), _buttonCount back to g_menu.first
        void RemoveTail(RE::GFxValue& a_panel)
        {
            RE::GFxValue buttons, button, empty;
            if (!InUse(a_panel, buttons)) return;
            for (std::size_t i = 0; i < g_menu.ours.size(); ++i) {
                if (!buttons.GetElement(static_cast<std::uint32_t>(g_menu.first + i), &button)) continue;
                button.SetMember("_visible", false);
                button.SetMember("label", "");  // through CLIK's setter, as clearButtons
                button.SetMember("_x", 0.0);
            }
            a_panel.SetMember("_buttonCount", static_cast<double>(g_menu.first));
            g_menu.ours.clear();
            g_menu.up = false;
        }

        // Menu_mc.updateBottomBar wrapped: SkyUI's (and whatever it chains to) first, then our buttons after its own - unless ours
        // are still there (a method that did not clear the panel: nothing is added twice)
        class UpdateBottomBarWrap final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params& a_params) override
            {
                if (!a_params.thisPtr) return;
                RE::GFxValue original;
                if (!a_params.thisPtr->GetMember(kSavedMethod, &original) || !original.IsObject()) return;
                original.Invoke("call", a_params.retVal, a_params.argsWithThisRef, a_params.argCount + 1);
                if (g_closed || !a_params.movie || g_menu.broken) return;
                ++g_hookCalls;
                g_menu.seen = true;
                g_menu.argPlain = a_params.argCount == 0 || (a_params.argCount == 1 && (a_params.args[0].IsBool() || a_params.args[0].IsUndefined()));
                g_menu.lastArg = a_params.argCount > 0 && a_params.args[0].IsBool() ? std::optional<bool>(a_params.args[0].GetBool()) : std::nullopt;
                RE::GFxValue bar, panel;
                if (!a_params.thisPtr->GetMember("bottomBar", &bar) || !bar.IsObject() || !bar.GetMember("buttonPanel", &panel) || !panel.IsDisplayObject()) return;
                if (g_menu.up && Intact(panel)) return;
                Append(a_params.movie, *a_params.thisPtr, panel);
            }
        };
        RE::GPtr<UpdateBottomBarWrap> g_wrap;

        // The wrap on this menu's Menu_mc (an own member shadows the class method, as STB does), tried for kInstallFrames frames;
        // then the panel grown for our buttons and ours added once (SkyUI may have filled it before the wrap went on)
        void Install(RE::GFxMovieView* a_movie)
        {
            if (g_menu.installed || g_menu.installLeft <= 0) return;
            RE::GFxValue menu, current, saved, wrap;
            if (!a_movie->GetVariable(&menu, kMenuPath) || !menu.IsObject() || !menu.GetMember(kMethod, &current) || !current.IsObject()) {
                if (--g_menu.installLeft == 0) {
                    logger::warn("KeyHints: no {}.{} after {} frames - no key hints in SkyUI's strip this menu", kMenuPath, kMethod, kInstallFrames);
                }
                return;
            }
            if (!g_wrap) g_wrap = RE::make_gptr<UpdateBottomBarWrap>();
            if (!(menu.GetMember(kSavedMethod, &saved) && saved.IsObject())) {
                a_movie->CreateFunction(&wrap, g_wrap.get());
                if (!menu.SetMember(kSavedMethod, current) || !menu.SetMember(kMethod, wrap)) {
                    menu.SetMember(kMethod, current);
                    g_menu.installLeft = 0;
                    logger::warn("KeyHints: {}.{} could not be wrapped (SetMember failed) - no key hints this menu", kMenuPath, kMethod);
                    return;
                }
            }
            g_menu.installed = true;
            RE::GFxValue again, panel, buttons;
            if (!Panel(a_movie, again, panel)) {
                logger::warn("KeyHints: {}.{} wrapped, but no bottomBar.buttonPanel - no key hints this menu", kMenuPath, kMethod);
                g_menu.broken = true;
                return;
            }
            if (const auto used = InUse(panel, buttons)) {
                Grow(a_movie, menu, panel, buttons, BlockRules::ButtonsToGrow(buttons.GetArraySize(), kOthersMost, g_menu.want.size(), kMaxGrown - g_menu.grown));
                logger::info("KeyHints: {}.{} wrapped; the panel has {} buttons, {} in use; our {} hint(s) follow SkyUI's", kMenuPath, kMethod,
                    buttons.GetArraySize(), *used, g_menu.want.size());
            }
            Append(a_movie, menu, panel);
        }

        // The hint set changed while ours are up: taken off the end and added again, or - when another mod's buttons came after
        // ours - SkyUI's updateBottomBar run again with its last arguments (the wrap adds ours after it)
        void Rebuild(RE::GFxMovieView* a_movie, RE::GFxValue& a_menu, RE::GFxValue& a_panel)
        {
            RE::GFxValue buttons;
            const auto used = InUse(a_panel, buttons);
            if (used && BlockRules::OursAtTail(g_menu.first, g_menu.ours.size(), *used)) {
                RemoveTail(a_panel);
                Append(a_movie, a_menu, a_panel);
                return;
            }
            if (g_menu.seen && g_menu.argPlain) {
                std::array<RE::GFxValue, 1> args{ RE::GFxValue{ g_menu.lastArg.value_or(false) } };
                a_menu.Invoke(kMethod, nullptr, args.data(), g_menu.lastArg ? 1 : 0);
                return;
            }
            // ours are not the panel's last and SkyUI's method never ran through the wrap: nothing safe to do - left as they are
            logger::warn("KeyHints: the hint set changed, but our buttons are not the strip's last and SkyUI's updateBottomBar has not run - "
                         "the hints stay as they were this menu");
            g_menu.broken = true;
        }
    }

    void Set(std::string_view a_sender, const BlockRules::KeyHintRead& a_hint)
    {
        static std::atomic<int> s_logs{ 0 };
        BlockRules::HintSet result;
        {
            const std::scoped_lock lock(g_hintLock);
            result = BlockRules::SetKeyHint(g_hints, a_hint, BlockApi::kMaxKeyHints);
        }
        if (s_logs.fetch_add(1) >= kMaxSetLogs) return;
        switch (result) {
        case BlockRules::HintSet::kFull:
            logger::warn("KeyHints: key hint {} of {} from {} refused - the block has {} hints already", a_hint.scanCode, a_hint.id, a_sender, BlockApi::kMaxKeyHints);
            break;
        case BlockRules::HintSet::kNotThere: logger::info("KeyHints: key hint {} of {} removed by {} - it had none", a_hint.scanCode, a_hint.id, a_sender); break;
        case BlockRules::HintSet::kRemoved: logger::info("KeyHints: key hint {} of {} removed by {}", a_hint.scanCode, a_hint.id, a_sender); break;
        default:
            logger::info("KeyHints: key hint {} \"{}\" of {} {} by {}", a_hint.scanCode, a_hint.label, a_hint.id,
                result == BlockRules::HintSet::kAdded ? "added" : "relabelled", a_sender);
            break;
        }
    }

    void OnMenuOpen()
    {
        g_menu = MenuState{};
        g_hookCalls = 0;
        g_dropped = 0;
        g_closed = false;
    }

    void OnMenuClose()
    {
        if (g_closed.exchange(true)) return;
        logger::info("KeyHints: this menu - SkyUI's updateBottomBar ran {} time(s) through our wrap; {} hint button(s) dropped for want of a free button",
            g_hookCalls.load(), g_dropped.load());
    }

    void Frame(RE::GFxMovieView* a_movie, const std::vector<BlockRules::HintBlock>& a_blocks, bool a_layoutMode)
    {
        if (!a_movie || g_closed || g_menu.broken) return;
        RE::GFxValue menu, panel;
        const bool found = Panel(a_movie, menu, panel);
        const bool gamepad = found && Gamepad(menu);
        if (gamepad && !std::exchange(g_menu.gamepadLogged, true)) logger::info("KeyHints: SkyUI shows gamepad buttons - our keyboard hints are hidden");
        const auto& settings = Settings::Get();
        const BlockRules::HintButton host{ settings.keyLayout <= BlockApi::kMaxHintScanCode ? settings.keyLayout : 0u,
            WidgetText::Text(WidgetText::Str::kHintLayout), false };
        {
            const std::scoped_lock lock(g_hintLock);
            g_menu.want = BlockRules::HintButtons(host, a_blocks, g_hints, a_layoutMode, gamepad);
        }
        Install(a_movie);
        if (!g_menu.installed || g_menu.broken || !found) return;
        Check(panel);
        if (g_menu.up && !Intact(panel)) {  // gone without the wrap (a mod cleared the panel on its own): added again, a few times
            g_menu.up = false;
            if (g_menu.repairs++ >= kMaxRepairs) {
                g_menu.broken = true;
                logger::warn("KeyHints: our key hints were cleared {} times without SkyUI's updateBottomBar - left off this menu", kMaxRepairs);
                return;
            }
            logger::info("KeyHints: our key hints were cleared without SkyUI's updateBottomBar - added again");
        }
        if (!g_menu.up) return Append(a_movie, menu, panel);
        if (!BlockRules::SameHintSet(g_menu.appended, g_menu.want)) return Rebuild(a_movie, menu, panel);
        RE::GFxValue buttons, button;
        bool changed = std::exchange(g_menu.relayout, false);
        if (!InUse(panel, buttons)) return;
        for (std::size_t i = 0; i < g_menu.ours.size(); ++i) {
            const bool shown = g_menu.want[i].shown;
            if (!buttons.GetElement(static_cast<std::uint32_t>(g_menu.first + i), &button)) continue;
            RE::GFxValue visible;
            const bool now = button.GetMember("_visible", &visible) && visible.IsBool() && visible.GetBool();
            if (now == shown && g_menu.ours[i].shown == shown) continue;
            button.SetMember("_visible", shown);
            g_menu.ours[i].shown = shown;
            changed = true;
        }
        if (!changed) return;
        panel.Invoke("updateButtons", std::array<RE::GFxValue, 1>{ RE::GFxValue{ true } });
        g_menu.checkDue = true;
    }
}

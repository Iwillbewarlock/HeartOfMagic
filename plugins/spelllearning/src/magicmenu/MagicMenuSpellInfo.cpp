#include "magicmenu/MagicMenuInternal.h"
#include "magicmenu/MagicMenuDraw.h"
#include "HudNotify.h"
#include "ProgressionManager.h"
#include "SpellScanner.h"
#include "librarian/Librarian.h"
#include "uimanager/UIManager.h"

#include <cmath>
#include <format>
#include <numbers>

// =============================================================================
// MAGIC MENU BOOKMARK - the selected spell's box in the magic menu widget
// (spec 2.2, moved off the item card 2026-10-09): an XP ring (blue arc =
// learning progress with the percent inside, full gold = mastered, grey = not
// started), the spell's name and up to four of the panel card's tags.
// Tree spells only; for powers, abilities, scrolls and spells without a tree
// the box is not shown.
// =============================================================================

namespace MagicMenuBookmark::SpellInfo
{
    namespace
    {
        // Box layout in units of a 720-high screen
        constexpr double kPad = 12.0, kRingR = 18.0;  // kBoxH: MagicMenuInternal.h
        constexpr double kNameY = 8.0, kNameH = 22.0, kNameSize = 15.0;
        constexpr double kTagsY = 31.0, kTagsH = 30.0, kTagsSize = 11.0;
        constexpr double kPctTop = 0.6, kPctHeight = 1.3, kPctSize = 0.75;  // the percent inside the ring, in radii
        constexpr std::uint32_t kBoxFill = 0x0C0C0C, kBoxEdge = 0x6B5A3A;
        constexpr double kBoxAlpha = 78.0;
        constexpr std::size_t kMaxChips = 4;  // spec 2.2
        constexpr const char* kChipSeparator = " \xC2\xB7 ";  // " · " (UTF-8 middle dot), as in the 2026-10-09 mockup
        constexpr std::uint32_t kLearning = 0x4A90D9, kMastered = 0xD6B56A, kNotStarted = 0x808080;
        constexpr std::uint32_t kNameColor = 0xF0F0F0, kTagColor = 0xBBBBBB, kPctColor = 0xDDDDDD;
        constexpr int kArcSegments = 32;
        constexpr double kEdgeLine = 1.0, kRingLine = 2.0, kArcLine = 3.0;  // line widths, scaled with the screen

        // One widget at a time (one magic menu); Attach resets these
        RE::FormID g_shown = 0;  // the spell drawn now; 0 = none
        struct Layout { double x = 0, y = 0, w = 0, unit = 1; } g_layout;

        void Ring(RE::GFxValue& a_clip, double a_r, float a_progress, bool a_mastered, double a_unit)
        {
            MagicMenuDraw::LineStyle(a_clip, kRingLine * a_unit, a_mastered ? kMastered : kNotStarted);
            if (a_mastered) MagicMenuDraw::BeginFill(a_clip, kMastered, MagicMenuDraw::kAlphaFull);
            MagicMenuDraw::MoveTo(a_clip, a_r, 0);
            for (int i = 1; i <= kArcSegments; ++i) {
                const double t = 2 * std::numbers::pi * i / kArcSegments;
                MagicMenuDraw::LineTo(a_clip, a_r * std::cos(t), a_r * std::sin(t));
            }
            if (a_mastered) {
                MagicMenuDraw::EndFill(a_clip);
                return;
            }
            if (a_progress <= 0.0f) return;
            MagicMenuDraw::LineStyle(a_clip, kArcLine * a_unit, kLearning);  // the learned part, clockwise from the top
            const int steps = std::max(1, static_cast<int>(std::ceil(kArcSegments * a_progress)));
            MagicMenuDraw::MoveTo(a_clip, 0, -a_r);
            for (int i = 1; i <= steps; ++i) {
                const double t = -std::numbers::pi / 2 + 2 * std::numbers::pi * a_progress * i / steps;
                MagicMenuDraw::LineTo(a_clip, a_r * std::cos(t), a_r * std::sin(t));
            }
        }

        // The panel card's chips (SpellScannerScan.cpp builds them the same way), the first
        // kMaxChips names in the panel's language and "+N"
        std::string ChipLine(RE::SpellItem* a_spell)
        {
            auto chips = SpellScanner::BuildSpellChips(a_spell);
            Librarian::MergeCatalogChips(chips, SpellScanner::GetPersistentFormId(a_spell->GetFormID()));
            std::string line;
            std::size_t shown = 0;
            for (const auto& chip : chips) {
                if (!chip.is_string()) continue;
                if (shown == kMaxChips) {
                    line += std::format("{}+{}", kChipSeparator, chips.size() - kMaxChips);
                    break;
                }
                const auto id = chip.get<std::string>();
                line += (shown ? kChipSeparator : "") + HudNotify::Text("chips." + id, id);
                ++shown;
            }
            return line;
        }

        void Draw(RE::GFxMovieView* a_movie, RE::GFxValue& a_widget, RE::SpellItem* a_spell)
        {
            RE::GFxValue old;
            if (a_widget.GetMember("hom_info", &old) && old.IsDisplayObject()) old.Invoke("removeMovieClip");
            if (!a_spell) return;
            const auto id = a_spell->GetFormID();
            auto* progression = ProgressionManager::GetSingleton();
            // m_treeSpells changes only when the panel sends a tree, and the panel is closed while the menu is open
            if (!progression->IsTreeSpell(id)) return;

            const double u = g_layout.unit, w = g_layout.w, h = kBoxH * u;
            auto info = MagicMenuDraw::Clip(a_widget, "hom_info");
            info.SetMember("_x", g_layout.x);
            info.SetMember("_y", g_layout.y);
            MagicMenuDraw::LineStyle(info, kEdgeLine * u, kBoxEdge);
            MagicMenuDraw::BeginFill(info, kBoxFill, kBoxAlpha);
            MagicMenuDraw::MoveTo(info, 0, 0);
            MagicMenuDraw::LineTo(info, w, 0);
            MagicMenuDraw::LineTo(info, w, h);
            MagicMenuDraw::LineTo(info, 0, h);
            MagicMenuDraw::LineTo(info, 0, 0);
            MagicMenuDraw::EndFill(info);

            const double r = kRingR * u, ringX = kPad * u + r, ringY = h / 2;
            auto ring = MagicMenuDraw::Clip(info, "ring");
            ring.SetMember("_x", ringX);
            ring.SetMember("_y", ringY);
            const bool mastered = progression->IsSpellMastered(id);
            const float progress = std::clamp(progression->GetProgressPercent(id), 0.0f, 1.0f);
            Ring(ring, r, progress, mastered, u);
            if (!mastered) {
                MagicMenuDraw::Text(a_movie, info, "pct", ringX - r, ringY - r * kPctTop, 2 * r, r * kPctHeight,
                    std::format("{}", static_cast<int>(progress * 100)), kPctColor, r * kPctSize, "center");
            }
            const double textX = ringX + r + kPad * u, textW = std::max(0.0, w - textX - kPad * u);
            MagicMenuDraw::Text(a_movie, info, "name", textX, kNameY * u, textW, kNameH * u, a_spell->GetName(), kNameColor,
                kNameSize * u);
            auto tags = MagicMenuDraw::Text(a_movie, info, "tags", textX, kTagsY * u, textW, kTagsH * u, ChipLine(a_spell),
                kTagColor, kTagsSize * u);
            tags.SetMember("wordWrap", true);  // two lines when four tags do not fit on one
        }

        RE::SpellItem* SelectedSpell()
        {
            auto menu = RE::UI::GetSingleton()->GetMenu<RE::MagicMenu>();
            auto* list = menu ? menu->GetRuntimeData().itemList : nullptr;
            if (!list) return nullptr;
            if (auto* item = list->GetSelectedItem(); item && item->data.baseForm) {
                return item->data.baseForm->As<RE::SpellItem>();
            }
            // A list that keeps no selectedIndex (SkyUI's, if so): its selected entry's formId
            RE::GFxValue entry, formId;
            if (list->root.GetMember("selectedEntry", &entry) && entry.IsObject() && entry.GetMember("formId", &formId) &&
                formId.IsNumber()) {
                return RE::TESForm::LookupByID<RE::SpellItem>(static_cast<RE::FormID>(formId.GetNumber()));
            }
            return nullptr;
        }

        // Simplification: one native call per frame while the magic menu is open (the game is paused
        // there), comparing one FormID. Upgrade path if it ever shows in a profile: a write_vfunc on
        // MagicMenu::PostDisplay (slot 6), the pattern SpellCastImmediateHook.cpp:56 uses.
        class Watch final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params& a_params) override
            {
                RE::GFxValue widget;
                auto* movie = OpenMagicMenuMovie();
                if (!movie || !a_params.thisPtr || !a_params.thisPtr->GetMember("_parent", &widget)) return;
                auto* spell = SelectedSpell();
                const RE::FormID id = spell ? spell->GetFormID() : 0;
                if (id == g_shown) return;
                logger::debug("MagicMenuBookmark: selected {} ({:08X})", spell ? spell->GetName() : "(none)", id);
                g_shown = id;
                Draw(movie, widget, spell);
            }
        };
        RE::GPtr<Watch> g_watch;
    }

    void Attach(RE::GFxMovieView* a_movie, RE::GFxValue& a_parent, double a_x, double a_y, double a_width, double a_unit)
    {
        if (!UIManager::MagicMenuSpellInfo()) return;
        if (!g_watch) g_watch = RE::make_gptr<Watch>();
        g_shown = 0;  // a new movie: draw the first selection
        g_layout = { a_x, a_y, a_width, a_unit };
        auto watch = MagicMenuDraw::Clip(a_parent, "hom_watch");
        if (!watch.IsDisplayObject()) return;
        RE::GFxValue fn;
        a_movie->CreateFunction(&fn, g_watch.get());
        watch.SetMember("onEnterFrame", fn);
        logger::info("MagicMenuBookmark: spell box attached");
    }
}

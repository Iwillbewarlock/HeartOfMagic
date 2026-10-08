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
// MAGIC MENU BOOKMARK - the selected spell's XP ring and tag chips (spec 2.2):
// a ring by the spell name (blue arc = learning progress, full gold = mastered,
// grey = not started) and up to four of the panel card's chips under it.
// Tree spells only; powers, abilities, scrolls and spells without a tree keep
// the card as it is.
// =============================================================================

namespace MagicMenuBookmark::SpellInfo
{
    namespace
    {
        constexpr std::size_t kMaxChips = 4;  // spec 2.2
        constexpr double kRingScale = 1.6;    // ring diameter / title height
        constexpr double kMaxRingRadius = 16.0;  // a "title" field that is really a tall text box keeps a small ring
        constexpr double kPctTop = 0.6, kPctHeight = 1.3, kPctSize = 0.75;  // the percent inside the ring, in radii
        constexpr double kChipsHeight = 0.9, kChipsSize = 0.55;             // the chip line, in name text heights
        constexpr double kGap = 6.0;
        constexpr const char* kChipSeparator = " \xC2\xB7 ";  // " · " (UTF-8 middle dot), as in the 2026-10-09 mockup
        constexpr double kTextGutter = 2.0;  // a text field draws its text this far inside its box
        constexpr double kFallbackTitleX = 12.0, kFallbackTitleY = 8.0, kFallbackTitleH = 24.0;  // no title field found
        constexpr std::uint32_t kLearning = 0x4A90D9, kMastered = 0xD6B56A, kNotStarted = 0x808080, kText = 0xDDDDDD;
        constexpr int kArcSegments = 32;

        RE::FormID g_shown = 0;    // the spell drawn now; 0 = none
        std::string g_shownTitle;  // and the name text and place it was drawn against
        bool g_layoutLogged = false;  // the first drawing of each menu open logs where things went

        double Number(RE::GFxValue& a_clip, const char* a_member)
        {
            RE::GFxValue value;
            return a_clip.GetMember(a_member, &value) && value.IsNumber() ? value.GetNumber() : 0.0;
        }

        // Where the spell name's letters are (not its field: SkyUI's is a wide box with centred text), in
        // the card's coordinates, and a signature that changes when the menu rewrites or moves the name
        struct Title
        {
            double left, top, width, height;
            std::string signature;
        };
        Title FindTitle(RE::GFxValue& a_card)
        {
            for (const auto& name : TitleFields()) {
                RE::GFxValue field;
                if (!a_card.GetMember(name.c_str(), &field) || !field.IsDisplayObject()) continue;
                const double x = Number(field, "_x"), y = Number(field, "_y"), w = Number(field, "_width");
                const double sx = Number(field, "_xscale") / 100.0, sy = Number(field, "_yscale") / 100.0;
                const double tw = Number(field, "textWidth") * sx, th = Number(field, "textHeight") * sy;
                std::string align = "left";
                RE::GFxValue format, value, text;
                if (field.Invoke("getTextFormat", &format) && format.IsObject() && format.GetMember("align", &value) &&
                    value.IsString()) {
                    align = value.GetString();
                }
                const double left = align == "center" ? x + (w - tw) / 2
                                  : align == "right"  ? x + w - tw - kTextGutter * sx
                                                      : x + kTextGutter * sx;
                field.GetMember("text", &text);
                return { left, y + kTextGutter * sy, tw, th,
                         std::format("{}|{:.0f}|{:.0f}", text.IsString() ? text.GetString() : "", y, tw) };
            }
            const auto box = Bounds(a_card);
            return { box.xMin + kFallbackTitleX, box.yMin + kFallbackTitleY, 0.0, kFallbackTitleH, "" };
        }

        void Ring(RE::GFxValue& a_clip, double a_r, float a_progress, bool a_mastered)
        {
            MagicMenuDraw::LineStyle(a_clip, 2.0, a_mastered ? kMastered : kNotStarted);
            if (a_mastered) MagicMenuDraw::BeginFill(a_clip, kMastered, 100.0);
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
            MagicMenuDraw::LineStyle(a_clip, 3.0, kLearning);  // the learned part, clockwise from the top
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

        void Draw(RE::GFxMovieView* a_movie, RE::GFxValue& a_card, RE::GFxValue& a_overlay, RE::SpellItem* a_spell)
        {
            RE::GFxValue old;
            if (a_overlay.GetMember("hom_info", &old) && old.IsDisplayObject()) old.Invoke("removeMovieClip");
            if (!a_spell) return;
            const auto id = a_spell->GetFormID();
            auto* progression = ProgressionManager::GetSingleton();
            // m_treeSpells changes only when the panel sends a tree, and the panel is closed while the menu is open
            if (!progression->IsTreeSpell(id)) return;

            const auto title = FindTitle(a_card);
            const double r = std::min(title.height * kRingScale / 2, kMaxRingRadius);
            const auto box = Bounds(a_card);
            const double bookmarkLeft = Tab::LeftEdge(box);
            auto info = MagicMenuDraw::Clip(a_overlay, "hom_info");
            auto ring = MagicMenuDraw::Clip(info, "ring");
            // Just left of the name's first letter, on its middle; never past the card's left edge
            const double ringX = std::max(title.left - kGap - r, box.xMin + r);
            const double ringY = title.top + title.height / 2;
            if (!g_layoutLogged) {
                g_layoutLogged = true;
                logger::info("MagicMenuBookmark: layout - box x {:.0f}..{:.0f} y {:.0f}..{:.0f}, name letters x {:.0f} w {:.0f} y {:.0f} h {:.0f}, bookmark from x {:.0f}",
                    box.xMin, box.xMax, box.yMin, box.yMax, title.left, title.width, title.top, title.height, bookmarkLeft);
            }
            ring.SetMember("_x", ringX);
            ring.SetMember("_y", ringY);
            const bool mastered = progression->IsSpellMastered(id);
            const float progress = std::clamp(progression->GetProgressPercent(id), 0.0f, 1.0f);
            Ring(ring, r, progress, mastered);
            if (!mastered) {
                MagicMenuDraw::Text(a_movie, info, "pct", ringX - r, ringY - r * kPctTop, 2 * r, r * kPctHeight,
                    std::format("{}", static_cast<int>(progress * 100)), kText, r * kPctSize, "center");
            }
            // Above the name, centred on it (the description starts right under the name), clear of the bookmark
            const double centre = title.left + title.width / 2;
            const double half = std::max(0.0, std::min(centre - box.xMin, bookmarkLeft - kGap - centre));
            const double chipsH = title.height * kChipsHeight;
            const double chipsY = std::max(box.yMin + kTextGutter, title.top - chipsH - kTextGutter);
            MagicMenuDraw::Text(a_movie, info, "chips", centre - half, chipsY, 2 * half, chipsH, ChipLine(a_spell), kText,
                title.height * kChipsSize, "center");
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
                RE::GFxValue overlay;
                auto* movie = OpenMagicMenuMovie();
                auto card = movie ? CurrentCard(movie) : std::nullopt;
                if (!card || !a_params.thisPtr || !a_params.thisPtr->GetMember("_parent", &overlay)) return;
                auto* spell = SelectedSpell();
                const RE::FormID id = spell ? spell->GetFormID() : 0;
                // The menu may rewrite or move the name a frame after the selection moves: redraw on either
                auto title = FindTitle(*card).signature;
                if (id == g_shown && title == g_shownTitle) return;
                if (id != g_shown) logger::debug("MagicMenuBookmark: selected {} ({:08X})", spell ? spell->GetName() : "(none)", id);
                g_shown = id;
                g_shownTitle = std::move(title);
                Draw(movie, *card, overlay, spell);
            }
        };
        RE::GPtr<Watch> g_watch;
    }

    void Attach(RE::GFxMovieView* a_movie, RE::GFxValue& a_overlay, RE::GFxValue& a_card)
    {
        if (!UIManager::MagicMenuSpellInfo()) return;
        if (!g_watch) g_watch = RE::make_gptr<Watch>();
        g_shown = 0;  // a new movie: draw the first selection
        g_shownTitle.clear();
        g_layoutLogged = false;
        auto watch = MagicMenuDraw::Clip(a_overlay, "hom_watch");
        if (!watch.IsDisplayObject()) return;
        RE::GFxValue fn;
        a_movie->CreateFunction(&fn, g_watch.get());
        watch.SetMember("onEnterFrame", fn);
        std::string titleField = "none - ring at the top left";
        for (const auto& name : TitleFields()) {
            RE::GFxValue field;
            if (a_card.GetMember(name.c_str(), &field) && field.IsDisplayObject()) {
                titleField = name;
                break;
            }
        }
        logger::info("MagicMenuBookmark: spell info attached (title field: {})", titleField);
    }
}

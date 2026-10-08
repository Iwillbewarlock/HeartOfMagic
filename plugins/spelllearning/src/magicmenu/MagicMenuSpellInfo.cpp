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
        constexpr double kGap = 6.0;
        constexpr double kBookmarkShare = 96.0 / 600.0;  // the bookmark and its inset, as a share of the card width
        constexpr double kFallbackTitleX = 12.0, kFallbackTitleY = 8.0, kFallbackTitleH = 24.0;  // no title field found
        constexpr std::uint32_t kLearning = 0x4A90D9, kMastered = 0xD6B56A, kNotStarted = 0x808080, kText = 0xDDDDDD;
        constexpr int kArcSegments = 32;

        RE::FormID g_shown = 0;  // the spell drawn now; 0 = none

        double Number(RE::GFxValue& a_clip, const char* a_member)
        {
            RE::GFxValue value;
            return a_clip.GetMember(a_member, &value) && value.IsNumber() ? value.GetNumber() : 0.0;
        }

        // Where the spell name is: x, y, height, text width. The first title field that exists,
        // else a top-left guess (logged once per menu)
        struct Title { double x, y, h, textWidth; };
        Title FindTitle(RE::GFxValue& a_card)
        {
            for (const auto& name : TitleFields()) {
                RE::GFxValue field;
                if (a_card.GetMember(name.c_str(), &field) && field.IsDisplayObject()) {
                    return { Number(field, "_x"), Number(field, "_y"), Number(field, "_height"), Number(field, "textWidth") };
                }
            }
            return { kFallbackTitleX, kFallbackTitleY, kFallbackTitleH, 0.0 };
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
                    line += std::format("   +{}", chips.size() - kMaxChips);
                    break;
                }
                const auto id = chip.get<std::string>();
                line += (shown ? "   " : "") + HudNotify::Text("chips." + id, id);
                ++shown;
            }
            return line;
        }

        void Draw(RE::GFxMovieView* a_movie, RE::GFxValue& a_card, RE::SpellItem* a_spell)
        {
            RE::GFxValue old;
            if (a_card.GetMember("hom_info", &old) && old.IsDisplayObject()) old.Invoke("removeMovieClip");
            if (!a_spell) return;
            const auto id = a_spell->GetFormID();
            auto* progression = ProgressionManager::GetSingleton();
            // m_treeSpells changes only when the panel sends a tree, and the panel is closed while the menu is open
            if (!progression->IsTreeSpell(id)) return;

            const auto title = FindTitle(a_card);
            const double r = title.h * kRingScale / 2;
            auto info = MagicMenuDraw::Clip(a_card, "hom_info");
            auto ring = MagicMenuDraw::Clip(info, "ring");
            // Left of the name; right of it when the name starts at the card's edge
            const double ringX = title.x - r - kGap >= r ? title.x - r - kGap : title.x + title.textWidth + kGap + r;
            const double ringY = title.y + title.h / 2;
            ring.SetMember("_x", ringX);
            ring.SetMember("_y", ringY);
            const bool mastered = progression->IsSpellMastered(id);
            const float progress = std::clamp(progression->GetProgressPercent(id), 0.0f, 1.0f);
            Ring(ring, r, progress, mastered);
            if (!mastered) {
                MagicMenuDraw::Text(a_movie, info, "pct", ringX - r, ringY - r * 0.6, 2 * r, r * 1.3,
                    std::format("{}", static_cast<int>(progress * 100)), kText, r * 0.75, "center");
            }
            // Up to the bookmark's left edge (MagicMenuTab.cpp: 96 of a 600-wide card from the right)
            const double cardWidth = Number(a_card, "_width");
            const double chipsWidth = std::max(0.0, cardWidth - title.x - cardWidth * kBookmarkShare - kGap);
            MagicMenuDraw::Text(a_movie, info, "chips", title.x, title.y + title.h, chipsWidth, title.h * 0.8,
                ChipLine(a_spell), kText, title.h * 0.5);
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
                auto* spell = SelectedSpell();
                const RE::FormID id = spell ? spell->GetFormID() : 0;
                if (id == g_shown) return;
                g_shown = id;
                logger::info("MagicMenuBookmark: selected {} ({:08X})", spell ? spell->GetName() : "(none)", id);
                RE::GFxValue card;
                auto* movie = OpenMagicMenuMovie();
                if (movie && a_params.thisPtr && a_params.thisPtr->GetMember("_parent", &card)) Draw(movie, card, spell);
            }
        };
        RE::GPtr<Watch> g_watch;
    }

    void Attach(RE::GFxMovieView* a_movie, RE::GFxValue& a_card)
    {
        if (!UIManager::MagicMenuSpellInfo()) return;
        if (!g_watch) g_watch = RE::make_gptr<Watch>();
        g_shown = 0;  // a new movie: draw the first selection
        auto watch = MagicMenuDraw::Clip(a_card, "hom_watch");
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

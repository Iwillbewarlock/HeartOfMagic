#include "Common.h"

#include "AddonState.h"

#include "HiddenBlocks.h"
#include "HomSettings.h"
#include "LangGame.h"
#include "Selection.h"
#include "SpellSchool.h"
#include "TreeMarks.h"

#include <algorithm>
#include <iterator>

namespace HomAddon
{
    namespace
    {
        Session g_session;
        MenuState g_menu;
        std::atomic<std::uint64_t> g_openGeneration{ 0 };
        std::atomic<bool> g_buttonPressed{ false };
        // fix round 1 I1: [Blocks] sHidden hides the tree and the node card this menu - the progress card's frame is the only one left
        // to do the shared work, so it never hides itself (Selection::ProgressHides)
        std::atomic<bool> g_progressAlone{ false };

        // The host's [Blocks] sHidden read the same way (the player's MagicMenuRewrite_User.ini first): both other blocks named, by their
        // final or their retired id
        bool ProgressAlone()
        {
            const auto ids = HiddenBlocks::Parse(LangGame::IniString(HiddenBlocks::kSection, HiddenBlocks::kKey, static_cast<std::uint32_t>(HiddenBlocks::kMaxChars)));
            const auto hidden = [&](BlockKind a_kind) {
                const auto index = Index(a_kind);
                return std::ranges::find(ids, kBlockIds[index]) != ids.end() || std::ranges::find(ids, kLegacyIds[index]) != ids.end();
            };
            return hidden(BlockKind::kTree) && hidden(BlockKind::kNodeCard);
        }

        // A string game setting, empty when there is none
        std::string GameString(const char* a_setting)
        {
            auto* settings = RE::GameSettingCollection::GetSingleton();
            auto* setting = settings ? settings->GetSetting(a_setting) : nullptr;
            const char* text = setting && setting->GetType() == RE::Setting::Type::kString ? setting->GetString() : nullptr;
            return text ? text : "";
        }

        void SelfTestLine(const std::string& a_line)
        {
            SelfTest(a_line);
        }
    }

    Session& TheSession()
    {
        return g_session;
    }

    MenuState& Menu()
    {
        return g_menu;
    }

    void OnMenuOpen()
    {
        g_buttonPressed = false;
        g_progressAlone = ProgressAlone();
        ++g_openGeneration;
        ReadHomStyle();
    }

    void EnsureMenu()
    {
        const auto generation = g_openGeneration.load();
        if (g_menu.generation == generation) return;
        g_menu = MenuState{};
        g_menu.generation = generation;
    }

    void PressButton()
    {
        g_buttonPressed = true;
    }

    bool TakeButton()
    {
        return g_buttonPressed.exchange(false);
    }

    const Spells::Data& Data()
    {
        return *Spells::Get();
    }

    std::string CentreSchool()
    {
        return Spells::Get() && g_session.centre >= 0 ? Data().tree.nodes[g_session.centre].school : std::string{};
    }

    // A node can be a window's centre only with a place in the tree (TreeData::Query shows nothing around a node without x/y):
    // one without counts as "not in the tree" for the centre rule
    bool Centrable(int a_index)
    {
        return Spells::Get() && a_index >= 0 && Data().tree.nodes[a_index].hasXY;
    }

    int DefaultCentre(const std::string& a_school, const Spells::Snapshot& a_snapshot)
    {
        if (a_school.empty()) return -1;
        const int target = Spells::FindNode(a_snapshot.Target(a_school), a_school);
        if (Centrable(target)) return target;
        const auto* school = Spells::SchoolNamed(a_school);
        return school && Centrable(school->rootIndex) ? school->rootIndex : -1;
    }

    // The active school's numbers for this snapshot (worked out again when either changes). The candidates are what the
    // next-learnable key walks and what the cards count (Q2: one set): learnable, non-dup, with a place in the tree.
    const SchoolInfo& SchoolInfoOf(const Spells::Snapshot& a_snapshot)
    {
        using State = Spells::State;
        auto& info = g_menu.schoolInfo;
        if (info.valid && info.school == g_session.school && info.epoch == a_snapshot.epoch) return info;
        info = SchoolInfo{ g_session.school, a_snapshot.epoch, {}, {}, true };
        if (!Spells::Get()) return info;
        const auto learnable = Spells::Learnable(a_snapshot, g_session.school);
        std::ranges::copy_if(learnable, std::back_inserter(info.candidates), Centrable);
        auto& t = info.tally;
        t.available = static_cast<int>(learnable.size());
        t.candidates = static_cast<int>(info.candidates.size());
        const auto* school = Spells::SchoolNamed(g_session.school);
        if (!school) return info;
        for (const int index : school->nodes) {
            if (Spells::IsDup(Data().tree.nodes[index])) continue;
            ++t.nodes;
            switch (Spells::LookOf(a_snapshot, index).state) {
            case State::kTarget: ++t.target; break;
            case State::kReady: ++t.ready; break;
            case State::kLearning: ++t.learning; break;
            case State::kMastered: ++t.mastered; break;
            case State::kLocked: ++t.locked; break;
            case State::kAvailable: break;  // counted by Spells::Learnable above
            default: ++t.unknown; break;
            }
        }
        return info;
    }

    // A centre the user chose (list selection, tree click, the next-learnable key)
    void SetCentre(int a_index)
    {
        g_session.provisionalSchool.clear();
        if (a_index == g_session.centre) return;
        g_session.centre = a_index;
        g_menu.refresh = true;
        g_menu.dirty = true;
    }

    // The school's default centre; provisional while the snapshot does not cover the school yet
    void SetDefaultCentre(const std::string& a_school, const Spells::Snapshot& a_snapshot)
    {
        SetCentre(DefaultCentre(a_school, a_snapshot));
        if (!a_snapshot.Covers(a_school)) g_session.provisionalSchool = a_school;
    }

    // A snapshot that covers the provisional centre's school arrived: pick that school's default centre again
    void SettleProvisionalCentre(const Spells::Snapshot& a_snapshot)
    {
        const auto school = g_session.provisionalSchool;
        if (school.empty() || !a_snapshot.Covers(school)) return;
        SetCentre(DefaultCentre(school, a_snapshot));  // clears the provisional mark
        logger::info("HoM block: provisional centre of {} settled on {} (snapshot {})", school,
            g_session.centre >= 0 ? Spells::NodeName(g_session.centre) : std::string("(none)"), a_snapshot.epoch);
    }

    void SetSchool(const std::string& a_school)
    {
        if (a_school.empty() || a_school == g_session.school) return;
        g_session.school = a_school;
        g_menu.refresh = true;
        g_menu.dirty = true;
    }

    int SettleShown(RE::FormID a_shown, const Spells::Snapshot& a_snapshot, bool a_fromDraw)
    {
        const auto pick = Selection::PickForShown({ g_menu.selected, g_menu.selectedByKey }, g_menu.pickShown, a_shown);
        g_menu.selected = pick.node;
        g_menu.selectedByKey = pick.byKey;
        g_menu.settledShown = a_shown;
        if (a_fromDraw && !Selection::DrawSettlesView(pick)) return pick.node;  // re-review B: the clicked node keeps the tree
        auto* form = a_shown ? RE::TESForm::LookupByID(a_shown) : nullptr;
        auto* spell = form ? form->As<RE::SpellItem>() : nullptr;
        const RE::FormID id = spell ? spell->GetFormID() : 0;
        const bool hasTree = Spells::Get() != nullptr;
        const RE::FormID pickedForm = hasTree && g_menu.selected >= 0 ? Data().gameIds[g_menu.selected] : 0;
        const int node = Selection::CentreNodeFor(g_menu.selected, pickedForm, id, Spells::PrimaryNode(id));
        const std::string nodeSchool = hasTree && node >= 0 ? Data().tree.nodes[node].school : std::string{};
        SetSchool(std::string(Selection::SchoolFor(g_menu.tabSchool, node, nodeSchool, SpellSchool::Of(spell))));
        g_menu.notInTree = id && node < 0;
        if (Centrable(node)) {
            SetCentre(node);
        } else if (g_session.centre < 0 || (!g_menu.tabSchool.empty() && !g_session.school.empty() && CentreSchool() != g_session.school)) {
            SetDefaultCentre(g_session.school, a_snapshot);
        }
        return node;
    }

    void SettleShownForDraw(RE::FormID a_shown)
    {
        if (!Selection::SettleDue(g_menu.settledShown, a_shown)) return;
        const bool dirty = g_menu.dirty;
        SettleShown(a_shown, *Spells::Current(), true);
        g_menu.dirty = dirty;
    }

    int KeyPick()
    {
        return g_menu.selectedByKey ? g_menu.selected : -1;
    }

    int KeyPickFor(RE::FormID a_shown)
    {
        return Selection::StaleKeyPick(g_menu.selectedByKey, g_menu.pickShown, a_shown) ? -1 : KeyPick();
    }

    int SelectedFor(RE::FormID a_shown)
    {
        return Selection::StaleKeyPick(g_menu.selectedByKey, g_menu.pickShown, a_shown) ? -1 : g_menu.selected;
    }

    bool Chosen(RE::FormID a_shown)
    {
        return Selection::Chosen(a_shown, KeyPickFor(a_shown));
    }

    // Only a change of the wanted state is sent (none twice in a row); the host takes one sent from the card's draw before that
    // frame is shown (fix round 1 I1)
    void SyncProgressShown()
    {
        const bool want = !Selection::ProgressHides(!g_session.school.empty(), g_progressAlone);
        if (g_menu.progressShown.value_or(true) != want) {
            RequestVisible(BlockKind::kProgress, want);
            logger::info("HoM block: the progress card {} ({})", want ? "shown again" : "hidden", want ? "a school" : "no school yet");
        }
        g_menu.progressShown = want;
    }

    TextFit::Metrics Metrics()
    {
        return g_menu.metrics;
    }

    double Size(double a_base)
    {
        return TextFit::Size(g_menu.metrics, a_base);
    }

    double FieldH(double a_size, int a_lines)
    {
        return TextFit::FieldH(g_menu.metrics, a_size, a_lines);
    }

    double LineH(double a_size)
    {
        return TextFit::LineH(g_menu.metrics, a_size);
    }

    // the vanilla GMSTs LevelText reads; the Adept tier is sJourneymanDisplayName
    std::string LevelName(const std::string& a_level)
    {
        constexpr std::array<std::pair<std::string_view, const char*>, 5> kLevels{ { { "Novice", "sNoviceDisplayName" },
            { "Apprentice", "sApprenticeDisplayName" }, { "Adept", "sJourneymanDisplayName" }, { "Expert", "sExpertDisplayName" },
            { "Master", "sMasterDisplayName" } } };
        for (const auto& [name, setting] : kLevels) {
            if (name != a_level) continue;
            auto text = GameString(setting);
            return text.empty() ? a_level : text;
        }
        return a_level;
    }

    void SelfTest(const std::string& a_line)
    {
        if (!HomSettings::Get().selfTest) return;
        logger::info("SelfTest: {}", a_line);
        if (a_line.find("PASS") != std::string::npos || a_line.find("FAIL") != std::string::npos || a_line.find("WARN") != std::string::npos) {
            FlushLog();
        }
    }

    void Q2State(std::string_view a_name, const std::string& a_intended, const RE::GFxValue* a_field)
    {
        TextFit::Q2State(g_menu.textLog, SelfTestLine, a_name, a_intended, a_field);
    }

    void Q2State(HomStrings::Str a_id, const std::string& a_intended, const RE::GFxValue* a_field)
    {
        Q2State(HomStrings::Key(a_id), a_intended, a_field);
    }

    std::optional<double> Number(const RE::GFxValue& a_object, const char* a_member)
    {
        return TextFit::Number(a_object, a_member);
    }

    RE::GFxValue Child(RE::GFxValue& a_parent, const char* a_name)
    {
        RE::GFxValue child;
        if (!a_parent.GetMember(a_name, &child) || !child.IsDisplayObject()) return {};
        return child;
    }

    void SetHandler(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, const char* a_event, RE::GFxFunctionHandler* a_handler)
    {
        RE::GFxValue fn;
        a_movie->CreateFunction(&fn, a_handler);
        a_clip.SetMember(a_event, fn);
    }

    std::optional<std::pair<double, double>> MapPoint(RE::GFxMovieView* a_movie, RE::GFxValue& a_from, RE::GFxValue& a_to, double a_x, double a_y)
    {
        RE::GFxValue point;
        a_movie->CreateObject(&point);
        point.SetMember("x", a_x);
        point.SetMember("y", a_y);
        const std::array<RE::GFxValue, 1> args{ point };
        if (!a_from.Invoke("localToGlobal", args) || !a_to.Invoke("globalToLocal", args)) return std::nullopt;
        const auto x = Number(point, "x"), y = Number(point, "y");
        if (!x || !y) return std::nullopt;
        return std::pair{ *x, *y };
    }

    RE::GFxValue CardClip(RE::GFxValue& a_content)
    {
        auto card = Draw::Clip(a_content, "card");
        if (!card.IsDisplayObject()) return card;
        card.SetMember("_x", kPad - kCardPad);
        card.SetMember("_y", -kCardPad);
        return card;
    }

    void FitCard(BlockKind a_kind, double a_needed)
    {
        const double height = LayoutMath::CardHeight(a_needed, RegisteredHeight(a_kind));
        if (g_menu.heights[Index(a_kind)].ShouldSend(height)) RequestHeight(a_kind, height);
    }

    void MarkDirty()
    {
        g_menu.dirty = true;
    }
}

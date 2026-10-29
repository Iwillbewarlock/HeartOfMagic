#pragma once

#include "ArchetypeIcons.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// =============================================================================
// SR BLOCK RULES - the pure parts of the Spell Research add-on block (Spell
// Research add-on design 2026-10-27, "Changes / 2"): which archetypes the block
// shows and in what order, what it says when it shows none, the list and line
// layouts and their wrapping to the column width, the icon subpaths drawn for a
// known and an unknown archetype, the link hit test, the reply code ->
// notification, the open request bookkeeping and the [MagicMenuBlock] sLayout
// reading of SpellResearchPrismaUI.ini. No game types: the plugin feeds it the
// values it reads; tools/magic-menu-sr-block-check runs these same functions on
// made-up cases. Stage units, y down, (0,0) = the content clip's top-left.
// =============================================================================

namespace SrBlock
{
    // ---------------------------------------------------------------------
    // Spell Research's archetypes (SpellResearch.esp _SR_ListSpellsArchetypes order = _SRU_Utility.GetArchetypeString =
    // SR Prisma's ARCHETYPES): 0-1 casting, 2-35 elements, 36-40 schools, 41-44 targeting, 45-59 techniques
    // ---------------------------------------------------------------------
    inline constexpr std::size_t kArchetypeCount = 60;
    inline constexpr std::size_t kTierCount = 5;  // _SR_ListAllSpells and each globals sub-list: [0] Novice .. [4] Master
    inline constexpr int kFirstElement = 2, kLastElement = 35, kFirstTargeting = 41, kLastTargeting = 44, kFirstTechnique = 45,
                         kLastTechnique = 59;
    static_assert(ArchetypeIcons::kIconCount == kArchetypeCount);

    /// The block's three groups (the card shows school and casting type already: those two groups are left out)
    enum class Group
    {
        kNone,
        kElement,
        kTargeting,
        kTechnique,
    };
    inline constexpr std::array<Group, 3> kShownGroups{ Group::kElement, Group::kTargeting, Group::kTechnique };

    constexpr Group GroupOf(int a_archetype)
    {
        if (a_archetype >= kFirstElement && a_archetype <= kLastElement) return Group::kElement;
        if (a_archetype >= kFirstTargeting && a_archetype <= kLastTargeting) return Group::kTargeting;
        if (a_archetype >= kFirstTechnique && a_archetype <= kLastTechnique) return Group::kTechnique;
        return Group::kNone;
    }

    /// The group's label key in the language file
    constexpr const char* GroupKey(Group a_group)
    {
        switch (a_group) {
        case Group::kElement: return "group.element";
        case Group::kTargeting: return "group.targeting";
        case Group::kTechnique: return "group.technique";
        default: return "";
        }
    }

    using ArchetypeBits = std::bitset<kArchetypeCount>;
    /// _SR_ListGlobalsArchetypes' values: [archetype][tier]
    using Knowledge = std::array<std::array<float, kTierCount>, kArchetypeCount>;

    /// One spell as SR files it: its tier (-1 = in no tier list) and the archetype lists it is in
    struct SpellEntry
    {
        int tier = -1;
        ArchetypeBits archetypes;
    };

    /// One archetype the block shows: known = the player has any knowledge of it at the spell's tier (global > 0)
    struct Item
    {
        int archetype = 0;
        bool known = false;
    };

    /// The shown archetypes of a spell, in SR's index order (elements, then targeting, then techniques)
    inline std::vector<Item> Items(const SpellEntry& a_entry, const Knowledge& a_knowledge)
    {
        std::vector<Item> items;
        for (int i = 0; i < static_cast<int>(kArchetypeCount); ++i) {
            if (!a_entry.archetypes.test(static_cast<std::size_t>(i)) || GroupOf(i) == Group::kNone) continue;
            const bool tierKnown = a_entry.tier >= 0 && a_entry.tier < static_cast<int>(kTierCount);
            items.push_back({ i, tierKnown && a_knowledge[static_cast<std::size_t>(i)][static_cast<std::size_t>(a_entry.tier)] > 0.0f });
        }
        return items;
    }

    // ---------------------------------------------------------------------
    // What the block shows for the card's form
    // ---------------------------------------------------------------------

    /// What draw knows about the card's form and SR's data
    struct View
    {
        std::uint32_t shown = 0;          // the card's form (0 = none)
        bool haveSnapshot = false;        // a snapshot was built this menu (it is built when the menu opens)
        bool ready = false;               // SR finished setting up and is not importing
        bool listsEmpty = false;          // SR's archetype lists hold no spell (its Import never ran)
        bool isSpell = false;             // the form is a SpellItem of the spell type (not a power, shout or effect)
        const SpellEntry* entry = nullptr;  // the spell in SR's lists, null when SR does not file it
        const Knowledge* knowledge = nullptr;
    };

    enum class Show
    {
        kNothing,  // no form on the card (the host hides the columns then)
        kReason,   // a "?" and the reason line
        kItems,    // the archetypes (and a reason line when some are "?")
    };

    struct Decision
    {
        Show show = Show::kNothing;
        const char* reasonKey = "";  // "" = no reason line
        std::vector<Item> items;
    };

    inline Decision Decide(const View& a_view)
    {
        if (a_view.shown == 0) return {};
        if (!a_view.haveSnapshot || !a_view.ready) return { Show::kReason, "reason.notReady", {} };
        if (!a_view.isSpell) return { Show::kReason, "reason.notSpell", {} };
        if (a_view.listsEmpty) return { Show::kReason, "reason.listsEmpty", {} };
        if (!a_view.entry || !a_view.knowledge) return { Show::kReason, "reason.unknownSpell", {} };
        auto items = Items(*a_view.entry, *a_view.knowledge);
        if (items.empty()) return { Show::kReason, "reason.noArchetypes", {} };
        const bool anyUnknown = std::ranges::any_of(items, [](const Item& a_item) { return !a_item.known; });
        const char* reason = a_view.entry->tier < 0 ? "reason.noTier" : anyUnknown ? "reason.unknownArchetype" : "";
        return { Show::kItems, reason, std::move(items) };
    }

    // ---------------------------------------------------------------------
    // Fixed block height (the same for both layouts): the host's title strip and padding (SL_MagicMenu Settings.h
    // kPanelTitleH 22, kPanelPad 8; Widget.cpp DrawAddons lends a clip from kTitleH + kPad down to the bottom padding) plus
    // three archetype lines, the link row and the reason line
    // ---------------------------------------------------------------------
    inline constexpr double kHostTitleH = 22.0, kHostPad = 8.0;
    inline constexpr double kRowH = 18.0;  // one archetype line (the mock's 18-unit rows)
    inline constexpr int kArchetypeLines = 3;
    inline constexpr double kLinksGap = 6.0, kLinkRowH = 18.0, kReasonH = 16.0;
    inline constexpr double kLinksTop = kRowH * kArchetypeLines + kLinksGap;
    inline constexpr double kReasonTop = kLinksTop + kLinkRowH;
    inline constexpr double kContentH = kReasonTop + kReasonH;
    inline constexpr double kBlockHeight = kContentH + kHostTitleH + 2 * kHostPad;

    // ---------------------------------------------------------------------
    // Layout: list (one line per group, its label on the left) and line (every archetype joined, wrapping). Text widths are
    // measured by the caller (AS2 textWidth). Neither leaves kArchetypeLines lines: what does not fit is left out and an
    // ellipsis ends the last line.
    // ---------------------------------------------------------------------
    enum class LayoutKind
    {
        kList,
        kLine,
    };

    struct Metrics
    {
        double width = 0.0;         // the content clip's width
        double iconSize = 14.0;     // an icon's box (square)
        double iconGap = 3.0;       // icon -> its text
        double separatorW = 0.0;    // the middle dot's text width
        double separatorPad = 4.0;  // on each side of the middle dot
        double ellipsisW = 0.0;     // "..." text width
        double labelGap = 8.0;      // list: the widest group label -> the archetypes
        double maxLabelShare = 0.4;  // list: the label column takes at most this share of the width
        int maxLines = kArchetypeLines;
    };

    /// An archetype to place with its measured text width (the name, or "?")
    struct Measured
    {
        Item item;
        double textW = 0.0;
    };

    struct Part
    {
        enum class Kind
        {
            kLabel,      // a group's label (list); index = the group's position in kShownGroups
            kIcon,       // index = the archetype
            kText,       // the archetype's name or "?"; w = the room it has (less than its text width: cut with "...")
            kSeparator,  // the middle dot
            kEllipsis,   // "...": archetypes left out
        };
        Kind kind = Kind::kText;
        int index = 0;
        bool known = false;
        int line = 0;
        double x = 0.0;
        double w = 0.0;
    };

    struct Placement
    {
        std::vector<Part> parts;
        int lines = 0;         // lines used
        int shown = 0;         // archetypes placed
        int hidden = 0;        // archetypes left out
        double labelColumn = 0.0;  // list: where the archetypes start
    };

    namespace Detail
    {
        inline double ItemW(const Metrics& a_m, double a_textW) { return a_m.iconSize + a_m.iconGap + a_textW; }
        inline double SeparatorW(const Metrics& a_m) { return a_m.separatorW + 2 * a_m.separatorPad; }

        // The right end of a_line's parts in a_out
        inline double LineEnd(const Placement& a_out, int a_line, double a_start)
        {
            double end = a_start;
            for (const auto& part : a_out.parts) {
                if (part.line == a_line) end = std::max(end, part.x + part.w);
            }
            return end;
        }

        // Takes the last archetype (its text, its icon and the separator before it) off a_line; false when it ends in none
        inline bool PopLastItem(Placement& a_out, int a_line)
        {
            if (a_out.parts.size() < 2 || a_out.parts.back().line != a_line || a_out.parts.back().kind != Part::Kind::kText) return false;
            a_out.parts.pop_back();  // the text
            a_out.parts.pop_back();  // its icon (always right before it)
            if (!a_out.parts.empty() && a_out.parts.back().line == a_line && a_out.parts.back().kind == Part::Kind::kSeparator) {
                a_out.parts.pop_back();
            }
            --a_out.shown;
            ++a_out.hidden;
            return true;
        }

        // Ends a_line with "...": the line's last archetypes are taken off until it fits (with none left it starts the line)
        inline void EndWithEllipsis(Placement& a_out, const Metrics& a_m, int a_line, double a_start)
        {
            while (true) {
                const double end = LineEnd(a_out, a_line, a_start);
                const double x = end > a_start ? end + a_m.separatorPad : a_start;
                if (x + a_m.ellipsisW <= a_m.width || !PopLastItem(a_out, a_line)) {
                    a_out.parts.push_back({ Part::Kind::kEllipsis, 0, false, a_line, x, a_m.ellipsisW });
                    return;
                }
            }
        }

        // Places a_items from a_line at a_start, wrapping back to a_start; true when all fit. a_line is the last line used
        inline bool Flow(Placement& a_out, const Metrics& a_m, const std::vector<Measured>& a_items, std::size_t& a_next, int& a_line,
            double a_start)
        {
            double x = a_start;
            bool lineEmpty = true;
            while (a_next < a_items.size()) {
                const auto& measured = a_items[a_next];
                const double itemW = ItemW(a_m, measured.textW);
                const double sepW = lineEmpty ? 0.0 : SeparatorW(a_m);
                if (!lineEmpty && x + sepW + itemW > a_m.width) {
                    if (a_line + 1 >= a_m.maxLines) return false;
                    ++a_line;
                    x = a_start;
                    lineEmpty = true;
                    continue;
                }
                if (!lineEmpty) {
                    a_out.parts.push_back({ Part::Kind::kSeparator, 0, false, a_line, x + a_m.separatorPad, a_m.separatorW });
                    x += sepW;
                }
                const double textX = x + a_m.iconSize + a_m.iconGap;
                // a name longer than the whole line: it gets the room there is and is cut ("...")
                const double room = std::max(0.0, std::min(measured.textW, a_m.width - textX));
                a_out.parts.push_back({ Part::Kind::kIcon, measured.item.archetype, measured.item.known, a_line, x, a_m.iconSize });
                a_out.parts.push_back({ Part::Kind::kText, measured.item.archetype, measured.item.known, a_line, textX, room });
                x = textX + room;
                ++a_out.shown;
                ++a_next;
                lineEmpty = false;
            }
            return true;
        }
    }

    /// list: a group per line, its label (a_labelW[g]: the measured width of kShownGroups[g]'s label) on the left, wrapping
    /// within the archetype column; line: every archetype in order, wrapping. Both stop at a_m.maxLines
    inline Placement Place(LayoutKind a_kind, const Metrics& a_m, const std::vector<Measured>& a_items,
        const std::array<double, kShownGroups.size()>& a_labelW)
    {
        Placement out;
        if (a_items.empty() || a_m.maxLines <= 0) return out;
        int line = 0;
        if (a_kind == LayoutKind::kLine) {
            std::size_t next = 0;
            if (!Detail::Flow(out, a_m, a_items, next, line, 0.0)) {
                out.hidden += static_cast<int>(a_items.size() - next);
                Detail::EndWithEllipsis(out, a_m, line, 0.0);
            }
            out.lines = line + 1;
            return out;
        }
        double widest = 0.0;
        for (std::size_t g = 0; g < kShownGroups.size(); ++g) {
            const bool used = std::ranges::any_of(a_items, [&](const Measured& a_x) { return GroupOf(a_x.item.archetype) == kShownGroups[g]; });
            if (used) widest = std::max(widest, a_labelW[g]);
        }
        out.labelColumn = std::min(widest + a_m.labelGap, a_m.width * a_m.maxLabelShare);
        bool first = true;
        for (std::size_t g = 0; g < kShownGroups.size(); ++g) {
            std::vector<Measured> group;
            for (const auto& measured : a_items) {
                if (GroupOf(measured.item.archetype) == kShownGroups[g]) group.push_back(measured);
            }
            if (group.empty()) continue;
            if (!first) {
                if (line + 1 >= a_m.maxLines) {  // no line left for this group (or the ones after it)
                    for (std::size_t r = g; r < kShownGroups.size(); ++r) {
                        out.hidden += static_cast<int>(std::ranges::count_if(a_items,
                            [&](const Measured& a_x) { return GroupOf(a_x.item.archetype) == kShownGroups[r]; }));
                    }
                    Detail::EndWithEllipsis(out, a_m, line, out.labelColumn);
                    out.lines = line + 1;
                    return out;
                }
                ++line;
            }
            first = false;
            out.parts.push_back({ Part::Kind::kLabel, static_cast<int>(g), false, line, 0.0, std::min(a_labelW[g], out.labelColumn) });
            std::size_t next = 0;
            if (!Detail::Flow(out, a_m, group, next, line, out.labelColumn)) {
                out.hidden += static_cast<int>(group.size() - next);
                for (std::size_t r = g + 1; r < kShownGroups.size(); ++r) {
                    out.hidden += static_cast<int>(std::ranges::count_if(a_items,
                        [&](const Measured& a_x) { return GroupOf(a_x.item.archetype) == kShownGroups[r]; }));
                }
                Detail::EndWithEllipsis(out, a_m, line, out.labelColumn);
                out.lines = line + 1;
                return out;
            }
        }
        out.lines = line + 1;
        return out;
    }

    // ---------------------------------------------------------------------
    // Icons: a known archetype draws its whole icon, an unknown one only its group's frame (the glyph would give it away)
    // ---------------------------------------------------------------------
    struct SubpathRange
    {
        std::size_t first = 0, count = 0;  // in ArchetypeIcons::kSubpaths
    };

    constexpr SubpathRange IconSubpaths(int a_archetype, bool a_known)
    {
        if (a_archetype < 0 || a_archetype >= static_cast<int>(kArchetypeCount)) return {};
        const auto& icon = ArchetypeIcons::kIcons[static_cast<std::size_t>(a_archetype)];
        return { static_cast<std::size_t>(icon.first), static_cast<std::size_t>(a_known ? icon.count : icon.frame) };
    }

    // ---------------------------------------------------------------------
    // Links (the row under the archetypes) and the click
    // ---------------------------------------------------------------------
    struct Rect
    {
        double left = 0.0, top = 0.0, right = 0.0, bottom = 0.0;
        [[nodiscard]] bool Contains(double a_x, double a_y) const { return a_x >= left && a_x <= right && a_y >= top && a_y <= bottom; }
    };

    /// The link a press and its release both fall in (the same one), or nullopt
    inline std::optional<std::size_t> ClickedLink(const std::vector<Rect>& a_links, double a_pressX, double a_pressY, double a_releaseX,
        double a_releaseY)
    {
        for (std::size_t i = 0; i < a_links.size(); ++i) {
            if (a_links[i].Contains(a_pressX, a_pressY)) {
                return a_links[i].Contains(a_releaseX, a_releaseY) ? std::optional<std::size_t>(i) : std::nullopt;
            }
        }
        return std::nullopt;
    }

    /// The links in the row, left to right: their SRPO "what" (SRPrismaAPI::What: 1 experience, 2 journal, 3 alembic) and key
    struct Link
    {
        std::uint32_t what;
        const char* key;
    };
    inline constexpr std::array<Link, 3> kLinks{ { { 2, "link.journal" }, { 3, "link.alembic" }, { 1, "link.experience" } } };
    inline constexpr double kLinkGap = 10.0;  // between two links

    /// Link rects in the row at a_top, a_textW[i] each link's measured text width; a link past the width is cut to it
    inline std::vector<Rect> PlaceLinks(const std::vector<double>& a_textW, double a_top, double a_width)
    {
        std::vector<Rect> rects;
        double x = 0.0;
        for (const double w : a_textW) {
            if (x >= a_width) break;
            const double right = std::min(x + w, a_width);
            rects.push_back({ x, a_top, right, a_top + kLinkRowH });
            x = right + kLinkGap;
        }
        return rects;
    }

    // ---------------------------------------------------------------------
    // SR Prisma's reply (SRPrismaAPI::Result) -> the HUD notification's key; nullptr: no notification
    // ---------------------------------------------------------------------
    inline constexpr std::uint32_t kResultAccepted = 0, kResultBusy = 1, kResultRefused = 2, kResultBadMessage = 3, kResultOpened = 4;

    constexpr const char* NotificationKey(std::uint32_t a_result)
    {
        switch (a_result) {
        case kResultBusy: return "notify.busy";
        case kResultRefused: return "notify.refused";
        case kResultBadMessage: return "notify.badMessage";
        default: return nullptr;  // accepted, opened, or a code this build does not know (logged by the caller)
        }
    }

    // ---------------------------------------------------------------------
    // The open request in flight: one at a time; a reply counts only for the pending requestId and within kTimeoutMs
    // (SR Prisma's own wait for the menu to close is 2 s). Atomics: Begin/Clear on the UI thread, Matches on whatever
    // thread SR Prisma replies from, Accept in the game-thread task the reply is handed to. Times are the caller's
    // milliseconds (steady clock)
    // ---------------------------------------------------------------------
    class PendingRequest
    {
    public:
        static constexpr std::int64_t kTimeoutMs = 5000;

        /// A request is in flight and not timed out
        [[nodiscard]] bool Busy(std::int64_t a_nowMs) const
        {
            return m_id.load() != 0 && a_nowMs - m_sinceMs.load() < kTimeoutMs;
        }

        /// Starts a_id (non-zero); false when another is in flight. A timed-out one is dropped first
        bool Begin(std::uint32_t a_id, std::int64_t a_nowMs)
        {
            if (a_id == 0 || Busy(a_nowMs)) return false;
            m_sinceMs.store(a_nowMs);
            m_id.store(a_id);
            return true;
        }

        /// The request was not delivered (Dispatch false) or is finished: no longer pending
        void Clear(std::uint32_t a_id)
        {
            auto expected = a_id;
            m_id.compare_exchange_strong(expected, 0);
        }

        /// A reply with a_id is for the pending request, in time (does not clear it)
        [[nodiscard]] bool Matches(std::uint32_t a_id, std::int64_t a_nowMs) const
        {
            return a_id != 0 && m_id.load() == a_id && a_nowMs - m_sinceMs.load() < kTimeoutMs;
        }

        /// The reply for the pending request: true (and cleared) when it matches in time; a timed-out one is cleared, false
        bool Accept(std::uint32_t a_id, std::int64_t a_nowMs)
        {
            if (a_id == 0 || m_id.load() != a_id) return false;
            const bool inTime = a_nowMs - m_sinceMs.load() < kTimeoutMs;
            Clear(a_id);
            return inTime;
        }

        [[nodiscard]] std::uint32_t Id() const { return m_id.load(); }

    private:
        std::atomic<std::uint32_t> m_id{ 0 };
        std::atomic<std::int64_t> m_sinceMs{ 0 };
    };

    // ---------------------------------------------------------------------
    // [MagicMenuBlock] sLayout=list|line in SpellResearchPrismaUI.ini, read as text (Windows ini rules: sections and keys
    // case-insensitive, ';' or '#' starts a comment, the first occurrence counts); missing or unknown -> list
    // ---------------------------------------------------------------------
    inline constexpr std::string_view kLayoutSection = "MagicMenuBlock", kLayoutKey = "sLayout";

    struct LayoutRead
    {
        LayoutKind kind = LayoutKind::kList;
        bool found = false;    // the key is there
        bool known = false;    // its value is list or line
        std::string value;     // the value as written (for the log)
    };

    namespace Detail
    {
        inline std::string_view Trim(std::string_view a_text)
        {
            const auto first = a_text.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos) return {};
            const auto last = a_text.find_last_not_of(" \t\r\n");
            return a_text.substr(first, last - first + 1);
        }

        inline bool SameAscii(std::string_view a_a, std::string_view a_b)
        {
            if (a_a.size() != a_b.size()) return false;
            for (std::size_t i = 0; i < a_a.size(); ++i) {
                auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
                if (lower(a_a[i]) != lower(a_b[i])) return false;
            }
            return true;
        }
    }

    inline LayoutRead ParseLayout(std::string_view a_iniText)
    {
        LayoutRead read;
        bool inSection = false;
        std::size_t start = 0;
        while (start <= a_iniText.size()) {
            const auto end = a_iniText.find('\n', start);
            const auto raw = a_iniText.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
            start = end == std::string_view::npos ? a_iniText.size() + 1 : end + 1;
            const auto line = Detail::Trim(raw);
            if (line.empty() || line.front() == ';' || line.front() == '#') continue;
            if (line.front() == '[') {
                const auto close = line.find(']');
                inSection = close != std::string_view::npos && Detail::SameAscii(Detail::Trim(line.substr(1, close - 1)), kLayoutSection);
                continue;
            }
            if (!inSection) continue;
            const auto equals = line.find('=');
            if (equals == std::string_view::npos || !Detail::SameAscii(Detail::Trim(line.substr(0, equals)), kLayoutKey)) continue;
            auto value = line.substr(equals + 1);
            const auto comment = value.find_first_of(";#");
            if (comment != std::string_view::npos) value = value.substr(0, comment);
            value = Detail::Trim(value);
            read.found = true;
            read.value = std::string(value);
            if (Detail::SameAscii(value, "list")) {
                read.known = true;
                read.kind = LayoutKind::kList;
            } else if (Detail::SameAscii(value, "line")) {
                read.known = true;
                read.kind = LayoutKind::kLine;
            }
            return read;  // the first occurrence counts
        }
        return read;
    }

    // ---------------------------------------------------------------------
    // Strings: the language's table, then English, then the key itself
    // ---------------------------------------------------------------------
    using StringTable = std::unordered_map<std::string, std::string>;

    enum class StringSource
    {
        kLanguage,
        kEnglish,
        kKey,
    };

    struct Looked
    {
        std::string text;
        StringSource source = StringSource::kKey;
    };

    inline Looked LookUp(const StringTable* a_language, const StringTable& a_english, const std::string& a_key)
    {
        if (a_language) {
            if (const auto it = a_language->find(a_key); it != a_language->end()) return { it->second, StringSource::kLanguage };
        }
        if (const auto it = a_english.find(a_key); it != a_english.end()) return { it->second, StringSource::kEnglish };
        return { a_key, StringSource::kKey };
    }

    /// The keys the block uses (en.json must hold every one; the check tool reads the file against this list)
    inline std::vector<std::string> RequiredKeys()
    {
        std::vector<std::string> keys{ "title", "unknown", "more", "group.element", "group.targeting", "group.technique", "link.journal",
            "link.alembic", "link.experience", "reason.notReady", "reason.notSpell", "reason.listsEmpty", "reason.unknownSpell",
            "reason.noArchetypes", "reason.noTier", "reason.unknownArchetype", "reason.noResponse", "notify.busy", "notify.refused",
            "notify.badMessage" };
        for (std::size_t t = 0; t < kTierCount; ++t) keys.push_back("tier." + std::to_string(t));
        for (std::size_t a = 0; a < kArchetypeCount; ++a) keys.push_back("archetype." + std::to_string(a));
        return keys;
    }
}

#include "Layout.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <iterator>
#include <limits>
#include <utility>

// =============================================================================
// LAYOUT - two snapping columns, the saved order and border, their JSON
// =============================================================================
//
// Arrange, ContentHeight and SnapSlot are O(blocks log blocks) (map lookups);
// Move and Repair O(blocks^2) worst case on the find/erase in a column (a column
// holds a handful of blocks). JSON fields are type-checked before they are read;
// nothing is thrown on bad input.
namespace Layout
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr int kColumnLeft = 0, kColumnRight = 1;

        bool ValidColumn(int a_column) { return a_column >= 0 && a_column < kColumns; }

        void SetWhy(std::string* a_why, std::string a_text)
        {
            if (a_why) {
                *a_why = std::move(a_text);
            }
        }

        /// A height Arrange can stack: finite and not negative (blocks plan Task 6: an add-on's bad height must not move the others)
        bool UsableHeight(const std::map<BlockId, double>& a_heights, const BlockId& a_id)
        {
            const auto it = a_heights.find(a_id);
            return it != a_heights.end() && std::isfinite(it->second) && it->second >= 0;
        }

        /// The column's shown blocks in order: in a_heights with a usable height, not hidden.
        std::vector<const BlockId*> Shown(const std::vector<BlockId>& a_column, const std::map<BlockId, double>& a_heights,
            const std::set<BlockId>& a_hidden)
        {
            std::vector<const BlockId*> shown;
            for (const auto& id : a_column) {
                if (UsableHeight(a_heights, id) && !a_hidden.contains(id)) {
                    shown.push_back(&id);
                }
            }
            return shown;
        }
    }

    State Default(const Settings::Values& a_values)
    {
        State state;
        state.columns[kColumnLeft] = { kPreview, kProgress, kSpellCard };
        state.columns[kColumnRight] = { kTree, kNodeCard };
        state.border = a_values.preview.right;
        return state;
    }

    std::array<Frame, kColumns> DefaultFrames(const Settings::Values& a_values)
    {
        const Frame frame{ a_values.preview.left, a_values.treeRight, a_values.preview.top, a_values.itemCard.bottom, a_values.panelGap,
            kMinColumn };
        return { frame, frame };
    }

    std::map<BlockId, double> DefaultHeights(const Settings::Values& a_values)
    {
        return {
            { kPreview, a_values.preview.Height() },
            { kSpellCard, a_values.itemCard.Height() },
        };
    }

    double ClampBorder(double a_x, const Frame& a_left, const Frame& a_right)
    {
        const double low = a_left.left + a_left.minColumn;
        const double high = a_right.right - a_left.gap - a_right.minColumn;
        if (low > high) {
            return (low + high) / 2;
        }
        return std::isnan(a_x) ? low : std::clamp(a_x, low, high);
    }

    std::map<BlockId, Settings::Rect> Arrange(const State& a_state, const std::map<BlockId, double>& a_heights, const std::set<BlockId>& a_hidden,
        const std::set<BlockId>& a_fill, const std::array<Frame, kColumns>& a_frames)
    {
        std::map<BlockId, Settings::Rect> rects;
        for (int column = 0; column < kColumns; ++column) {
            const Frame& frame = a_frames[column];
            const Settings::Rect area = ColumnArea(a_state, column, a_frames);
            const auto shown = Shown(a_state.columns[column], a_heights, a_hidden);

            double stacked = 0;  // every shown block's own height plus the gaps between them
            for (std::size_t i = 0; i < shown.size(); ++i) {
                stacked += a_heights.at(*shown[i]) + (i > 0 ? frame.gap : 0);
            }
            bool filled = false;
            double top = frame.top;
            for (const BlockId* id : shown) {
                double height = a_heights.at(*id);
                if (!filled && a_fill.contains(*id)) {
                    filled = true;
                    height = std::max(height, frame.Height() - (stacked - height));
                }
                rects[*id] = { area.left, top, area.right, top + height };
                top += height + frame.gap;
            }
        }
        return rects;
    }

    Settings::Rect ColumnArea(const State& a_state, int a_column, const std::array<Frame, kColumns>& a_frames)
    {
        if (!ValidColumn(a_column)) {
            return {};
        }
        const double border = ClampBorder(a_state.border, a_frames[kColumnLeft], a_frames[kColumnRight]);
        const Frame& frame = a_frames[a_column];
        if (a_column == kColumnLeft) {
            return { frame.left, frame.top, border, frame.bottom };
        }
        return { border + a_frames[kColumnLeft].gap, frame.top, frame.right, frame.bottom };
    }

    std::string CardBoxProblem(const Settings::Rect& a_box, const Settings::Rect& a_column, double a_content)
    {
        constexpr double kSlack = 1e-6;  // Arrange's sums, not a pixel tolerance
        const auto finite = [](const Settings::Rect& a_rect) {
            return std::isfinite(a_rect.left) && std::isfinite(a_rect.top) && std::isfinite(a_rect.right) && std::isfinite(a_rect.bottom);
        };
        if (!finite(a_box) || !finite(a_column) || !std::isfinite(a_content)) return "an edge is not finite";
        if (a_box.bottom <= a_box.top) return std::format("inverted or empty: bottom {:.1f} not below top {:.1f}", a_box.bottom, a_box.top);
        if (a_box.right <= a_box.left) return std::format("inverted or empty: right {:.1f} not right of left {:.1f}", a_box.right, a_box.left);
        if (a_box.left < a_column.left - kSlack || a_box.right > a_column.right + kSlack) {
            return std::format("x {:.1f}..{:.1f} outside the column's {:.1f}..{:.1f}", a_box.left, a_box.right, a_column.left, a_column.right);
        }
        const double end = a_column.top + std::max(a_column.Height(), a_content);
        if (a_box.top < a_column.top - kSlack || a_box.bottom > end + kSlack) {
            return std::format("y {:.1f}..{:.1f} outside the column's {:.1f}..{:.1f}", a_box.top, a_box.bottom, a_column.top, end);
        }
        return {};
    }

    std::optional<Settings::Rect> VisibleBox(const Settings::Rect& a_box, const Settings::Rect& a_viewport)
    {
        const auto finite = [](const Settings::Rect& a_rect) {
            return std::isfinite(a_rect.left) && std::isfinite(a_rect.top) && std::isfinite(a_rect.right) && std::isfinite(a_rect.bottom);
        };
        if (!finite(a_box) || !finite(a_viewport)) {
            return std::nullopt;  // checked first: std::max / std::min drop a NaN in one argument order
        }
        const Settings::Rect cut{ std::max(a_box.left, a_viewport.left), std::max(a_box.top, a_viewport.top), std::min(a_box.right, a_viewport.right),
            std::min(a_box.bottom, a_viewport.bottom) };
        if (!(cut.right > cut.left) || !(cut.bottom > cut.top)) {
            return std::nullopt;
        }
        return cut;
    }

    std::array<double, kColumns> ContentHeight(const State& a_state, const std::map<BlockId, Settings::Rect>& a_arranged,
        const std::array<Frame, kColumns>& a_frames)
    {
        std::array<double, kColumns> content{};
        for (int column = 0; column < kColumns; ++column) {
            for (const auto& id : a_state.columns[column]) {
                if (const auto it = a_arranged.find(id); it != a_arranged.end()) {
                    content[column] = std::max(content[column], it->second.bottom - a_frames[column].top);
                }
            }
        }
        return content;
    }

    double ClampScroll(double a_offset, double a_content, double a_visible)
    {
        const double most = a_content - a_visible;
        if (!std::isfinite(a_offset) || !std::isfinite(most) || most <= 0) {
            return 0;
        }
        return std::clamp(a_offset, 0.0, most);
    }

    Slot SnapSlot(const State& a_state, const BlockId& a_dragged, double a_x, double a_y, const std::map<BlockId, Settings::Rect>& a_arranged,
        const std::array<Frame, kColumns>& a_frames)
    {
        const double border = ClampBorder(a_state.border, a_frames[kColumnLeft], a_frames[kColumnRight]);
        Slot slot{ a_x < border + a_frames[kColumnLeft].gap / 2 ? kColumnLeft : kColumnRight, std::nullopt };
        for (const auto& id : a_state.columns[slot.column]) {
            if (id == a_dragged) {
                continue;
            }
            if (const auto it = a_arranged.find(id); it != a_arranged.end() && it->second.CenterY() > a_y) {
                slot.before = id;
                break;
            }
        }
        return slot;
    }

    State Move(State a_state, const BlockId& a_block, const Slot& a_slot)
    {
        if (!ValidColumn(a_slot.column) || a_slot.before == a_block) {
            return a_state;
        }
        bool found = false;
        for (auto& column : a_state.columns) {
            if (const auto it = std::ranges::find(column, a_block); it != column.end()) {
                column.erase(it);
                found = true;
                break;
            }
        }
        if (!found) {
            return a_state;
        }
        auto& target = a_state.columns[a_slot.column];
        const auto at = a_slot.before ? std::ranges::find(target, *a_slot.before) : target.end();
        target.insert(at, a_block);
        return a_state;
    }

    double SlotLineY(const State& a_state, const Slot& a_slot, const BlockId& a_dragged, const std::map<BlockId, Settings::Rect>& a_arranged,
        const std::array<Frame, kColumns>& a_frames)
    {
        if (!ValidColumn(a_slot.column)) {
            return a_frames[kColumnLeft].top;
        }
        const Frame& frame = a_frames[a_slot.column];
        if (a_slot.before) {
            if (const auto it = a_arranged.find(*a_slot.before); it != a_arranged.end()) {
                return it->second.top - frame.gap / 2;
            }
        }
        double y = frame.top;
        for (const auto& id : a_state.columns[a_slot.column]) {
            if (id == a_dragged) {
                continue;
            }
            if (const auto it = a_arranged.find(id); it != a_arranged.end()) {
                y = std::max(y, it->second.bottom + frame.gap / 2);
            }
        }
        return y;
    }

    double ScrollToShow(double a_scroll, double a_top, double a_bottom, double a_viewTop, double a_visible, double a_content)
    {
        double scroll = std::isfinite(a_scroll) ? a_scroll : 0.0;
        if (a_bottom - a_top >= a_visible || a_top < a_viewTop + a_scroll) {
            scroll = a_top - a_viewTop;
        } else if (a_bottom > a_viewTop + a_scroll + a_visible) {
            scroll = a_bottom - a_viewTop - a_visible;
        }
        return ClampScroll(scroll, a_content, a_visible);
    }

    namespace
    {
        constexpr double kSameBorder = 1e-9;

        std::optional<int> ColumnHolding(const State& a_state, const BlockId& a_id)
        {
            for (int column = 0; column < kColumns; ++column) {
                if (std::ranges::find(a_state.columns[column], a_id) != a_state.columns[column].end()) {
                    return column;
                }
            }
            return std::nullopt;
        }

        /// The column's visible ids in order, a_skip left out
        std::vector<BlockId> VisibleIn(const State& a_state, int a_column, const std::set<BlockId>& a_visible, const BlockId* a_skip = nullptr)
        {
            std::vector<BlockId> ids;
            if (!ValidColumn(a_column)) {
                return ids;
            }
            for (const auto& id : a_state.columns[a_column]) {
                if (a_visible.contains(id) && (!a_skip || id != *a_skip)) {
                    ids.push_back(id);
                }
            }
            return ids;
        }

        std::size_t IndexOf(const std::vector<BlockId>& a_ids, const std::optional<BlockId>& a_id)
        {
            if (!a_id) {
                return a_ids.size();
            }
            return static_cast<std::size_t>(std::ranges::find(a_ids, *a_id) - a_ids.begin());
        }

        /// The picked block's target one slot up / down in its column, or to the other column (true when it moved)
        bool MoveTarget(Slot& a_slot, const BlockId& a_picked, Nav a_nav, const State& a_state, const std::set<BlockId>& a_visible,
            const std::map<BlockId, Settings::Rect>& a_arranged, const std::array<Frame, kColumns>& a_frames)
        {
            if (a_nav == Nav::kUp || a_nav == Nav::kDown) {
                const auto others = VisibleIn(a_state, a_slot.column, a_visible, &a_picked);
                std::size_t index = IndexOf(others, a_slot.before);
                if (a_nav == Nav::kUp && index > 0) {
                    --index;
                } else if (a_nav == Nav::kDown && index < others.size()) {
                    ++index;
                } else {
                    return false;
                }
                a_slot.before = index < others.size() ? std::optional<BlockId>{ others[index] } : std::nullopt;
                return true;
            }
            const int other = a_nav == Nav::kLeft ? kColumnLeft : kColumnRight;
            if (a_slot.column == other) {
                return false;
            }
            const double line = SlotLineY(a_state, a_slot, a_picked, a_arranged, a_frames);
            Slot next{ other, std::nullopt };
            for (const auto& id : VisibleIn(a_state, other, a_visible, &a_picked)) {
                if (const auto it = a_arranged.find(id); it != a_arranged.end() && it->second.CenterY() > line) {
                    next.before = id;
                    break;
                }
            }
            a_slot = next;
            return true;
        }

        /// The focus to the visible block above / below in its column, or the nearest by centre y in the other column
        std::optional<BlockId> MoveFocus(const BlockId& a_id, int a_column, Nav a_nav, const State& a_state, const std::set<BlockId>& a_visible,
            const std::map<BlockId, Settings::Rect>& a_arranged)
        {
            if (a_nav == Nav::kUp || a_nav == Nav::kDown) {
                const auto ids = VisibleIn(a_state, a_column, a_visible);
                const std::size_t index = IndexOf(ids, a_id);
                if (index >= ids.size()) {
                    return std::nullopt;
                }
                if (a_nav == Nav::kUp) {
                    return index > 0 ? std::optional<BlockId>{ ids[index - 1] } : std::nullopt;
                }
                return index + 1 < ids.size() ? std::optional<BlockId>{ ids[index + 1] } : std::nullopt;
            }
            const int other = a_nav == Nav::kLeft ? kColumnLeft : kColumnRight;
            if (a_column == other) {
                return std::nullopt;
            }
            const auto from = a_arranged.find(a_id);
            std::optional<BlockId> best;
            double bestDistance = std::numeric_limits<double>::infinity();
            for (const auto& id : VisibleIn(a_state, other, a_visible)) {
                const auto it = a_arranged.find(id);
                if (it == a_arranged.end()) {
                    continue;
                }
                // a focused block with no rect (not arranged on this tab): the other column's top visible block
                const double distance = from == a_arranged.end() ? 0.0 : std::abs(it->second.CenterY() - from->second.CenterY());
                if (distance < bestDistance) {
                    best = id;
                    bestDistance = distance;
                }
            }
            return best;
        }
    }

    std::optional<BlockId> FirstFocus(const State& a_state, const std::set<BlockId>& a_visible)
    {
        for (int column = 0; column < kColumns; ++column) {
            if (const auto ids = VisibleIn(a_state, column, a_visible); !ids.empty()) {
                return ids.front();
            }
        }
        return std::nullopt;
    }

    std::optional<Slot> SlotOf(const State& a_state, const BlockId& a_block, const std::set<BlockId>& a_visible)
    {
        const auto column = ColumnHolding(a_state, a_block);
        if (!column) {
            return std::nullopt;
        }
        const auto& ids = a_state.columns[*column];
        Slot slot{ *column, std::nullopt };
        for (auto it = std::ranges::find(ids, a_block) + 1; it != ids.end(); ++it) {
            if (a_visible.contains(*it) && *it != a_block) {
                slot.before = *it;
                break;
            }
        }
        return slot;
    }

    Focus Normalize(const State& a_state, const Focus& a_focus, const std::set<BlockId>& a_visible)
    {
        Focus out = a_focus;
        if (!out.focus) {
            out.target.reset();
            return out;
        }
        const auto column = ColumnHolding(a_state, *out.focus);
        if (!column) {
            return {};
        }
        if (!a_visible.contains(*out.focus)) {
            const auto& ids = a_state.columns[*column];
            const auto at = static_cast<std::ptrdiff_t>(std::ranges::find(ids, *out.focus) - ids.begin());
            std::optional<BlockId> nearest;
            for (auto i = at + 1; !nearest && i < std::ssize(ids); ++i) {
                if (a_visible.contains(ids[static_cast<std::size_t>(i)])) {
                    nearest = ids[static_cast<std::size_t>(i)];
                }
            }
            for (auto i = at - 1; !nearest && i >= 0; --i) {
                if (a_visible.contains(ids[static_cast<std::size_t>(i)])) {
                    nearest = ids[static_cast<std::size_t>(i)];
                }
            }
            return { nearest, std::nullopt };
        }
        if (out.target) {
            if (!ValidColumn(out.target->column)) {
                out.target.reset();
            } else if (const auto& before = out.target->before;
                       before && (*before == *out.focus || !a_visible.contains(*before) || ColumnHolding(a_state, *before) != out.target->column)) {
                out.target->before.reset();
            }
        }
        return out;
    }

    NavResult Navigate(const State& a_state, const Focus& a_focus, Nav a_nav, bool a_modeOn, const std::set<BlockId>& a_visible,
        const std::map<BlockId, Settings::Rect>& a_arranged, const std::array<Frame, kColumns>& a_frames)
    {
        NavResult result{ a_focus, a_state, NavEffect::kIgnored };
        if (!a_modeOn) {
            return result;
        }
        result.effect = NavEffect::kNone;
        Focus& focus = result.focus;
        focus = Normalize(a_state, a_focus, a_visible);
        if (a_nav == Nav::kBorderLeft || a_nav == Nav::kBorderRight) {
            const double now = ClampBorder(a_state.border, a_frames[kColumnLeft], a_frames[kColumnRight]);
            const double next = ClampBorder(now + (a_nav == Nav::kBorderLeft ? -kBorderStep : kBorderStep), a_frames[kColumnLeft], a_frames[kColumnRight]);
            if (std::abs(next - now) > kSameBorder) {
                result.state.border = next;
                result.effect = NavEffect::kBorder;
            }
            return result;
        }
        if (a_nav == Nav::kCancel) {
            result.effect = focus.Picked() ? NavEffect::kPickCancelled : NavEffect::kLeave;
            focus.target.reset();
            return result;
        }
        if (!focus.focus) {
            focus.focus = FirstFocus(a_state, a_visible);
            if (focus.focus) {
                result.effect = NavEffect::kFocused;
            }
            return result;
        }
        const BlockId id = *focus.focus;
        const int column = ColumnHolding(a_state, id).value_or(kColumnLeft);  // Normalize kept it in the layout
        if (a_nav == Nav::kAccept) {
            if (!focus.target) {
                focus.target = SlotOf(a_state, id, a_visible);
                if (focus.target) {
                    result.effect = NavEffect::kPicked;
                }
                return result;
            }
            const Slot slot = *focus.target;
            focus.target.reset();
            if (slot == SlotOf(a_state, id, a_visible)) {
                result.effect = NavEffect::kDroppedInPlace;
                return result;
            }
            result.state = Move(a_state, id, slot);
            result.effect = result.state == a_state ? NavEffect::kDroppedInPlace : NavEffect::kDropped;
            return result;
        }
        if (focus.target) {
            if (MoveTarget(*focus.target, id, a_nav, a_state, a_visible, a_arranged, a_frames)) {
                result.effect = NavEffect::kTargeted;
            }
            return result;
        }
        if (const auto next = MoveFocus(id, column, a_nav, a_state, a_visible, a_arranged)) {
            focus.focus = next;
            result.effect = NavEffect::kFocused;
        }
        return result;
    }

    std::string ToJson(const State& a_state)
    {
        Json columns = Json::array();
        for (const auto& column : a_state.columns) {
            columns.push_back(column);
        }
        const Json json{ { "version", kJsonVersion }, { "columns", std::move(columns) }, { "border", a_state.border } };
        return json.dump(2);
    }

    std::optional<State> FromJson(std::string_view a_text, std::string* a_why, bool* a_newerVersion)
    {
        if (a_newerVersion) {
            *a_newerVersion = false;
        }
        const Json json = Json::parse(a_text, nullptr, false);
        if (json.is_discarded()) {
            SetWhy(a_why, "not JSON");
            return std::nullopt;
        }
        if (!json.is_object()) {
            SetWhy(a_why, "not a JSON object");
            return std::nullopt;
        }
        if (const auto it = json.find("version"); it != json.end()) {
            if (!it->is_number_integer() || it->get<std::int64_t>() < 1) {
                SetWhy(a_why, "bad version " + it->dump() + " (want an integer >= 1)");
                return std::nullopt;
            }
            if (const auto version = it->get<std::int64_t>(); version > kJsonVersion) {
                SetWhy(a_why, "unsupported version " + std::to_string(version) + " (this build reads " + std::to_string(kJsonVersion) + ")");
                if (a_newerVersion) {
                    *a_newerVersion = true;
                }
                return std::nullopt;
            }
        }
        const auto columns = json.find("columns");
        if (columns == json.end() || !columns->is_array() || columns->size() != static_cast<std::size_t>(kColumns)) {
            SetWhy(a_why, "\"columns\" is not an array of " + std::to_string(kColumns) + " arrays");
            return std::nullopt;
        }
        State state;
        for (int column = 0; column < kColumns; ++column) {
            const Json& ids = (*columns)[static_cast<std::size_t>(column)];
            if (!ids.is_array()) {
                SetWhy(a_why, "\"columns\"[" + std::to_string(column) + "] is not an array");
                return std::nullopt;
            }
            for (const Json& id : ids) {
                if (!id.is_string()) {
                    SetWhy(a_why, "\"columns\"[" + std::to_string(column) + "] holds " + id.dump() + ", not a block id string");
                    return std::nullopt;
                }
                state.columns[column].push_back(id.get<std::string>());
            }
        }
        state.border = std::numeric_limits<double>::quiet_NaN();
        if (const auto it = json.find("border"); it != json.end()) {
            if (!it->is_number()) {
                SetWhy(a_why, "\"border\" " + it->dump() + " is not a number");
                return std::nullopt;
            }
            state.border = it->get<double>();
        }
        return state;
    }

    State Repair(State a_state, const std::set<BlockId>& a_known, const State& a_fallback)
    {
        std::set<BlockId> seen;
        for (auto& column : a_state.columns) {
            std::erase_if(column, [&](const BlockId& a_id) { return a_id.empty() || !seen.insert(a_id).second; });
        }
        const auto append = [&](const BlockId& a_id, int a_column) {
            if (a_known.contains(a_id) && seen.insert(a_id).second) {
                a_state.columns[a_column].push_back(a_id);
            }
        };
        for (int column = 0; column < kColumns; ++column) {
            for (const auto& id : a_fallback.columns[column]) {
                append(id, column);
            }
        }
        for (const auto& id : a_known) {
            append(id, kColumnLeft);
        }
        if (!std::isfinite(a_state.border)) {
            a_state.border = a_fallback.border;
        }
        return a_state;
    }

    State AddBlocks(State a_state, const std::vector<BlockId>& a_ids, int a_column)
    {
        if (!ValidColumn(a_column)) {
            return a_state;
        }
        for (const auto& id : a_ids) {
            const bool placed = std::ranges::any_of(a_state.columns, [&](const auto& a_list) { return std::ranges::find(a_list, id) != a_list.end(); });
            if (!id.empty() && !placed) {
                a_state.columns[a_column].push_back(id);
            }
        }
        return a_state;
    }

    State AddAddons(State a_default, const std::vector<BlockId>& a_ids)
    {
        std::vector<BlockId> right, left;
        for (const auto& id : a_ids) (id == kDebugFillerId ? left : right).push_back(id);
        return AddBlocks(AddBlocks(std::move(a_default), right, kColumnRight), left, kColumnLeft);
    }

    namespace
    {
        /// Each id that is a key of a_rename becomes its value in place; a value whose key is in the layout too is dropped (the
        /// key's place wins). ToFinal is this rule with the map, ToLegacy with the map inverted
        State Rename(State a_state, const std::map<BlockId, BlockId>& a_rename)
        {
            if (a_rename.empty()) {
                return a_state;
            }
            std::set<BlockId> present;
            for (const auto& column : a_state.columns) present.insert(column.begin(), column.end());
            std::set<BlockId> beaten;  // the values whose key is in the layout
            for (const auto& [from, to] : a_rename) {
                if (present.contains(from)) beaten.insert(to);
            }
            for (auto& column : a_state.columns) {
                std::vector<BlockId> renamed;
                for (const auto& id : column) {
                    if (const auto it = a_rename.find(id); it != a_rename.end()) {
                        renamed.push_back(it->second);
                    } else if (!beaten.contains(id)) {
                        renamed.push_back(id);
                    }
                }
                column = std::move(renamed);
            }
            return a_state;
        }
    }

    State ToFinal(State a_state, const std::map<BlockId, BlockId>& a_legacyToFinal)
    {
        return Rename(std::move(a_state), a_legacyToFinal);
    }

    State ToLegacy(State a_state, const std::map<BlockId, BlockId>& a_legacyToFinal)
    {
        std::map<BlockId, BlockId> finalToLegacy;
        for (const auto& [legacyId, finalId] : a_legacyToFinal) finalToLegacy.emplace(finalId, legacyId);
        return Rename(std::move(a_state), finalToLegacy);
    }

    WithAddonsResult WithAddons(const State& a_read, const std::set<BlockId>& a_builtins, const std::map<BlockId, BlockId>& a_legacyToFinal,
        const std::vector<BlockId>& a_addons, const State& a_default)
    {
        WithAddonsResult result;
        result.renamed = ToFinal(a_read, a_legacyToFinal);
        result.fallback = AddAddons(ToFinal(a_default, a_legacyToFinal), a_addons);
        auto known = a_builtins;
        for (const auto& [legacyId, finalId] : a_legacyToFinal) known.erase(legacyId);
        known.insert(a_addons.begin(), a_addons.end());
        result.layout = Repair(result.renamed, known, result.fallback);
        return result;
    }

    SavePlan PlanSave(FileState a_state, bool a_badKept, bool a_fileExists)
    {
        SavePlan plan;
        if (a_state == FileState::kNewer) {
            plan.refuse = true;
            return plan;
        }
        plan.copyBadFirst = a_state == FileState::kBroken && !a_badKept && a_fileExists;
        plan.replace = a_fileExists;
        return plan;
    }

    FileState AfterSave(FileState a_state, bool a_written)
    {
        return a_written ? FileState::kRead : a_state;
    }

    FailedPut AfterFailedPut(bool a_replace, bool a_fileThere, bool a_bakThere)
    {
        if (a_fileThere) return FailedPut::kFileThere;
        return a_replace && a_bakThere ? FailedPut::kRestoreBak : FailedPut::kNoFile;
    }

    bool KeepTmp(FailedPut a_put, bool a_restored)
    {
        return a_put == FailedPut::kRestoreBak && !a_restored;
    }

    bool ReadBackup(FileState a_file, bool a_bakThere)
    {
        return a_file == FileState::kMissing && a_bakThere;
    }
}

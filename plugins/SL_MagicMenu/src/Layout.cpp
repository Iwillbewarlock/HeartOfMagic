#include "Layout.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
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

        /// The column's shown blocks in order: in a_heights, not hidden.
        std::vector<const BlockId*> Shown(const std::vector<BlockId>& a_column, const std::map<BlockId, double>& a_heights,
            const std::set<BlockId>& a_hidden)
        {
            std::vector<const BlockId*> shown;
            for (const auto& id : a_column) {
                if (a_heights.contains(id) && !a_hidden.contains(id)) {
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
        const Frame frame{ a_values.preview.left, a_values.tree.right, a_values.preview.top, a_values.itemCard.bottom, a_values.panelGap,
            kMinColumn };
        return { frame, frame };
    }

    std::map<BlockId, double> DefaultHeights(const Settings::Values& a_values)
    {
        return {
            { kPreview, a_values.preview.Height() },
            { kTree, a_values.tree.Height() },
            { kProgress, a_values.progress.Height() },
            { kSpellCard, a_values.itemCard.Height() },
            { kNodeCard, a_values.nodeCard.Height() },
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
}

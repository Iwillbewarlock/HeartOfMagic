#include "HomStyle.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <numbers>
#include <system_error>

// =============================================================================
// HOM STYLE - see HomStyle.h. Every JSON value is type-checked before it is
// read (a wrong type keeps the default and adds a note), never thrown on.
// O(preset files + config keys) to read; the shape and dash helpers are
// O(points).
// =============================================================================

namespace HomStyle
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::string_view kBom = "\xEF\xBB\xBF";
        constexpr double kChannelMax = 255.0;
        constexpr std::uint32_t kFallbackSchool = 0x888888;  // _getSchoolColor's last fallback (canvasRendererColors.js:27)
        constexpr int kMaxLabelChars = 1000;                  // a labelMaxChars past this is taken for a broken value

        // HoM's school colours before the player picks any (canvasRendererV2.js:226-232, state.js:143-149)
        constexpr std::array<std::pair<std::string_view, std::uint32_t>, 5> kDefaultSchools{ {
            { "Destruction", 0xEF4444 },
            { "Restoration", 0xFACC15 },
            { "Alteration", 0x22C55E },
            { "Conjuration", 0xA855F7 },
            { "Illusion", 0x38BDF8 },
        } };

        // designPresets.js:27-129: these ids are HoM's own (an add-on file cannot replace them, :337) and live in its
        // script, which the prototype does not read
        constexpr std::array<std::string_view, 3> kBuiltIn{ "classic", "modern", "arcane" };
        constexpr std::string_view kDefaultDesign = "arcane";  // DesignPresets.DEFAULT_ID (designPresets.js:25)

        // The shipped defaults of the colours a design may re-colour (canvasRendererV2.js:236-237, PLAYER_COLOR_DEFAULTS)
        constexpr std::string_view kDefaultLearningPath = "#00ffff", kDefaultHeartRing = "#b8a878";

        // nodeBatch.js:29-49: the unit shapes (first point first) and the turn added to atan2(y, x)
        struct Shape
        {
            std::string_view school;
            std::vector<Point> points;
            double turn{};
        };

        const std::vector<Shape>& Shapes()
        {
            static const std::vector<Shape> shapes = [] {
                std::vector<Point> pentagon;
                constexpr int kSides = 5;
                constexpr double kStepDeg = 72.0, kStartDeg = -90.0;
                for (int i = 0; i < kSides; ++i) {
                    const double a = (i * kStepDeg + kStartDeg) * std::numbers::pi / 180.0;
                    pentagon.push_back({ std::cos(a), std::sin(a) });
                }
                return std::vector<Shape>{
                    { "Destruction", { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } }, std::numbers::pi / 4 },
                    { "Alteration", { { 0, -1 }, { 0.9, -0.5 }, { 0.9, 0.5 }, { 0, 1 }, { -0.9, 0.5 }, { -0.9, -0.5 } }, std::numbers::pi / 6 },
                    { "Conjuration", std::move(pentagon), std::numbers::pi / 2 },
                    { "Illusion", { { 0, 1 }, { -0.85, -0.6 }, { 0.85, -0.6 } }, std::numbers::pi / 2 },
                };
            }();
            return shapes;
        }

        const Shape* ShapeOf(std::string_view a_school)
        {
            const auto& shapes = Shapes();
            const auto it = std::ranges::find_if(shapes, [&](const Shape& a_shape) { return a_shape.school == a_school; });
            return it == shapes.end() ? nullptr : &*it;
        }

        /// Math.round on the values here (all >= 0): half up
        double RoundHalfUp(double a_value) { return std::floor(a_value + 0.5); }

        std::uint32_t Channel(double a_value) { return static_cast<std::uint32_t>(std::clamp(RoundHalfUp(a_value), 0.0, kChannelMax)); }

        std::uint32_t Pack(double a_r, double a_g, double a_b) { return (Channel(a_r) << 16) | (Channel(a_g) << 8) | Channel(a_b); }

        double R(Color a_c) { return static_cast<double>((a_c.rgb >> 16) & 0xFF); }
        double G(Color a_c) { return static_cast<double>((a_c.rgb >> 8) & 0xFF); }
        double B(Color a_c) { return static_cast<double>(a_c.rgb & 0xFF); }

        std::string_view Trim(std::string_view a_text)
        {
            while (!a_text.empty() && (a_text.front() == ' ' || a_text.front() == '\t')) a_text.remove_prefix(1);
            while (!a_text.empty() && (a_text.back() == ' ' || a_text.back() == '\t')) a_text.remove_suffix(1);
            return a_text;
        }

        std::string Lower(std::string_view a_text)
        {
            std::string out(a_text);
            std::ranges::transform(out, out.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
            return out;
        }

        std::optional<double> ParseNumber(std::string_view a_text)
        {
            a_text = Trim(a_text);
            double value{};
            const auto* end = a_text.data() + a_text.size();
            const auto [ptr, ec] = std::from_chars(a_text.data(), end, value);
            if (a_text.empty() || ec != std::errc{} || ptr != end || !std::isfinite(value)) return std::nullopt;
            return value;
        }

        std::optional<Color> ParseHex(std::string_view a_hex)
        {
            constexpr std::size_t kLong = 6, kShort = 3;
            if (a_hex.size() != kLong && a_hex.size() != kShort) return std::nullopt;
            std::uint32_t value{};
            const auto* end = a_hex.data() + a_hex.size();
            const auto [ptr, ec] = std::from_chars(a_hex.data(), end, value, 16);
            if (ec != std::errc{} || ptr != end) return std::nullopt;
            if (a_hex.size() == kShort) {  // "#abc" = "#aabbcc"
                const std::uint32_t r = (value >> 8) & 0xF, g = (value >> 4) & 0xF, b = value & 0xF;
                value = (r * 0x11 << 16) | (g * 0x11 << 8) | (b * 0x11);
            }
            return Color{ value, 1.0 };
        }

        /// "rgb(r, g, b)" / "rgba(r, g, b, a)": channels 0..255, alpha 0..1
        std::optional<Color> ParseFunction(std::string_view a_text)
        {
            const bool withAlpha = a_text.starts_with("rgba(");
            if (!withAlpha && !a_text.starts_with("rgb(")) return std::nullopt;
            if (!a_text.ends_with(')')) return std::nullopt;
            a_text.remove_prefix(withAlpha ? 5 : 4);
            a_text.remove_suffix(1);
            std::vector<double> parts;
            while (true) {
                const auto comma = a_text.find(',');
                const auto part = ParseNumber(a_text.substr(0, comma));
                if (!part) return std::nullopt;
                parts.push_back(*part);
                if (comma == std::string_view::npos) break;
                a_text.remove_prefix(comma + 1);
            }
            if (parts.size() != (withAlpha ? 4u : 3u)) return std::nullopt;
            for (std::size_t i = 0; i < 3; ++i) {
                if (parts[i] < 0 || parts[i] > kChannelMax) return std::nullopt;
            }
            const double alpha = withAlpha ? parts[3] : 1.0;
            if (alpha < 0 || alpha > 1) return std::nullopt;
            return Color{ Pack(parts[0], parts[1], parts[2]), alpha };
        }

        const Json* Field(const Json& a_object, const char* a_key)
        {
            const auto it = a_object.find(a_key);
            return it == a_object.end() ? nullptr : &*it;
        }

        /// JavaScript's truthiness of a JSON value (settingsPanel.js reads some switches as `data.x || false`)
        bool Truthy(const Json& a_value)
        {
            if (a_value.is_boolean()) return a_value.get<bool>();
            if (a_value.is_number()) {
                const auto number = a_value.get<double>();
                return number != 0.0 && !std::isnan(number);
            }
            if (a_value.is_string()) return !a_value.get_ref<const std::string&>().empty();
            return !a_value.is_null();  // objects and arrays are truthy, null is not
        }

        std::optional<Json> ParseJson(std::string_view a_text)
        {
            if (a_text.starts_with(kBom)) a_text.remove_prefix(kBom.size());
            auto json = Json::parse(a_text.begin(), a_text.end(), nullptr, false);
            if (json.is_discarded()) return std::nullopt;
            return json;
        }

        /// Reads a preset's "tree" block into the tokens, key by key, with TreeStyle.set's type rule (treeStyle.js:133)
        class TokenReader
        {
        public:
            TokenReader(const Json& a_tree, std::string a_where, std::vector<std::string>& a_notes) :
                _tree(a_tree), _where(std::move(a_where)), _notes(a_notes) {}

            int taken = 0;

            void Colour(const char* a_key, Color& a_out)
            {
                const auto* value = Field(_tree, a_key);
                if (!value) return;
                const auto parsed = value->is_string() ? ParseColor(value->get_ref<const std::string&>()) : std::nullopt;
                if (!parsed) return Wrong(a_key, *value, "a colour");
                a_out = *parsed;
                ++taken;
            }

            /// '' is HoM's "none" (the school colour, the player's colour)
            void OptionalColour(const char* a_key, std::optional<Color>& a_out)
            {
                const auto* value = Field(_tree, a_key);
                if (!value) return;
                if (value->is_string() && value->get_ref<const std::string&>().empty()) {
                    a_out.reset();
                    ++taken;
                    return;
                }
                const auto parsed = value->is_string() ? ParseColor(value->get_ref<const std::string&>()) : std::nullopt;
                if (!parsed) return Wrong(a_key, *value, "a colour or \"\"");
                a_out = *parsed;
                ++taken;
            }

            void Number(const char* a_key, double& a_out, double a_min, double a_max)
            {
                const auto* value = Field(_tree, a_key);
                if (!value) return;
                const auto number = value->is_number() ? value->get<double>() : std::nan("");
                if (!(number >= a_min && number <= a_max)) return Wrong(a_key, *value, std::format("a number {}..{}", a_min, a_max));
                a_out = number;
                ++taken;
            }

            void Count(const char* a_key, int& a_out, int a_min, int a_max)
            {
                double number = a_out;
                const auto before = taken;
                Number(a_key, number, a_min, a_max);
                if (taken != before) a_out = static_cast<int>(number);
            }

            void Switch(const char* a_key, bool& a_out)
            {
                const auto* value = Field(_tree, a_key);
                if (!value) return;
                if (!value->is_boolean()) return Wrong(a_key, *value, "true or false");
                a_out = value->get<bool>();
                ++taken;
            }

        private:
            void Wrong(const char* a_key, const Json& a_value, std::string_view a_want)
            {
                _notes.push_back(std::format("{}: tree.{} = {} is not {} - treeStyle.js default kept", _where, a_key, a_value.dump(), a_want));
            }

            const Json& _tree;
            std::string _where;
            std::vector<std::string>& _notes;
        };

        void ReadTokens(const Json& a_tree, const std::string& a_where, Tokens& a_tokens, std::vector<std::string>& a_notes, int& a_taken)
        {
            constexpr double kAlphaMax = 1.0, kInkMax = 1.0, kWidthMax = 50.0;
            TokenReader r(a_tree, a_where, a_notes);
            r.Count("labelMaxChars", a_tokens.labelMaxChars, 1, kMaxLabelChars);
            r.Colour("labelUnlocked", a_tokens.labelUnlocked);
            r.Colour("labelAvailable", a_tokens.labelAvailable);
            r.Colour("labelHidden", a_tokens.labelHidden);
            r.Colour("nodeFill", a_tokens.nodeFill);
            r.OptionalColour("unlockedFill", a_tokens.unlockedFill);
            r.OptionalColour("unlockedRim", a_tokens.unlockedRim);
            r.OptionalColour("unlockedCore", a_tokens.unlockedCore);
            r.OptionalColour("lockedStroke", a_tokens.lockedStroke);
            r.Colour("mysteryFill", a_tokens.mysteryFill);
            r.Colour("focusStroke", a_tokens.focusStroke);
            r.Number("availableAlpha", a_tokens.availableAlpha, 0.0, kAlphaMax);
            r.Switch("availableRing", a_tokens.availableRing);
            r.OptionalColour("hubRing", a_tokens.hubRing);
            r.OptionalColour("learningColor", a_tokens.learningColor);
            r.Number("schoolInk", a_tokens.schoolInk, 0.0, kInkMax);
            r.Colour("schoolInkTone", a_tokens.schoolInkTone);
            r.Colour("dimEdgeColor", a_tokens.dimEdgeColor);
            r.OptionalColour("unlockedEdgeColor", a_tokens.unlockedEdgeColor);
            r.Colour("lockedEdgeColor", a_tokens.lockedEdgeColor);
            r.Number("lockedEdgeAlpha", a_tokens.lockedEdgeAlpha, 0.0, kAlphaMax);
            r.Number("unlockedEdgeAlpha", a_tokens.unlockedEdgeAlpha, 0.0, kAlphaMax);
            r.Number("unlockedEdgeWidth", a_tokens.unlockedEdgeWidth, 0.0, kWidthMax);
            r.Number("frontierEdgeAlpha", a_tokens.frontierEdgeAlpha, 0.0, kAlphaMax);
            r.Colour("selectedPathColor", a_tokens.selectedPathColor);
            r.Number("selectedPathAlpha", a_tokens.selectedPathAlpha, 0.0, kAlphaMax);
            r.Number("selectedPathWidth", a_tokens.selectedPathWidth, 0.0, kWidthMax);
            a_taken = r.taken;
        }

        /// DesignPresets.renderValue(key, '') (designPresets.js:211-232): the design's "render" block, else the player's
        /// setting; a number is made text, other types skipped. "" when neither has it.
        std::string RenderValue(const Json* a_render, const Json* a_config, const char* a_key)
        {
            for (const auto* block : { a_render, a_config }) {
                const auto* value = block && block->is_object() ? Field(*block, a_key) : nullptr;
                if (!value || value->is_null()) continue;
                if (value->is_string()) return value->get<std::string>();
                if (value->is_number()) return value->dump();
            }
            return {};
        }

        /// _designOrPlayer (canvasRendererColors.js:39-46): the player's colour once changed from the shipped default, else
        /// the design's token, else the player's
        Color DesignOrPlayer(const std::optional<Color>& a_token, const std::string& a_player, std::string_view a_shipped,
            const char* a_what, std::vector<std::string>& a_notes)
        {
            const auto player = ParseColor(a_player);
            if (!player) {
                a_notes.push_back(std::format("config: {} \"{}\" is not a colour - HoM's {}", a_what, a_player, a_shipped));
            }
            const auto shipped = *ParseColor(a_shipped);
            if (!a_token) return player.value_or(shipped);
            const bool own = player && Lower(Trim(a_player)) != a_shipped;
            return own ? *player : *a_token;
        }

        struct Matched
        {
            std::string stem;
            Json data;
        };

        /// DesignPresets.onLoaded (designPresets.js:332-341): a file's id is its "id" (made text) else its file name, lowercased;
        /// a built-in id is skipped; a later file with the same id replaces an earlier one (files in name order here)
        std::optional<Matched> FindPreset(const std::string& a_id, std::vector<PresetFile> a_files, std::vector<std::string>& a_notes)
        {
            std::ranges::sort(a_files, {}, &PresetFile::stem);
            std::optional<Matched> found;
            for (const auto& file : a_files) {
                auto data = ParseJson(file.text);
                if (!data || !data->is_object()) {
                    a_notes.push_back(std::format("presets/design/{}.json is not a JSON object - skipped (HoM skips it too)", file.stem));
                    continue;
                }
                std::string id = file.stem;
                if (const auto* own = Field(*data, "id"); own && (own->is_string() || own->is_number())) {
                    const auto text = own->is_string() ? own->get<std::string>() : own->dump();
                    if (!text.empty()) id = text;
                }
                id = Lower(id);
                if (std::ranges::find(kBuiltIn, id) != kBuiltIn.end()) continue;
                if (id == a_id) found = Matched{ file.stem, std::move(*data) };
            }
            return found;
        }

        void ReadSchoolMaps(const Json& a_config, Style& a_style)
        {
            if (const auto* colours = Field(a_config, "schoolColors"); colours && colours->is_object()) {
                for (const auto& [school, value] : colours->items()) {
                    const auto parsed = value.is_string() ? ParseColor(value.get_ref<const std::string&>()) : std::nullopt;
                    if (parsed) {
                        a_style.schoolColors[school] = *parsed;
                    } else {
                        a_style.notes.push_back(std::format("config: schoolColors.{} = {} is not a colour - HoM's default", school, value.dump()));
                    }
                }
            }
            if (const auto* visibility = Field(a_config, "schoolVisibility"); visibility && visibility->is_object()) {
                for (const auto& [school, value] : visibility->items()) {
                    // treeStyle.js:370 / canvasRendererNodes.js:303: hidden only by a literal false
                    a_style.schoolVisibility[school] = !(value.is_boolean() && !value.get<bool>());
                }
            }
        }

        /// settingsPanel.js's load rules for the switches the window uses
        void ReadSwitches(const Json& a_config, Style& a_style)
        {
            const auto notFalse = [&](const char* a_key) {
                const auto* value = Field(a_config, a_key);
                return !(value && value->is_boolean() && !value->get<bool>());
            };
            if (const auto* value = Field(a_config, "discoveryMode")) a_style.discoveryMode = Truthy(*value);  // :1182, default true
            if (const auto* value = Field(a_config, "cheatMode")) a_style.cheatMode = Truthy(*value);          // :1072 `|| false`
            a_style.showNodeNames = notFalse("showNodeNames");                                                // :1074
            a_style.showSelectionPath = notFalse("showSelectionPath");                                        // :1434
            a_style.showBaseConnections = notFalse("showBaseConnections");                                    // :1435
            a_style.focusDimOthers = notFalse("focusDimOthers");                                              // :1447
            if (const auto* value = Field(a_config, "revealName")) {                                          // :1116, default 0
                if (value->is_number() && std::isfinite(value->get<double>())) {
                    a_style.revealName = value->get<double>();
                } else {
                    a_style.notes.push_back(std::format("config: revealName = {} is not a number - 0 (HoM's default)", value->dump()));
                }
            }
        }

        std::optional<std::string> ReadFile(const std::filesystem::path& a_path)
        {
            std::ifstream file(a_path, std::ios::binary);
            if (!file) return std::nullopt;
            std::string text{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
            if (file.bad()) return std::nullopt;
            return text;
        }

        std::filesystem::path Utf8Path(const std::string& a_path) { return std::filesystem::path(std::u8string(a_path.begin(), a_path.end())); }

        bool IsJsonExtension(std::wstring_view a_extension)
        {
            constexpr std::wstring_view kJson = L".json";
            return a_extension.size() == kJson.size() &&
                   std::ranges::equal(a_extension, kJson, [](wchar_t a, wchar_t b) { return std::towlower(a) == b; });
        }
    }

    std::optional<Color> ParseColor(std::string_view a_text)
    {
        a_text = Trim(a_text);
        if (a_text.starts_with('#')) return ParseHex(a_text.substr(1));
        return ParseFunction(a_text);
    }

    Color Ink(Color a_color, double a_amount, Color a_tone)
    {
        if (!(a_amount > 0)) return a_color;
        const auto mix = [&](double a_from, double a_to) { return a_from + (a_to - a_from) * a_amount; };
        // review v11 #3: TreeStyle.ink writes an opaque '#rrggbb' - a mixed colour loses its alpha
        return { Pack(mix(R(a_color), R(a_tone)), mix(G(a_color), G(a_tone)), mix(B(a_color), B(a_tone))), 1.0 };
    }

    Color Dim(Color a_color, double a_factor)
    {
        return { Pack(R(a_color) * a_factor, G(a_color) * a_factor, B(a_color) * a_factor), a_color.alpha };
    }

    Color InnerAccent(Color a_color)
    {
        constexpr double kRed = 0.35, kGreen = 0.3, kBlue = 0.4;
        return { Pack(R(a_color) * kRed, G(a_color) * kGreen, B(a_color) * kBlue), a_color.alpha };
    }

    std::string Text(Color a_color)
    {
        return a_color.alpha < 1.0 ? std::format("#{:06x}@{:.2f}", a_color.rgb, a_color.alpha) : std::format("#{:06x}", a_color.rgb);
    }

    double MysterySize(std::string_view a_skillLevel, int a_tier)
    {
        constexpr std::array<std::string_view, 5> kLevels{ "novice", "apprentice", "adept", "expert", "master" };
        constexpr int kMaxIndex = 4;
        const auto level = Lower(a_skillLevel);
        const auto it = std::ranges::find(kLevels, level);
        int index = 0;
        if (it != kLevels.end()) {
            index = static_cast<int>(it - kLevels.begin());
        } else if (a_tier > 0) {
            index = std::min(a_tier - 1, kMaxIndex);
        }
        return kLockedSize + index;
    }

    MarkSize SizeOf(Display a_display, bool a_mystery, std::string_view a_skillLevel, int a_tier, bool a_focus, bool a_availableRing)
    {
        MarkSize mark;
        switch (a_display) {
        case Display::kUnlocked:
        case Display::kLearning: mark = { kKnownSize, kKnownStroke, false }; break;
        case Display::kAvailable: mark = { kLearnableSize, kOtherStroke, a_availableRing }; break;
        default: mark = { a_mystery ? MysterySize(a_skillLevel, a_tier) : kLockedSize, a_mystery ? kMysteryStroke : kOtherStroke, false }; break;
        }
        if (a_focus) {
            mark.size += kFocusGrow;
            mark.stroke = kFocusStroke;
        }
        return mark;
    }

    double Reach(const MarkSize& a_mark, std::string_view a_school, double a_scale, double a_lineScale)
    {
        const double body = ShapeReach(a_school) * a_mark.size * a_scale + std::max(a_mark.stroke, kFocusStroke) * a_lineScale / 2;
        const double ring = a_mark.ring ? (a_mark.size + kRingGap) * a_scale + kRingWidth * a_lineScale / 2 : 0.0;
        return std::max(body, ring);
    }

    double MaxReach(double a_scale, bool a_availableRing, double a_lineScale)
    {
        constexpr int kMaxTier = 5;  // MysterySize's tiers 0..4 come from tier 1..5
        double reach = 0.0;
        for (const std::string_view school : { "Destruction", "Alteration", "Conjuration", "Illusion", "Restoration" }) {
            for (const auto display : { Display::kUnlocked, Display::kLearning, Display::kAvailable, Display::kLocked }) {
                for (const bool mystery : { false, true }) {
                    for (const bool focus : { false, true }) {
                        reach = std::max(reach, Reach(SizeOf(display, mystery, "", kMaxTier, focus, a_availableRing), school, a_scale, a_lineScale));
                    }
                }
            }
        }
        return reach;
    }

    std::vector<Point> ShapePoints(std::string_view a_school, double a_worldX, double a_worldY, double a_cx, double a_cy, double a_size)
    {
        std::vector<Point> out;
        const auto* shape = ShapeOf(a_school);
        if (!shape) {
            out.reserve(kCircleSegments);
            for (int i = 0; i < kCircleSegments; ++i) {
                const double a = 2 * std::numbers::pi * i / kCircleSegments;
                out.push_back({ a_cx + a_size * std::cos(a), a_cy + a_size * std::sin(a) });
            }
            return out;
        }
        const double turn = std::atan2(a_worldY, a_worldX) + shape->turn;
        const double c = std::cos(turn) * a_size, s = std::sin(turn) * a_size;
        out.reserve(shape->points.size());
        for (const auto& p : shape->points) {
            out.push_back({ a_cx + p.x * c - p.y * s, a_cy + p.x * s + p.y * c });
        }
        return out;
    }

    double ShapeReach(std::string_view a_school)
    {
        const auto* shape = ShapeOf(a_school);
        if (!shape) return 1.0;
        double reach = 0.0;
        for (const auto& p : shape->points) reach = std::max(reach, std::hypot(p.x, p.y));
        return reach;
    }

    std::vector<Segment> DashOutline(const std::vector<Point>& a_points, double a_dash, double a_gap)
    {
        constexpr double kTiny = 1e-9;
        std::vector<Segment> out;
        if (a_points.size() < 2 || !(a_dash > 0)) return out;
        const double gap = std::max(0.0, a_gap), period = a_dash + gap;
        double phase = 0.0;  // where in the pattern the walk is: [0, a_dash) drawn, [a_dash, period) left out
        for (std::size_t i = 0; i < a_points.size(); ++i) {
            const auto& from = a_points[i];
            const auto& to = a_points[(i + 1) % a_points.size()];
            const double length = std::hypot(to.x - from.x, to.y - from.y);
            if (length <= kTiny) continue;
            const auto at = [&](double a_t) { return Point{ from.x + (to.x - from.x) * a_t / length, from.y + (to.y - from.y) * a_t / length }; };
            double t = 0.0;
            while (length - t > kTiny) {
                const bool drawn = phase < a_dash;
                const double step = std::min((drawn ? a_dash : period) - phase, length - t);
                if (step <= 0) {  // rounding left the phase on a boundary: step past it
                    phase = drawn ? a_dash : 0.0;
                    continue;
                }
                if (drawn) out.push_back({ at(t), at(t + step) });
                t += step;
                phase += step;
                if (phase >= period - kTiny) phase = 0.0;
            }
        }
        return out;
    }

    Display DisplayOf(Behaviour a_state, bool a_isTarget)
    {
        switch (a_state) {
        case Behaviour::kMastered: return Display::kUnlocked;
        case Behaviour::kUnknown: return Display::kLocked;  // no state for it yet: nothing claimed
        default: break;
        }
        if (a_isTarget || a_state == Behaviour::kTarget) return Display::kLearning;
        switch (a_state) {
        case Behaviour::kReady:
        case Behaviour::kLearning:
        case Behaviour::kAvailable: return Display::kAvailable;
        default: return Display::kLocked;
        }
    }

    const char* DisplayName(Display a_display)
    {
        switch (a_display) {
        case Display::kUnlocked: return "unlocked";
        case Display::kLearning: return "learning";
        case Display::kAvailable: return "available";
        default: return "locked";
        }
    }

    Name Reveal(Display a_display, double a_percent, double a_revealName, bool a_cheatMode)
    {
        // review v11 #9: cheatMode names a locked spell in the tooltip and the card (canvasRendererInput.js:29,34,
        // spellCardView.js:190); its label stays none (NodeLabelText checks kLocked first)
        if (a_display == Display::kLocked) return a_cheatMode ? Name::kShown : Name::kNone;
        if (a_display == Display::kAvailable && !a_cheatMode && a_percent < a_revealName) return Name::kHidden;
        return Name::kShown;
    }

    std::string ClipLabel(std::string_view a_name, int a_maxChars)
    {
        const auto isLead = [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; };
        const auto chars = static_cast<int>(std::ranges::count_if(a_name, isLead));
        if (a_maxChars < 1 || chars <= a_maxChars) return std::string(a_name);
        int kept = 0;
        std::size_t i = 0;
        for (; i < a_name.size(); ++i) {
            if (isLead(a_name[i]) && kept++ == a_maxChars - 1) break;
        }
        return std::string(a_name.substr(0, i)) + kClipMark;
    }

    bool Style::SchoolShown(const std::string& a_school) const
    {
        const auto it = schoolVisibility.find(a_school);
        return it == schoolVisibility.end() || it->second;
    }

    Color Style::SchoolColor(const std::string& a_school, std::string_view a_themeColor) const
    {
        if (!a_themeColor.empty()) {
            if (const auto theme = ParseColor(a_themeColor)) return Ink(*theme, tree.schoolInk, tree.schoolInkTone);
        }
        Color colour{ kFallbackSchool, 1.0 };
        if (const auto it = schoolColors.find(a_school); it != schoolColors.end()) {
            colour = it->second;
        } else {
            // getOrAssignSchoolColor would give a new school a palette colour (colorUtils.js:13); the fixed grey here
            const auto known = std::ranges::find(kDefaultSchools, a_school, &std::pair<std::string_view, std::uint32_t>::first);
            if (known != kDefaultSchools.end()) colour = { known->second, 1.0 };
        }
        return Ink(colour, tree.schoolInk, tree.schoolInkTone);
    }

    std::string Style::Summary() const
    {
        return std::format("config {}, design preset '{}' {}, discoveryMode {}, cheatMode {}, showSelectionPath {}, showNodeNames {}, "
                           "showBaseConnections {}, focusDimOthers {}, revealName {}, learningPathColor {}, hub ring {}, schoolInk {} toward {}, "
                           "labelMaxChars {}, {} school colour(s), {} school(s) hidden, {} note(s)",
            configFound ? "found" : "missing", presetId, presetFile.empty() ? "not read - treeStyle.js DEFAULTS" : "from " + presetFile,
            discoveryMode, cheatMode, showSelectionPath, showNodeNames, showBaseConnections, focusDimOthers, revealName, Text(learning),
            Text(hubRing), tree.schoolInk, Text(tree.schoolInkTone), tree.labelMaxChars, schoolColors.size(),
            std::ranges::count_if(schoolVisibility, [](const auto& a_entry) { return !a_entry.second; }), notes.size());
    }

    Style Build(const std::optional<std::string>& a_config, const std::vector<PresetFile>& a_presets)
    {
        Style style;
        Json config = Json::object();
        style.configFound = a_config.has_value();
        if (!a_config) {
            style.notes.push_back("config.json not found - every setting at HoM's default");
        } else if (auto parsed = ParseJson(*a_config); !parsed || !parsed->is_object()) {
            style.notes.push_back("config.json is not a JSON object - every setting at HoM's default");
        } else {
            config = std::move(*parsed);
        }

        // designPresets.js:186 savedChoice: config's designPreset lowercased, else the default design (built in)
        if (const auto* design = Field(config, "designPreset"); design && design->is_string() && !design->get_ref<const std::string&>().empty()) {
            style.presetId = Lower(design->get<std::string>());
        } else {
            style.presetId = kDefaultDesign;
            style.notes.push_back(std::format("config: no designPreset - HoM's default design '{}'", kDefaultDesign));
        }
        const Json* render = nullptr;
        std::optional<Matched> preset;
        if (std::ranges::find(kBuiltIn, style.presetId) != kBuiltIn.end()) {
            style.notes.push_back(std::format("preset {} is one of HoM's built-in designs, which the prototype does not read - treeStyle.js DEFAULTS",
                style.presetId));
        } else if (preset = FindPreset(style.presetId, a_presets, style.notes); !preset) {
            style.notes.push_back(std::format("preset {} not found, HoM's built-in presets are not read by the prototype - treeStyle.js DEFAULTS",
                style.presetId));
        } else {
            style.presetFile = "presets/design/" + preset->stem + ".json";
            int taken = 0;
            if (const auto* tree = Field(preset->data, "tree"); tree && tree->is_object()) {
                ReadTokens(*tree, style.presetFile, style.tree, style.notes, taken);
            } else {
                style.notes.push_back(style.presetFile + ": no \"tree\" object - treeStyle.js DEFAULTS");
            }
            render = Field(preset->data, "render");
        }

        ReadSwitches(config, style);
        ReadSchoolMaps(config, style);
        // settingsPanel.js:2310-2311: renderValue(key, '') || HoM's default, then _designOrPlayer with the design's token
        auto learningPath = RenderValue(render, &config, "learningPathColor");
        if (learningPath.empty()) learningPath = kDefaultLearningPath;
        auto heartRing = RenderValue(render, &config, "heartRingColor");
        if (heartRing.empty()) heartRing = kDefaultHeartRing;
        style.learning = DesignOrPlayer(style.tree.learningColor, learningPath, kDefaultLearningPath, "learningPathColor", style.notes);
        style.hubRing = DesignOrPlayer(style.tree.hubRing, heartRing, kDefaultHeartRing, "heartRingColor", style.notes);
        return style;
    }

    Style Read(const std::string& a_configPath, const std::string& a_presetDir)
    {
        const auto config = ReadFile(Utf8Path(a_configPath));
        std::vector<PresetFile> presets;
        std::vector<std::string> readNotes;
        std::error_code ec;
        const auto dir = Utf8Path(a_presetDir);
        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            const auto& path = it->path();
            // review v11 #4: the extension compared as wide text (string() throws std::system_error on a name outside the
            // ANSI code page), and a bad entry's own error code, so it neither ends the listing nor reads as an unlistable folder
            std::error_code entryError;
            if (!it->is_regular_file(entryError) || entryError || !IsJsonExtension(path.extension().native())) continue;
            const auto stem = path.stem().u8string();
            if (auto text = ReadFile(path)) {
                presets.push_back({ std::string(stem.begin(), stem.end()), std::move(*text) });
            } else {
                readNotes.push_back(std::format("presets/design/{}.json could not be read - skipped", std::string(stem.begin(), stem.end())));
            }
        }
        if (ec) readNotes.push_back(std::format("{} could not be listed ({}) - no design preset files", a_presetDir, ec.message()));
        auto style = Build(config, presets);
        style.notes.insert(style.notes.begin(), readNotes.begin(), readNotes.end());
        return style;
    }
}

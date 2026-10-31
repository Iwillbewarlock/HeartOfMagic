#pragma once

#include <DirectXPackedVector.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numbers>
#include <optional>
#include <string_view>
#include <vector>

// =============================================================================
// PREVIEW MATH - the arithmetic of Preview.cpp's place, fit and clip, with no
// game types (spec v5 Q5): the plugin converts its NiPoint3 / NiMatrix3 at the
// edge, tools/magic-menu-preview-check runs these same functions on made-up
// cases. A camera looks along Axes::look with up and right (Preview.cpp
// AxesOf); a point's frustum share across is (p - eye).right / (p - eye).look,
// along the same with up. Rects are y down: stage units or pixels.
// =============================================================================

namespace PreviewMath
{
    /// A PostDisplay with no box to draw in (nothing chosen, the Powers hide): every model up is hidden (P7.2) while place/fit is on
    /// (bMovePreview=1) and the preview block is there, and on the Powers tab with bHidePowers3D (a_hide3D) whatever the rest says.
    /// Fix round 1 (GPT P2): a preview block hidden by [Blocks] sHidden counts as bMovePreview=0 - the game's own 3D stays, and the
    /// caller gives back what the Powers hide hid when it ends (before, the tab hide went on and the model stayed hidden)
    constexpr bool HidesForTab(bool a_movePreview, bool a_previewBlockHidden, bool a_hide3D)
    {
        return (a_movePreview && !a_previewBlockHidden) || a_hide3D;
    }

    inline constexpr float kGameShare = 0.45f;     // share of the screen height the game's own preview takes (IR)
    inline constexpr float kFill = 0.92f;          // what is seen of a model fills this share of the box
    inline constexpr float kGrowMost = 3.0f;       // and grows at most this many times the game's size
    inline constexpr int kSolveSteps = 20;         // halvings of the grow search (kGrowMost / 2^20 apart at the end)
    inline constexpr float kStableShare = 0.10f;   // two sphere readings agree within this share of the half size
    inline constexpr int kSpherePoints = 8;        // points round a bound sphere in the camera's right/up plane
    inline constexpr float kZeroTolerance = 1e-5f; // WorldPtToScreenPt3's divide guard
    inline constexpr double kPixelSnap = 1e-6;     // a scissor edge this close to a whole pixel counts as on it

    struct Vec3
    {
        float x{}, y{}, z{};

        friend constexpr Vec3 operator+(const Vec3& a_lhs, const Vec3& a_rhs) { return { a_lhs.x + a_rhs.x, a_lhs.y + a_rhs.y, a_lhs.z + a_rhs.z }; }
        friend constexpr Vec3 operator-(const Vec3& a_lhs, const Vec3& a_rhs) { return { a_lhs.x - a_rhs.x, a_lhs.y - a_rhs.y, a_lhs.z - a_rhs.z }; }
        friend constexpr Vec3 operator*(const Vec3& a_lhs, float a_scale) { return { a_lhs.x * a_scale, a_lhs.y * a_scale, a_lhs.z * a_scale }; }
        friend constexpr Vec3 operator/(const Vec3& a_lhs, float a_scale) { return { a_lhs.x / a_scale, a_lhs.y / a_scale, a_lhs.z / a_scale }; }
        constexpr Vec3& operator+=(const Vec3& a_rhs) { return *this = *this + a_rhs; }
        constexpr Vec3& operator*=(float a_scale) { return *this = *this * a_scale; }

        [[nodiscard]] constexpr float Dot(const Vec3& a_rhs) const { return x * a_rhs.x + y * a_rhs.y + z * a_rhs.z; }
        [[nodiscard]] float Length() const { return std::sqrt(Dot(*this)); }
        [[nodiscard]] bool Finite() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
    };

    /// A 3x3 turn by rows, as NiMatrix3::entry (point = entry * point, NiMatrix3::operator*)
    struct Mat3
    {
        float entry[3][3]{};

        [[nodiscard]] constexpr Mat3 Transpose() const
        {
            Mat3 result;
            for (int row = 0; row < 3; ++row) {
                for (int column = 0; column < 3; ++column) result.entry[row][column] = entry[column][row];
            }
            return result;
        }

        friend constexpr Vec3 operator*(const Mat3& a_turn, const Vec3& a_point)
        {
            const auto& m = a_turn.entry;
            return { m[0][0] * a_point.x + m[0][1] * a_point.y + m[0][2] * a_point.z, m[1][0] * a_point.x + m[1][1] * a_point.y + m[1][2] * a_point.z,
                m[2][0] * a_point.x + m[2][1] * a_point.y + m[2][2] * a_point.z };
        }
    };

    struct Axes
    {
        Vec3 look, up, right;
    };

    /// NiFrustum's sides at depth 1 (fLeft, fRight, fTop, fBottom)
    struct Frustum
    {
        float left{}, right{}, top{}, bottom{};
    };

    struct Rect
    {
        double left{}, top{}, right{}, bottom{};

        [[nodiscard]] constexpr double Width() const { return right - left; }
        [[nodiscard]] constexpr double Height() const { return bottom - top; }
        [[nodiscard]] constexpr double CenterX() const { return (left + right) / 2; }
        [[nodiscard]] constexpr double CenterY() const { return (top + bottom) / 2; }
    };

    inline float DepthOf(const Vec3& a_point, const Vec3& a_eye, const Axes& a_axes)
    {
        return (a_point - a_eye).Dot(a_axes.look);
    }

    // -------------------------------------------------------------------------
    // Place: the box as a window on the frustum at the game's preview depth
    // -------------------------------------------------------------------------

    /// The preview box at the preview's depth, world units
    struct Room
    {
        float wide{}, high{};
        // the line of sight through the box's middle: look + right * across + up * along (frustum units), and the
        // depth wide/high hold at; a model at another depth is aimed on that line at its own depth (review R6)
        float across{}, along{}, depth{};
    };

    struct Placed
    {
        Vec3 place;                 // the box's middle at the game's depth
        float scale{};              // the base scale for the box
        float fromLeft{}, fromTop{};  // the box's middle as shares of the visible rect
        Room room;
    };

    /// The box's middle as a world point at the depth of the game's own place (a_gamePos, never the live one), the
    /// base scale for its height (a_gameScale * box share / kGameShare) and the room. None for an empty box or visible
    /// rect, a flat frustum, a game scale that is not positive, or a game place not in front of the camera (an empty
    /// box would make the base scale 0 or negative, second review X3)
    inline std::optional<Placed> Place(const Vec3& a_eye, const Axes& a_axes, const Frustum& a_frustum, const Vec3& a_gamePos, float a_gameScale,
        const Rect& a_box, const Rect& a_visible)
    {
        const float depth = DepthOf(a_gamePos, a_eye, a_axes);
        const double visibleW = a_visible.Width(), visibleH = a_visible.Height();
        if (!(depth > 0.0f) || !(a_gameScale > 0.0f) || !(visibleW > 0) || !(visibleH > 0) || a_frustum.right == a_frustum.left ||
            a_frustum.top == a_frustum.bottom || !(a_box.Width() > 0) || !(a_box.Height() > 0)) {
            return std::nullopt;
        }
        Placed placed;
        placed.fromLeft = static_cast<float>((a_box.CenterX() - a_visible.left) / visibleW);
        placed.fromTop = static_cast<float>((a_box.CenterY() - a_visible.top) / visibleH);
        const float across = a_frustum.left + (a_frustum.right - a_frustum.left) * placed.fromLeft;
        const float along = a_frustum.top + (a_frustum.bottom - a_frustum.top) * placed.fromTop;
        placed.place = a_eye + (a_axes.look + a_axes.right * across + a_axes.up * along) * depth;
        placed.scale = a_gameScale * static_cast<float>(a_box.Height() / visibleH) / kGameShare;
        const float wide = std::abs(depth * (a_frustum.right - a_frustum.left) * static_cast<float>(a_box.Width() / visibleW));
        const float high = std::abs(depth * (a_frustum.top - a_frustum.bottom) * static_cast<float>(a_box.Height() / visibleH));
        placed.room = Room{ wide, high, across, along, depth };
        return placed;
    }

    /// Where a model at a_modelPos is aimed: on the room's line of sight at the model's own depth, with the room
    /// scaled to that depth (perspective, review R6); a model not in front of the camera takes the room's depth
    struct Aimed
    {
        Vec3 aim;
        float depth{}, share{}, wide{}, high{};
    };

    inline Aimed AimAt(const Vec3& a_modelPos, const Vec3& a_eye, const Axes& a_axes, const Room& a_room)
    {
        const float own = DepthOf(a_modelPos, a_eye, a_axes);
        const float depth = own > 0.0f ? own : a_room.depth;
        const float share = depth / a_room.depth;
        return { a_eye + (a_axes.look + a_axes.right * a_room.across + a_axes.up * a_room.along) * depth, depth, share, a_room.wide * share,
            a_room.high * share };
    }

    /// The part of a move across the line of sight (on the camera's right and up)
    inline Vec3 Across(const Vec3& a_move, const Axes& a_axes)
    {
        return a_axes.right * a_move.Dot(a_axes.right) + a_axes.up * a_move.Dot(a_axes.up);
    }

    /// A world move as a change of a child's local.translate: through its parent's world turn and scale (world =
    /// parent.rotate * local * parent.scale + parent.translate, NiTransform::operator*); a scale not above 0 counts as 1
    inline Vec3 LocalMove(const Mat3& a_parentTurn, float a_parentScale, const Vec3& a_move)
    {
        const float scale = a_parentScale > 0.0f ? a_parentScale : 1.0f;
        return a_parentTurn.Transpose() * a_move / scale;
    }

    /// A cache entry kept from an earlier menu whose model is not at the scale we left it at: a model built anew at
    /// the same address (same spell), so the entry's scales are not its own (second review X2). In the same menu a
    /// changed scale is the game's (Preview.cpp Notice), not a new model
    inline bool Rebuilt(bool a_earlierMenu, float a_localScale, float a_setScale)
    {
        return a_earlierMenu && a_localScale != a_setScale;
    }

    // -------------------------------------------------------------------------
    // Measure: what the camera sees of a model
    // -------------------------------------------------------------------------

    struct Seen  // at the game's scale; middle measured from the model's place, on the camera's right/up
    {
        float wide{}, high{};
        Vec3 middle;
    };

    struct Measured
    {
        Seen seen;
        // the measured points from the model's place, at the game's size: grown and projected again to choose the
        // grow (third check: a perspective size does not scale with grow)
        std::vector<Vec3> outline;
    };

    /// A bound sphere as kSpherePoints points round its centre in the camera's right/up plane (its seen size whichever
    /// way it turns); none when the radius is not positive or a value is not finite
    inline std::vector<Vec3> SpherePoints(const Vec3& a_centre, float a_radius, const Axes& a_axes)
    {
        std::vector<Vec3> points;
        if (!(a_radius > 0.0f) || !std::isfinite(a_radius) || !a_centre.Finite()) return points;
        points.reserve(kSpherePoints);
        for (int i = 0; i < kSpherePoints; ++i) {
            const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / kSpherePoints;
            points.push_back(a_centre + (a_axes.right * std::cos(angle) + a_axes.up * std::sin(angle)) * a_radius);
        }
        return points;
    }

    /// What is seen of world points a_world of a model placed at a_place, scaled to its game scale by a_toBaseline;
    /// none when there are none, a value is not finite, or a point is not in front of the camera. Seen as the camera
    /// sees it: each point is brought along its line of sight to the model's own depth d_m ((p - eye) . right * d_m /
    /// (p - eye) . look, the same for up), so a near point counts bigger, as the self-test projects it (second review R4)
    inline std::optional<Measured> Measure(const std::vector<Vec3>& a_world, const Vec3& a_place, const Vec3& a_eye, const Axes& a_axes, float a_toBaseline)
    {
        if (a_world.empty()) return std::nullopt;
        const auto centre = a_place - a_eye;
        const float depth = centre.Dot(a_axes.look);
        if (!(depth > kZeroTolerance)) return std::nullopt;
        const float centreAcross = centre.Dot(a_axes.right), centreAlong = centre.Dot(a_axes.up);
        constexpr float kHuge = std::numeric_limits<float>::max();
        float left = kHuge, right = -kHuge, low = kHuge, high = -kHuge;
        Measured measured;
        measured.outline.reserve(a_world.size());
        for (const auto& point : a_world) {
            measured.outline.push_back((point - a_place) * a_toBaseline);
            const auto from = point - a_eye;
            const float pointDepth = from.Dot(a_axes.look);
            if (!(pointDepth > kZeroTolerance)) return std::nullopt;
            const float toDepth = depth / pointDepth;
            const float across = from.Dot(a_axes.right) * toDepth - centreAcross, along = from.Dot(a_axes.up) * toDepth - centreAlong;
            left = std::min(left, across);
            right = std::max(right, across);
            low = std::min(low, along);
            high = std::max(high, along);
        }
        auto& seen = measured.seen;
        seen.wide = (right - left) * a_toBaseline;
        seen.high = (high - low) * a_toBaseline;
        seen.middle = (a_axes.right * ((left + right) / 2) + a_axes.up * ((low + high) / 2)) * a_toBaseline;
        if (!std::isfinite(seen.wide) || !std::isfinite(seen.high) || !(seen.wide > 0) || !(seen.high > 0) || !seen.middle.Finite()) {
            return std::nullopt;
        }
        return measured;
    }

    /// Two readings agree: size and middle within kStableShare of the first one's half size
    inline bool Agrees(const Seen& a_one, const Seen& a_other)
    {
        const float half = std::max(a_one.wide, a_one.high) / 2;
        const float most = kStableShare * half;
        return std::abs(a_one.wide - a_other.wide) < most && std::abs(a_one.high - a_other.high) < most && (a_one.middle - a_other.middle).Length() < most;
    }

    // -------------------------------------------------------------------------
    // Fit: the grow that fills the room
    // -------------------------------------------------------------------------

    struct Extent
    {
        float left = std::numeric_limits<float>::max(), right = -std::numeric_limits<float>::max(), low = std::numeric_limits<float>::max(),
              high = -std::numeric_limits<float>::max();
    };

    /// The outline grown by a_grow about a place at a_toAim (from the eye), projected as Measure does (along each point's
    /// line of sight to a_depth, from the aim on the camera's right/up); false when a point is not in front of the camera
    inline bool GrownExtent(const std::vector<Vec3>& a_outline, float a_grow, const Vec3& a_toAim, const Axes& a_axes, float a_depth, Extent& a_out)
    {
        const float aimAcross = a_toAim.Dot(a_axes.right), aimAlong = a_toAim.Dot(a_axes.up);
        a_out = {};
        for (const auto& point : a_outline) {
            const auto from = a_toAim + point * a_grow;
            const float pointDepth = from.Dot(a_axes.look);
            if (!(pointDepth > kZeroTolerance)) return false;
            const float toDepth = a_depth / pointDepth;
            const float across = from.Dot(a_axes.right) * toDepth - aimAcross, along = from.Dot(a_axes.up) * toDepth - aimAlong;
            a_out = { std::min(a_out.left, across), std::max(a_out.right, across), std::min(a_out.low, along), std::max(a_out.high, along) };
        }
        return !a_outline.empty();
    }

    struct Solved  // the grow worked out for one reading in one room at one depth
    {
        bool valid = false;
        float depth{}, wide{}, high{};  // what it was worked out for
        float grow = 1;
        Vec3 middle;                    // what is seen of the grown model, from its place, on the camera's right/up
        int steps = 0;                  // search steps (0: kGrowMost fits)
    };

    /// The largest grow in (0, kGrowMost] whose grown, re-projected outline fills at most kFill of the room both ways.
    /// Growing about the model's place moves each point's depth too, so the perspective size is not seen * grow
    /// (third check: centre depth 10, vertices at 15, 20 x 20 outline, room 10 x 10 -> seen 13.33, grow .69, but the
    /// grown outline projects 10.26 and overflows). Bisection, kSolveSteps steps; the seen size grows with the grow
    /// (assumed, true for points in front of the camera). Bound-sphere points take the same path.
    /// O(kSolveSteps * points)
    inline Solved Solve(const std::vector<Vec3>& a_outline, const Vec3& a_toAim, const Axes& a_axes, float a_depth, float a_wide, float a_high)
    {
        Solved solved{ true, a_depth, a_wide, a_high };
        const auto fits = [&](float a_grow, Extent& a_extent) {
            return GrownExtent(a_outline, a_grow, a_toAim, a_axes, a_depth, a_extent) && a_extent.right - a_extent.left <= a_wide * kFill &&
                   a_extent.high - a_extent.low <= a_high * kFill;
        };
        Extent extent;
        float grow = kGrowMost;
        if (!fits(grow, extent)) {
            float low = 0.0f, high = kGrowMost;
            for (int step = 0; step < kSolveSteps; ++step) {
                const float middle = (low + high) / 2;
                (fits(middle, extent) ? low : high) = middle;
                ++solved.steps;
            }
            grow = low > 0.0f ? low : kGrowMost / static_cast<float>(1 << kSolveSteps);
            GrownExtent(a_outline, grow, a_toAim, a_axes, a_depth, extent);
        }
        solved.grow = grow;
        solved.middle = a_axes.right * ((extent.left + extent.right) / 2) + a_axes.up * ((extent.low + extent.high) / 2);
        return solved;
    }

    // -------------------------------------------------------------------------
    // Clip: the box in render-target pixels (spec v5 Q3 A)
    // -------------------------------------------------------------------------

    /// A stage rect through the movie's visible rect onto its viewport (a_viewport: left, top, right, bottom in pixels of
    /// the viewport's buffer)
    inline Rect StageToPixels(const Rect& a_stage, const Rect& a_visible, const Rect& a_viewport)
    {
        const double visibleW = a_visible.Width(), visibleH = a_visible.Height();
        const double viewportW = a_viewport.Width(), viewportH = a_viewport.Height();
        const auto x = [&](double a_x) { return a_viewport.left + (a_x - a_visible.left) / visibleW * viewportW; };
        const auto y = [&](double a_y) { return a_viewport.top + (a_y - a_visible.top) / visibleH * viewportH; };
        return { x(a_stage.left), y(a_stage.top), x(a_stage.right), y(a_stage.bottom) };
    }

    inline constexpr double kScreenMatchShare = 0.1;   // TranslateToScreen's box is used when its size is within this share of the mapping's
    inline constexpr double kScreenMatchPixels = 2.0;  // and its centre within this many pixels (review round 1, #13)

    /// The box through GFxMovieView::TranslateToScreen agrees with the visible-rect mapping: finite, the same size within
    /// kScreenMatchShare and the same centre within kScreenMatchPixels (a size check alone let a shifted box through)
    inline bool ScreenAgrees(const Rect& a_mapped, const Rect& a_translated)
    {
        const double mappedW = a_mapped.Width(), mappedH = a_mapped.Height();
        const double translatedW = a_translated.Width(), translatedH = a_translated.Height();
        return std::isfinite(translatedW) && std::isfinite(translatedH) && std::isfinite(a_translated.CenterX()) &&
               std::isfinite(a_translated.CenterY()) && std::abs(translatedW - mappedW) <= kScreenMatchShare * std::abs(mappedW) &&
               std::abs(translatedH - mappedH) <= kScreenMatchShare * std::abs(mappedH) &&
               std::hypot(a_translated.CenterX() - a_mapped.CenterX(), a_translated.CenterY() - a_mapped.CenterY()) <= kScreenMatchPixels;
    }

    /// As D3D11_RECT: left, top inclusive, right, bottom exclusive (pixels kept: x1 <= x < x2)
    struct PixelBox
    {
        std::int32_t x1{}, y1{}, x2{}, y2{};

        bool operator==(const PixelBox&) const = default;

        /// a_inner lies wholly inside this box
        [[nodiscard]] constexpr bool Holds(const PixelBox& a_inner) const
        {
            return a_inner.x1 >= x1 && a_inner.y1 >= y1 && a_inner.x2 <= x2 && a_inner.y2 <= y2;
        }
    };

    /// What an over-pass does with the scissor ScissorBox gave (blocks plan Task 3, review fix 1; final review)
    enum class ClipPlan
    {
        kScissor,       // the scissor rect has pixels: Render() scissored to them
        kNothingShows,  // the box alone has pixels, cut to its column none (under one pixel left): no Render(), no alpha save
        kUnclipped      // the box alone has none either (a buffer or target size of 0, a sub-pixel box): build 13's fallback
                        // (drawn unclipped, the whole target's alpha restored)
    };

    /// a_scissor: ScissorBox of the rect the over-pass scissors to (the box cut to its column, or the column's viewport);
    /// a_alone: ScissorBox of the box alone, by the same mapping. Only a box with pixels of its own that its column cuts away
    /// shows nothing; whatever else leaves no pixel (the preview always has a column clip, so this cannot key on that) draws
    /// as build 13 did
    inline ClipPlan PlanClip(const std::optional<PixelBox>& a_scissor, const std::optional<PixelBox>& a_alone)
    {
        if (a_scissor) return ClipPlan::kScissor;
        return a_alone ? ClipPlan::kNothingShows : ClipPlan::kUnclipped;
    }

    /// SelfTest L2 preview clip (final review: a real test): judged only on a frame whose column cuts the box in pixels (the
    /// box alone does not lie inside the column's pixels); then the scissor must differ from the box alone and lie inside the
    /// column's pixels. kNotCut frames are counted, never a PASS
    enum class ColumnClipVerdict
    {
        kPass,
        kFail,
        kNotCut,
    };

    inline ColumnClipVerdict JudgeColumnClip(const PixelBox& a_scissor, const std::optional<PixelBox>& a_alone, const std::optional<PixelBox>& a_column)
    {
        if (a_alone && a_column && a_column->Holds(*a_alone)) return ColumnClipVerdict::kNotCut;
        const bool differs = !a_alone || !(a_scissor == *a_alone);
        const bool inside = a_column && a_column->Holds(a_scissor);
        return differs && inside ? ColumnClipVerdict::kPass : ColumnClipVerdict::kFail;
    }

    /// The box (a_pixels, in pixels of the viewport's buffer) in pixels of the bound render target: scaled from the
    /// buffer's size to the target's (Community Shaders' upscaling and frame generation can bind a target of another
    /// size; Grid Inventory ItemPreview.cpp:1397-1436 reads the bound one the same way), rounded inward (no pixel
    /// outside the box is kept) and clamped to the target. None when a size is not positive, a value is not finite, or
    /// nothing is left
    inline std::optional<PixelBox> ScissorBox(const Rect& a_pixels, double a_bufferW, double a_bufferH, double a_targetW, double a_targetH)
    {
        if (!(a_bufferW > 0) || !(a_bufferH > 0) || !(a_targetW > 0) || !(a_targetH > 0)) return std::nullopt;
        const double toX = a_targetW / a_bufferW, toY = a_targetH / a_bufferH;
        double left = std::ceil(a_pixels.left * toX - kPixelSnap), top = std::ceil(a_pixels.top * toY - kPixelSnap);
        double right = std::floor(a_pixels.right * toX + kPixelSnap), bottom = std::floor(a_pixels.bottom * toY + kPixelSnap);
        if (!std::isfinite(left) || !std::isfinite(top) || !std::isfinite(right) || !std::isfinite(bottom)) return std::nullopt;
        left = std::max(left, 0.0);
        top = std::max(top, 0.0);
        right = std::min(right, std::floor(a_targetW));
        bottom = std::min(bottom, std::floor(a_targetH));
        if (!(right > left) || !(bottom > top)) return std::nullopt;
        return PixelBox{ static_cast<std::int32_t>(left), static_cast<std::int32_t>(top), static_cast<std::int32_t>(right), static_cast<std::int32_t>(bottom) };
    }

    // -------------------------------------------------------------------------
    // Animation while hidden from the game's own pass (spec v5 Q3: unverified that culling leaves it running)
    // -------------------------------------------------------------------------

    inline constexpr float kSizeMoved = 1e-4f;  // a bound size change under this share of the size is not movement

    enum class Motion
    {
        kMoved,  // its first time controller's time or its own-scale bound size changed
        kStill,  // it has a time controller and nothing changed
        kNone,   // no time controller and a steady size: nothing shows animation
    };

    /// Samples of one model while hidden. A sample counts only while our fit has settled (a_steady) and has not changed
    /// the scale it leaves the model at (a_setScale) since the series began; otherwise the series starts over, so our own
    /// rescale is never taken for animation (review round 1, #4/#7). The bound radius is compared at the model's own
    /// scale (radius / world scale): a grow of ours moves the radius, not that size
    struct MotionSamples
    {
        int samples = 0;
        int restarts = 0;  // the series started over after at least one sample
        std::optional<float> firstTime, lastTime;
        float firstSize = 0, lastSize = 0;
        float setScale = 0;  // our scale when the series began

        /// False when this sample did not count (the fit has not settled)
        bool Add(std::optional<float> a_time, float a_radius, float a_worldScale, bool a_steady, float a_setScale)
        {
            if (!a_steady || (samples > 0 && a_setScale != setScale)) {
                if (samples > 0) ++restarts;
                samples = 0;
                if (!a_steady) return false;
            }
            const float size = a_worldScale > 0.0f ? a_radius / a_worldScale : a_radius;
            if (samples == 0) {
                firstTime = a_time;
                firstSize = size;
                setScale = a_setScale;
            }
            lastTime = a_time;
            lastSize = size;
            ++samples;
            return true;
        }

        [[nodiscard]] bool TimeMoved() const { return firstTime && lastTime && *firstTime != *lastTime; }
        [[nodiscard]] bool SizeMoved() const { return std::abs(lastSize - firstSize) > kSizeMoved * std::max(std::abs(firstSize), 1.0f); }

        [[nodiscard]] Motion Verdict() const
        {
            return TimeMoved() || SizeMoved() ? Motion::kMoved : firstTime ? Motion::kStill : Motion::kNone;
        }
    };

    // -------------------------------------------------------------------------
    // Spell art shapes that are not the effect (spec v7 P7.1)
    // -------------------------------------------------------------------------

    // NiAlphaProperty::alphaFlags, decoded as CommonLib's NiAlphaProperty.cpp does: bit 0 blending (GetAlphaBlending, :7),
    // bits 1-4 the source factor (GetSrcBlendMode, :22), bits 5-8 the destination factor (GetDestBlendMode, :17); the
    // factor numbers are NiAlphaProperty::AlphaFunction (NiAlphaProperty.h: kOne 0, kSrcAlpha 6, kInvSrcAlpha 7)
    inline constexpr std::uint16_t kAlphaBlending = 1;
    inline constexpr int kAlphaSrcShift = 1, kAlphaDstShift = 5, kAlphaFactorMask = 15;
    inline constexpr int kAlphaOne = 0, kAlphaSrcAlpha = 6, kAlphaInvSrcAlpha = 7;

    enum class ArtShape
    {
        kKeep,      // the effect itself, or not a shape we touch
        kBackdrop,  // the disc behind the effect: a normally blended "Cylinder*" (bHideBackdrop)
        kGlow,      // an additive halo: "Sphere_Halo*", "glow01*", "lightRays*" (bHideGlow)
    };

    inline bool StartsWithNoCase(std::string_view a_text, std::string_view a_prefix)
    {
        if (a_text.size() < a_prefix.size()) return false;
        for (std::size_t i = 0; i < a_prefix.size(); ++i) {
            const auto lower = [](char a_char) { return a_char >= 'A' && a_char <= 'Z' ? static_cast<char>(a_char - 'A' + 'a') : a_char; };
            if (lower(a_text[i]) != lower(a_prefix[i])) return false;
        }
        return true;
    }

    /// What a shape of a spell's menu art is. Only a non-particle tri shape with an alpha property counts. A backdrop
    /// blends normally (blending on, SRC_ALPHA -> INV_SRC_ALPHA: vanilla "Cylinder08:1" has 0x10ED) and is named
    /// "Cylinder*"; a glow is additive (destination ONE) and named "Sphere_Halo*", "glow01*" or "lightRays*" (names from
    /// MAGINVHealSpellArt.nif and FireballInvArt.nif, read with houseCARL). Names compare without case
    inline ArtShape ClassifyArtShape(std::string_view a_name, bool a_triShape, bool a_particles, bool a_hasAlpha, std::uint16_t a_alphaFlags,
        bool a_hideBackdrop, bool a_hideGlow)
    {
        if (!a_triShape || a_particles || !a_hasAlpha || !(a_alphaFlags & kAlphaBlending)) return ArtShape::kKeep;
        const int src = (a_alphaFlags >> kAlphaSrcShift) & kAlphaFactorMask, dst = (a_alphaFlags >> kAlphaDstShift) & kAlphaFactorMask;
        if (a_hideBackdrop && src == kAlphaSrcAlpha && dst == kAlphaInvSrcAlpha && StartsWithNoCase(a_name, "Cylinder")) return ArtShape::kBackdrop;
        if (a_hideGlow && dst == kAlphaOne &&
            (StartsWithNoCase(a_name, "Sphere_Halo") || StartsWithNoCase(a_name, "glow01") || StartsWithNoCase(a_name, "lightRays"))) {
            return ArtShape::kGlow;
        }
        return ArtShape::kKeep;
    }

    // =========================================================================
    // P8 dark check (spec v11 H2/H3, design review R2/R3/R8). Build 10 logged SKIP every time: the bound target was not
    // 8-bit RGBA/BGRA. Build 11 reads 4 channels of the formats below as floats, channel order normalised to RGBA, and
    // compares with a per-format tolerance. Measurement only: no verdict here rules H6 (a later composite of the
    // target's alpha) out; that stays open until the composite itself is checked (R2)
    // =========================================================================

    /// How the render target stores a pixel (the RTV's typed format; a typeless resource is read by it, R8)
    enum class PixelLayout
    {
        kRgba8,           // R8G8B8A8_UNORM / _SRGB
        kBgra8,           // B8G8R8A8_UNORM / _SRGB
        kRgba16Float,     // R16G16B16A16_FLOAT
        kR11G11B10Float,  // R11G11B10_FLOAT: no alpha storage
        kRgb10A2,         // R10G10B10A2_UNORM (spec v12 C2: the over-pass's screen target): 10-bit RGB / 1023, 2-bit alpha / 3
    };

    inline constexpr std::size_t kPixelChannels = 4;  // every unpacked pixel is R, G, B, A (A = 1 when the format has none)

    inline constexpr std::size_t PixelBytes(PixelLayout a_layout) { return a_layout == PixelLayout::kRgba16Float ? 8 : 4; }
    inline constexpr bool HasAlpha(PixelLayout a_layout) { return a_layout != PixelLayout::kR11G11B10Float; }
    inline constexpr bool IsFloat(PixelLayout a_layout) { return a_layout == PixelLayout::kRgba16Float || a_layout == PixelLayout::kR11G11B10Float; }

    inline constexpr std::string_view LayoutName(PixelLayout a_layout)
    {
        switch (a_layout) {
        case PixelLayout::kRgba8:
            return "8-bit RGBA";
        case PixelLayout::kBgra8:
            return "8-bit BGRA";
        case PixelLayout::kRgba16Float:
            return "16-bit float RGBA";
        case PixelLayout::kR11G11B10Float:
            return "R11G11B10 float";
        case PixelLayout::kRgb10A2:
            return "10-bit RGB 2-bit alpha";
        }
        return "?";
    }

    /// One stored pixel as R, G, B, A floats. 8-bit channels are the stored value / 255 (an sRGB target's stored, encoded
    /// value: the tolerance is in stored steps); half floats through XMConvertHalfToFloat, R11G11B10 through XMLoadFloat3PK
    /// (both keep Inf and NaN, which the count reports)
    inline std::array<float, kPixelChannels> UnpackPixel(const std::uint8_t* a_pixel, PixelLayout a_layout)
    {
        constexpr float kByteMax = 255.0f;
        switch (a_layout) {
        case PixelLayout::kRgba8:
            return { a_pixel[0] / kByteMax, a_pixel[1] / kByteMax, a_pixel[2] / kByteMax, a_pixel[3] / kByteMax };
        case PixelLayout::kBgra8:
            return { a_pixel[2] / kByteMax, a_pixel[1] / kByteMax, a_pixel[0] / kByteMax, a_pixel[3] / kByteMax };
        case PixelLayout::kRgba16Float:
            {
                std::array<DirectX::PackedVector::HALF, kPixelChannels> half{};
                std::memcpy(half.data(), a_pixel, sizeof(half));
                using DirectX::PackedVector::XMConvertHalfToFloat;
                return { XMConvertHalfToFloat(half[0]), XMConvertHalfToFloat(half[1]), XMConvertHalfToFloat(half[2]), XMConvertHalfToFloat(half[3]) };
            }
        case PixelLayout::kR11G11B10Float:
            {
                DirectX::PackedVector::XMFLOAT3PK packed{};
                std::memcpy(&packed.v, a_pixel, sizeof(packed.v));
                DirectX::XMFLOAT3 rgb{};
                DirectX::XMStoreFloat3(&rgb, DirectX::PackedVector::XMLoadFloat3PK(&packed));
                return { rgb.x, rgb.y, rgb.z, 1.0f };
            }
        case PixelLayout::kRgb10A2:
            {
                // one little-endian 32-bit word: R bits 0-9, G 10-19, B 20-29, A 30-31 (DXGI_FORMAT_R10G10B10A2_UNORM)
                constexpr std::uint32_t kTenBits = 0x3FF, kTwoBits = 0x3;
                constexpr float kTenMax = 1023.0f, kTwoMax = 3.0f;
                const std::uint32_t word = static_cast<std::uint32_t>(a_pixel[0]) | static_cast<std::uint32_t>(a_pixel[1]) << 8 |
                                           static_cast<std::uint32_t>(a_pixel[2]) << 16 | static_cast<std::uint32_t>(a_pixel[3]) << 24;
                return { (word & kTenBits) / kTenMax, (word >> 10 & kTenBits) / kTenMax, (word >> 20 & kTenBits) / kTenMax,
                    (word >> 30 & kTwoBits) / kTwoMax };
            }
        }
        return {};
    }

    /// A mapped box (a_rowPitch bytes per row, at least a_size bytes readable) as a_width x a_height pixels of 4 floats,
    /// row by row. nullopt when the rows do not fit (a row pitch under a row of pixels, or a buffer under the last row's end)
    inline std::optional<std::vector<float>> UnpackBox(const std::uint8_t* a_data, std::size_t a_size, std::size_t a_rowPitch, std::size_t a_width,
        std::size_t a_height, PixelLayout a_layout)
    {
        const std::size_t rowBytes = a_width * PixelBytes(a_layout);
        if (!a_data || a_width == 0 || a_height == 0 || a_rowPitch < rowBytes || a_size < a_rowPitch * (a_height - 1) + rowBytes) return std::nullopt;
        std::vector<float> pixels(a_width * a_height * kPixelChannels);
        for (std::size_t y = 0; y < a_height; ++y) {
            const auto* row = a_data + y * a_rowPitch;
            for (std::size_t x = 0; x < a_width; ++x) {
                const auto rgba = UnpackPixel(row + x * PixelBytes(a_layout), a_layout);
                std::copy(rgba.begin(), rgba.end(), pixels.begin() + static_cast<std::ptrdiff_t>((y * a_width + x) * kPixelChannels));
            }
        }
        return pixels;
    }

    /// The change a channel must exceed to count (H3, R2). 8-bit: 24 steps for colour, 2 for alpha; values are whole
    /// steps, so the bound sits half a step above (24.5/255, 2.5/255) to keep float rounding off the edge: a fall of
    /// exactly 24 steps does not count, 25 does. R10G10B10A2 (spec v12 C2) keeps the same values (0.0961 colour, about 98
    /// of its 1023 steps; alpha 0.0098, under its one step of 1/3, so any alpha change counts). Float formats: colour 0.05 absolute AND 10 % of the larger of the two
    /// values (read as "both must be exceeded", like isclose: a change counts past max(0.05, 0.10 x larger); the spec's
    /// wording is ambiguous, this reading is ours); alpha 0.005 absolute
    struct PixelTolerance
    {
        float colour = 0, colourShare = 0, alpha = 0;
    };

    inline constexpr PixelTolerance k8BitTolerance{ 24.5f / 255.0f, 0.0f, 2.5f / 255.0f };
    inline constexpr PixelTolerance kFloatTolerance{ 0.05f, 0.10f, 0.005f };

    inline constexpr PixelTolerance ToleranceOf(PixelLayout a_layout) { return IsFloat(a_layout) ? kFloatTolerance : k8BitTolerance; }

    /// a_now - a_was past the bound (a_share of the larger magnitude, at least a_abs)
    inline bool PastTolerance(float a_was, float a_now, float a_abs, float a_share)
    {
        const float bound = std::max(a_abs, a_share * std::max(std::abs(a_was), std::abs(a_now)));
        return std::abs(a_now - a_was) > bound;
    }

    struct PixelBounds  // in the box; Empty() when nothing was added
    {
        int left = std::numeric_limits<int>::max(), top = std::numeric_limits<int>::max(), right = -1, bottom = -1;

        void Add(int a_x, int a_y)
        {
            left = std::min(left, a_x);
            top = std::min(top, a_y);
            right = std::max(right, a_x);
            bottom = std::max(bottom, a_y);
        }

        [[nodiscard]] bool Empty() const { return right < 0; }
    };

    struct ReadStats  // one side's (before or after) values, finite pixels only for the ranges
    {
        std::size_t nan = 0, inf = 0;  // channel values (alpha only when the format has it)
        float colourMin = std::numeric_limits<float>::infinity(), colourMax = -std::numeric_limits<float>::infinity();
        float alphaMin = std::numeric_limits<float>::infinity(), alphaMax = -std::numeric_limits<float>::infinity();
    };

    /// P8: two readbacks of the same box (4 floats per pixel, RGBA), before and after Render(). A pixel is darker when any
    /// colour channel fell past the tolerance, brighter when any rose past it (review v10: per channel, (200,200,200) ->
    /// (200,50,50) is darker). A pixel with a NaN or Inf channel on either side is left out of every comparison and counted
    /// (invalid). Sizes that differ, an empty box or one that is not whole rows of a_width: nullopt
    struct DarkCount
    {
        std::size_t pixels = 0, invalid = 0;
        std::size_t darker = 0, brighter = 0;
        std::size_t alphaUp = 0, alphaDown = 0, alphaOnlyUp = 0;  // alphaOnlyUp: no colour channel past the tolerance, alpha up
        PixelBounds darkerBounds, alphaUpBounds, alphaDownBounds;
        float largestAlphaChange = 0;  // after - before of the finite pixel whose alpha moved most (signed)
        ReadStats before, after;
        bool hasAlpha = false;
        PixelTolerance tolerance;
    };

    namespace Detail
    {
        // counts the side's non-finite values; true when the pixel's are all finite
        inline bool CountFinite(const float* a_pixel, bool a_hasAlpha, ReadStats& a_stats)
        {
            bool finite = true;
            for (std::size_t c = 0; c < (a_hasAlpha ? kPixelChannels : kPixelChannels - 1); ++c) {
                if (std::isnan(a_pixel[c])) {
                    ++a_stats.nan;
                    finite = false;
                } else if (std::isinf(a_pixel[c])) {
                    ++a_stats.inf;
                    finite = false;
                }
            }
            return finite;
        }

        inline void AddRange(const float* a_pixel, bool a_hasAlpha, ReadStats& a_stats)
        {
            for (std::size_t c = 0; c < kPixelChannels - 1; ++c) {
                a_stats.colourMin = std::min(a_stats.colourMin, a_pixel[c]);
                a_stats.colourMax = std::max(a_stats.colourMax, a_pixel[c]);
            }
            if (!a_hasAlpha) return;
            a_stats.alphaMin = std::min(a_stats.alphaMin, a_pixel[3]);
            a_stats.alphaMax = std::max(a_stats.alphaMax, a_pixel[3]);
        }
    }

    inline std::optional<DarkCount> CountDarker(const std::vector<float>& a_before, const std::vector<float>& a_after, int a_width, PixelLayout a_layout)
    {
        if (a_width <= 0 || a_before.empty() || a_before.size() != a_after.size() || a_before.size() % kPixelChannels != 0) return std::nullopt;
        DarkCount count;
        count.pixels = a_before.size() / kPixelChannels;
        const auto width = static_cast<std::size_t>(a_width);
        if (count.pixels % width != 0) return std::nullopt;
        count.hasAlpha = HasAlpha(a_layout);
        count.tolerance = ToleranceOf(a_layout);
        const auto& tol = count.tolerance;
        for (std::size_t p = 0; p < count.pixels; ++p) {
            const float* was = a_before.data() + p * kPixelChannels;
            const float* now = a_after.data() + p * kPixelChannels;
            const bool finiteBefore = Detail::CountFinite(was, count.hasAlpha, count.before);
            const bool finiteAfter = Detail::CountFinite(now, count.hasAlpha, count.after);
            if (!finiteBefore || !finiteAfter) {
                ++count.invalid;
                continue;
            }
            Detail::AddRange(was, count.hasAlpha, count.before);
            Detail::AddRange(now, count.hasAlpha, count.after);
            const int x = static_cast<int>(p % width), y = static_cast<int>(p / width);
            bool darker = false, brighter = false;
            for (std::size_t c = 0; c < kPixelChannels - 1; ++c) {
                if (!PastTolerance(was[c], now[c], tol.colour, tol.colourShare)) continue;
                (now[c] < was[c] ? darker : brighter) = true;
            }
            count.brighter += brighter ? 1 : 0;
            if (darker) {
                ++count.darker;
                count.darkerBounds.Add(x, y);
            }
            if (!count.hasAlpha) continue;
            const float change = now[3] - was[3];
            if (std::abs(change) > std::abs(count.largestAlphaChange)) count.largestAlphaChange = change;
            if (!PastTolerance(was[3], now[3], tol.alpha, 0.0f)) continue;
            if (change > 0) {
                ++count.alphaUp;
                count.alphaUpBounds.Add(x, y);
                count.alphaOnlyUp += !darker && !brighter ? 1 : 0;
            } else {
                ++count.alphaDown;
                count.alphaDownBounds.Add(x, y);
            }
        }
        return count;
    }

    /// review v12 #C1: the smallest step a stored channel can take (colour, alpha); 0 for float formats (compared exactly).
    /// The alpha-restored check (spec v13 A3) asks "is alpha back on its stored step", not P8's alpha tolerance
    struct Quantum
    {
        float colour = 0, alpha = 0;
    };

    inline constexpr Quantum QuantumOf(PixelLayout a_layout)
    {
        switch (a_layout) {
        case PixelLayout::kRgba8:
        case PixelLayout::kBgra8:
            return { 1.0f / 255.0f, 1.0f / 255.0f };
        case PixelLayout::kRgb10A2:
            return { 1.0f / 1023.0f, 1.0f / 3.0f };
        default:
            return {};
        }
    }

    /// One channel moved by at least one stored step: past half a step for UNORM (float rounding of a whole step stays off
    /// the edge), any difference for a float format
    inline bool ChangedByStep(float a_was, float a_now, float a_quantum)
    {
        return a_quantum > 0.0f ? std::abs(a_now - a_was) > a_quantum * 0.5f : a_now != a_was;
    }

    /// Spec v13 A1: which over-pass targets the alpha restore handles, by the layout P8 reads (Preview.cpp LayoutOf maps
    /// the DXGI format; a format it does not map is kUnsupportedFormat). A layout without alpha storage has no alpha the
    /// art could raise: nothing to restore
    enum class AlphaRestore
    {
        kRestore,
        kNoAlphaStorage,
        kUnsupportedFormat,
    };

    inline constexpr AlphaRestore AlphaRestoreFor(std::optional<PixelLayout> a_layout)
    {
        if (!a_layout) return AlphaRestore::kUnsupportedFormat;
        return HasAlpha(*a_layout) ? AlphaRestore::kRestore : AlphaRestore::kNoAlphaStorage;
    }

    /// Spec v13 A3: pixels of two reads of the same box (before Render(), after the alpha restore) whose alpha moved by a
    /// stored step or more (R10G10B10A2: 1/3; 8-bit: 1/255; float: any difference). A pixel with a NaN/Inf alpha on either
    /// side is left out and counted (invalid). No alpha storage, sizes that differ, an empty box or not whole rows of
    /// a_width: nullopt
    struct AlphaMoved
    {
        std::size_t pixels = 0, moved = 0, invalid = 0;
        PixelBounds movedBounds;
        float step = 0;  // the stored step (0 = exact)
    };

    inline std::optional<AlphaMoved> CountAlphaMoved(const std::vector<float>& a_before, const std::vector<float>& a_after, int a_width,
        PixelLayout a_layout)
    {
        if (!HasAlpha(a_layout) || a_width <= 0 || a_before.empty() || a_before.size() != a_after.size() || a_before.size() % kPixelChannels != 0) {
            return std::nullopt;
        }
        AlphaMoved count;
        count.pixels = a_before.size() / kPixelChannels;
        const auto width = static_cast<std::size_t>(a_width);
        if (count.pixels % width != 0) return std::nullopt;
        count.step = QuantumOf(a_layout).alpha;
        for (std::size_t p = 0; p < count.pixels; ++p) {
            const float was = a_before[p * kPixelChannels + 3], now = a_after[p * kPixelChannels + 3];
            if (!std::isfinite(was) || !std::isfinite(now)) {
                ++count.invalid;
                continue;
            }
            if (!ChangedByStep(was, now, count.step)) continue;
            ++count.moved;
            count.movedBounds.Add(static_cast<int>(p % width), static_cast<int>(p / width));
        }
        return count;
    }

    /// Review v13 round 2 #4: whether Render() changed the box's alpha, by the stored step (CountAlphaMoved over the reads
    /// before and after Render()), not P8's significance tolerance (8-bit 2.5/255), which let a one-step change restored
    /// correctly read NOTHING_TO_RESTORE. nullopt when the two reads cannot be compared (CountAlphaMoved's nullopt)
    inline std::optional<bool> RenderChangedAlpha(const std::vector<float>& a_before, const std::vector<float>& a_afterRender, int a_width,
        PixelLayout a_layout)
    {
        const auto count = CountAlphaMoved(a_before, a_afterRender, a_width, a_layout);
        if (!count) return std::nullopt;
        return count->moved > 0;
    }

    /// `P8 alpha restored` (spec v13 A3): INCONCLUSIVE on any NaN/Inf alpha; FAIL when a pixel's alpha is not back on its
    /// before-Render() step; else PASS
    inline constexpr std::string_view AlphaRestoredVerdict(const AlphaMoved& a_count)
    {
        if (a_count.invalid > 0) return "INCONCLUSIVE";
        return a_count.moved > 0 ? "FAIL" : "PASS";
    }

    /// Review v13 #4: the same, told apart from a frame whose Render() did not change the box's alpha (a_renderChangedAlpha:
    /// that frame's `P8 alpha` is ALPHA_CHANGED): there the restore had nothing to put back, so a match is NOTHING_TO_RESTORE,
    /// never PASS. INCONCLUSIVE and FAIL as above
    inline constexpr std::string_view AlphaRestoredVerdict(const AlphaMoved& a_count, bool a_renderChangedAlpha)
    {
        const auto verdict = AlphaRestoredVerdict(a_count);
        return verdict == "PASS" && !a_renderChangedAlpha ? std::string_view{ "NOTHING_TO_RESTORE" } : verdict;
    }
    /// `P8 dark` (H3, R3): any NaN/Inf on either side -> INCONCLUSIVE (never PASS, also an all-invalid box); else FAIL
    /// when a pixel got darker, PASS when none did
    inline constexpr std::string_view DarkVerdict(const DarkCount& a_count)
    {
        if (a_count.invalid > 0) return "INCONCLUSIVE";
        return a_count.darker > 0 ? "FAIL" : "PASS";
    }

    /// `P8 alpha` (R2, R3): INCONCLUSIVE on any NaN/Inf; NO_ALPHA_STORAGE for a format without alpha; else
    /// ALPHA_CHANGED when a pixel's alpha moved past the alpha tolerance, NO_SIGNIFICANT_ALPHA_DELTA when none did.
    /// None of these rules H6 out (R2)
    inline constexpr std::string_view AlphaVerdict(const DarkCount& a_count)
    {
        if (a_count.invalid > 0) return "INCONCLUSIVE";
        if (!a_count.hasAlpha) return "NO_ALPHA_STORAGE";
        return a_count.alphaUp + a_count.alphaDown > 0 ? "ALPHA_CHANGED" : "NO_SIGNIFICANT_ALPHA_DELTA";
    }
}

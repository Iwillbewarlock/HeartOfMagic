// magic-menu-preview-check - offline check of the magic menu prototype's preview math
// (plugins/SL_MagicMenu/src/PreviewMath.h, the same functions Preview.cpp runs on the game's values):
// place, measure, grow, the parent-space move, the rebuilt-model rule and the clip's pixel rect on
// made-up cases, the build 4/5 review counter-examples among them (spec 2026-10-25-magic-menu-proto-v5.md Q5).
// Also the tree window's and the progress card's layout arithmetic (LayoutMath.h, build 6 review round 1 #7):
// label widths at the window edge, badge sectors, label places and what gives way in the progress card.
//
//   magic-menu-preview-check     one PASS/FAIL line per check, a total line
//
// Each check is measured independently of the function under test where it can be: a fitted outline
// is projected again with a plain pinhole projection, a parent move is checked by recomputing the
// child's world point. No game types. Exit 0 when every check passes, 1 otherwise.

#include "BlockRules.h"
#include "LayoutMath.h"
#include "PreviewMath.h"
#include "Settings.h"

#include <DirectXPackedVector.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

namespace
{
    using PreviewMath::Axes;
    using PreviewMath::Mat3;
    using PreviewMath::Rect;
    using PreviewMath::Vec3;

    constexpr int kExitOk = 0;
    constexpr int kExitFail = 1;

    // The camera of every case: at the origin, looking along +y, z up, x right (Skyrim's world axes)
    constexpr Vec3 kEye{ 0, 0, 0 };
    constexpr Axes kAxes{ { 0, 1, 0 }, { 0, 0, 1 }, { 1, 0, 0 } };
    // A 16:9 frustum at depth 1 and the movie's 1280 x 720 stage
    constexpr PreviewMath::Frustum kFrustum{ -0.6f, 0.6f, 0.3375f, -0.3375f };
    constexpr Rect kStage{ 0, 0, 1280, 720 };

    int g_pass = 0;
    int g_fail = 0;

    void Expect(bool a_ok, const std::string& a_what)
    {
        std::printf("  %s  %s\n", a_ok ? "PASS" : "FAIL", a_what.c_str());
        (a_ok ? g_pass : g_fail) += 1;
    }

    bool Near(double a_lhs, double a_rhs, double a_tolerance)
    {
        return std::abs(a_lhs - a_rhs) <= a_tolerance;
    }

    std::string Text(const char* a_format, auto... a_values)
    {
        char buffer[512];
        std::snprintf(buffer, sizeof(buffer), a_format, a_values...);
        return buffer;
    }

    /// The outline grown by a_grow about a_place, projected with a plain pinhole onto the depth a_depth: its extent across
    /// (right) and along (up), from a_place's own line of sight. Written apart from PreviewMath::GrownExtent on purpose
    struct Projected
    {
        double left = 1e30, right = -1e30, low = 1e30, high = -1e30;

        [[nodiscard]] double Wide() const { return right - left; }
        [[nodiscard]] double High() const { return high - low; }
    };

    Projected Project(const std::vector<Vec3>& a_outline, double a_grow, const Vec3& a_place, double a_depth)
    {
        const double placeAcross = a_place.x / a_place.y * a_depth, placeAlong = a_place.z / a_place.y * a_depth;
        Projected projected;
        for (const auto& offset : a_outline) {
            const double x = a_place.x + offset.x * a_grow, y = a_place.y + offset.y * a_grow, z = a_place.z + offset.z * a_grow;
            const double across = x / y * a_depth - placeAcross, along = z / y * a_depth - placeAlong;
            projected.left = std::min(projected.left, across);
            projected.right = std::max(projected.right, across);
            projected.low = std::min(projected.low, along);
            projected.high = std::max(projected.high, along);
        }
        return projected;
    }

    /// A world point on the stage (y down), through the case camera and frustum
    struct StagePoint
    {
        double x{}, y{};
    };

    StagePoint ToStage(const Vec3& a_point)
    {
        const double across = a_point.x / a_point.y, along = a_point.z / a_point.y;
        const double fromLeft = (across - kFrustum.left) / (kFrustum.right - kFrustum.left);
        const double fromTop = (kFrustum.top - along) / (kFrustum.top - kFrustum.bottom);
        return { kStage.left + fromLeft * kStage.Width(), kStage.top + fromTop * kStage.Height() };
    }

    Mat3 Multiply(const Mat3& a_lhs, const Mat3& a_rhs)
    {
        Mat3 result;
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                float sum = 0;
                for (int k = 0; k < 3; ++k) sum += a_lhs.entry[row][k] * a_rhs.entry[k][column];
                result.entry[row][column] = sum;
            }
        }
        return result;
    }

    std::vector<Vec3> Square(float a_half, float a_look)
    {
        return { { -a_half, a_look, -a_half }, { a_half, a_look, -a_half }, { a_half, a_look, a_half }, { -a_half, a_look, a_half } };
    }

    // ---- the cases ----

    // Third check of build 4: centre depth 10, vertices 5 behind it (depth 15), a 20 x 20 outline, room 10 x 10. Seen at the
    // centre's depth 13.33 wide; grow 10 * .92 / 13.33 = .69 would project 10.26 and overflow. The re-projected grow is .597
    // (200 g / (10 + 5 g) = 9.2), the outline 9.20 = .92 of the room
    void DeepModel()
    {
        std::printf("deep model (centre depth 10, vertices at 15, 20 x 20, room 10 x 10)\n");
        const Vec3 place{ 0, 10, 0 };
        std::vector<Vec3> world;
        for (const auto& offset : Square(10, 5)) world.push_back(place + offset);
        const auto measured = PreviewMath::Measure(world, place, kEye, kAxes, 1.0f);
        Expect(measured && Near(measured->seen.wide, 200.0 / 15, 1e-3) && Near(measured->seen.high, 200.0 / 15, 1e-3),
            Text("measured at the centre's depth: seen %.3f x %.3f (want 13.333 x 13.333)", measured ? measured->seen.wide : -1.0f,
                measured ? measured->seen.high : -1.0f));
        if (!measured) return;
        const auto solved = PreviewMath::Solve(measured->outline, place - kEye, kAxes, 10, 10, 10);
        const auto projected = Project(measured->outline, solved.grow, place, 10);
        Expect(Near(solved.grow, 92.0 / 154, 2e-3), Text("grow %.4f by re-projection (want %.4f, not the naive .69)", solved.grow, 92.0 / 154));
        Expect(projected.Wide() <= 0.92 * 10 + 1e-3 && projected.High() <= 0.92 * 10 + 1e-3,
            Text("projected again: %.3f x %.3f <= 0.92 of the room (9.200)", projected.Wide(), projected.High()));
        Expect(projected.Wide() >= 0.92 * 10 - 1e-2, Text("and the largest such grow: width %.3f >= 9.19", projected.Wide()));
    }

    // The box is off the stage's centre (the shipped preview panel's content area): the place projects to the box's centre,
    // the room is the box at that depth, the base scale follows the box's height, and an off-centre outline fitted there
    // lands centred in the box and inside it
    void OffCentreBox()
    {
        std::printf("off-centre box (the shipped preview panel's content area)\n");
        const Settings::Values defaults;
        const auto content = Settings::PanelContent(defaults.preview);
        const Rect box{ content.left, content.top, content.right, content.bottom };
        const Vec3 gamePos{ 0, 120, 0 };
        const auto placed = PreviewMath::Place(kEye, kAxes, kFrustum, gamePos, 1.0f, box, kStage);
        Expect(placed.has_value(), Text("placed (box %.0f..%.0f x %.0f..%.0f)", box.left, box.right, box.top, box.bottom));
        if (!placed) return;
        const auto at = ToStage(placed->place);
        Expect(Near(at.x, box.CenterX(), 0.01) && Near(at.y, box.CenterY(), 0.01),
            Text("the place projects to stage %.2f,%.2f (box centre %.2f,%.2f)", at.x, at.y, box.CenterX(), box.CenterY()));
        Expect(Near(PreviewMath::DepthOf(placed->place, kEye, kAxes), 120, 1e-3), "at the game's own depth (120)");
        const auto& room = placed->room;
        const double roomOnStageW = room.wide / room.depth / (kFrustum.right - kFrustum.left) * kStage.Width();
        const double roomOnStageH = room.high / room.depth / (kFrustum.top - kFrustum.bottom) * kStage.Height();
        Expect(Near(roomOnStageW, box.Width(), 0.01) && Near(roomOnStageH, box.Height(), 0.01),
            Text("the room is the box: %.2f x %.2f stage units (box %.2f x %.2f)", roomOnStageW, roomOnStageH, box.Width(), box.Height()));
        const double wantScale = box.Height() / kStage.Height() / PreviewMath::kGameShare;
        Expect(Near(placed->scale, wantScale, 1e-4), Text("base scale %.4f (want %.4f: the box's share of the height / .45)", placed->scale, wantScale));

        // an outline off its own centre, flat in the camera's right/up plane (so its projection is exact): 40 x 20 (wider
        // than the room, so the search binds), its middle 10 right and 5 up of the model's place
        const std::vector<Vec3> world{ placed->place + Vec3{ -10, 0, -5 }, placed->place + Vec3{ 30, 0, -5 }, placed->place + Vec3{ 30, 0, 15 },
            placed->place + Vec3{ -10, 0, 15 } };
        const auto measured = PreviewMath::Measure(world, placed->place, kEye, kAxes, 1.0f);
        if (!measured) {
            Expect(false, "the off-centre outline measures");
            return;
        }
        const auto aimed = PreviewMath::AimAt(placed->place, kEye, kAxes, room);
        const auto solved = PreviewMath::Solve(measured->outline, aimed.aim - kEye, kAxes, aimed.depth, aimed.wide, aimed.high);
        const auto fitted = placed->place + PreviewMath::Across(aimed.aim - (placed->place + solved.middle), kAxes);
        double left = 1e30, right = -1e30, top = 1e30, bottom = -1e30;
        for (const auto& offset : measured->outline) {
            const auto point = ToStage(fitted + offset * solved.grow);
            left = std::min(left, point.x);
            right = std::max(right, point.x);
            top = std::min(top, point.y);
            bottom = std::max(bottom, point.y);
        }
        const double centreX = (left + right) / 2, centreY = (top + bottom) / 2;
        Expect(Near(centreX, box.CenterX(), 0.5) && Near(centreY, box.CenterY(), 0.5),
            Text("the fitted outline's centre %.2f,%.2f is the box's %.2f,%.2f (grow %.3f)", centreX, centreY, box.CenterX(), box.CenterY(), solved.grow));
        Expect(left >= box.left - 1e-3 && right <= box.right + 1e-3 && top >= box.top - 1e-3 && bottom <= box.bottom + 1e-3,
            Text("and inside the box: %.2f..%.2f x %.2f..%.2f", left, right, top, bottom));
        const double filled = std::max((right - left) / box.Width(), (bottom - top) / box.Height());
        Expect(Near(filled, PreviewMath::kFill, 1e-3), Text("filling %.4f of the box the binding way (want %.2f)", filled, PreviewMath::kFill));
    }

    // A model much smaller than the room grows by the cap and no further, with no search
    void TinyModel()
    {
        std::printf("tiny model (0.1 x 0.1 in a 10 x 10 room)\n");
        const auto outline = Square(0.05f, 0);
        const auto solved = PreviewMath::Solve(outline, Vec3{ 0, 10, 0 }, kAxes, 10, 10, 10);
        Expect(solved.grow == PreviewMath::kGrowMost && solved.steps == 0,
            Text("grow %.3f with %d search steps (want the cap %.1f, 0 steps)", solved.grow, solved.steps, PreviewMath::kGrowMost));
    }

    // A shape known by its bound sphere only (particles): 8 points round it in the right/up plane, measured as a 2r square
    // about its centre, stable readings agree and a jumping one does not, and it grows to .92 of the room
    void SphereOnly()
    {
        std::printf("sphere-only model (radius 2, centre 1 right and .5 up of the place)\n");
        const Vec3 place{ 0, 10, 0 };
        const auto read = [&](float a_radius) {
            return PreviewMath::Measure(PreviewMath::SpherePoints(place + Vec3{ 1, 0, 0.5f }, a_radius, kAxes), place, kEye, kAxes, 1.0f);
        };
        const auto reading = read(2.0f);
        Expect(reading && Near(reading->seen.wide, 4, 1e-4) && Near(reading->seen.high, 4, 1e-4) && Near(reading->seen.middle.x, 1, 1e-4) &&
                   Near(reading->seen.middle.z, 0.5, 1e-4),
            Text("%d points measured %.3f x %.3f, middle %.2f,%.2f (want 4 x 4 at 1,.5)", PreviewMath::kSpherePoints,
                reading ? reading->seen.wide : -1.0f, reading ? reading->seen.high : -1.0f, reading ? reading->seen.middle.x : 0.0f,
                reading ? reading->seen.middle.z : 0.0f));
        if (!reading) return;
        const auto steady = read(2.05f), jumped = read(2.3f);
        Expect(steady && PreviewMath::Agrees(reading->seen, steady->seen), "a reading 2.5% bigger agrees (within 10% of the half size)");
        Expect(jumped && !PreviewMath::Agrees(reading->seen, jumped->seen), "a reading 15% bigger does not (the measure waits for it to settle)");
        const auto solved = PreviewMath::Solve(reading->outline, place - kEye, kAxes, 10, 10, 10);
        Expect(Near(solved.grow, 0.92 * 10 / 4, 1e-3), Text("grow %.4f (want 2.3000 = .92 x 10 / 4)", solved.grow));
        Expect(PreviewMath::SpherePoints(place, 0.0f, kAxes).empty() && PreviewMath::SpherePoints(Vec3{ NAN, 0, 0 }, 1.0f, kAxes).empty() &&
                   PreviewMath::SpherePoints(place, INFINITY, kAxes).empty(),
            "a zero, infinite or not-finite sphere gives no points");
    }

    // A model under a turned and scaled parent: the world move taken into the parent's space lands the world point exactly
    // (local += parent.rotate^T * move / parent.scale; IR adds the world move to local.translate, which misses here)
    void ParentSpace()
    {
        std::printf("parent with rotation and scale (Rz 30 * Rx 20 degrees, scale 2.5)\n");
        const float a = 30.0f * std::numbers::pi_v<float> / 180, b = 20.0f * std::numbers::pi_v<float> / 180;
        const Mat3 rz{ { { std::cos(a), -std::sin(a), 0 }, { std::sin(a), std::cos(a), 0 }, { 0, 0, 1 } } };
        const Mat3 rx{ { { 1, 0, 0 }, { 0, std::cos(b), -std::sin(b) }, { 0, std::sin(b), std::cos(b) } } };
        const auto turn = Multiply(rz, rx);
        const float scale = 2.5f;
        const Vec3 translate{ 100, -50, 7 };
        const auto world = [&](const Vec3& a_local) { return turn * a_local * scale + translate; };  // NiTransform * point
        const Vec3 local{ 3, 4, 5 };
        const Vec3 move{ 1.5f, -2, 0.75f };
        const auto want = world(local) + move;
        const auto got = world(local + PreviewMath::LocalMove(turn, scale, move));
        Expect((got - want).Length() < 1e-4f, Text("the child's world point moved by the world move (miss %.2e)", (got - want).Length()));
        const auto naive = world(local + move);
        Expect((naive - want).Length() > 1.0f, Text("(the move added to local.translate unchanged would miss by %.2f)", (naive - want).Length()));
        const auto unscaled = PreviewMath::LocalMove(turn, 0.0f, move), turned = turn.Transpose() * move;
        Expect((unscaled - turned).Length() < 1e-6f, "a parent scale of 0 counts as 1 (no divide by zero)");
    }

    // A model of an earlier menu at the same address and of the same spell: rebuilt when it is not at the scale we left it at
    // (its scales are not the entry's), kept when it is (its fitted scale is never taken as its game scale), and in the same
    // menu a changed scale is the game's (Notice), not a new model
    void RebuiltModel()
    {
        std::printf("X2 re-created model at the same address\n");
        const float leftAt = 0.8f * 1.7f;  // the game's scale times our grow, as an earlier menu left it
        Expect(PreviewMath::Rebuilt(true, 0.8f, leftAt), "earlier menu, now at the game's 0.80 (left at 1.36): a new model");
        Expect(!PreviewMath::Rebuilt(true, leftAt, leftAt), "earlier menu, still at 1.36: the same model (its baseline stays 0.80)");
        Expect(!PreviewMath::Rebuilt(false, 0.8f, leftAt), "this menu, scale changed: the game's change, not a rebuild");
    }

    // An empty or turned-over box is refused (it would make the base scale 0 or negative), and so is a place the camera
    // cannot see, a game scale of 0, an empty visible rect and a flat frustum
    void RefusedPlaces()
    {
        std::printf("zero / negative content box and other refused places\n");
        const Rect box{ 656, 97, 949, 322 };
        const Vec3 gamePos{ 0, 120, 0 };
        const auto place = [&](const Rect& a_box, const Vec3& a_pos, float a_scale, const Rect& a_visible, const PreviewMath::Frustum& a_frustum) {
            return PreviewMath::Place(kEye, kAxes, a_frustum, a_pos, a_scale, a_box, a_visible).has_value();
        };
        Expect(place(box, gamePos, 1, kStage, kFrustum), "the shipped box is placed (control)");
        Expect(!place({ 656, 97, 656, 322 }, gamePos, 1, kStage, kFrustum), "a box of width 0 is refused");
        Expect(!place({ 656, 322, 949, 97 }, gamePos, 1, kStage, kFrustum), "a box of negative height is refused");
        const auto tiny = Settings::PanelContent({ 100, 100, 115, 115 });
        Expect(!place({ tiny.left, tiny.top, tiny.right, tiny.bottom }, gamePos, 1, kStage, kFrustum),
            Text("a 15 x 15 panel's content area (%.0f x %.0f) is refused", tiny.Width(), tiny.Height()));
        Expect(!place(box, Vec3{ 0, -5, 0 }, 1, kStage, kFrustum), "a game place behind the camera is refused");
        Expect(!place(box, gamePos, 0, kStage, kFrustum), "a game scale of 0 is refused");
        Expect(!place(box, gamePos, 1, { 0, 0, 0, 720 }, kFrustum), "an empty visible rect is refused");
        Expect(!place(box, gamePos, 1, kStage, { 0.6f, 0.6f, 0.3375f, -0.3375f }), "a flat frustum is refused");
    }

    // The clip's rect: stage -> viewport pixels -> the bound render target's pixels, rounded inward and clamped
    void ScissorPixels()
    {
        std::printf("clip rect in render-target pixels (spec v5 Q3 A)\n");
        const Rect box{ 656, 97, 949, 322 };
        const auto expect = [](const std::optional<PreviewMath::PixelBox>& a_box, PreviewMath::PixelBox a_want, const char* a_what) {
            const bool ok = a_box && a_box->x1 == a_want.x1 && a_box->y1 == a_want.y1 && a_box->x2 == a_want.x2 && a_box->y2 == a_want.y2;
            Expect(ok, Text("%s: %d,%d..%d,%d (want %d,%d..%d,%d)", a_what, a_box ? a_box->x1 : -1, a_box ? a_box->y1 : -1, a_box ? a_box->x2 : -1,
                           a_box ? a_box->y2 : -1, a_want.x1, a_want.y1, a_want.x2, a_want.y2));
        };
        const Rect full{ 0, 0, 1920, 1080 };
        const auto pixels = PreviewMath::StageToPixels(box, kStage, full);  // 984, 145.5 .. 1423.5, 483
        expect(PreviewMath::ScissorBox(pixels, 1920, 1080, 1920, 1080), { 984, 146, 1423, 483 }, "1080p, target = buffer (rounded inward)");
        expect(PreviewMath::ScissorBox(pixels, 1920, 1080, 3840, 2160), { 1968, 291, 2847, 966 },
            "a 2x target (upscaling / frame generation binds another size)");
        const auto letterbox = PreviewMath::StageToPixels(box, kStage, { 0, 60, 1920, 1020 });  // y: 60 + y * 960 / 720
        expect(PreviewMath::ScissorBox(letterbox, 1920, 1080, 1920, 1080), { 984, 190, 1423, 489 }, "a letterboxed viewport (top 60)");
        expect(PreviewMath::ScissorBox({ -10, -10, 50, 40 }, 1920, 1080, 1920, 1080), { 0, 0, 50, 40 }, "a box over the target's edge is clamped");
        Expect(!PreviewMath::ScissorBox({ 100, 100, 100.5, 200 }, 1920, 1080, 1920, 1080), "a box under one pixel wide is refused");
        Expect(!PreviewMath::ScissorBox(pixels, 0, 1080, 1920, 1080) && !PreviewMath::ScissorBox(pixels, 1920, 1080, 1920, 0),
            "a buffer or target size of 0 is refused");

        // blocks plan Task 3, review fix 1: the box cut to its column (Layout::VisibleBox) can be non-empty yet under one pixel -
        // scrolled to 0.4 stage units above the viewport's bottom (623): 0.6 px at 1080p. It must count as out of view, not
        // fall back to the unclipped draw
        const Rect sliver{ 656, 622.6, 949, 623 };
        const auto sliverBox = PreviewMath::ScissorBox(PreviewMath::StageToPixels(sliver, kStage, full), 1920, 1080, 1920, 1080);
        Expect(!sliverBox, "a box cut to 0.4 stage units high has no scissor pixel");
        const auto aloneBox = PreviewMath::ScissorBox(pixels, 1920, 1080, 1920, 1080);
        Expect(PreviewMath::PlanClip(sliverBox, aloneBox) == PreviewMath::ClipPlan::kNothingShows,
            "the box alone has pixels, cut to its column none -> nothing drawn");
        Expect(PreviewMath::PlanClip(aloneBox, aloneBox) == PreviewMath::ClipPlan::kScissor, "pixels -> scissored");
        Expect(PreviewMath::PlanClip(sliverBox, sliverBox) == PreviewMath::ClipPlan::kUnclipped,
            "the box alone has no pixel either (a sub-pixel box) -> build 13's unclipped fallback");
        // final review: the preview always has a column clip; a buffer or target size of 0 leaves no pixel for the box alone too -
        // build 13 drew those frames unclipped, so must this one (not "nothing shows")
        const auto zeroBufferCut = PreviewMath::ScissorBox(PreviewMath::StageToPixels(sliver, kStage, full), 0, 1080, 1920, 1080);
        const auto zeroBufferAlone = PreviewMath::ScissorBox(pixels, 0, 1080, 1920, 1080);
        Expect(PreviewMath::PlanClip(zeroBufferCut, zeroBufferAlone) == PreviewMath::ClipPlan::kUnclipped,
            "buffer width 0 with a column clip -> unclipped, as build 13");
        const auto zeroTarget = PreviewMath::ScissorBox(pixels, 1920, 1080, 1920, 0);
        Expect(PreviewMath::PlanClip(zeroTarget, zeroTarget) == PreviewMath::ClipPlan::kUnclipped, "a 0-size target with a column clip -> unclipped");
        const auto zeroBufferPlain = PreviewMath::ScissorBox(pixels, 1920, 0, 1920, 1080);
        Expect(PreviewMath::PlanClip(zeroBufferPlain, zeroBufferPlain) == PreviewMath::ClipPlan::kUnclipped,
            "buffer height 0, box not cut -> unclipped");
    }

    // SelfTest L2 preview clip as a real test (final review): judged only where the column cuts the box in pixels
    void L2ClipVerdict()
    {
        std::printf("L2 preview clip verdict (final review)\n");
        using PreviewMath::ColumnClipVerdict;
        using PreviewMath::PixelBox;
        const PixelBox alone{ 984, 146, 1423, 483 };
        const PixelBox column{ 960, 100, 1440, 400 };  // the column's viewport ends at y 400: the box's bottom is cut
        const PixelBox cut{ 984, 146, 1423, 400 };
        Expect(PreviewMath::JudgeColumnClip(cut, alone, column) == ColumnClipVerdict::kPass, "cut: the scissor differs from the box and lies in the column -> PASS");
        Expect(PreviewMath::JudgeColumnClip(alone, alone, column) == ColumnClipVerdict::kFail,
            "cut, but the scissor is the box's own pixels (the column clip not applied) -> FAIL");
        Expect(PreviewMath::JudgeColumnClip(PixelBox{ 984, 146, 1423, 410 }, alone, column) == ColumnClipVerdict::kFail,
            "cut, the scissor reaches past the column's bottom -> FAIL");
        Expect(PreviewMath::JudgeColumnClip(cut, alone, std::nullopt) == ColumnClipVerdict::kFail, "the column has no pixels in the target -> FAIL");
        const PixelBox wide{ 900, 50, 1500, 600 };
        Expect(PreviewMath::JudgeColumnClip(alone, alone, wide) == ColumnClipVerdict::kNotCut, "the box lies inside its column -> not judged (never PASS)");
        // a sub-pixel cut rounds to the same pixels on both sides: the box alone still lies in the column's pixels -> not judged
        const auto boxPx = PreviewMath::ScissorBox({ 100, 100, 300.3, 200 }, 1920, 1080, 1920, 1080);
        const auto colPx = PreviewMath::ScissorBox({ 50, 50, 300.1, 400 }, 1920, 1080, 1920, 1080);
        Expect(boxPx && colPx && PreviewMath::JudgeColumnClip(*boxPx, boxPx, colPx) == ColumnClipVerdict::kNotCut,
            "a cut under one pixel (300.3 against 300.1) -> not judged");
    }

    // Which on-screen box the self-test and the clip use: TranslateToScreen's only when it agrees with the visible-rect
    // mapping in size and in centre (review round 1, #13: a size check alone took a shifted box)
    void ScreenBoxChoice()
    {
        std::printf("TranslateToScreen against the visible-rect mapping (size within 10%%, centre within 2 px)\n");
        const Rect mapped{ 984, 145.5, 1423.5, 483 };
        const auto shifted = [&](double a_dx, double a_dy, double a_grow) {
            return Rect{ mapped.left + a_dx - a_grow, mapped.top + a_dy - a_grow, mapped.right + a_dx + a_grow, mapped.bottom + a_dy + a_grow };
        };
        Expect(PreviewMath::ScreenAgrees(mapped, mapped), "the same box agrees");
        Expect(PreviewMath::ScreenAgrees(mapped, shifted(1.2, -1.0, 3)), "1.6 px off and 6 px bigger agrees");
        Expect(!PreviewMath::ScreenAgrees(mapped, shifted(3, 0, 0)), "the same size 3 px off does not (a size-only check took it)");
        Expect(!PreviewMath::ScreenAgrees(mapped, shifted(0, 0, 30)), "the same centre 60 px bigger (14%) does not");
        Expect(!PreviewMath::ScreenAgrees(mapped, { NAN, 0, 10, 10 }), "a box that is not finite does not");
    }

    // Animation samples of a hidden model: our fit's rescale is not animation (review round 1, #4/#7). The raw world
    // bound radius grows with our grow; the size at the model's own scale does not
    void MotionWhileHidden()
    {
        std::printf("animation samples while hidden (controller frozen, fit only, and the rest)\n");
        {
            PreviewMath::MotionSamples motion;
            const bool before = motion.Add(2.0f, 8.0f, 0.8f, false, 0.8f) || motion.Add(2.0f, 8.0f, 0.8f, false, 0.8f);
            for (int i = 0; i < 5; ++i) motion.Add(2.0f, 13.6f, 1.36f, true, 1.36f);  // the fit settled at grow 1.7
            Expect(!before && motion.samples == 5, Text("samples while the fit measures do not count (%d counted)", motion.samples));
            Expect(motion.Verdict() == PreviewMath::Motion::kStill,
                "controller frozen, fit only: still, not moved (raw radius 8.0 -> 13.6, size 10.0 -> 10.0)");
        }
        {
            PreviewMath::MotionSamples motion;
            motion.Add(2.0f, 13.6f, 1.36f, true, 1.36f);
            motion.Add(2.0f, 13.6f, 1.36f, true, 1.36f);
            motion.Add(2.0f, 15.0f, 1.5f, true, 1.5f);  // the fit changed its scale: the series starts over
            Expect(motion.restarts == 1 && motion.samples == 1 && motion.Verdict() == PreviewMath::Motion::kStill,
                Text("our scale change mid-series starts it over (%d restart, %d sample)", motion.restarts, motion.samples));
        }
        {
            PreviewMath::MotionSamples motion;
            for (int i = 0; i < 5; ++i) motion.Add(2.0f + 0.5f * static_cast<float>(i), 13.6f, 1.36f, true, 1.36f);
            Expect(motion.Verdict() == PreviewMath::Motion::kMoved && motion.TimeMoved(), "a controller time that moves: moved");
        }
        {
            PreviewMath::MotionSamples motion;
            for (int i = 0; i < 5; ++i) motion.Add(std::nullopt, 10.0f + 0.5f * static_cast<float>(i), 1.0f, true, 1.0f);
            Expect(motion.Verdict() == PreviewMath::Motion::kMoved && !motion.TimeMoved(), "no controller, a particle bound that grows at one scale: moved");
        }
        {
            PreviewMath::MotionSamples motion;
            for (int i = 0; i < 5; ++i) motion.Add(std::nullopt, 10.0f, 1.0f, true, 1.0f);
            Expect(motion.Verdict() == PreviewMath::Motion::kNone, "no controller and a steady bound: nothing to judge");
        }
    }

    // Build 6 (LayoutMath.h): the tree window's labels, its faint locked neighbours and exits, and the progress card's plan
    void TreeLayout()
    {
        std::printf("tree window layout (labels at the edge, badge sectors, label places)\n");
        const Settings::Values defaults;
        const auto area = Settings::TreeContent(defaults.tree);
        const double edge = area.halfW + Settings::kPanelPad, gap = 10.0 + 1.0, most = 104.0, least = 20.0;
        Expect(Near(area.halfW, 122.0, 1e-9) && Near(area.halfH, 87.0, 1e-9), Text("E2 tree window half size %.1f x %.1f (want 122 x 87)", area.halfW, area.halfH));
        const double atEdge = LayoutMath::LabelWidth(120.0, gap, edge, false, most);
        Expect(atEdge < least, Text("a far-side label of a node at sx 120 has %.1f units to the edge: left out (< %.0f)", atEdge, least));
        Expect(Near(LayoutMath::LabelWidth(52.6, gap, edge, false, most), 66.4, 1e-9), "a far-side label at sx 52.6 shrinks to the edge: 66.4");
        bool towardCentre = true;
        for (double sx = -area.halfW; sx <= area.halfW; sx += 1.0) {
            // the selected node's brackets at fTreeMarkScale: the widest gap a drawn node's label uses (spec v10 T2)
            const double reach = 15.0 * defaults.treeMarkScale + 1.0;
            towardCentre = towardCentre && LayoutMath::LabelWidth(sx, reach, edge, sx > 0, most) >= most;
        }
        Expect(towardCentre, "a drawn node's label (facing the centre) keeps its full 104 everywhere in the window");

        // the off-window badges' sectors (spec v10 T7): from the window centre, y down; each node of Window::outside once
        {
            const std::vector<TreeData::PlacedNode> outside{ { 1, 300.0, 0.0 }, { 2, 0.0, -200.0 }, { 3, 0.0, 150.0 }, { 4, -250.0, 0.0 },
                { 5, 200.0, 190.0 }, { 6, 400.0, 30.0 } };
            const auto badges = LayoutMath::Badges(outside);
            Expect(badges.total == 6 && badges.sectors[0] == 2 && badges.sectors[6] == 1 && badges.sectors[2] == 1 && badges.sectors[4] == 1 &&
                       badges.sectors[1] == 1,
                Text("badges: right 2 (one 4 degrees down), up 1, down 1, left 1, down-right 1 (total %d)", badges.total));
            const auto none = LayoutMath::Badges({});
            Expect(none.total == 0 && std::ranges::all_of(none.sectors, [](int a_n) { return a_n == 0; }), "nothing outside: no badge");
        }

        // the centre's label place (round 3, build 7 T7.2): each place's box against what is reserved; a one-line label at size 14
        // (FieldH 21.5) 88 wide, beside the centre at gap 11 (round 3's numbers)
        using LayoutMath::Place;
        const LayoutMath::Box right{ 11.0, -10.75, 99.0, 10.75 }, left{ -99.0, -10.75, -11.0, 10.75 };
        const std::vector<LayoutMath::PlacedBox> sides{ { Place::kRight, right }, { Place::kLeft, left } };
        const auto nodeBox = [](double a_x, double a_y, double a_r) { return LayoutMath::Box{ a_x - a_r, a_y - a_r, a_x + a_r, a_y + a_r }; };
        {
            const auto clear = LayoutMath::ChoosePlace(sides, { nodeBox(0, 0, 10) });
            Expect(clear.clear && clear.index == 0 && clear.overlap == 0.0 && clear.firstOverlap == 0.0, "both sides clear: the right (as before)");
        }
        {
            // GPT round 3: a drawn neighbour at (-52.5, 0) and an off-window exit up-right (sector 7: arrow at (80, -80), text
            // 36 wide at about (52, -69)); the counting rule turned the label left onto the neighbour
            const std::vector<LayoutMath::Box> reserved{ nodeBox(0, 0, 10), nodeBox(-52.5, 0, 10), nodeBox(80, -80, 10),
                { 34.0, -79.6, 70.0, -58.1 } };
            const auto side = LayoutMath::ChoosePlace(sides, reserved);
            Expect(side.clear && side.index == 0 && side.overlap == 0.0, Text("a neighbour left, a badge up-right: right (overlap %.1f)", side.overlap));
        }
        {
            // round 1 #4: the E2 scene, a faint locked neighbour up-right at (52.6, -45.7) with its link from the centre
            std::vector<LayoutMath::Box> reserved{ nodeBox(0, 0, 10), nodeBox(52.6, -45.7, 10) };
            const auto link = LayoutMath::LinkBoxes(0, 0, 52.6, -45.7, 10, 4, 1);
            reserved.insert(reserved.end(), link.begin(), link.end());
            const auto side = LayoutMath::ChoosePlace(sides, reserved);
            Expect(side.clear && side.index == 1 && side.firstOverlap > 0.0 && side.overlap == 0.0,
                Text("a faint locked neighbour up-right: its link crosses the right label, so left (overlap right %.1f, left %.1f)", side.firstOverlap,
                    side.overlap));
        }
        {
            // spec v10 T5: no overlap is accepted any more (build 9 took the smaller one, left 50 against right 200)
            const std::vector<LayoutMath::Box> reserved{ { 20.0, -5.0, 40.0, 5.0 }, { -30.0, -5.0, -25.0, 5.0 } };
            const auto side = LayoutMath::ChoosePlace(sides, reserved);
            Expect(!side.clear && Near(side.firstOverlap, 200.0, 1e-9) && Near(side.overlap, 50.0, 1e-9),
                "both sides covered: no place, the label is dropped (the log names the least covered: left 50 against right 200)");
            Expect(!LayoutMath::ChoosePlace({}, reserved).clear, "no place offered at all: not clear");
        }

        // build 7 T7.2: labels start past the state mark's ring, and a kept label leaves two close neighbours' marks clear
        {
            constexpr double kReach = LayoutMath::MarkReach(7.0, 3.0, 2.5), kLabelGap = 1.0;
            Expect(Near(kReach, 11.25, 1e-12), Text("the mark reaches %.2f (diamond 7, ring gap 3, half the 2.5 arc)", kReach));
            const auto mark = nodeBox(0, 0, kReach);
            const LayoutMath::Box oldLeft{ -11.0 - 30.0, -10.75, -11.0, 10.75 }, newLeft{ -(kReach + kLabelGap) - 30.0, -10.75, -(kReach + kLabelGap), 10.75 };
            Expect(LayoutMath::OverlapArea(oldLeft, mark) > 0.0 && LayoutMath::OverlapArea(newLeft, mark) == 0.0,
                Text("a left label at build 6's gap 11 reaches into the ring (%.2f); at %.2f it is clear", LayoutMath::OverlapArea(oldLeft, mark),
                    kReach + kLabelGap));
        }
        {
            // the build-6 game run (log "Heal ... centre label right (overlap right 297.8, left 365.5)", frames f_024-f_040): the centre
            // with neighbours 15.5 right, 19 left and one up-left; a 30 x 21.5 label at gap 12.25
            constexpr double kReach = 11.25, kGap = 12.25, kEdgeX = 130.0, kEdgeY = 95.0;
            const std::vector<LayoutMath::Box> reserved{ nodeBox(0, 0, kReach), nodeBox(15.5, 0, kReach), nodeBox(-19.0, 0, kReach),
                nodeBox(-9.0, -10.0, kReach), nodeBox(6.7, -48.0, kReach) };
            const auto places = LayoutMath::KeptLabelPlaces(0, 0, 30.0, 21.5, kGap, false, kEdgeX, kEdgeY, 10.0, 2);
            const auto choice = LayoutMath::ChoosePlace(places, reserved);
            Expect(places.size() == 8 && places[0].place == Place::kRight && places[1].place == Place::kLeft && places[2].place == Place::kBelow,
                Text("places tried: right, left, below, above, then both sides further out twice (%zu)", places.size()));
            Expect(choice.clear && choice.firstOverlap > 0.0 && choice.overlap == 0.0 && places[choice.index].place == Place::kBelow,
                Text("both sides cover a diamond (right %.1f): the label goes below, clear", choice.firstOverlap));
        }
        {
            // a node near the right edge: a place past the window is not offered; the default side stays offered
            const auto places = LayoutMath::KeptLabelPlaces(100.0, 0, 60.0, 21.5, 12.25, false, 130.0, 95.0, 10.0, 2);
            const bool noRightFar = std::ranges::none_of(places, [](const LayoutMath::PlacedBox& a_p) { return a_p.place == Place::kRightFar; });
            Expect(places.front().place == Place::kRight && noRightFar && places[1].place == Place::kLeft,
                "near the right edge: no place further right; the drawn (default) side is still the first");
        }
        {
            const auto link = LayoutMath::LinkBoxes(0, 0, 10, 0, 2, 2, 0.5);
            Expect(link.size() == 4 && Near(link.front().left, 1.5, 1e-9) && Near(link.back().right, 8.5, 1e-9),
                Text("a 10-long link, 2 off each end, every 2: 4 boxes from x 1.5 to 8.5 (got %zu)", link.size()));
            Expect(LayoutMath::LinkBoxes(0, 0, 3, 0, 2, 2, 0.5).empty(), "a link shorter than both ends' room: no boxes");
        }
    }

    void ProgressLayout()
    {
        std::printf("progress card plan (102 high: what gives way)\n");
        constexpr double kAscent = 0.90;  // the build-3 frame's ascent share
        const auto plan = [](double a_first, double a_title, double a_hint, double a_offset) {
            const LayoutMath::TextMetrics metrics{ a_first, a_first, 4.0 };
            return std::pair{ metrics, LayoutMath::PlanProgress(metrics, { 102.0, 4.0, 2.0, a_title, 19.0, 12.0, 2.0, a_hint, 8.0, 0.5, a_offset }) };
        };
        const auto last = [](const LayoutMath::TextMetrics& a_metrics, const LayoutMath::ProgressPlan& a_plan) {
            return a_plan.hint ? a_plan.hintY + a_metrics.FieldH(a_plan.hintSize) : a_plan.barRowY + a_plan.barRowH;
        };
        {
            const auto [m, p] = plan(1.25, 14.0, 14.0, kAscent * (19.0 - 14.0));
            Expect(!p.offsetDropped && Near(p.offset, 4.5, 1e-9) && p.title && p.hint && Near(p.hintSize, 14.0, 1e-9) && Near(last(m, p), 98.25, 1e-9),
                Text("rows 14, line 1.25: offset 4.50 kept, hint at 14, last line ends at %.2f of 100", last(m, p)));
            const double titleBase = p.titleY + 2.0 + kAscent * 14.0, nameBase = 4.0 + 2.0 + kAscent * 19.0;
            Expect(Near(titleBase, nameBase, 1e-9), Text("and the title's baseline %.2f meets the name's %.2f (both 306 + this = 329.1)", titleBase, nameBase));
        }
        {
            const auto [m, p] = plan(1.25, 16.0, 16.0, kAscent * (19.0 - 16.0));
            Expect(!p.offsetDropped && p.hint && p.hintSize < 16.0 && last(m, p) <= p.bottom,
                Text("rows 16: the hint shrinks to %.1f first, the offset %.2f stays, ends at %.2f", p.hintSize, p.offset, last(m, p)));
        }
        {
            const auto [m, p] = plan(1.25, 14.0, 16.0, 4.5);
            Expect(!p.offsetDropped && p.hint && p.hintSize < 16.0 && last(m, p) <= p.bottom,
                Text("review table case (offset 4.5, hint 16): the hint shrinks to %.1f, the offset stays, ends at %.2f", p.hintSize, last(m, p)));
        }
        {
            const auto [m, p] = plan(1.5, 14.0, 14.0, 4.5);
            const double withOffset = 4.0 + 4.5 + m.FieldH(14.0) + p.targetH + p.barRowH + m.FieldH(8.0);
            Expect(p.offsetDropped && p.title && p.hint && Near(withOffset, 104.0, 1e-9) && Near(last(m, p), 99.5, 1e-9),
                Text("line 1.5: with the offset a size-8 hint would end at %.2f, so the offset goes and the hint stays (size %.1f, ends %.2f)",
                    withOffset, p.hintSize, last(m, p)));
        }
        {
            const auto [m, p] = plan(1.6, 14.0, 14.0, 4.5);
            Expect(p.offsetDropped && p.offset == 0.0 && p.title && !p.hint && last(m, p) <= p.bottom,
                Text("a tall font (line 1.6): not even a size-8 hint fits, so the offset goes, then the hint; ends at %.2f", last(m, p)));
        }
        {
            const auto [m, p] = plan(2.5, 14.0, 14.0, 4.5);
            Expect(p.offsetDropped && !p.title && Near(p.targetY, 4.0, 1e-9) && !p.hint && last(m, p) <= p.bottom,
                Text("line 2.5: offset, hint and title give way; the target and bar end at %.2f", last(m, p)));
        }
    }

    // P7.1 (spec v7): which shapes of a spell's menu art are hidden. Alpha flags as the NIFs have them (houseCARL):
    // Cylinder08:1 0x10ED (blending, SRC_ALPHA -> INV_SRC_ALPHA); an additive shape has destination ONE (0x000D here:
    // blending, source SRC_ALPHA, destination ONE)
    // user 2026-10-30: blocks fit their content - each fit read back at the fitted height lays out the same lines (no loop)
    void FitMath()
    {
        std::printf("blocks fit their content (LayoutMath)\n");
        constexpr double kSlack = BlockRules::kFitSlack;  // the slack FitBlock adds (review R9: the host's own value)
        const double rowH = 20.0, pitch = 17.0;
        for (const int count : { 1, 2, 3, 7 }) {
            const auto rows = static_cast<std::size_t>(count);
            const double h = LayoutMath::RowsHeight(rows, rowH, pitch);
            Expect(LayoutMath::RowsFit(h + kSlack, rowH, pitch) == rows && LayoutMath::RowsFit(h - 1.0, rowH, pitch) == rows - 1,
                Text("%zu rows take %.1f: that room (+ slack) fits %zu, 1 less fits one fewer", rows, h, rows));
        }
        Expect(LayoutMath::RowsHeight(0, rowH, pitch) == 0.0 && LayoutMath::RowsFit(rowH - 1.0, rowH, pitch) == 0, "no rows: 0 high; under one row: none fit");
        const LayoutMath::TextMetrics metrics{ 1.2, 1.1, 4.0 };
        const double size = 13.0, lineH = size * metrics.linePitch;
        for (const int lines : { 1, 2, 5 }) {
            Expect(LayoutMath::LinesFit(metrics.FieldH(size, lines) + kSlack, metrics.FieldH(size), lineH) == lines,
                Text("a description of %d line(s): its field's height (+ slack) gives %d lines back", lines, lines));
        }
        Expect(LayoutMath::LinesFit(0.0, metrics.FieldH(size), lineH) == 1, "no room: still one line (DrawHtmlText's floor)");
        // the progress card: the plan at its needed height is the plan at its full height (hint, sizes, places)
        for (const double hint : { 13.0, 16.0 }) {
            const LayoutMath::ProgressIn full{ 102.0, 4.0, 2.0, 11.0, 19.0, 12.0, 2.0, hint, 8.0, 0.5, 0.0 };
            const auto plan = LayoutMath::PlanProgress(metrics, full);
            const double needed = LayoutMath::ProgressNeeded(metrics, full, plan);
            auto fitted = full;
            fitted.cardH = std::min(needed + kSlack, full.cardH);
            const auto again = LayoutMath::PlanProgress(metrics, fitted);
            Expect(needed < full.cardH && again.hint == plan.hint && again.hintSize == plan.hintSize && again.hintY == plan.hintY && again.title == plan.title &&
                       LayoutMath::ProgressNeeded(metrics, fitted, again) == needed,
                Text("progress card, hint size %.0f: needs %.1f of 102, the plan at that height is the same (hint %.1f at %.1f)", hint, needed, plan.hintSize, plan.hintY));
        }
        Expect(LayoutMath::NodeCardNeeded(80.0, 37.0, 4.0, 74.0) == 195.0, "node card: rows top 80 + rows 37 + gap 4 + bottom part 74 = 195");
        // the tree window: the space the drawn boxes leave above and below the body (half height 100)
        const auto trim = LayoutMath::TreeTrim({ { -30, -40, 30, -20 }, { -10, 10, 10, 60 } }, 100.0);
        Expect(trim.top == 60.0 && trim.bottom == 40.0, Text("boxes from -40 to 60 in a body of -100..100: 60 off the top, 40 off the bottom (got %.1f, %.1f)", trim.top, trim.bottom));
        const auto over = LayoutMath::TreeTrim({ { -10, -120, 10, 130 } }, 100.0);
        Expect(over.top == 0.0 && over.bottom == 0.0, "a box over both edges (a badge in the padding): nothing taken off");
        const auto none = LayoutMath::TreeTrim({}, 100.0);
        Expect(none.top == 0.0 && none.bottom == 0.0, "nothing drawn: nothing taken off (the window keeps its height)");
        const auto flat = LayoutMath::TreeTrim({ { 0, 0, 0, 0 } }, 100.0);
        Expect(flat.top + flat.bottom <= 200.0 && flat.top == 100.0, "a box of no height at the centre: 100 off the top, the rest off the bottom");
    }

    void ArtShapes()
    {
        std::printf("spell art shapes: backdrop disc and glow (spec v7 P7.1)\n");
        using PreviewMath::ArtShape;
        using PreviewMath::ClassifyArtShape;
        constexpr std::uint16_t kNormal = 0x10ED, kAdditive = 0x000D;
        Expect(ClassifyArtShape("Cylinder08:1", true, false, true, kNormal, true, false) == ArtShape::kBackdrop,
            "Cylinder08:1, normal blend (0x10ED): the backdrop");
        Expect(ClassifyArtShape("cylinder08:2", true, false, true, kNormal, true, false) == ArtShape::kBackdrop, "the name compares without case");
        Expect(ClassifyArtShape("Cylinder08:1", true, false, true, kNormal, false, false) == ArtShape::kKeep, "bHideBackdrop=0: kept");
        Expect(ClassifyArtShape("Cylinder08:1", true, false, true, kAdditive, true, true) == ArtShape::kKeep,
            "an additive Cylinder is not the backdrop (and not a glow name)");
        Expect(ClassifyArtShape("Cylinder08:1", true, true, true, kNormal, true, false) == ArtShape::kKeep, "a particle shape is never hidden");
        Expect(ClassifyArtShape("Cylinder08:1", false, false, true, kNormal, true, false) == ArtShape::kKeep, "not a tri shape: kept");
        Expect(ClassifyArtShape("Cylinder08:1", true, false, false, 0, true, false) == ArtShape::kKeep, "no alpha property: kept");
        Expect(ClassifyArtShape("Cylinder08:1", true, false, true, kNormal & ~std::uint16_t{ 1 }, true, false) == ArtShape::kKeep,
            "blending off: kept");
        Expect(ClassifyArtShape("Sphere_Halo", true, false, true, kAdditive, true, false) == ArtShape::kKeep, "Sphere_Halo with bHideGlow=0: kept");
        Expect(ClassifyArtShape("Sphere_Halo", true, false, true, kAdditive, true, true) == ArtShape::kGlow, "Sphere_Halo, additive, bHideGlow=1: a glow");
        Expect(ClassifyArtShape("glow01", true, false, true, kAdditive, true, true) == ArtShape::kGlow, "glow01: a glow");
        Expect(ClassifyArtShape("lightRays01:0", true, false, true, kAdditive, true, true) == ArtShape::kGlow, "lightRays01:0: a glow");
        Expect(ClassifyArtShape("Sphere05", true, false, true, kAdditive, true, true) == ArtShape::kKeep, "Sphere05 (the effect) is kept");
        Expect(ClassifyArtShape("Sphere_Halo", true, false, true, kNormal, true, true) == ArtShape::kKeep, "a normally blended Sphere_Halo is kept");
    }

    // P8's readback decode and comparison (spec v11 H2/H3, review R2/R3/R8). Inputs in the stored formats are made here with
    // DirectXMath's own encoders (XMConvertFloatToHalf, XMStoreFloat3PK), apart from the decoder under test
    using PreviewMath::PixelLayout;

    std::vector<std::uint8_t> Bytes8(std::initializer_list<int> a_values)
    {
        std::vector<std::uint8_t> bytes;
        for (const int value : a_values) bytes.push_back(static_cast<std::uint8_t>(value));
        return bytes;
    }

    std::vector<std::uint8_t> Halves(std::initializer_list<float> a_values)
    {
        std::vector<std::uint8_t> bytes;
        for (const float value : a_values) {
            const auto half = DirectX::PackedVector::XMConvertFloatToHalf(value);
            const auto* raw = reinterpret_cast<const std::uint8_t*>(&half);
            bytes.insert(bytes.end(), raw, raw + sizeof(half));
        }
        return bytes;
    }

    std::vector<std::uint8_t> Packed111110(float a_r, float a_g, float a_b)
    {
        DirectX::PackedVector::XMFLOAT3PK packed{};
        const DirectX::XMFLOAT3 rgb{ a_r, a_g, a_b };
        DirectX::PackedVector::XMStoreFloat3PK(&packed, DirectX::XMLoadFloat3(&rgb));
        std::vector<std::uint8_t> bytes(sizeof(packed.v));
        std::memcpy(bytes.data(), &packed.v, sizeof(packed.v));
        return bytes;
    }

    std::vector<float> Unpacked(const std::vector<std::uint8_t>& a_bytes, std::size_t a_width, std::size_t a_height, PixelLayout a_layout)
    {
        const auto pixels = PreviewMath::UnpackBox(a_bytes.data(), a_bytes.size(), a_width * PreviewMath::PixelBytes(a_layout), a_width, a_height, a_layout);
        return pixels.value_or(std::vector<float>{});
    }

    bool NearAll(const std::vector<float>& a_values, std::initializer_list<float> a_expected, float a_tolerance)
    {
        if (a_values.size() != a_expected.size()) return false;
        std::size_t i = 0;
        for (const float expected : a_expected) {
            if (!Near(a_values[i++], expected, a_tolerance)) return false;
        }
        return true;
    }

    void PixelDecode()
    {
        std::printf("pixel decode: P8's readback formats to RGBA floats\n");
        constexpr float kByte = 1.0f / 255.0f;
        // 2 x 2 RGBA8 with a row pitch of 12 (4 bytes of padding per row, as a mapped staging texture can have)
        const auto rgba = Bytes8({ 255, 0, 0, 255, 0, 255, 0, 128, 0xEE, 0xEE, 0xEE, 0xEE, 0, 0, 255, 0, 10, 20, 30, 40, 0xEE, 0xEE, 0xEE, 0xEE });
        const auto padded = PreviewMath::UnpackBox(rgba.data(), rgba.size(), 12, 2, 2, PixelLayout::kRgba8);
        Expect(padded && NearAll(*padded, { 1, 0, 0, 1, 0, 1, 0, 128 * kByte, 0, 0, 1, 0, 10 * kByte, 20 * kByte, 30 * kByte, 40 * kByte }, 1e-6f),
            "RGBA8 with a padded row pitch: the padding is skipped, each byte / 255");
        const auto bgra = Unpacked(Bytes8({ 30, 20, 10, 40 }), 1, 1, PixelLayout::kBgra8);
        Expect(NearAll(bgra, { 10 * kByte, 20 * kByte, 30 * kByte, 40 * kByte }, 1e-6f), "BGRA8 (30,20,10,40) comes back as RGBA (10,20,30,40)");
        const auto half = Unpacked(Halves({ 0.5f, 2.0f, -1.0f, 0.25f }), 1, 1, PixelLayout::kRgba16Float);
        Expect(NearAll(half, { 0.5f, 2.0f, -1.0f, 0.25f }, 1e-6f), "R16G16B16A16_FLOAT (0.5, 2, -1, 0.25) round-trips");
        const auto halfInf = Unpacked(Halves({ std::numeric_limits<float>::infinity(), 0, 0, 1 }), 1, 1, PixelLayout::kRgba16Float);
        Expect(halfInf.size() == 4 && std::isinf(halfInf[0]), "a half +Inf stays Inf");
        const auto packed = Unpacked(Packed111110(1.5f, 0.25f, 8.0f), 1, 1, PixelLayout::kR11G11B10Float);
        Expect(NearAll(packed, { 1.5f, 0.25f, 8.0f, 1.0f }, 1e-6f), "R11G11B10_FLOAT (1.5, 0.25, 8) round-trips, alpha reads 1");
        Expect(!PreviewMath::HasAlpha(PixelLayout::kR11G11B10Float) && PreviewMath::HasAlpha(PixelLayout::kRgba16Float) &&
                   PreviewMath::HasAlpha(PixelLayout::kBgra8),
            "R11G11B10 has no alpha storage; the others have");
        Expect(!PreviewMath::UnpackBox(rgba.data(), rgba.size(), 4, 2, 2, PixelLayout::kRgba8), "a row pitch under a row of pixels: none");
        Expect(!PreviewMath::UnpackBox(rgba.data(), 15, 12, 2, 2, PixelLayout::kRgba8), "a buffer that ends before the last row: none");
        Expect(!PreviewMath::UnpackBox(rgba.data(), rgba.size(), 12, 0, 2, PixelLayout::kRgba8), "width 0: none");
    }

    /// One R10G10B10A2 pixel's 4 bytes, built with plain shifts: R bits 0-9, G 10-19, B 20-29, A 30-31, little-endian
    std::vector<std::uint8_t> Packed1010102(std::uint32_t a_r, std::uint32_t a_g, std::uint32_t a_b, std::uint32_t a_a)
    {
        const std::uint32_t word = a_r | a_g << 10 | a_b << 20 | a_a << 30;
        return { static_cast<std::uint8_t>(word), static_cast<std::uint8_t>(word >> 8), static_cast<std::uint8_t>(word >> 16),
            static_cast<std::uint8_t>(word >> 24) };
    }

    void Rgb10A2Decode()
    {
        std::printf("R10G10B10A2_UNORM decode (spec v12 C2): 10-bit RGB / 1023, 2-bit alpha / 3\n");
        constexpr float kTen = 1.0f / 1023.0f;
        // raw bytes, written out by hand: each channel alone at its full value
        Expect(NearAll(Unpacked(Bytes8({ 0xFF, 0x03, 0x00, 0x00 }), 1, 1, PixelLayout::kRgb10A2), { 1, 0, 0, 0 }, 1e-6f),
            "bits 0-9 set (FF 03 00 00): R = 1, nothing else");
        Expect(NearAll(Unpacked(Bytes8({ 0x00, 0xFC, 0x0F, 0x00 }), 1, 1, PixelLayout::kRgb10A2), { 0, 1, 0, 0 }, 1e-6f),
            "bits 10-19 set (00 FC 0F 00): G = 1, nothing else");
        Expect(NearAll(Unpacked(Bytes8({ 0x00, 0x00, 0xF0, 0x3F }), 1, 1, PixelLayout::kRgb10A2), { 0, 0, 1, 0 }, 1e-6f),
            "bits 20-29 set (00 00 F0 3F): B = 1, nothing else");
        Expect(NearAll(Unpacked(Bytes8({ 0x00, 0x00, 0x00, 0xC0 }), 1, 1, PixelLayout::kRgb10A2), { 0, 0, 0, 1 }, 1e-6f),
            "bits 30-31 set (00 00 00 C0): A = 1, nothing else");
        // alpha's four steps
        const auto alpha = [](std::uint32_t a_a) { return Unpacked(Packed1010102(0, 0, 0, a_a), 1, 1, PixelLayout::kRgb10A2); };
        Expect(NearAll(alpha(0), { 0, 0, 0, 0 }, 1e-6f) && NearAll(alpha(1), { 0, 0, 0, 1.0f / 3.0f }, 1e-6f) &&
                   NearAll(alpha(2), { 0, 0, 0, 2.0f / 3.0f }, 1e-6f) && NearAll(alpha(3), { 0, 0, 0, 1 }, 1e-6f),
            "alpha 0/1/2/3 reads 0, 1/3, 2/3, 1");
        Expect(NearAll(Unpacked(Packed1010102(512, 1, 1022, 2), 1, 1, PixelLayout::kRgb10A2), { 512 * kTen, kTen, 1022 * kTen, 2.0f / 3.0f }, 1e-6f),
            "(512, 1, 1022, 2): each channel its own value / 1023, alpha / 3 (no bleed between neighbours)");
        // two pixels in a padded row: the 4-byte stride
        auto two = Packed1010102(1023, 0, 0, 3);
        const auto second = Packed1010102(0, 0, 1023, 1);
        two.insert(two.end(), second.begin(), second.end());
        two.insert(two.end(), { 0xEE, 0xEE, 0xEE, 0xEE });
        const auto row = PreviewMath::UnpackBox(two.data(), two.size(), 12, 2, 1, PixelLayout::kRgb10A2);
        Expect(row && NearAll(*row, { 1, 0, 0, 1, 0, 0, 1, 1.0f / 3.0f }, 1e-6f), "two pixels in a padded row: 4 bytes each");
        Expect(PreviewMath::PixelBytes(PixelLayout::kRgb10A2) == 4 && PreviewMath::HasAlpha(PixelLayout::kRgb10A2) &&
                   !PreviewMath::IsFloat(PixelLayout::kRgb10A2),
            "R10G10B10A2: 4 bytes, has alpha, not a float format (the 8-bit tolerance values)");
        // the dark count on it: 0.0961 colour (about 98 of 1023 steps), alpha any step
        const auto one = [](std::uint32_t a_value, std::uint32_t a_a) {
            return Unpacked(Packed1010102(a_value, a_value, a_value, a_a), 1, 1, PixelLayout::kRgb10A2);
        };
        const auto fell300 = PreviewMath::CountDarker(one(1000, 3), one(700, 3), 1, PixelLayout::kRgb10A2);
        const auto fell50 = PreviewMath::CountDarker(one(1000, 3), one(950, 3), 1, PixelLayout::kRgb10A2);
        const auto alphaStep = PreviewMath::CountDarker(one(500, 3), one(500, 2), 1, PixelLayout::kRgb10A2);
        Expect(fell300 && fell300->darker == 1 && PreviewMath::DarkVerdict(*fell300) == "FAIL", "a fall of 300 of 1023 steps: darker, FAIL");
        Expect(fell50 && fell50->darker == 0 && PreviewMath::DarkVerdict(*fell50) == "PASS", "a fall of 50 of 1023 steps (0.049): under the tolerance, PASS");
        Expect(alphaStep && alphaStep->alphaDown == 1 && alphaStep->darker == 0 && PreviewMath::AlphaVerdict(*alphaStep) == "ALPHA_CHANGED",
            "alpha 3 -> 2 (one 2-bit step): an alpha change, not darker");
    }

    // spec v13 A1: which target formats the alpha restore handles (by the layout P8 reads)
    void AlphaRestoreFormats()
    {
        std::printf("alpha restore format table (spec v13 A1): the layouts P8 reads with alpha restored, R11G11B10 none, others skipped\n");
        using PreviewMath::AlphaRestore;
        using PreviewMath::AlphaRestoreFor;
        Expect(AlphaRestoreFor(PixelLayout::kRgb10A2) == AlphaRestore::kRestore, "R10G10B10A2_UNORM (the build 12 target): restored");
        Expect(AlphaRestoreFor(PixelLayout::kRgba8) == AlphaRestore::kRestore && AlphaRestoreFor(PixelLayout::kBgra8) == AlphaRestore::kRestore,
            "8-bit RGBA / BGRA: restored");
        Expect(AlphaRestoreFor(PixelLayout::kRgba16Float) == AlphaRestore::kRestore, "R16G16B16A16_FLOAT: restored");
        Expect(AlphaRestoreFor(PixelLayout::kR11G11B10Float) == AlphaRestore::kNoAlphaStorage, "R11G11B10_FLOAT: no alpha storage, nothing to restore");
        Expect(AlphaRestoreFor(std::nullopt) == AlphaRestore::kUnsupportedFormat, "a format P8 does not read: skipped");
    }

    // spec v13 A3: `P8 alpha restored` - the box's alpha after the restore against before Render(), to one stored step
    void AlphaRestoredCompare()
    {
        std::printf("alpha restored check (spec v13 A3): alpha back on its stored step\n");
        using PreviewMath::AlphaRestoredVerdict;
        using PreviewMath::CountAlphaMoved;
        const auto one = [](std::uint32_t a_value, std::uint32_t a_a) {
            return Unpacked(Packed1010102(a_value, a_value, a_value, a_a), 1, 1, PixelLayout::kRgb10A2);
        };
        // normal: the build 12 case - alpha 2 (0.667) before, 2 after the restore, though RGB changed (the art stays)
        const auto back = CountAlphaMoved(one(100, 2), one(900, 2), 1, PixelLayout::kRgb10A2);
        Expect(back && back->moved == 0 && AlphaRestoredVerdict(*back) == "PASS", "R10: alpha 2 -> 2 with RGB 100 -> 900: PASS (colour is not compared)");
        // failure: alpha left raised to 1.0 (step 3), as Render() leaves it
        const auto raised = CountAlphaMoved(one(100, 2), one(900, 3), 1, PixelLayout::kRgb10A2);
        Expect(raised && raised->moved == 1 && raised->movedBounds.left == 0 && AlphaRestoredVerdict(*raised) == "FAIL",
            "R10: alpha 2 -> 3 (0.667 -> 1.0): FAIL at (0,0)");
        // boundary: under half a step does not count; a float format compares exactly; 8-bit one step counts
        const std::vector<float> was{ 0.5f, 0.5f, 0.5f, 2.0f / 3.0f };
        const std::vector<float> nearly{ 0.5f, 0.5f, 0.5f, 2.0f / 3.0f + 0.1f };
        const auto under = CountAlphaMoved(was, nearly, 1, PixelLayout::kRgb10A2);
        Expect(under && under->moved == 0 && AlphaRestoredVerdict(*under) == "PASS", "R10: 0.1 off (under half of a 1/3 step): PASS");
        const auto floatTiny = CountAlphaMoved({ 0, 0, 0, 0.5f }, { 0, 0, 0, 0.5001f }, 1, PixelLayout::kRgba16Float);
        Expect(floatTiny && floatTiny->moved == 1 && floatTiny->step == 0.0f, "float: any alpha difference counts (exact)");
        const auto byteStep = CountAlphaMoved({ 0, 0, 0, 100 / 255.0f }, { 0, 0, 0, 101 / 255.0f }, 1, PixelLayout::kRgba8);
        Expect(byteStep && byteStep->moved == 1, "8-bit: one alpha step of 1/255 counts");
        // a 2 x 1 box: one restored, one not
        auto two = one(10, 2);
        const auto second = one(10, 2);
        two.insert(two.end(), second.begin(), second.end());
        auto twoAfter = one(10, 2);
        const auto secondAfter = one(10, 3);
        twoAfter.insert(twoAfter.end(), secondAfter.begin(), secondAfter.end());
        const auto mixed = CountAlphaMoved(two, twoAfter, 2, PixelLayout::kRgb10A2);
        Expect(mixed && mixed->pixels == 2 && mixed->moved == 1 && mixed->movedBounds.left == 1 && mixed->movedBounds.right == 1,
            "2 x 1: the second pixel's alpha not back, bounds x 1..1");
        // INCONCLUSIVE and no count
        constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
        const auto nan = CountAlphaMoved({ 0, 0, 0, kNan }, { 0, 0, 0, 1 }, 1, PixelLayout::kRgba16Float);
        Expect(nan && nan->invalid == 1 && nan->moved == 0 && AlphaRestoredVerdict(*nan) == "INCONCLUSIVE", "a NaN alpha: INCONCLUSIVE");
        Expect(!CountAlphaMoved(one(1, 3), {}, 1, PixelLayout::kRgb10A2) && !CountAlphaMoved({}, {}, 1, PixelLayout::kRgb10A2),
            "reads of different sizes or empty: no count");
        Expect(!CountAlphaMoved({ 0, 0, 0, 1 }, { 0, 0, 0, 1 }, 1, PixelLayout::kR11G11B10Float), "R11G11B10 (no alpha storage): no count");
        Expect(!CountAlphaMoved(std::vector<float>(12, 0.0f), std::vector<float>(12, 0.0f), 2, PixelLayout::kRgba8), "3 pixels are not whole rows of 2: no count");
        // review v13 #4: told apart from a frame whose Render() did not change the alpha
        Expect(AlphaRestoredVerdict(*back, true) == "PASS", "Render() changed the alpha, the restore put it back: PASS");
        Expect(AlphaRestoredVerdict(*back, false) == "NOTHING_TO_RESTORE", "Render() did not change the alpha: NOTHING_TO_RESTORE, not PASS");
        Expect(AlphaRestoredVerdict(*raised, false) == "FAIL" && AlphaRestoredVerdict(*raised, true) == "FAIL",
            "alpha not back: FAIL whatever Render() did");
        Expect(AlphaRestoredVerdict(*nan, false) == "INCONCLUSIVE", "a NaN alpha: INCONCLUSIVE, not NOTHING_TO_RESTORE");
        // review v13 round 2 #4: an 8-bit one-step change over Render(), restored: P8's tolerance (2.5/255) does not see it,
        // the stored-step compare does, so the restore line is PASS, not NOTHING_TO_RESTORE
        using PreviewMath::RenderChangedAlpha;
        const std::vector<float> stepBefore{ 0.5f, 0.5f, 0.5f, 100 / 255.0f };
        const std::vector<float> stepRendered{ 0.5f, 0.5f, 0.5f, 101 / 255.0f };
        const auto overRender = PreviewMath::CountDarker(stepBefore, stepRendered, 1, PixelLayout::kRgba8);
        Expect(overRender && PreviewMath::AlphaVerdict(*overRender) == "NO_SIGNIFICANT_ALPHA_DELTA",
            "8-bit 100 -> 101 over Render(): under P8's alpha tolerance (the round 1 signal missed it)");
        const auto renderChanged = RenderChangedAlpha(stepBefore, stepRendered, 1, PixelLayout::kRgba8);
        Expect(renderChanged && *renderChanged, "8-bit 100 -> 101 over Render(): changed by a stored step");
        const auto stepBack = CountAlphaMoved(stepBefore, stepBefore, 1, PixelLayout::kRgba8);
        Expect(stepBack && renderChanged && AlphaRestoredVerdict(*stepBack, *renderChanged) == "PASS",
            "one step changed, restored to 100: PASS, not NOTHING_TO_RESTORE");
        const auto unchanged = RenderChangedAlpha(stepBefore, stepBefore, 1, PixelLayout::kRgba8);
        Expect(unchanged && !*unchanged && AlphaRestoredVerdict(*stepBack, *unchanged) == "NOTHING_TO_RESTORE",
            "8-bit alpha unchanged over Render(): NOTHING_TO_RESTORE");
        Expect(!RenderChangedAlpha(stepBefore, {}, 1, PixelLayout::kRgba8), "reads of different sizes: not known (nullopt)");
    }

    void DarkCompare()
    {
        std::printf("dark compare: P8's float before/after count and verdicts\n");
        using PreviewMath::AlphaVerdict;
        using PreviewMath::CountDarker;
        using PreviewMath::DarkVerdict;
        constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
        constexpr float kInf = std::numeric_limits<float>::infinity();

        // 8-bit, 2 x 2: (0,0) unchanged, (1,0) brighter, (0,1) 200,200,200 -> 200,50,50, (1,1) unchanged; alpha 255 kept
        const auto before8 = Unpacked(Bytes8({ 10, 10, 10, 255, 50, 50, 50, 255, 200, 200, 200, 255, 90, 90, 90, 255 }), 2, 2, PixelLayout::kRgba8);
        const auto after8 = Unpacked(Bytes8({ 10, 10, 10, 255, 120, 50, 50, 255, 200, 50, 50, 255, 90, 90, 90, 255 }), 2, 2, PixelLayout::kRgba8);
        const auto count = CountDarker(before8, after8, 2, PixelLayout::kRgba8);
        Expect(count && count->pixels == 4 && count->darker == 1 && count->brighter == 1 && count->darkerBounds.left == 0 &&
                   count->darkerBounds.right == 0 && count->darkerBounds.top == 1 && count->darkerBounds.bottom == 1 && DarkVerdict(*count) == "FAIL",
            "8-bit: (200,200,200) -> (200,50,50) is darker though its largest channel held; one brighter; bounds x 0..0 y 1..1; FAIL");
        Expect(count && AlphaVerdict(*count) == "NO_SIGNIFICANT_ALPHA_DELTA" && count->alphaUp == 0 && count->alphaDown == 0,
            "8-bit: alpha kept at 255: NO_SIGNIFICANT_ALPHA_DELTA");
        const auto same = CountDarker(before8, before8, 2, PixelLayout::kRgba8);
        Expect(same && same->darker == 0 && same->brighter == 0 && same->darkerBounds.Empty() && DarkVerdict(*same) == "PASS",
            "the same pixels: none darker, no bounds, PASS");

        // 8-bit edges: colour 24 steps / 25 steps, alpha 2 steps / 3 steps
        const auto one8 = [](int a_r, int a_a) { return Unpacked(Bytes8({ a_r, a_r, a_r, a_a }), 1, 1, PixelLayout::kRgba8); };
        const auto at24 = CountDarker(one8(200, 255), one8(176, 255), 1, PixelLayout::kRgba8);
        Expect(at24 && at24->darker == 0, "8-bit: a fall of exactly 24 steps (200 -> 176) is not darker");
        const auto at25 = CountDarker(one8(200, 255), one8(175, 255), 1, PixelLayout::kRgba8);
        Expect(at25 && at25->darker == 1, "8-bit: a fall of 25 steps (200 -> 175) is darker");
        const auto alpha2 = CountDarker(one8(100, 255), one8(100, 253), 1, PixelLayout::kRgba8);
        Expect(alpha2 && alpha2->alphaDown == 0 && AlphaVerdict(*alpha2) == "NO_SIGNIFICANT_ALPHA_DELTA", "8-bit: alpha 255 -> 253 (2 steps) is no change");
        const auto alpha3 = CountDarker(one8(100, 255), one8(100, 252), 1, PixelLayout::kRgba8);
        Expect(alpha3 && alpha3->alphaDown == 1 && alpha3->alphaUp == 0 && AlphaVerdict(*alpha3) == "ALPHA_CHANGED" &&
                   Near(alpha3->largestAlphaChange, -3.0 / 255.0, 1e-6),
            "8-bit: alpha 255 -> 252 (3 steps) is a drop; largest change -3/255");

        // float edges: colour past max(0.05, 10 % of the larger), alpha past 0.005
        const auto oneF = [](float a_r, float a_a) { return std::vector<float>{ a_r, a_r, a_r, a_a }; };
        const auto CountF = [&](float a_was, float a_now, float a_alphaWas = 1, float a_alphaNow = 1) {
            return CountDarker(oneF(a_was, a_alphaWas), oneF(a_now, a_alphaNow), 1, PixelLayout::kRgba16Float);
        };
        Expect(CountF(10.0f, 9.05f)->darker == 0, "float: 10 -> 9.05 (under 10 % of 10) is not darker");
        Expect(CountF(10.0f, 8.9f)->darker == 1, "float: 10 -> 8.9 (past 10 % of 10) is darker");
        Expect(CountF(0.10f, 0.06f)->darker == 0, "float: 0.10 -> 0.06 (under 0.05) is not darker");
        Expect(CountF(0.10f, 0.04f)->darker == 1, "float: 0.10 -> 0.04 (past 0.05) is darker");
        Expect(CountF(0.5f, 0.5f, 0.5f, 0.504f)->alphaUp == 0, "float: alpha 0.5 -> 0.504 (under 0.005) is no change");
        Expect(CountF(0.5f, 0.5f, 0.5f, 0.51f)->alphaUp == 1, "float: alpha 0.5 -> 0.51 (past 0.005) is up");

        // half-float input, alpha-only change: colour held, alpha 0.25 -> 0.75 on one of two pixels
        const auto halfBefore = Unpacked(Halves({ 0.2f, 0.3f, 0.4f, 0.25f, 1.0f, 1.0f, 1.0f, 1.0f }), 2, 1, PixelLayout::kRgba16Float);
        const auto halfAfter = Unpacked(Halves({ 0.2f, 0.3f, 0.4f, 0.75f, 1.0f, 1.0f, 1.0f, 1.0f }), 2, 1, PixelLayout::kRgba16Float);
        const auto alphaOnly = CountDarker(halfBefore, halfAfter, 2, PixelLayout::kRgba16Float);
        Expect(alphaOnly && alphaOnly->darker == 0 && alphaOnly->alphaUp == 1 && alphaOnly->alphaOnlyUp == 1 && alphaOnly->alphaUpBounds.left == 0 &&
                   alphaOnly->alphaUpBounds.right == 0 && Near(alphaOnly->largestAlphaChange, 0.5, 1e-6) && DarkVerdict(*alphaOnly) == "PASS" &&
                   AlphaVerdict(*alphaOnly) == "ALPHA_CHANGED",
            "half float, alpha 0.25 -> 0.75 with colour held: dark PASS, alpha ALPHA_CHANGED, 1 colour-unchanged-alpha-up, largest +0.5");
        Expect(alphaOnly && Near(alphaOnly->before.alphaMin, 0.25, 1e-6) && Near(alphaOnly->after.alphaMax, 1.0, 1e-6) &&
                   Near(alphaOnly->before.colourMin, 0.2, 1e-3) && Near(alphaOnly->before.colourMax, 1.0, 1e-6),
            "half float: min/max of colour and alpha per side");

        // R11G11B10 input: darker read, no alpha storage (the alpha slot is not compared even if it differed)
        auto packedBefore = Unpacked(Packed111110(2.0f, 2.0f, 2.0f), 1, 1, PixelLayout::kR11G11B10Float);
        auto packedAfter = Unpacked(Packed111110(1.0f, 2.0f, 2.0f), 1, 1, PixelLayout::kR11G11B10Float);
        packedAfter.back() = 0.0f;
        const auto packed = CountDarker(packedBefore, packedAfter, 1, PixelLayout::kR11G11B10Float);
        Expect(packed && packed->darker == 1 && packed->alphaUp + packed->alphaDown == 0 && DarkVerdict(*packed) == "FAIL" &&
                   AlphaVerdict(*packed) == "NO_ALPHA_STORAGE",
            "R11G11B10: 2 -> 1 in red is darker (FAIL); alpha NO_ALPHA_STORAGE");

        // NaN and +/-Inf on each side: never PASS, the pixel is left out and counted
        const std::vector<float> twoDark{ 0.5f, 0.5f, 0.5f, 1, 0.5f, 0.5f, 0.5f, 1 };
        const auto nanBefore = CountDarker({ kNan, 0.5f, 0.5f, 1, 0.5f, 0.5f, 0.5f, 1 }, twoDark, 2, PixelLayout::kRgba16Float);
        Expect(nanBefore && nanBefore->invalid == 1 && nanBefore->before.nan == 1 && nanBefore->after.nan == 0 && DarkVerdict(*nanBefore) == "INCONCLUSIVE" &&
                   AlphaVerdict(*nanBefore) == "INCONCLUSIVE",
            "NaN before (nothing darker): dark and alpha INCONCLUSIVE, not PASS");
        const auto nanAfter = CountDarker(twoDark, { 0.5f, 0.5f, 0.5f, kNan, 0.1f, 0.1f, 0.1f, 1 }, 2, PixelLayout::kRgba16Float);
        Expect(nanAfter && nanAfter->invalid == 1 && nanAfter->after.nan == 1 && nanAfter->darker == 1 && DarkVerdict(*nanAfter) == "INCONCLUSIVE",
            "NaN after in alpha, another pixel darker: INCONCLUSIVE (the darker one still counted)");
        const auto infBefore = CountDarker({ kInf, 0.5f, 0.5f, 1, 0.5f, 0.5f, 0.5f, 1 }, twoDark, 2, PixelLayout::kRgba16Float);
        Expect(infBefore && infBefore->before.inf == 1 && infBefore->invalid == 1 && DarkVerdict(*infBefore) == "INCONCLUSIVE",
            "+Inf before: counted as Inf, INCONCLUSIVE");
        const auto infAfter = CountDarker(twoDark, { 0.5f, -kInf, 0.5f, 1, 0.5f, 0.5f, 0.5f, 1 }, 2, PixelLayout::kRgba16Float);
        Expect(infAfter && infAfter->after.inf == 1 && infAfter->invalid == 1 && DarkVerdict(*infAfter) == "INCONCLUSIVE" &&
                   AlphaVerdict(*infAfter) == "INCONCLUSIVE" && Near(infAfter->after.colourMin, 0.5, 1e-6),
            "-Inf after: INCONCLUSIVE; the range is of the finite pixels only");
        const auto halfInfCount = CountDarker(Unpacked(Halves({ kInf, 0, 0, 1 }), 1, 1, PixelLayout::kRgba16Float),
            Unpacked(Halves({ 0, 0, 0, 1 }), 1, 1, PixelLayout::kRgba16Float), 1, PixelLayout::kRgba16Float);
        Expect(halfInfCount && halfInfCount->before.inf == 1 && DarkVerdict(*halfInfCount) == "INCONCLUSIVE",
            "a half +Inf through the decoder: INCONCLUSIVE");
        const std::vector<float> allNan(8, kNan);
        const auto allInvalid = CountDarker(allNan, allNan, 2, PixelLayout::kRgba16Float);
        Expect(allInvalid && allInvalid->invalid == 2 && allInvalid->darker == 0 && allInvalid->before.colourMin > allInvalid->before.colourMax &&
                   DarkVerdict(*allInvalid) == "INCONCLUSIVE" && AlphaVerdict(*allInvalid) == "INCONCLUSIVE",
            "an all-NaN box: both INCONCLUSIVE, no finite range");

        // shapes
        Expect(!CountDarker(before8, std::vector<float>(12, 0), 2, PixelLayout::kRgba8), "sizes that differ: no count");
        Expect(!CountDarker(before8, after8, 3, PixelLayout::kRgba8), "4 pixels are not whole rows of 3: no count");
        Expect(!CountDarker(before8, after8, 0, PixelLayout::kRgba8), "width 0: no count");
        Expect(!CountDarker({}, {}, 1, PixelLayout::kRgba8), "an empty box: no count (never a PASS on nothing)");
        Expect(!CountDarker(std::vector<float>(5, 0), std::vector<float>(5, 0), 1, PixelLayout::kRgba8), "not whole pixels of 4 floats: no count");
    }
}

int main()
{
    DeepModel();
    OffCentreBox();
    TinyModel();
    SphereOnly();
    ParentSpace();
    RebuiltModel();
    RefusedPlaces();
    ScissorPixels();
    L2ClipVerdict();
    ScreenBoxChoice();
    MotionWhileHidden();
    TreeLayout();
    ProgressLayout();
    FitMath();
    ArtShapes();
    PixelDecode();
    DarkCompare();
    Rgb10A2Decode();
    AlphaRestoreFormats();
    AlphaRestoredCompare();
    std::printf(
"preview-check: %d PASS, %d FAIL\n", g_pass, g_fail);
    return g_fail == 0 ? kExitOk : kExitFail;
}

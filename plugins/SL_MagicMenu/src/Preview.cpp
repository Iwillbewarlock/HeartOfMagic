#include "Common.h"

#include "Preview.h"

#include "GameThread.h"
#include "Layout.h"
#include "PreviewMath.h"

#include "REX/W32/COMPTR.h"
#include "REX/W32/D3DCOMPILER.h"
#include "REX/W32/DXGI.h"
#include "REX/W32/KERNEL32.h"

#include <DirectXPackedVector.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// =============================================================================
// PREVIEW - see Preview.h. Method after Inventory Reforged's ItemView
// PlacePreview/FitPreview (GPL-3, read for the method only; our own code):
// the game keeps a rest place (itemPos, itemScale) and a current place
// (itemPosCopy, itemScaleCopy); at rest a model already up is moved along.
// The box is a window on the camera's frustum at the depth the game puts its
// preview; each model is measured by what the camera sees of it and grown and
// moved to fill that window. Changes from IR (GPT design review 2026-10-25):
// both moves go into the model's parent space, a measure that rests on bound
// spheres counts only once it holds still, the cache keeps the game's own
// scale apart from ours, and the self-test projects the drawn points.
// The arithmetic is PreviewMath.h's: this file gathers the game's values,
// converts them (V, Ni, TurnOf) and applies the results.
// g_state is touched on the main thread only (OnPostDisplay, Reset, the close
// task); g_slot is the one thing the frame handler shares, under its lock.
// =============================================================================

namespace Preview
{
    namespace
    {
        using PreviewMath::Axes;
        using PreviewMath::kGrowMost;
        using PreviewMath::kZeroTolerance;
        using PreviewMath::Room;
        using PreviewMath::Seen;
        using PreviewMath::Solved;
        using PreviewMath::Vec3;
        using PixelRect = PreviewMath::Rect;

        constexpr int kMeasuresMost = 6;             // measured this often (the game keeps turning it): bound spheres only
        constexpr int kMeasureLinesMost = 3;         // per model and menu: its measure and P1 lines; later ones only counted
        constexpr int kTurnStillFrames = 5;          // a turned model is measured again once its turn held still this long
        constexpr float kSameTurn = 1e-4f;
        constexpr float kShiftShare = 0.002f;        // a centring move under this share of the box height is skipped
        constexpr int kStableFrames = 3;             // a sphere measure counts once this many frames in a row agree
        constexpr int kSettleFramesMost = 60;        // never settled: base scale, centred, no grow
        constexpr std::uint64_t kStrideMask = 0xF;   // VertexDesc: a vertex's size in fours of bytes, lowest bits
        constexpr double kPassPixels = 4.0;          // spec: the drawn centre within 4 px of the box centre
        constexpr double kInsideSlackPixels = 1.0;   // rounding room for "inside the box"
        constexpr double kFlat = 1e-9;
        constexpr float kProbeShare = 0.1f;          // the axis probe steps this share of the depth

        // Q3 (spec v5)
        constexpr std::uint32_t kScissorOn = 1;      // rasterStateScissorMode: the engine's raster states are [..][scissor 0/1]
        constexpr std::uint32_t kRectsMost = REX::W32::D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        constexpr auto kAnimEvery = std::chrono::seconds(1);  // a hidden model's animation is sampled this often
        constexpr int kAnimSamples = 5;              // samples per model and menu, then its Q3 anim verdict
        constexpr int kControllerSearchMost = 256;   // objects looked at for a time controller under a model
        constexpr int kSightLinesMost = 8;           // per menu: "became the selection's" lines; later ones only counted
        constexpr int kRestartLinesMost = 3;         // per model and menu: "samples start over" lines
        constexpr int kArtSearchMost = 512;          // P7: objects looked at under one model for its backdrop and glow shapes
        constexpr float kUnplaceSlack = 1e-3f;       // P7.2 self-test: allowed error, a share of the value (at least of 1)
        constexpr int kArtLogDelay = 10;             // LogSelectionArt this many frames after a selection change
        constexpr int kDarkCheckDelay = 30;          // P8: frames after a selection change (its particles are up by then)
        constexpr int kDarkChecksMost = 8;           // P8 checks per menu (each a staging copy and a blocking Map)
        // per menu; 48 since build 12 (was 12: used up before the Powers tab was reached, so the A->B power switch that
        // shows and then loses its effect left no art-match line - ledger 2026-10-26)
        constexpr int kArtLinesMost = 48;

        // A (spec v13): keep the panel's alpha
        constexpr auto kTargetsMost = static_cast<std::uint32_t>(REX::W32::D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT);
        constexpr std::uint8_t kWriteAlpha = REX::W32::D3D11_COLOR_WRITE_ENABLE_ALPHA;
        constexpr std::uint32_t kTriangleVertices = 3;  // one triangle over the whole target (SV_VertexID, no vertex buffer)

        // ---------------------------------------------------------------------
        // State
        // ---------------------------------------------------------------------

        struct Saved
        {
            RE::NiPoint3 pos, posCopy;
            float scale{}, scaleCopy{};
            std::uint64_t generation{};  // the menu generation whose close puts them back
        };

        enum class Kind
        {
            kVertex,  // every drawn shape gave its vertices
            kMixed,   // some shapes only their bound sphere
            kSphere,  // bound spheres only (particles, or measured too often)
        };

        struct Reading
        {
            Seen seen;  // at the game's scale; middle measured from the model's place
            Kind kind = Kind::kSphere;
            std::size_t points = 0;
            // the measured points from the model's place (world.translate), at the game's size: grown and projected again
            // to choose the grow (third check: a perspective size does not scale with grow)
            std::vector<Vec3> outline;
        };

        enum class Phase
        {
            kMeasure,    // measure (again) this frame
            kSettling,   // a sphere measure waits until it holds still
            kFitted,     // measured: grown and centred by its reading
            kUnsettled,  // never held still: base scale, its place centred
        };

        struct Model
        {
            const RE::NiAVObject* key = nullptr;  // identity only, never read through
            const RE::TESForm* base = nullptr;
            std::string name, spell;
            float baseline = 0;  // the game's scale for it: when first seen, or what the game set since
            float setScale = 0;  // what we left it at
            float builtAt = 0;   // the base scale (itemScaleCopy) its baseline belongs to: when first seen, or our last place's
            RE::NiMatrix3 setTurn;
            float grow = 1;
            float depthShare = 1;  // the model's depth over the room's, last Apply (the room it was fitted to)
            int measures = 0;
            int counted = 0;       // measures against the vertex cap (kMeasuresMost): not the ones a turn asked for
            int measureLines = 0;  // measures logged this menu (kMeasureLinesMost, then counted only)
            bool turning = false;  // turned since the last measure; measured again after kTurnStillFrames still frames
            int stillFrames = 0;
            bool turnMeasure = false;  // the measure due was asked for by a turn
            Solved solved;             // Apply's grow for the reading, worked out again when it or the room changes
            Phase phase = Phase::kMeasure;
            Reading reading;       // the last settled reading; it stays in use while a new measure settles (R11)
            bool hasFit = false;   // reading holds a settled reading of this menu
            std::optional<Reading> last;  // the previous frame's reading while settling
            int settleFrames = 0, stableRun = 0;
            std::uint64_t generation = 0;  // the menu it was last seen in: a new one measures it afresh (R5)
        };

        struct FirstRender  // a model whose first over-pass of one menu is logged
        {
            const RE::NiAVObject* key = nullptr;
            std::uint64_t generation = 0;
        };

        struct Tally
        {
            int pass = 0, fail = 0, bound = 0;

            void Add(const char* a_verdict)
            {
                const std::string_view verdict = a_verdict;
                (verdict == "PASS" ? pass : verdict == "FAIL" ? fail : bound) += 1;
            }
        };

        // Q3: the selection of one frame info: the only models we hide from the game's own pass. One form: the tree
        // card's spell while P3 asked for its 3D (the game's preview shows that one then), else the list's item; the
        // list's model is not given back next to the P3 one (review round 2, #1)
        struct Shown
        {
            RE::FormID selected = 0, treeSpell = 0;

            [[nodiscard]] RE::FormID Form() const { return treeSpell ? treeSpell : selected; }

            [[nodiscard]] bool Has(const RE::TESForm* a_base) const
            {
                const auto id = a_base ? a_base->GetFormID() : 0;
                return id != 0 && id == Form();
            }

            // A loaded model is the selection's when its form is, or when its art object is the selection's menu art: spells
            // that share an art (FireballInvArt for Fireball, Firebolt and many mod spells) are shown with the model of the
            // first one loaded, whose itemBase is that spell (game run 2026-10-25 23:43: Flames selected, P7.3 hid 소각's
            // FireballInvArt, the box stayed empty; GPT diagnosis video7/codex_h5_result.md). Whether the game reuses such a
            // model is not proven: LogSelectionArt logs the art objects so the next run settles it. Review (build 7 run fixes):
            // no itemBase, no match (callers read itemBase); the art counts only while no model of the selection's own form
            // is up (formUp), so an earlier spell's model of the same art is not shown beside it; only the art the game would
            // use (the spell's own MDOB, the effect's only when the spell has none), so another art never stays in the box
            [[nodiscard]] bool Has(const RE::LoadedInventoryModel& a_loaded) const
            {
                if (!a_loaded.itemBase) return false;
                if (Has(a_loaded.itemBase)) return true;
                const auto* wanted = art[0] ? art[0] : art[1];
                return !formUp && wanted && a_loaded.modelObj == wanted;
            }

            std::array<const RE::TESBoundObject*, 2> art{};  // the selection's own MDOB, its costliest effect's MDOB
            bool formUp = false;                             // a model of the selection's own form is in loadedModels
        };

        // The menu art a form's preview would use: its own menu display object (a spell's MDOB), then its costliest
        // effect's (the magic effect's MDOB)
        std::array<const RE::TESBoundObject*, 2> ArtOf(RE::FormID a_form)
        {
            std::array<const RE::TESBoundObject*, 2> art{};
            auto* form = a_form ? RE::TESForm::LookupByID(a_form) : nullptr;
            if (!form) return art;
            if (const auto* display = skyrim_cast<RE::BGSMenuDisplayObject*>(form)) art[0] = display->GetMenuDisplayObject();
            if (const auto* magic = form->As<RE::MagicItem>()) {
                if (const auto* effect = magic->GetCostliestEffectItem(); effect && effect->baseEffect) {
                    art[1] = effect->baseEffect->GetMenuDisplayObject();
                }
            }
            return art;
        }

        // A model we hid, and its form. Held by a strong reference while we remember it, so its address cannot be taken
        // by a new model of the same form while the old one is freed (review third check, #2: an address and a FormID
        // alone let close/reset show a model we never hid). Compared by identity, acted on only through loadedModels;
        // the reference is dropped when the model leaves loadedModels (prune) and at close/reset, always on the main
        // thread (the release of a model the game unloaded meanwhile runs its destructor there, as the game's own
        // NiPointer releases do; Inventory Reforged's DrawUnder holds such references across a Render the same way)
        struct Hidden
        {
            RE::NiPointer<RE::NiAVObject> model;
            RE::FormID base = 0;

            [[nodiscard]] const RE::NiAVObject* Key() const { return model.get(); }
        };

        struct Watched  // a selection's model seen this menu while the hide is on: hide verdict, animation samples
        {
            const RE::NiAVObject* key = nullptr;
            RE::FormID base = 0;
            std::string name, spell;
            float seenScale = 0;  // its local.scale at the last hook: one neither the game's zoom nor our fit set is a new model
            bool hideJudged = false;
            std::chrono::steady_clock::time_point nextSample{};
            PreviewMath::MotionSamples motion;  // counted only while our fit has settled (review round 1, #4/#7)
            int restartLines = 0;
            bool animJudged = false;
        };

        // P7.2 a model's own transform when we first saw it this menu, and the game's place it was built at. Held by a
        // strong reference for the same reason as Hidden (its address cannot be taken while we remember it)
        struct Original
        {
            RE::NiPointer<RE::NiAVObject> model;
            RE::FormID base = 0;
            RE::NiPoint3 translate;
            float scale = 0;
            RE::NiPoint3 builtPos;  // itemPos when first seen
            float builtScale = 0;   // itemScaleCopy when first seen
            // its world place and scale then, as the engine worked them out: the P7.2 self-test checks the put-back against
            // the engine's own Update, not against our arithmetic
            RE::NiPoint3 world;
            float worldScale = 0;
        };

        struct Clip  // Q3 and P7, one menu
        {
            std::vector<Hidden> hidden;  // hidden by us after the last over-pass
            // every model we hid this menu that is still up (pruned when it leaves loadedModels): a model of an earlier
            // selection the game keeps and does not show again when it is selected again is given back from here
            // (review round 1, #6/#8: A -> B -> A)
            std::vector<Hidden> everHidden;
            // a model of ours the game replaced with a new one of the same form: never given back again (review third
            // check, #1: once the new one was hidden too, the next frame gave both back), left hidden at close as well
            std::vector<Hidden> replacedModels;
            Shown shown;                 // from the last frame info
            std::vector<const RE::NiAVObject*> shownLast;  // the selection's models at the last hook: a new one is a change
            std::vector<Watched> watched;
            std::uint64_t frame = 0;     // PostDisplay calls of this menu (the frame counter of the Q3 lines)
            bool modesSet = false, hideOn = false, clipOn = false;  // this menu's Q3 modes, from its first over-pass (the summary's)
            bool inactive = false;       // the last hook had the preview box not shown
            bool clipLogged = false, clipFailLogged = false, clipSkipLogged = false, clipTargetLogged = false;
            bool unfittedLogged = false, inactiveLogged = false, replacedLogged = false, doublesLogged = false;
            int sightLines = 0, againLines = 0, rebuiltLines = 0;
            std::vector<RE::NiPointer<RE::NiAVObject>> tabHidden;  // HideForTab: hidden on a tab without the box
            bool tabHideLogged = false;
            int tabHides = 0;
            bool dirtyLogged = false;            // P8: the dirty-state line logged this menu
            std::uint64_t selectionFrame = 0;    // the frame of the last selection change (P8's check waits kDarkCheckDelay after it)
            int darkChecks = 0;
            std::uint64_t darkCheckedFor = 0;    // the selectionFrame P8 last checked (one check per selection)
            bool swapChainLogged = false;  // spec v12 C3 / G10
            // A (spec v13 A4): this menu's alpha restore lines and counts
            bool restoreLogged = false, restoreSkipLogged = false;
            int restores = 0, restoreSkipped = 0;
            // review v13 #2: frames without the scissor restore the whole target (counted apart from the box restores), and
            // a frame with no target to save at all is counted and explained apart from a real skip (logged once each)
            int restoresWhole = 0, restoreNoTarget = 0;
            bool restoreWholeLogged = false, restoreNoTargetLogged = false;
            int scratchCreated = 0;  // review v13 #5, round 2 #5: successful scratch (re)creations this menu
            // review v13 round 2 #2: the Powers hide (a_hide3D) at the last PostDisplay for this menu's movie, so its end is seen
            bool powersHid = false;
            std::uint64_t artPending = 0;  // LogSelectionArt: the frame of the selection change waiting (0 = none)
            int artLines = 0;              // its lines this menu
            std::vector<const RE::NiAVObject*> leftLogged;  // earlier selections' models logged as left hidden
            // for the close summary
            int overPasses = 0, hides = 0, held = 0, lost = 0, hidePass = 0, hideFail = 0, sights = 0, flashes = 0, again = 0;
            int left = 0, replaced = 0, doubles = 0, rebuilt = 0;
            int lostLines = 0;  // the "Q3 lost" lines this menu (the first kSightLinesMost; later ones only counted in lost)
            int clipped = 0, clipHeld = 0, clipDropped = 0, clipUnjudged = 0, clipSkipped = 0, clipUnfitted = 0, clipTargetChanged = 0;
            // blocks plan Task 3 (D2): frames whose box was wholly out of its column (nothing drawn), unfitted frames scissored to
            // the column alone, and the L2 preview clip verdicts (the scissor inside the column's pixels)
            int columnOut = 0, columnOnly = 0, columnClipPass = 0, columnClipFail = 0, columnClipNotCut = 0;
            bool columnOutLogged = false, columnClipPassLogged = false, columnClipFailLogged = false;
            int animMoved = 0, animStill = 0, animNone = 0;
            // P7 (spec v7)
            std::vector<Original> originals;                // P7.2
            std::vector<Hidden> artLogged;  // P7.1: models whose art shapes were logged (strong refs, by identity and form)
            bool othersLogged = false, emptyLogged = false, coveredLogged = false;
            int backdrops = 0, glows = 0, artAlready = 0, artShownAgain = 0, othersFrames = 0, emptyFrames = 0, coveredFrames = 0, unplaced = 0;
            int unplacePass = 0, unplaceFail = 0;
        };

        struct State
        {
            std::uint64_t generation = 0;        // the menu this state was reset for
            std::optional<Saved> saved;          // the game's place before our first change
            std::optional<FrameInfo> placedFor;  // the box and visible rect the place was worked out for
            std::optional<Room> room;
            std::vector<Model> models;           // per-model cache, kept across menus; entries leave with their model
            std::vector<FirstRender> firstRenders;
            bool probed = false, waitLogged = false;
            // this menu, for the close summary (R4, R5)
            int measures = 0, hiddenLines = 0;
            Tally logged, hidden;  // P1 verdicts of the measures logged / only counted
            std::size_t lastLoaded = 0;  // loadedModels at the menu's last fitted frame
            Clip clip;
        };
        // Never destroyed (left to the process's end on purpose): Clip holds NiPointers, and releasing one in a static
        // destructor at game exit would run a model's destructor after the game's own systems have gone
        State& g_state = *new State{};

        struct Slot
        {
            std::mutex lock;
            std::optional<FrameInfo> frame;
            std::uint64_t generation = 0;
        } g_slot;

        std::atomic<bool> g_closing{ false };   // the close event came: nothing until the next open
        std::atomic<bool> g_irActive{ false };  // Inventory Reforged's panel is in this magic menu
        // Bumped on every menu open and close; main-thread work runs only for the generation it belongs to
        std::atomic<std::uint64_t> g_generation{ 0 };

        // ---------------------------------------------------------------------
        // Helpers
        // ---------------------------------------------------------------------

        // Runs a_work on the main thread: now when this is it, else as an SKSE task
        template <class Work>
        void OnMainThread(Work&& a_work)
        {
            if (GameThread::On()) {
                a_work();
            } else if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask(std::forward<Work>(a_work));
            } else {
                logger::error("Preview: no SKSE task interface - main-thread work dropped");
            }
        }

        void SelfTest(const std::string& a_line)
        {
            if (!Settings::Get().selfTest) return;
            logger::info("SelfTest: {}", a_line);
            // info lines wait in the buffer (Common.h): the verdicts are written through, the details ride along
            if (a_line.find("PASS") != std::string::npos || a_line.find("FAIL") != std::string::npos ||
                a_line.find("BOUND") != std::string::npos || a_line.find("WARN") != std::string::npos ||
                a_line.find("INCONCLUSIVE") != std::string::npos) {
                FlushLog();
            }
        }

        bool Current(std::uint64_t a_generation)
        {
            return a_generation == g_generation.load();
        }

        // zoomDistance is the zoom's direction (0 when it stands), zoomProgress how far in it is
        bool AtRest(RE::Inventory3DManager& a_manager)
        {
            const auto& zoom = a_manager.GetRuntimeData();
            return zoom.zoomProgress == 0.0f && zoom.zoomDistance == 0.0f;
        }

        RE::NiCamera* Camera()
        {
            auto* scene = RE::UI3DSceneManager::GetSingleton();
            return scene ? scene->camera.get() : nullptr;
        }

        Vec3 V(const RE::NiPoint3& a_point)
        {
            return { a_point.x, a_point.y, a_point.z };
        }

        RE::NiPoint3 Ni(const Vec3& a_point)
        {
            return { a_point.x, a_point.y, a_point.z };
        }

        PreviewMath::Mat3 TurnOf(const RE::NiMatrix3& a_turn)
        {
            PreviewMath::Mat3 turn;
            for (int row = 0; row < 3; ++row) {
                for (int column = 0; column < 3; ++column) turn.entry[row][column] = a_turn.entry[row][column];
            }
            return turn;
        }

        PreviewMath::Rect MathRect(const Settings::Rect& a_rect)
        {
            return { a_rect.left, a_rect.top, a_rect.right, a_rect.bottom };
        }

        PreviewMath::Rect MathRect(const RE::GRectF& a_rect)
        {
            return { a_rect.left, a_rect.top, a_rect.right, a_rect.bottom };
        }

        // A Gamebryo camera looks along the first column of its world turn; the second is up, the third right
        Axes AxesOf(const RE::NiCamera& a_camera)
        {
            const auto& m = a_camera.world.rotate.entry;
            return { { m[0][0], m[1][0], m[2][0] }, { m[0][1], m[1][1], m[2][1] }, { m[0][2], m[1][2], m[2][2] } };
        }

        bool SameTurn(const RE::NiMatrix3& a_one, const RE::NiMatrix3& a_other)
        {
            for (int row = 0; row < 3; ++row) {
                for (int column = 0; column < 3; ++column) {
                    if (std::abs(a_one.entry[row][column] - a_other.entry[row][column]) > kSameTurn) return false;
                }
            }
            return true;
        }

        // A world move as a change of the model's own local.translate: through its parent's turn and scale
        void MoveInParent(RE::NiAVObject& a_model, const Vec3& a_move)
        {
            const auto* parent = a_model.parent;
            a_model.local.translate += Ni(parent ? PreviewMath::LocalMove(TurnOf(parent->world.rotate), parent->world.scale, a_move) : a_move);
        }

        std::string SpellName(const RE::TESForm* a_base)
        {
            return a_base ? std::format("{:08X} {}", a_base->GetFormID(), a_base->GetName()) : "(no base)";
        }

        // ---------------------------------------------------------------------
        // Screen pixels (self-test and clip)
        // ---------------------------------------------------------------------

        // NiRect keeps left/right/top/bottom protected (CommonLib NiRect.h): read the same 16 bytes as plain floats
        struct Port
        {
            float left, right, top, bottom;
        };
        static_assert(sizeof(Port) == sizeof(RE::NiRect<float>));

        Port PortOf(RE::NiCamera& a_camera)
        {
            return std::bit_cast<Port>(a_camera.GetRuntimeData2().port);
        }

        struct Pixel
        {
            double x{}, y{};
        };

        // A world point in screen pixels: the camera's port output (Gamebryo: y up) onto the movie's viewport
        // (the 3D scene and the movie cover the same screen; the axis probe checks the directions)
        std::optional<Pixel> ToPixels(RE::NiCamera& a_camera, const Vec3& a_point, const RE::GRectF& a_viewport)
        {
            float x = 0, y = 0, z = 0;
            if (!a_camera.WorldPtToScreenPt3(Ni(a_point), x, y, z, kZeroTolerance)) return std::nullopt;
            const auto port = PortOf(a_camera);
            const double portW = port.right - port.left, portH = port.top - port.bottom;
            if (std::abs(portW) < kFlat || std::abs(portH) < kFlat) return std::nullopt;
            const double fx = (x - port.left) / portW, fyDown = (port.top - y) / portH;
            return Pixel{ a_viewport.left + fx * (a_viewport.right - a_viewport.left), a_viewport.top + fyDown * (a_viewport.bottom - a_viewport.top) };
        }

        struct BoxOnScreen
        {
            PixelRect rect;
            const char* source = "";
            PixelRect mapped, translated;
        };

        // The preview box in screen pixels on the movie's side: TranslateToScreen when it agrees in size and centre with
        // the visible-rect mapping the place uses (PreviewMath::ScreenAgrees), else that mapping (logged as which)
        BoxOnScreen BoxPixels(const FrameInfo& a_frame)
        {
            const auto mapped = PreviewMath::StageToPixels(MathRect(a_frame.box), MathRect(a_frame.visible), MathRect(a_frame.viewport));
            const auto& screen = a_frame.boxScreen;
            const PixelRect translated{ screen.left, screen.top, screen.right, screen.bottom };
            // size and centre (review round 1, #13: a size check alone let a shifted box through); every P1, probe and clip
            // line names the source and the probe line gives both rects
            const bool agrees = PreviewMath::ScreenAgrees(mapped, translated);
            return { agrees ? translated : mapped, agrees ? "TranslateToScreen" : "visible-rect mapping (TranslateToScreen's size or centre disagrees)",
                mapped, translated };
        }

        // A stage rect in screen pixels: the box itself as BoxPixels gives it, any other rect (the box cut to its column, the
        // column's viewport, blocks plan Task 3) by the visible-rect mapping the place uses
        BoxOnScreen StagePixels(const FrameInfo& a_frame, const Settings::Rect& a_stage)
        {
            const auto& b = a_frame.box;
            if (a_stage.left == b.left && a_stage.top == b.top && a_stage.right == b.right && a_stage.bottom == b.bottom) return BoxPixels(a_frame);
            const auto mapped = PreviewMath::StageToPixels(MathRect(a_stage), MathRect(a_frame.visible), MathRect(a_frame.viewport));
            return { mapped, "visible-rect mapping (the box cut to its column, or the column's viewport)", mapped, {} };
        }

        // ---------------------------------------------------------------------
        // What the camera sees of a model
        // ---------------------------------------------------------------------

        struct Points
        {
            std::vector<Vec3> world;
            int vertexShapes = 0, sphereShapes = 0;
            int hiddenParts = 0;  // parts left out because they are hidden (P7.1's backdrop and glow, the game's own)
            // particle systems' bound spheres, kept apart: used only when nothing else is drawn. Live particles' bounds are many
            // times the art (game run 2026-10-25 23:43: 347.8 against a 24.9 outline, grow 0.09 - a dot in the box); before the
            // first particles they are empty, which is why first measures were right
            std::vector<Vec3> particleWorld;
            int particleShapes = 0;
        };

        RE::NiPoint3 ReadPosition(const std::uint8_t* a_at, bool a_floats)
        {
            if (a_floats) {
                float value[3];
                std::memcpy(value, a_at, sizeof(value));
                return { value[0], value[1], value[2] };
            }
            std::uint16_t half[3];
            std::memcpy(half, a_at, sizeof(half));
            using DirectX::PackedVector::XMConvertHalfToFloat;
            return { XMConvertHalfToFloat(half[0]), XMConvertHalfToFloat(half[1]), XMConvertHalfToFloat(half[2]) };
        }

        // The renderer's CPU copy of a shape's vertices (rawVertexData), in the world. False when there is none to
        // read: particles, dynamic (face) and skinned shapes, a copy without positions
        bool AddVertices(RE::NiAVObject& a_part, std::vector<Vec3>& a_out)
        {
            auto* shape = a_part.AsTriShape();
            if (!shape || a_part.AsDynamicTriShape() || a_part.AsParticlesGeom()) return false;
            const auto& geometry = shape->GetGeometryRuntimeData();
            const auto* data = geometry.rendererData;
            if (geometry.skinInstance || !data || !data->rawVertexData) return false;
            auto desc = data->vertexDesc;
            if (!desc.HasFlag(RE::BSGraphics::Vertex::VF_VERTEX)) return false;
            // VertexDesc::GetSize leaves land and instance data out, so the float test below would misjudge such a layout:
            // those shapes count by their sphere (second review R5; none expected in a spell's menu art)
            if (desc.HasFlag(RE::BSGraphics::Vertex::VF_LANDDATA) || desc.HasFlag(RE::BSGraphics::Vertex::VF_INSTANCEDATA)) return false;
            const auto stride = static_cast<std::uint32_t>(std::bit_cast<std::uint64_t>(desc) & kStrideMask) * 4;
            const std::uint32_t offset = desc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_POSITION);
            // GetSize counts a position as four floats: a stride with room for them holds floats even without VF_FULLPREC
            const bool floats = desc.HasFlag(RE::BSGraphics::Vertex::VF_FULLPREC) || stride == desc.GetSize();
            const std::uint32_t need = floats ? 3 * sizeof(float) : 3 * sizeof(std::uint16_t);
            const std::uint32_t count = shape->GetTrishapeRuntimeData().vertexCount;
            if (stride == 0 || count == 0 || offset + need > stride) return false;
            a_out.reserve(a_out.size() + count);
            for (std::uint32_t i = 0; i < count; ++i) {
                const auto local = ReadPosition(data->rawVertexData + static_cast<std::size_t>(i) * stride + offset, floats);
                a_out.push_back(V(a_part.world * local));
            }
            return true;
        }

        // A bound sphere as points round its centre in the camera's right/up plane (PreviewMath::SpherePoints)
        void AddSphere(const RE::NiBound& a_bound, const Axes& a_axes, Points& a_points)
        {
            const auto sphere = PreviewMath::SpherePoints(V(a_bound.center), a_bound.radius, a_axes);
            if (sphere.empty()) return;
            a_points.world.insert(a_points.world.end(), sphere.begin(), sphere.end());
            ++a_points.sphereShapes;
        }

        // Every drawn shape under a_part (not culled, with a shader): its vertices when allowed and readable, else its sphere
        void Gather(RE::NiAVObject& a_part, const Axes& a_axes, bool a_vertices, Points& a_points)
        {
            if (a_part.GetAppCulled()) {
                ++a_points.hiddenParts;
                return;
            }
            if (auto* geometry = a_part.AsGeometry(); geometry && geometry->GetGeometryRuntimeData().shaderProperty) {
                if (a_part.AsParticlesGeom()) {
                    const auto sphere = PreviewMath::SpherePoints(V(a_part.worldBound.center), a_part.worldBound.radius, a_axes);
                    if (!sphere.empty()) {
                        a_points.particleWorld.insert(a_points.particleWorld.end(), sphere.begin(), sphere.end());
                        ++a_points.particleShapes;
                    }
                } else if (a_vertices && AddVertices(a_part, a_points.world)) {
                    ++a_points.vertexShapes;
                } else {
                    AddSphere(a_part.worldBound, a_axes, a_points);
                }
            }
            if (auto* node = a_part.AsNode()) {
                for (const auto& child : node->GetChildren()) {
                    if (child) Gather(*child, a_axes, a_vertices, a_points);
                }
            }
        }

        Kind KindOf(const Points& a_points)
        {
            return a_points.sphereShapes == 0 ? Kind::kVertex : a_points.vertexShapes == 0 ? Kind::kSphere : Kind::kMixed;
        }

        Points Outline(RE::NiAVObject& a_model, const Axes& a_axes, bool a_vertices)
        {
            Points points;
            Gather(a_model, a_axes, a_vertices, points);
            // particle-only art: its particles' spheres are all there is
            if (points.world.empty() && points.particleShapes > 0) {
                points.world = std::move(points.particleWorld);
                points.sphereShapes += points.particleShapes;
            }
            // nothing drawn below it: its own bound - but not when its parts are hidden on purpose (every shape a backdrop or
            // glow P7.1 hid): the root's bound still holds them, so nothing is measured (v7 design review #8)
            if (points.world.empty() && points.hiddenParts == 0) AddSphere(a_model.worldBound, a_axes, points);
            return points;
        }

        // What is seen of the model now, scaled to its game scale by a_toBaseline (PreviewMath::Measure: perspective,
        // each point brought along its line of sight to the model's own depth); none when it is empty, not finite, or
        // a point is not in front of the camera
        std::optional<Reading> Read(RE::NiAVObject& a_model, const Axes& a_axes, bool a_vertices, float a_toBaseline, const Vec3& a_eye)
        {
            const auto points = Outline(a_model, a_axes, a_vertices);
            auto measured = PreviewMath::Measure(points.world, V(a_model.world.translate), a_eye, a_axes, a_toBaseline);
            if (!measured) return std::nullopt;
            return Reading{ measured->seen, KindOf(points), points.world.size(), std::move(measured->outline) };
        }

        const char* KindText(Kind a_kind)
        {
            return a_kind == Kind::kVertex ? "vertices" : a_kind == Kind::kMixed ? "vertices and bound spheres" : "bound spheres";
        }

        // ---------------------------------------------------------------------
        // The per-model cache
        // ---------------------------------------------------------------------

        // A reading taken at one scale, at another (every part of it is a length)
        void ScaleReading(Reading& a_reading, float a_ratio)
        {
            a_reading.seen.wide *= a_ratio;
            a_reading.seen.high *= a_ratio;
            a_reading.seen.middle *= a_ratio;
            for (auto& point : a_reading.outline) point *= a_ratio;
        }

        // The entry's game scale and its readings follow a scale change of the game's (a_ratio = new / old)
        void ScaleEntry(Model& a_entry, float a_ratio)
        {
            a_entry.baseline *= a_ratio;
            a_entry.setScale *= a_ratio;
            a_entry.solved.valid = false;
            ScaleReading(a_entry.reading, a_ratio);
            if (a_entry.last) ScaleReading(*a_entry.last, a_ratio);
        }

        Model* FindEntry(const RE::NiAVObject* a_model, const RE::TESForm* a_base)
        {
            const auto known = std::ranges::find_if(g_state.models, [&](const Model& a_entry) { return a_entry.key == a_model && a_entry.base == a_base; });
            return known != g_state.models.end() ? &*known : nullptr;
        }

        // The cache entry of a model up. A model seen first in this menu is measured afresh: a model built anew at
        // the address of an earlier one (same base) must not inherit its measure count (vertices off after 6) or its
        // reading (review R5). The game scale it was left at (baseline, setScale) stays: a surviving model's fitted
        // scale is never taken as its baseline
        Model& Entry(RE::NiAVObject& a_model, const RE::TESForm* a_base, float a_baseScale)
        {
            const auto known = std::ranges::find_if(g_state.models, [&](const Model& a_entry) { return a_entry.key == &a_model; });
            if (known != g_state.models.end() && known->base == a_base) {
                if (known->generation != g_state.generation) {
                    // not left at our scale: a model built anew at this address (or rescaled by the game) since; its scale
                    // now is the game's at this base scale (second review X2)
                    if (PreviewMath::Rebuilt(true, a_model.local.scale, known->setScale)) {
                        known->baseline = known->setScale = a_model.local.scale;
                        known->builtAt = a_baseScale;
                        known->setTurn = a_model.local.rotate;
                    }
                    known->generation = g_state.generation;
                    known->measures = known->counted = known->measureLines = 0;
                    known->phase = Phase::kMeasure;
                    known->hasFit = false;
                    known->grow = known->depthShare = 1;
                    known->last.reset();
                    known->settleFrames = known->stableRun = 0;
                    known->turning = known->turnMeasure = false;
                    known->stillFrames = 0;
                }
                return *known;
            }
            if (known != g_state.models.end()) g_state.models.erase(known);  // the address was taken by a new model
            Model entry;
            entry.key = &a_model;
            entry.base = a_base;
            entry.name = a_model.name.c_str();
            entry.spell = SpellName(a_base);
            entry.baseline = entry.setScale = a_model.local.scale;
            entry.setTurn = a_model.local.rotate;
            entry.builtAt = a_baseScale;
            entry.generation = g_state.generation;
            return g_state.models.emplace_back(std::move(entry));
        }

        // Drops the cache entries (and first-render marks) of models that left loadedModels
        void Forget(RE::Inventory3DManager& a_manager)
        {
            const auto& loaded = a_manager.GetRuntimeData().loadedModels;
            const auto gone = [&](const RE::NiAVObject* a_key) {
                return std::ranges::none_of(loaded, [&](const auto& a_loaded) { return a_loaded.spModel.get() == a_key; });
            };
            std::erase_if(g_state.models, [&](const Model& a_entry) { return gone(a_entry.key); });
            std::erase_if(g_state.firstRenders, [&](const FirstRender& a_mark) { return gone(a_mark.key); });
        }

        // The game changed the model since we left it (a zoom sets its scale, a turn of its own): measure it again.
        // A scale the game set is its new baseline (the readings follow it); ours never is. A turn changes what is
        // seen of every kind: an outline of vertices, and spheres off the model's centre (review G1: room 40x20,
        // outline 40x10 at grow .92 turned 90 degrees is 9.2 x 36.8 and overflows)
        // A turn is measured once it has held still for kTurnStillFrames (a drag or a turn of the game's own turns it
        // every frame: measuring each frame would spend the vertex measures in a few frames and drop to bound spheres,
        // second review R3), fitted and unsettled models alike (I2); such a measure is not counted against the vertex cap
        void Notice(Model& a_entry, const RE::NiAVObject& a_model)
        {
            if (a_model.local.scale != a_entry.setScale) {
                if (a_entry.baseline > 0.0f && a_model.local.scale > 0.0f) {
                    const float ratio = a_model.local.scale / a_entry.baseline;
                    a_entry.solved.valid = false;
                    ScaleReading(a_entry.reading, ratio);
                    if (a_entry.last) ScaleReading(*a_entry.last, ratio);
                }
                a_entry.baseline = a_model.local.scale;
                a_entry.phase = Phase::kMeasure;
            }
            if (!SameTurn(a_model.local.rotate, a_entry.setTurn)) {
                a_entry.turning = true;
                a_entry.stillFrames = 0;
            } else if (a_entry.turning) {
                ++a_entry.stillFrames;
            }
            const bool measured = a_entry.phase == Phase::kFitted || a_entry.phase == Phase::kUnsettled;
            if (a_entry.turning && a_entry.stillFrames >= kTurnStillFrames && measured) {
                a_entry.turning = false;
                a_entry.turnMeasure = true;
                a_entry.phase = Phase::kMeasure;
            }
        }

        // One measure step; true when the measure settled this frame (fitted or given up on)
        bool StepMeasure(Model& a_entry, RE::NiAVObject& a_model, const Axes& a_axes, const Vec3& a_eye)
        {
            if (a_entry.phase == Phase::kMeasure) {
                ++a_entry.measures;
                if (!std::exchange(a_entry.turnMeasure, false)) ++a_entry.counted;
                a_entry.settleFrames = a_entry.stableRun = 0;
                a_entry.last.reset();
                a_entry.phase = Phase::kSettling;
            }
            if (a_entry.phase != Phase::kSettling) return false;
            // the points are world points: what is seen scales with the model's world scale, which is the parent's times
            // its own (and may not have caught up with a local.scale the game just set), so the game's size is
            // baseline * parent's world scale (review R12)
            const float parentScale = a_model.parent && a_model.parent->world.scale > 0.0f ? a_model.parent->world.scale : 1.0f;
            const float toBaseline = a_model.world.scale > 0.0f ? a_entry.baseline * parentScale / a_model.world.scale : 1.0f;
            const auto reading = Read(a_model, a_axes, a_entry.counted <= kMeasuresMost, toBaseline, a_eye);
            // a measure with any bound sphere in it counts only once it holds still (a particle bound changes on every read)
            a_entry.stableRun = reading && a_entry.last && PreviewMath::Agrees(a_entry.last->seen, reading->seen) ? a_entry.stableRun + 1 :
                                reading                                                                          ? 1 :
                                                                                                                   0;
            a_entry.last = reading;
            ++a_entry.settleFrames;
            if (reading && (reading->kind == Kind::kVertex || a_entry.stableRun >= kStableFrames)) {
                a_entry.reading = *reading;
                a_entry.solved.valid = false;
                a_entry.hasFit = true;
                a_entry.phase = Phase::kFitted;
                return true;
            }
            if (a_entry.settleFrames >= kSettleFramesMost) {
                a_entry.phase = Phase::kUnsettled;
                return true;
            }
            return false;
        }

        // Grows the model by its settled reading and moves what is seen of it onto the line of sight through the box's
        // middle, at the model's own depth (the room scaled to that depth: the camera is perspective, review R6),
        // across the line of sight only. While a new measure settles, or when it never did, the last settled
        // reading of this menu stays in use (no drop to grow 1, review R11); with none yet: base scale, its place centred.
        // The grow is PreviewMath::Solve's (re-projected, cached per reading, room and depth)
        void Apply(Model& a_entry, RE::NiAVObject& a_model, const Room& a_room, const Axes& a_axes, const Vec3& a_eye)
        {
            const auto place = V(a_model.world.translate);
            const auto aimed = PreviewMath::AimAt(place, a_eye, a_axes, a_room);
            float grow = 1.0f;
            Vec3 middle;
            if (a_entry.hasFit) {
                auto& solved = a_entry.solved;
                if (!solved.valid || solved.depth != aimed.depth || solved.wide != aimed.wide || solved.high != aimed.high) {
                    solved = PreviewMath::Solve(a_entry.reading.outline, aimed.aim - a_eye, a_axes, aimed.depth, aimed.wide, aimed.high);
                }
                grow = solved.grow;
                middle = solved.middle;
            }
            bool changed = false;
            if (const float scale = a_entry.baseline * grow; a_model.local.scale != scale) {
                a_model.local.scale = scale;
                changed = true;
            }
            // the model's own scale does not move its place (world.translate), so it is read before the Update
            const auto shift = PreviewMath::Across(aimed.aim - (place + middle), a_axes);
            if (shift.Length() > aimed.high * kShiftShare) {
                MoveInParent(a_model, shift);
                changed = true;
            }
            if (changed) {
                RE::NiUpdateData update{};
                a_model.Update(update);
            }
            a_entry.setScale = a_model.local.scale;
            a_entry.setTurn = a_model.local.rotate;
            a_entry.grow = grow;
            a_entry.depthShare = aimed.share;
        }

        void LogMeasure(const Model& a_entry, const Room& a_room)
        {
            const float wide = a_room.wide * a_entry.depthShare, high = a_room.high * a_entry.depthShare;
            if (a_entry.phase == Phase::kUnsettled) {
                logger::info("Preview: measure {} of {} ({}): its bound never held still in {} frames - {}", a_entry.measures, a_entry.name,
                    a_entry.spell, kSettleFramesMost,
                    a_entry.hasFit ? std::format("its last settled reading stays (grow {:.2f})", a_entry.grow) : "base scale, its place centred");
                return;
            }
            const auto& seen = a_entry.reading.seen;
            logger::info("Preview: measure {} of {} ({}): {} ({} points), seen {:.2f} x {:.2f} at the game's size, room {:.2f} x {:.2f} "
                         "at its depth ({:.3f} of the box's), grow {:.3f} ({})",
                a_entry.measures, a_entry.name, a_entry.spell, KindText(a_entry.reading.kind), a_entry.reading.points, seen.wide, seen.high,
                wide, high, a_entry.depthShare, a_entry.grow,
                a_entry.solved.steps == 0 ? std::format("the cap {:.1f} fits", kGrowMost) :
                                            std::format("re-projected, {} bisection steps", a_entry.solved.steps));
        }

        // ---------------------------------------------------------------------
        // Self-test: the drawn points, projected, against the box on screen
        // ---------------------------------------------------------------------

        // The P1 verdict ("PASS", "FAIL", "BOUND"; null with bSelfTest=0), its line logged only with a_log (R4)
        const char* TestFit(const Model& a_entry, RE::NiAVObject& a_model, const Axes& a_axes, RE::NiCamera& a_camera, const FrameInfo& a_frame,
            bool a_log)
        {
            if (!Settings::Get().selfTest) return nullptr;
            const auto line = [&](const std::string& a_text) {
                if (a_log) SelfTest(a_text);
            };
            const auto points = Outline(a_model, a_axes, a_entry.counted <= kMeasuresMost);
            constexpr double kHuge = std::numeric_limits<double>::max();
            PixelRect drawn{ kHuge, kHuge, -kHuge, -kHuge };
            for (const auto& point : points.world) {
                const auto pixel = ToPixels(a_camera, point, a_frame.viewport);
                if (!pixel) {
                    line(std::format("P1 FAIL {} {}: a drawn point does not project (behind the camera or a flat port)", a_entry.name, a_entry.spell));
                    return "FAIL";
                }
                drawn = { std::min(drawn.left, pixel->x), std::min(drawn.top, pixel->y), std::max(drawn.right, pixel->x), std::max(drawn.bottom, pixel->y) };
            }
            if (points.world.empty() && points.hiddenParts > 0) {
                line(std::format("P1 BOUND {} {}: nothing drawn to project ({} hidden parts: P7.1's backdrop and glow, or the game's own)",
                    a_entry.name, a_entry.spell, points.hiddenParts));
                return "BOUND";
            }
            if (points.world.empty()) {
                line(std::format("P1 FAIL {} {}: nothing to project", a_entry.name, a_entry.spell));
                return "FAIL";
            }
            const auto box = BoxPixels(a_frame);
            const auto& rect = box.rect;
            const double off = std::hypot(drawn.CenterX() - rect.CenterX(), drawn.CenterY() - rect.CenterY());
            const bool inside = drawn.left >= rect.left - kInsideSlackPixels && drawn.right <= rect.right + kInsideSlackPixels &&
                                drawn.top >= rect.top - kInsideSlackPixels && drawn.bottom <= rect.bottom + kInsideSlackPixels;
            // only a vertex measure is the real outline; one with bound spheres in it is an estimate
            const bool exact = a_entry.phase == Phase::kFitted && a_entry.reading.kind == Kind::kVertex;
            const char* verdict = !exact ? "BOUND" : off <= kPassPixels && inside ? "PASS" : "FAIL";
            line(std::format("P1 {} {} {}: {}{} ({} points, grow {:.2f}), drawn px {:.1f}..{:.1f} x {:.1f}..{:.1f} centre {:.1f},{:.1f}; "
                                 "box px {:.1f}..{:.1f} x {:.1f}..{:.1f} centre {:.1f},{:.1f} ({}); centre off {:.1f} px (pass <= {}), inside {}{}",
                verdict, a_entry.name, a_entry.spell, a_entry.phase == Phase::kUnsettled ? "unsettled, " : "", KindText(KindOf(points)),
                points.world.size(), a_entry.grow, drawn.left, drawn.right, drawn.top, drawn.bottom, drawn.CenterX(), drawn.CenterY(), rect.left,
                rect.right, rect.top, rect.bottom, rect.CenterX(), rect.CenterY(), box.source, off, kPassPixels, inside ? "yes" : "no",
                // particles left out of the measure are not judged: live ones can spread past the box (the scissor cuts them)
                points.particleWorld.empty() ? "" : std::format("; {} particle system(s) left out, not judged", points.particleShapes)));
            return verdict;
        }

        // Once per menu: do the camera's right and up go right and up on screen, and the two box mappings agree?
        void ProbeAxes(RE::NiCamera& a_camera, const Axes& a_axes, float a_depth, const FrameInfo& a_frame)
        {
            g_state.probed = true;
            const auto centre = V(a_camera.world.translate) + a_axes.look * a_depth;
            const float step = a_depth * kProbeShare;
            const auto here = ToPixels(a_camera, centre, a_frame.viewport);
            const auto right = ToPixels(a_camera, centre + a_axes.right * step, a_frame.viewport);
            const auto up = ToPixels(a_camera, centre + a_axes.up * step, a_frame.viewport);
            const auto& frustum = a_camera.GetRuntimeData2().viewFrustum;
            const auto port = PortOf(a_camera);
            const auto box = BoxPixels(a_frame);
            const std::string setup = std::format("frustum l{:.3f} r{:.3f} t{:.3f} b{:.3f} ortho {}, port l{} r{} t{} b{}, depth {:.1f}, viewport "
                                                  "{:.0f}..{:.0f} x {:.0f}..{:.0f}; box px mapped {:.1f}..{:.1f} x {:.1f}..{:.1f}, TranslateToScreen "
                                                  "{:.1f}..{:.1f} x {:.1f}..{:.1f}, using {}",
                frustum.fLeft, frustum.fRight, frustum.fTop, frustum.fBottom, frustum.bOrtho, port.left, port.right, port.top, port.bottom,
                a_depth, a_frame.viewport.left, a_frame.viewport.right, a_frame.viewport.top, a_frame.viewport.bottom, box.mapped.left,
                box.mapped.right, box.mapped.top, box.mapped.bottom, box.translated.left, box.translated.right, box.translated.top,
                box.translated.bottom, box.source);
            if (!here || !right || !up) {
                SelfTest(std::format("P1 axes FAIL: a probe point does not project ({})", setup));
                return;
            }
            const bool rightGoesRight = right->x > here->x, upGoesUp = up->y < here->y;
            SelfTest(std::format("P1 axes {}: centre {:.1f},{:.1f} px, +right {:.1f},{:.1f} (goes right: {}), +up {:.1f},{:.1f} (goes up: {}) ({})",
                rightGoesRight && upGoesUp ? "PASS" : "FAIL", here->x, here->y, right->x, right->y, rightGoesRight ? "yes" : "no", up->x, up->y,
                upGoesUp ? "yes" : "no", setup));
        }

        // ---------------------------------------------------------------------
        // Place and fit
        // ---------------------------------------------------------------------

        // The game's place, saved before our first change. Originals an earlier menu saved and did not put back
        // yet are still the game's (its Restore task may run after this menu's first frame): this menu's close
        // restores them. False while the game's place is not usable yet (retried next frame): no scale, or not in
        // front of the camera (a place saved then would make every Place of this menu fail and be put back at close)
        bool EnsureSaved(RE::Inventory3DManager& a_manager, RE::NiCamera& a_camera, std::uint64_t a_generation)
        {
            if (g_state.saved) {
                g_state.saved->generation = a_generation;
                return true;
            }
            const float depth = PreviewMath::DepthOf(V(a_manager.itemPos), V(a_camera.world.translate), AxesOf(a_camera));
            if (!(a_manager.itemScale > 0.0f) || !V(a_manager.itemPos).Finite() || !(depth > 0.0f)) {
                if (!std::exchange(g_state.waitLogged, true)) {
                    logger::info("Preview: the game's preview place is not set yet (itemScale {:.3f}, depth {:.2f}) - waiting",
                        a_manager.itemScale, depth);
                }
                return false;
            }
            g_state.saved = Saved{ a_manager.itemPos, a_manager.itemPosCopy, a_manager.itemScale, a_manager.itemScaleCopy, a_generation };
            return true;
        }

        bool SameWindow(const FrameInfo& a_one, const FrameInfo& a_other)
        {
            const auto& b1 = a_one.box;
            const auto& b2 = a_other.box;
            const auto& v1 = a_one.visible;
            const auto& v2 = a_other.visible;
            return b1.left == b2.left && b1.top == b2.top && b1.right == b2.right && b1.bottom == b2.bottom && v1.left == v2.left &&
                   v1.top == v2.top && v1.right == v2.right && v1.bottom == v2.bottom;
        }

        // The box's middle as a world point at the game's preview depth (from the saved place, never the live one),
        // its size as the room (PreviewMath::Place); every model up moves along (absolute: place - itemPos, nothing
        // piles up)
        bool Place(RE::Inventory3DManager& a_manager, RE::NiCamera& a_camera, const FrameInfo& a_frame)
        {
            const auto axes = AxesOf(a_camera);
            const auto eye = V(a_camera.world.translate);
            const auto& saved = *g_state.saved;
            const auto& frustum = a_camera.GetRuntimeData2().viewFrustum;
            const auto& box = a_frame.box;
            const auto placed = PreviewMath::Place(eye, axes, { frustum.fLeft, frustum.fRight, frustum.fTop, frustum.fBottom }, V(saved.pos),
                saved.scale, MathRect(box), MathRect(a_frame.visible));
            // an empty box would make the base scale 0 or negative (Settings refuses one too, second review X3)
            if (!placed) {
                if (!std::exchange(g_state.waitLogged, true)) {
                    const auto& visible = a_frame.visible;
                    logger::warn("Preview: no place - depth {:.2f}, game scale {:.3f}, visible {:.0f} x {:.0f}, box {:.0f} x {:.0f}, frustum l{} r{} t{} b{}",
                        PreviewMath::DepthOf(V(saved.pos), eye, axes), saved.scale, visible.right - visible.left, visible.bottom - visible.top,
                        box.Width(), box.Height(), frustum.fLeft, frustum.fRight, frustum.fTop, frustum.fBottom);
                }
                return false;
            }
            const float scale = placed->scale;
            const auto move = placed->place - V(a_manager.itemPos);
            // a model already up was built at the base scale of its time (the game's before our first place): it takes
            // the new one too, or it keeps the game's size (review R2: 0.45 / (263/800), about 37% too big). Its cache
            // entry knows the base scale it was built at (a model kept from an earlier menu was rescaled then, while
            // itemScaleCopy is the game's again after that menu's restore); a model without one was built at
            // itemScaleCopy. The entry follows, so the fit neither re-measures it nor takes the change for the game's
            const float current = a_manager.itemScaleCopy;
            for (const auto& loaded : a_manager.GetRuntimeData().loadedModels) {
                if (!loaded.spModel) continue;
                auto& model = *loaded.spModel;
                MoveInParent(model, move);
                auto* entry = FindEntry(&model, loaded.itemBase);
                // an entry of an earlier menu whose model is not at the scale we left it at: a model built anew at the same
                // address (same spell), so its builtAt is not its own; it was built at itemScaleCopy (second review X2)
                if (entry && PreviewMath::Rebuilt(entry->generation != g_state.generation, model.local.scale, entry->setScale)) {
                    logger::info("Preview: {} ({}) is not at the scale an earlier menu left it at ({:.3f}, now {:.3f}) - taken as a new model",
                        entry->name, entry->spell, entry->setScale, model.local.scale);
                    std::erase_if(g_state.models, [&](const Model& a_entry) { return a_entry.key == &model; });
                    entry = nullptr;
                }
                const float before = entry ? entry->builtAt : current;
                const float ratio = before > 0.0f && std::isfinite(before) ? scale / before : 1.0f;
                if (ratio != 1.0f) {
                    const float was = model.local.scale;
                    model.local.scale = was * ratio;
                    if (entry) ScaleEntry(*entry, ratio);
                    logger::info("Preview: place scales {} ({}) by {:.3f} (base scale {:.3f} / {:.3f} it was built at): local.scale {:.3f} -> "
                                 "{:.3f}{}",
                        model.name.c_str(), SpellName(loaded.itemBase), ratio, scale, before, was, model.local.scale,
                        entry ? ", its cache entry too" : "");
                }
                if (entry) entry->builtAt = scale;
                // our rescale, not a model built anew (LooksRebuilt): back from a tab without the box it read as one (game run
                // 2026-10-25 23:43:50, "scale 0.064, was 0.120")
                for (auto& watched : g_state.clip.watched) {
                    if (watched.key == &model) watched.seenScale = model.local.scale;
                }
                RE::NiUpdateData update{};
                model.Update(update);
            }
            a_manager.itemPos = a_manager.itemPosCopy = Ni(placed->place);
            a_manager.itemScale = a_manager.itemScaleCopy = scale;
            const auto& room = placed->room;
            g_state.room = room;
            for (auto& entry : g_state.models) entry.solved.valid = false;  // a new room's line of sight
            g_state.placedFor = a_frame;
            logger::info("Preview: placed at the box (stage centre {:.1f},{:.1f} = {:.3f},{:.3f} of the visible rect): itemPos {:.2f},{:.2f},{:.2f} "
                         "(game {:.2f},{:.2f},{:.2f}), scale {:.3f} (game {:.3f}), room {:.2f} x {:.2f} at depth {:.2f}",
                box.CenterX(), box.CenterY(), placed->fromLeft, placed->fromTop, placed->place.x, placed->place.y, placed->place.z, saved.pos.x,
                saved.pos.y, saved.pos.z, scale, saved.scale, room.wide, room.high, room.depth);
            if (!g_state.probed) ProbeAxes(a_camera, axes, room.depth, a_frame);
            return true;
        }

        void FitAll(RE::Inventory3DManager& a_manager, RE::NiCamera& a_camera, const FrameInfo& a_frame)
        {
            const auto axes = AxesOf(a_camera);
            const auto eye = V(a_camera.world.translate);
            const auto& room = *g_state.room;
            for (const auto& loaded : a_manager.GetRuntimeData().loadedModels) {
                auto* model = loaded.spModel.get();
                if (!model || model->GetAppCulled()) continue;
                auto& entry = Entry(*model, loaded.itemBase, a_manager.itemScaleCopy);
                Notice(entry, *model);
                const bool settled = StepMeasure(entry, *model, axes, eye);
                Apply(entry, *model, room, axes, eye);
                if (!settled) continue;
                // the first kMeasureLinesMost measures of a model per menu are logged, later ones only counted (a model the
                // game keeps turning is measured again on every turn)
                ++g_state.measures;
                const bool log = ++entry.measureLines <= kMeasureLinesMost;
                if (log) {
                    LogMeasure(entry, room);
                    if (entry.measureLines == kMeasureLinesMost) {
                        logger::info("Preview: {} ({}) measured {} times this menu - later measures are counted only (summary at close)",
                            entry.name, entry.spell, kMeasureLinesMost);
                    }
                } else {
                    ++g_state.hiddenLines;
                }
                if (const char* verdict = TestFit(entry, *model, axes, a_camera, a_frame, log)) (log ? g_state.logged : g_state.hidden).Add(verdict);
            }
        }

        // Null when every model up was placed and fitted this frame, else why not
        const char* PlaceAndFit(RE::Inventory3DManager& a_manager, RE::NiCamera& a_camera, const FrameInfo& a_frame, std::uint64_t a_generation)
        {
            if (!EnsureSaved(a_manager, a_camera, a_generation)) return "the game's preview place is not set yet";
            // a place skipped while zooming stays due: placedFor still differs when the preview is at rest again
            if (!AtRest(a_manager)) return "the preview is zooming";
            if ((!g_state.placedFor || !SameWindow(*g_state.placedFor, a_frame)) && !Place(a_manager, a_camera, a_frame)) {
                return "no place (see the warning above)";
            }
            if (!g_state.room) return "no place yet";
            FitAll(a_manager, a_camera, a_frame);
            return nullptr;
        }

        // Once per model: was it fitted before the over-pass drew it the first time? (The vanilla pass under the movie
        // can still show one unfitted frame; accepted)
        // Marks are per menu (a_generation): a model built anew at an old address, or kept from an earlier menu, is
        // logged again (review R5)
        void LogFirstRenders(RE::Inventory3DManager& a_manager, const char* a_skipped, std::uint64_t a_generation, bool a_overPass)
        {
            const char* pass = a_overPass ? "first over-pass render" : "first PostDisplay (no over-pass: the game's own pass draws it)";
            for (const auto& loaded : a_manager.GetRuntimeData().loadedModels) {
                const auto* model = loaded.spModel.get();
                if (!model || std::ranges::any_of(g_state.firstRenders, [&](const FirstRender& a_mark) {
                        return a_mark.key == model && a_mark.generation == a_generation;
                    })) {
                    continue;
                }
                std::erase_if(g_state.firstRenders, [&](const FirstRender& a_mark) { return a_mark.key == model; });
                g_state.firstRenders.push_back({ model, a_generation });
                const auto entry = std::ranges::find_if(g_state.models, [&](const Model& a_entry) { return a_entry.key == model; });
                const bool fresh = entry != g_state.models.end() && entry->generation == a_generation;
                const char* how = a_skipped                                      ? a_skipped :
                                  !fresh                                         ? "not fitted (culled)" :
                                  entry->phase == Phase::kFitted                 ? "fitted to the box" :
                                  entry->hasFit                                  ? "fitted by its last reading (a new measure is settling)" :
                                  entry->phase == Phase::kUnsettled              ? "centred at its base scale (its bound never held still)" :
                                                                                   "centred at its base scale (its bound is settling)";
                logger::info("Preview: {} of {} ({}) at PostDisplay {}: {}", pass, model->name.c_str(), SpellName(loaded.itemBase),
                    g_state.generation == a_generation ? g_state.clip.frame : 0, how);
            }
        }

        std::optional<FrameInfo> TakeFrame(std::uint64_t a_generation)
        {
            std::scoped_lock lock(g_slot.lock);
            if (g_slot.generation != a_generation) return std::nullopt;
            return g_slot.frame;
        }

        // Place and fit for one PostDisplay (build 4 P1), with its own conditions: not on VR, not with IR, bMovePreview.
        // Null when every model up was placed and fitted this frame, else why not (the clip is off on such a frame: a
        // model away from the box, zooming or not placed yet, would be cut away, review round 1 #2)
        const char* Fit(RE::Inventory3DManager& a_manager, bool a_overPass, std::uint64_t a_generation, const std::optional<FrameInfo>& a_frame)
        {
            if (REL::Module::IsVR() || g_irActive || !Settings::Get().movePreview) return "place/fit is off";
            Forget(a_manager);
            // the frames before this menu's reset or its first frame info still render: their first renders say so (R3)
            if (g_state.generation != a_generation) {
                const char* why = "not fitted: no frame info yet (this menu's reset has not run)";
                LogFirstRenders(a_manager, why, a_generation, a_overPass);
                return why;
            }
            auto* camera = Camera();
            if (!camera || !a_frame) {
                const char* why = !camera ? "not fitted: no 3D camera" : "not fitted: no frame info yet";
                LogFirstRenders(a_manager, why, a_generation, a_overPass);
                return why;
            }
            std::size_t loaded = 0;
            for (const auto& model : a_manager.GetRuntimeData().loadedModels) loaded += model.spModel ? 1 : 0;
            g_state.lastLoaded = loaded;
            const char* why = PlaceAndFit(a_manager, *camera, *a_frame, a_generation);
            LogFirstRenders(a_manager, why, a_generation, a_overPass);
            return why;
        }

        // ---------------------------------------------------------------------
        // Q3 C: the selection's model hidden from the game's own pass (spec v5)
        // ---------------------------------------------------------------------
        // The game draws the preview before the movie too, outside the box (the over-pass after the movie is ours).
        // SetAppCulled is one bit with no owner: the game hides an earlier selection's model with it as well. So (the
        // rule of Inventory Reforged's KeepPreview, Scene.cpp:574-590, and its DrawUnder, :405-465, which keeps and
        // puts back cull flags around an extra Render; method only) we only ever touch a model whose itemBase is the
        // current selection's (the list's, or the tree card's spell P3 asked for): after our over-pass Render() it is
        // hidden and remembered; before the next over-pass it is given back only when it is still in loadedModels,
        // still of the form it was hidden as, still the selection's and still hidden. A model of an earlier selection
        // stays hidden: assumed, not verified here, that the game hides an earlier selection's model itself (Inventory
        // Reforged's KeepPreview rests on the same assumption); any other model is never hidden or given back by us. A
        // remembered model is an address only: it is found again through loadedModels, never read through.
        // "The selection" is one form (Shown::Form): the tree card's spell while P3 asked for its 3D, else the list's
        // item, so the list's model is never shown again next to the P3 one (review round 2, #1).
        // Every model hidden this menu and still up is remembered too (everHidden): one that is selected again and is
        // still hidden (a model the game kept and did not show again, A -> B -> A) is given back from there - unless
        // the game already shows another model of that form (it replaced ours: showing the old copy would double it,
        // review round 2 #5).
        // When the preview box stops being shown (nothing chosen; before build 16 also Active Effects) the game's own place comes back and the selection's model
        // we hid is given back (spec v7 P7.2, replacing build 5's keep-the-hide: with the game's place back, its pass draws
        // the model as without us); an earlier selection's model stays hidden until the close, as at any selection change.
        // The menu's close and the next menu's reset give back every model we hid that is still up and hidden,
        // whatever the selection: no over-pass follows then, so our hide must not outlive the menu (review round 2 #4).
        // The game's pass runs before our hook in the same frame, so a model can still be drawn outside the box in the
        // frame it becomes the selection's: the promise is "nothing outside the box after that frame" (counted at every
        // such change, review round 1 #8).

        bool Contains(const std::vector<Hidden>& a_list, const RE::NiAVObject* a_key, RE::FormID a_base)
        {
            return std::ranges::any_of(a_list, [&](const Hidden& a_hidden) { return a_hidden.Key() == a_key && a_hidden.base == a_base; });
        }

        // Drops remembered models no longer up, and with them our references (the game unloaded them)
        void PruneEverHidden(RE::Inventory3DManager& a_manager)
        {
            const auto& loadedModels = a_manager.GetRuntimeData().loadedModels;
            const auto gone = [&](const Hidden& a_hidden) {
                return std::ranges::none_of(loadedModels, [&](const auto& a_loaded) {
                    return a_loaded.spModel.get() == a_hidden.Key() && a_loaded.itemBase && a_loaded.itemBase->GetFormID() == a_hidden.base;
                });
            };
            std::erase_if(g_state.clip.everHidden, gone);
            std::erase_if(g_state.clip.replacedModels, gone);
        }

        // Gives back every model up that we hid (this over-pass's list or earlier this menu) and that is the selection's
        // now (a_shown) and still hidden, unless the game shows another model of its form already (a form that has a
        // model up and not hidden before we give anything back: the game's copy); forgets the over-pass list. Returns how
        // many it showed again
        int GiveBack(RE::Inventory3DManager& a_manager, const Shown& a_shown)
        {
            auto& clip = g_state.clip;
            PruneEverHidden(a_manager);
            if (clip.hidden.empty() && clip.everHidden.empty()) return 0;
            int back = 0;
            const auto& loadedModels = a_manager.GetRuntimeData().loadedModels;
            std::vector<RE::FormID> shownByGame;
            for (const auto& loaded : loadedModels) {
                if (loaded.spModel && loaded.itemBase && !loaded.spModel->GetAppCulled()) shownByGame.push_back(loaded.itemBase->GetFormID());
            }
            for (const auto& loaded : loadedModels) {
                auto* model = loaded.spModel.get();
                if (!model || !loaded.itemBase || !model->GetAppCulled()) continue;
                const auto base = loaded.itemBase->GetFormID();
                if (Contains(clip.replacedModels, model, base)) continue;  // replaced by the game: never shown again by us
                const bool justHidden = Contains(clip.hidden, model, base);
                if (!justHidden && !Contains(clip.everHidden, model, base)) continue;  // never hidden by us: not ours to show
                if (a_shown.Has(loaded)) {
                    if (std::ranges::find(shownByGame, base) != shownByGame.end()) {
                        ++clip.replaced;
                        clip.replacedModels.push_back({ RE::NiPointer<RE::NiAVObject>(model), base });
                        std::erase_if(clip.everHidden, [&](const Hidden& a_hidden) { return a_hidden.Key() == model; });
                        if (!std::exchange(clip.replacedLogged, true)) {
                            logger::info("Preview: Q3 {} ({}) at PostDisplay {} not given back, for the rest of the menu - the game shows another "
                                         "model of that form already (ours was replaced; later ones counted only)",
                                model->name.c_str(), SpellName(loaded.itemBase), clip.frame);
                        }
                        continue;
                    }
                    model->SetAppCulled(false);
                    ++back;
                    if (!justHidden) {
                        ++clip.again;
                        if (clip.againLines++ < kSightLinesMost) {
                            logger::info("Preview: Q3 gave back {} ({}) at PostDisplay {}: hidden by us earlier this menu, the selection's again and "
                                         "still hidden (the game did not show it again){}",
                                model->name.c_str(), SpellName(loaded.itemBase), clip.frame,
                                clip.againLines == kSightLinesMost ? " - later ones counted only" : "");
                        }
                    }
                } else if (justHidden && std::ranges::find(clip.leftLogged, model) == clip.leftLogged.end()) {
                    clip.leftLogged.push_back(model);
                    ++clip.left;
                    logger::info("Preview: Q3 {} ({}) is no longer the selection's - left hidden (assumed: the game hides an earlier selection's "
                                 "model itself, as Inventory Reforged's KeepPreview assumes; given back if it is selected again still hidden, "
                                 "and at close)",
                        model->name.c_str(), SpellName(loaded.itemBase));
                }
            }
            clip.hidden.clear();
            return back;
        }

        // The menu's close or the next menu's reset: every model we hid that is still up and hidden comes back, whatever
        // the selection (no over-pass follows, so our hide must not outlive the menu, review round 2 #4); one line with
        // what was up, what we remembered and what was still hidden
        void GiveBackAll(RE::Inventory3DManager& a_manager, const char* a_why)
        {
            auto& clip = g_state.clip;
            PruneEverHidden(a_manager);
            std::size_t loaded = 0, culled = 0;
            for (const auto& entry : a_manager.GetRuntimeData().loadedModels) {
                auto* model = entry.spModel.get();
                if (!model) continue;
                ++loaded;
                const auto base = entry.itemBase ? entry.itemBase->GetFormID() : 0;
                if (!model->GetAppCulled() || Contains(clip.replacedModels, model, base) ||
                    (!Contains(clip.hidden, model, base) && !Contains(clip.everHidden, model, base))) {
                    continue;
                }
                model->SetAppCulled(false);
                ++culled;
            }
            const auto remembered = clip.hidden.size() + clip.everHidden.size() + clip.replacedModels.size();
            if (remembered > 0 || culled > 0) {
                logger::info("Preview: Q3 {} - loadedModels {}, remembered hidden models {} (this over-pass {}, this menu {}, replaced by the game "
                             "{}: left hidden, giving one back would show it next to the game's new copy), still hidden and given back {}",
                    a_why, loaded, remembered, clip.hidden.size(), clip.everHidden.size(), clip.replacedModels.size(), culled);
            }
            // our references go here, on the main thread
            clip.hidden.clear();
            clip.everHidden.clear();
            clip.replacedModels.clear();
        }

        // A menu state without the box (nothing chosen; the Powers tab with bHidePowers3D=1; Active Effects before build 16,
        // which shows its preview box since): every model up that is shown is hidden, held by a strong reference, each
        // frame (the game loads the selected item's art meanwhile). Given back when the box shows again or the
        // menu closes, only those still up and still hidden
        void HideForTab(RE::Inventory3DManager& a_manager)
        {
            auto& clip = g_state.clip;
            int now = 0;
            for (const auto& loaded : a_manager.GetRuntimeData().loadedModels) {
                auto* model = loaded.spModel.get();
                if (!model || model->GetAppCulled()) continue;
                model->SetAppCulled(true);
                if (std::ranges::none_of(clip.tabHidden, [&](const auto& a_held) { return a_held.get() == model; })) clip.tabHidden.emplace_back(model);
                ++now;
            }
            if (now > 0 && !std::exchange(clip.tabHideLogged, true)) {
                logger::info("Preview: P7 a tab without the box at PostDisplay {}: {} model(s) hidden so no 3D shows over the game view (later frames "
                             "counted only)",
                    clip.frame, now);
            }
            clip.tabHides += now;
        }

        // Back on a tab with the box nothing is shown here: every model still up and hidden goes to everHidden, so Q3's GiveBack
        // (after Watch has read this frame, with its check for a copy the game shows already) gives the selection's back and
        // keeps an earlier selection's hidden until it is selected again or the menu closes (GPT review: giving every model
        // back showed an earlier effect; reviewer: giving the selection's back here came before Watch and skipped the copy
        // check). One without an itemBase stays in tabHidden. At the close (a_all) every one still hidden is given back
        void GiveBackTab(RE::Inventory3DManager& a_manager, const char* a_why, bool a_all)
        {
            auto& clip = g_state.clip;
            if (clip.tabHidden.empty()) return;
            int back = 0, handed = 0;
            std::vector<RE::NiPointer<RE::NiAVObject>> stay;
            const auto total = clip.tabHidden.size();
            for (const auto& held : clip.tabHidden) {
                const auto& loadedModels = a_manager.GetRuntimeData().loadedModels;
                const auto loaded = std::ranges::find_if(loadedModels, [&](const auto& a_loaded) { return a_loaded.spModel.get() == held.get(); });
                if (loaded == loadedModels.end() || !held->GetAppCulled()) continue;
                if (a_all) {
                    held->SetAppCulled(false);
                    ++back;
                } else if (loaded->itemBase) {
                    const Hidden hidden{ held, loaded->itemBase->GetFormID() };
                    if (!Contains(clip.everHidden, held.get(), hidden.base)) clip.everHidden.push_back(hidden);
                    ++handed;
                } else {
                    stay.push_back(held);
                }
            }
            logger::info("Preview: P7 tab hide ends ({}) at PostDisplay {}: of {} model(s), {} given back, {} handed to Q3 (the selection's comes back "
                         "by its rules), {} kept for the close",
                a_why, clip.frame, total, back, handed, stay.size());
            clip.tabHidden = std::move(stay);  // our references go here, on the main thread
        }

        // A frame Render() is about to draw two or more models of one form (a copy the game replaced, shown next to its
        // new one): counted for the summary, the first logged
        void CountDoubles(RE::Inventory3DManager& a_manager)
        {
            auto& clip = g_state.clip;
            std::vector<RE::FormID> drawn;
            for (const auto& loaded : a_manager.GetRuntimeData().loadedModels) {
                if (!loaded.spModel || !loaded.itemBase || loaded.spModel->GetAppCulled()) continue;
                const auto base = loaded.itemBase->GetFormID();
                if (std::ranges::find(drawn, base) == drawn.end()) {
                    drawn.push_back(base);
                    continue;
                }
                ++clip.doubles;
                if (!std::exchange(clip.doublesLogged, true)) {
                    logger::info("Preview: Q3 two or more models of {} are drawn at PostDisplay {} (later such frames counted only)",
                        SpellName(loaded.itemBase), clip.frame);
                }
                return;
            }
        }

        // After the over-pass Render(): the selection's models up and not hidden are hidden from the game's next own pass
        void HideSelection(RE::Inventory3DManager& a_manager, const Shown& a_shown)
        {
            auto& clip = g_state.clip;
            for (const auto& loaded : a_manager.GetRuntimeData().loadedModels) {
                auto* model = loaded.spModel.get();
                if (!model || !a_shown.Has(loaded) || model->GetAppCulled()) continue;
                model->SetAppCulled(true);
                const Hidden hidden{ RE::NiPointer<RE::NiAVObject>(model), loaded.itemBase->GetFormID() };
                clip.hidden.push_back(hidden);
                if (!Contains(clip.everHidden, model, hidden.base)) clip.everHidden.push_back(hidden);
                ++clip.hides;
            }
        }

        // The first time controller under a model (the model first, then depth first; kControllerSearchMost objects at
        // most): its lastTime moves while the model animates. None when nothing under it has one
        std::optional<float> ControllerTime(RE::NiAVObject& a_model)
        {
            std::vector<RE::NiAVObject*> stack{ &a_model };
            for (int visited = 0; !stack.empty() && visited < kControllerSearchMost; ++visited) {
                auto* object = stack.back();
                stack.pop_back();
                if (const auto* controller = object->GetControllers()) return controller->lastTime;
                if (auto* node = object->AsNode()) {
                    for (const auto& child : node->GetChildren()) {
                        if (child) stack.push_back(child.get());
                    }
                }
            }
            return std::nullopt;
        }

        Watched* FindWatched(const RE::NiAVObject* a_key, RE::FormID a_base)
        {
            const auto known = std::ranges::find_if(g_state.clip.watched, [&](const Watched& a_watched) { return a_watched.key == a_key && a_watched.base == a_base; });
            return known != g_state.clip.watched.end() ? &*known : nullptr;
        }

        // One animation sample of a model we hid (still hidden), a second apart, and its verdict after kAnimSamples:
        // does anything that moves with animation move while the game's own pass skips it? (UNVERIFIED that culling
        // leaves the animation running: the spec's open question; the user's video is the final check.) A sample counts
        // only while our fit has settled and keeps its scale (PreviewMath::MotionSamples: the series starts over
        // otherwise), and the bound is compared at the model's own scale, so our grow is never taken for animation
        // (review round 1, #4/#7). A model without this menu's fit entry is not fitted by us: its scale is the game's
        void SampleAnimation(Watched& a_watched, RE::NiAVObject& a_model, RE::Inventory3DManager& a_manager)
        {
            const auto now = std::chrono::steady_clock::now();
            if (a_watched.animJudged || now < a_watched.nextSample) return;
            a_watched.nextSample = now + kAnimEvery;
            const auto entry = std::ranges::find_if(g_state.models, [&](const Model& a_entry) { return a_entry.key == &a_model; });
            const bool ours = entry != g_state.models.end() && entry->generation == g_state.generation;
            const bool steady = !ours || entry->phase == Phase::kFitted || entry->phase == Phase::kUnsettled;
            const float setScale = ours ? entry->setScale : a_model.local.scale;
            const auto time = ControllerTime(a_model);
            const float radius = a_model.worldBound.radius, worldScale = a_model.world.scale;
            auto& motion = a_watched.motion;
            const int restarts = motion.restarts;
            const bool counted = motion.Add(time, radius, worldScale, steady, setScale);
            if (motion.restarts != restarts && a_watched.restartLines++ < kRestartLinesMost) {
                logger::info("Preview: Q3 hidden {} samples start over at PostDisplay {}: our fit {} (its own rescale is not animation)", a_watched.name,
                    g_state.clip.frame, steady ? "changed its scale" : "is measuring it again");
            }
            if (!counted) return;
            const bool loading = a_manager.GetRuntimeData().loadTask.get() != nullptr;
            logger::info("Preview: Q3 hidden {} sample {}/{} at PostDisplay {}: controller time {}, bound radius {:.3f} at world scale {:.3f} (size "
                         "{:.3f}), load task {}",
                a_watched.name, motion.samples, kAnimSamples, g_state.clip.frame, time ? std::format("{:.3f}", *time) : "(no controller)", radius,
                worldScale, motion.lastSize, loading ? "pending" : "none");
            if (motion.samples < kAnimSamples) return;
            a_watched.animJudged = true;
            auto& clip = g_state.clip;
            const std::string what = std::format("{} {}: over {} samples a second apart while hidden from the game's own pass and our fit held its "
                                                 "scale",
                a_watched.name, a_watched.spell, kAnimSamples);
            switch (motion.Verdict()) {
            case PreviewMath::Motion::kMoved:
                ++clip.animMoved;
                SelfTest(std::format("Q3 anim PASS {}, {} moved ({})", what, motion.TimeMoved() ? "its controller time" : "its bound size at its own scale",
                    motion.TimeMoved() ? std::format("{:.3f} -> {:.3f}", *motion.firstTime, *motion.lastTime) :
                                         std::format("{:.3f} -> {:.3f}", motion.firstSize, motion.lastSize)));
                break;
            case PreviewMath::Motion::kStill:
                ++clip.animStill;
                // Apply's own Update(NiUpdateData{}) passes time 0 when the fit changes the model: not ruled out as the cause
                SelfTest(std::format("Q3 anim WARN {}, its controller time stayed {:.3f} - the hide may freeze its animation (bHideGamePass=0 is "
                                     "the way back; the fit's Update with time 0 is not ruled out)",
                    what, *motion.firstTime));
                break;
            case PreviewMath::Motion::kNone:
                ++clip.animNone;
                logger::info("Preview: Q3 anim not judged - {}, no time controller under it and its bound size stayed {:.3f} (nothing here shows "
                             "animation)",
                    what, motion.firstSize);
                break;
            }
        }

        // A watched model whose scale changed since the last hook while the preview stood still, and not to what our
        // fit left it at: a model built anew at the same address (the game's own scale again, like PreviewMath::Rebuilt
        // across menus). Its judgements start over and it is no longer one we hid (review round 2, #6)
        bool LooksRebuilt(const Watched& a_watched, const RE::NiAVObject& a_model, RE::Inventory3DManager& a_manager)
        {
            if (!(a_watched.seenScale > 0.0f) || a_model.local.scale == a_watched.seenScale || !AtRest(a_manager)) return false;
            const auto entry = std::ranges::find_if(g_state.models, [&](const Model& a_entry) { return a_entry.key == &a_model; });
            const bool ours = entry != g_state.models.end() && entry->generation == g_state.generation && entry->setScale == a_model.local.scale;
            return !ours;
        }

        // At the hook, before anything is given back (the game's own pass of this frame has run). A model that became the
        // selection's since the last hook (first seen, selected again, back from a tab without the box, or built anew):
        // was that pass's frame of it hidden? Counted at every such change (review round 1, #8), its line logged for the
        // first kSightLinesMost. A model we hid after the last over-pass: still hidden now (the game's pass drew nothing
        // of it: Q3 hide PASS) or given back by something else (FAIL)? While it stays hidden: its animation samples
        void Watch(RE::Inventory3DManager& a_manager, const Shown& a_shown)
        {
            auto& clip = g_state.clip;
            const auto& loadedModels = a_manager.GetRuntimeData().loadedModels;
            // a watched model that left is forgotten: the next model at its address is a new one
            std::erase_if(clip.watched, [&](const Watched& a_watched) {
                return std::ranges::none_of(loadedModels, [&](const auto& a_loaded) {
                    return a_loaded.spModel.get() == a_watched.key && a_loaded.itemBase && a_loaded.itemBase->GetFormID() == a_watched.base;
                });
            });
            const bool loading = a_manager.GetRuntimeData().loadTask.get() != nullptr;
            std::vector<const RE::NiAVObject*> shownNow;
            for (const auto& loaded : loadedModels) {
                auto* model = loaded.spModel.get();
                if (!model || !a_shown.Has(loaded)) continue;
                shownNow.push_back(model);
                const auto base = loaded.itemBase->GetFormID();
                auto* watched = FindWatched(model, base);
                if (watched && LooksRebuilt(*watched, *model, a_manager)) {
                    ++clip.rebuilt;
                    if (clip.rebuiltLines++ < kSightLinesMost) {
                        logger::info("Preview: Q3 {} ({}) at PostDisplay {} looks built anew at the same address (scale {:.3f}, was {:.3f}, not our "
                                     "fit's) - its hide and animation judgements start over",
                            watched->name, watched->spell, clip.frame, model->local.scale, watched->seenScale);
                    }
                    std::erase_if(clip.hidden, [&](const Hidden& a_hidden) { return a_hidden.Key() == model; });
                    std::erase_if(clip.everHidden, [&](const Hidden& a_hidden) { return a_hidden.Key() == model; });
                    std::erase_if(clip.replacedModels, [&](const Hidden& a_hidden) { return a_hidden.Key() == model; });
                    std::erase(clip.shownLast, model);
                    std::erase_if(clip.watched, [&](const Watched& a_watched) { return a_watched.key == model; });
                    watched = nullptr;
                }
                if (!watched) {
                    Watched fresh;
                    fresh.key = model;
                    fresh.base = base;
                    fresh.name = model->name.c_str();
                    fresh.spell = SpellName(loaded.itemBase);
                    watched = &clip.watched.emplace_back(std::move(fresh));
                }
                watched->seenScale = model->local.scale;
                if (std::ranges::find(clip.shownLast, model) != clip.shownLast.end()) continue;  // the selection's at the last hook too
                const bool culled = model->GetAppCulled();
                ++clip.sights;
                if (!culled) ++clip.flashes;
                if (clip.sightLines++ < kSightLinesMost) {
                    logger::info("Preview: Q3 {} ({}) became the selection's model at PostDisplay {}: the game's own pass {} (load task {}){}",
                        model->name.c_str(), SpellName(loaded.itemBase), clip.frame,
                        culled ? "drew nothing of it (hidden already)" :
                                 "drew this frame of it without the clip (accepted: the promise starts after the frame it becomes the selection's)",
                        loading ? "pending" : "none", clip.sightLines == kSightLinesMost ? " - later ones counted only (summary at close)" : "");
                }
            }
            clip.shownLast = std::move(shownNow);
            for (const auto& hidden : clip.hidden) {
                for (const auto& loaded : loadedModels) {
                    auto* model = loaded.spModel.get();
                    if (!model || model != hidden.Key() || !loaded.itemBase || loaded.itemBase->GetFormID() != hidden.base) continue;
                    const bool held = model->GetAppCulled();
                    (held ? clip.held : clip.lost) += 1;
                    // 2026-10-31 (the "Q3 lost" of the last game run): what the model and the manager were when it came back
                    if (!held && clip.lostLines++ < kSightLinesMost) {
                        logger::info("Preview: Q3 lost {} ({}) at PostDisplay {}: hidden after the last over-pass, shown again before this hook - {} "
                                     "frame(s) after the last selection change, scale {:.3f}, itemPos {:.2f},{:.2f},{:.2f} itemScale {:.3f}, load task {}{}",
                            model->name.c_str(), SpellName(loaded.itemBase), clip.frame, clip.frame - clip.selectionFrame, model->local.scale,
                            a_manager.itemPos.x, a_manager.itemPos.y, a_manager.itemPos.z, a_manager.itemScale, loading ? "pending" : "none",
                            clip.lostLines == kSightLinesMost ? " - later ones counted only (summary at close)" : "");
                    }
                    auto* watched = FindWatched(model, hidden.base);
                    if (watched && !std::exchange(watched->hideJudged, true)) {
                        (held ? clip.hidePass : clip.hideFail) += 1;
                        SelfTest(std::format("Q3 hide {} {} {}: hidden after the over-pass of the last PostDisplay, at PostDisplay {} {}",
                            held ? "PASS" : "FAIL", watched->name, watched->spell, clip.frame,
                            held ? "still hidden (the game's own pass drew nothing of it)" :
                                   "NOT hidden (something gave it back before the game's own pass, which may have drawn it outside the box)"));
                    }
                    if (held && watched) SampleAnimation(*watched, *model, a_manager);
                    break;
                }
            }
        }

        // ---------------------------------------------------------------------
        // P7 preview fixes (spec v7)
        // ---------------------------------------------------------------------

        // P7.1 The spell art's backdrop disc (and, with bHideGlow, its additive halo shapes) hidden: every shape under every
        // loaded model, every frame (cheap: a spell's art has a few dozen objects), so a model that attaches a part late is
        // covered too and a hidden shape stays out of the fit measure (Gather and the P1 test skip hidden parts; the disc
        // made the fit small, heal grow 0.55). Logged per model once a menu; a shape found shown again after we hid it is
        // counted. Not given back at close: the art is the menu's own copy (Inventory3DManager's clone of the NIF)
        void HideArt(RE::Inventory3DManager& a_manager)
        {
            const auto& settings = Settings::Get();
            if (!settings.hideBackdrop && !settings.hideGlow) return;
            auto& clip = g_state.clip;
            std::erase_if(clip.artLogged, [&](const Hidden& a_logged) {
                return std::ranges::none_of(a_manager.GetRuntimeData().loadedModels, [&](const auto& a_loaded) { return a_loaded.spModel.get() == a_logged.Key(); });
            });
            for (const auto& loaded : a_manager.GetRuntimeData().loadedModels) {
                auto* model = loaded.spModel.get();
                if (!model) continue;
                int backdrops = 0, glows = 0, already = 0;
                std::string names;
                // from the root's children: the root is the model itself (the Q3 hide's object), never one of its shapes
                // (review build 7 #1: a root shape named Cylinder* would have hidden the whole spell)
                std::vector<RE::NiAVObject*> stack;
                if (auto* root = model->AsNode()) {
                    for (const auto& child : root->GetChildren()) {
                        if (child) stack.push_back(child.get());
                    }
                }
                for (int visited = 0; !stack.empty() && visited < kArtSearchMost; ++visited) {
                    auto* object = stack.back();
                    stack.pop_back();
                    if (auto* geometry = object->AsGeometry()) {
                        const auto* alpha = geometry->GetGeometryRuntimeData().alphaProperty.get();
                        const auto kind = PreviewMath::ClassifyArtShape(object->name.c_str(), object->AsTriShape() != nullptr,
                            object->AsParticlesGeom() != nullptr, alpha != nullptr, alpha ? alpha->alphaFlags : std::uint16_t{ 0 },
                            settings.hideBackdrop, settings.hideGlow);
                        if (kind != PreviewMath::ArtShape::kKeep && object->GetAppCulled()) {
                            ++already;  // hidden before this frame: by us earlier, or carried over from an earlier menu
                        } else if (kind != PreviewMath::ArtShape::kKeep) {
                            object->SetAppCulled(true);
                            (kind == PreviewMath::ArtShape::kBackdrop ? backdrops : glows) += 1;
                            names += std::format("{}{} ({}, alpha flags 0x{:04X})", names.empty() ? "" : ", ", object->name.c_str(),
                                kind == PreviewMath::ArtShape::kBackdrop ? "backdrop" : "glow", alpha ? alpha->alphaFlags : 0);
                        }
                    }
                    if (auto* node = object->AsNode()) {
                        for (const auto& child : node->GetChildren()) {
                            if (child) stack.push_back(child.get());
                        }
                    }
                }
                if (backdrops == 0 && glows == 0 && already == 0) continue;
                clip.backdrops += backdrops;
                clip.glows += glows;
                const auto base = loaded.itemBase ? loaded.itemBase->GetFormID() : 0;
                if (Contains(clip.artLogged, model, base)) {
                    if (backdrops + glows > 0) ++clip.artShownAgain;  // hidden by us before in this menu: something showed them again
                    continue;
                }
                clip.artLogged.push_back({ RE::NiPointer<RE::NiAVObject>(model), base });
                clip.artAlready += already;
                logger::info("Preview: P7 art of {} ({}) at PostDisplay {}: hid {} backdrop and {} glow shape(s){}{}", model->name.c_str(),
                    SpellName(loaded.itemBase), clip.frame, backdrops, glows, names.empty() ? "" : ": " + names,
                    already > 0 ? std::format("; {} matching shape(s) were hidden already (carried over from an earlier menu)", already) : "");
            }
        }

        // P7.2 Each loaded model's own transform when we first see it this menu, and the game's place it was built at
        // (models loaded after our place are built there), so the game's own place can be put back exactly; dropped when
        // the model leaves loadedModels. Recorded before anything of ours moves it that frame
        void RecordOriginals(RE::Inventory3DManager& a_manager)
        {
            auto& clip = g_state.clip;
            const auto& loadedModels = a_manager.GetRuntimeData().loadedModels;
            // a record without the game's place could not be undone (review build 7 #2): none until it is set, and one taken
            // without a scale is taken again
            std::erase_if(clip.originals, [&](const Original& a_original) {
                return !(a_original.builtScale > 0.0f) ||
                       std::ranges::none_of(loadedModels, [&](const auto& a_loaded) { return a_loaded.spModel.get() == a_original.model.get(); });
            });
            if (!(a_manager.itemScaleCopy > 0.0f) || !V(a_manager.itemPos).Finite()) return;
            for (const auto& loaded : loadedModels) {
                auto* model = loaded.spModel.get();
                if (!model) continue;
                const auto base = loaded.itemBase ? loaded.itemBase->GetFormID() : 0;
                if (std::ranges::any_of(clip.originals, [&](const Original& a_original) { return a_original.model.get() == model && a_original.base == base; })) {
                    continue;
                }
                std::erase_if(clip.originals, [&](const Original& a_original) { return a_original.model.get() == model; });
                clip.originals.push_back({ RE::NiPointer<RE::NiAVObject>(model), base, model->local.translate, model->local.scale, a_manager.itemPos,
                    a_manager.itemScaleCopy, model->world.translate, model->world.scale });
            }
        }

        // P7.2 The game's own place back (a tab without the box, the menu's close): itemPos/itemScale(Copy) as saved, every
        // loaded model at its recorded transform moved from the place it was built at to the game's (in its parent's
        // space, the place's rule) and rescaled by the game's base scale over the one it was built at; a model without a
        // record (not seen on a tab with the box) by Place's rule reversed. The fit cache forgets them (a re-place
        // measures them afresh, built at the game's scale), and place/fit waits for a preview tab (placedFor cleared)
        // a_closing: only the values are set, no Update (the menu is gone and its models with it; a model kept into the
        // next menu gets its world from them at the game's next update), and no self-test (it reads the world)
        void Unplace(RE::Inventory3DManager& a_manager, const char* a_why, bool a_closing)
        {
            if (!g_state.saved || !g_state.placedFor) return;
            const auto& saved = *g_state.saved;
            auto& clip = g_state.clip;
            int recorded = 0, reversed = 0;
            // P7.2 self-test (review build 7 #3): a recorded model's world place after Update is where the engine had it when
            // first seen, moved by the game's place minus the place it was built at (parents do not move), and its world
            // scale that one times the game's base scale over the one it was built at
            const bool test = !a_closing && Settings::Get().selfTest;
            float worst = 0;
            std::string worstWhat = "none";
            for (const auto& loaded : a_manager.GetRuntimeData().loadedModels) {
                auto* model = loaded.spModel.get();
                if (!model) continue;
                const auto base = loaded.itemBase ? loaded.itemBase->GetFormID() : 0;
                const auto original = std::ranges::find_if(clip.originals, [&](const Original& a_original) {
                    return a_original.model.get() == model && a_original.base == base;
                });
                if (original != clip.originals.end() && original->builtScale > 0.0f) {
                    const auto* parent = model->parent;
                    const auto move = V(saved.pos) - V(original->builtPos);
                    model->local.translate =
                        original->translate + Ni(parent ? PreviewMath::LocalMove(TurnOf(parent->world.rotate), parent->world.scale, move) : move);
                    model->local.scale = original->scale * saved.scaleCopy / original->builtScale;
                    ++recorded;
                    if (!a_closing) {
                        RE::NiUpdateData update{};
                        model->Update(update);
                    }
                    if (test) {
                        const auto wantPos = V(original->world) + move;
                        const float wantScale = original->worldScale * saved.scaleCopy / original->builtScale;
                        const float posMiss = (V(model->world.translate) - wantPos).Length() / std::max(1.0f, wantPos.Length());
                        const float scaleMiss = std::abs(model->world.scale - wantScale) / std::max(1.0f, std::abs(wantScale));
                        if (const float miss = std::max(posMiss, scaleMiss); miss >= worst) {
                            worst = miss;
                            worstWhat = std::format("{}: world {:.2f},{:.2f},{:.2f} scale {:.4f}, want {:.2f},{:.2f},{:.2f} scale {:.4f}", model->name.c_str(),
                                model->world.translate.x, model->world.translate.y, model->world.translate.z, model->world.scale, wantPos.x, wantPos.y,
                                wantPos.z, wantScale);
                        }
                    }
                } else {
                    MoveInParent(*model, V(saved.pos) - V(a_manager.itemPos));
                    if (a_manager.itemScaleCopy > 0.0f) model->local.scale *= saved.scaleCopy / a_manager.itemScaleCopy;
                    ++reversed;
                    if (!a_closing) {
                        RE::NiUpdateData update{};
                        model->Update(update);
                    }
                }
                std::erase_if(g_state.models, [&](const Model& a_entry) { return a_entry.key == model; });
                // not taken for a model built anew when the box shows again (LooksRebuilt): this scale is the game's, set by us
                for (auto& watched : clip.watched) {
                    if (watched.key == model) watched.seenScale = model->local.scale;
                }
            }
            a_manager.itemPos = saved.pos;
            a_manager.itemPosCopy = saved.posCopy;
            a_manager.itemScale = saved.scale;
            a_manager.itemScaleCopy = saved.scaleCopy;
            g_state.placedFor.reset();
            g_state.room.reset();
            ++clip.unplaced;
            logger::info("Preview: P7 the game's own place restored ({}) at PostDisplay {}: itemPos {:.2f},{:.2f},{:.2f}, scale {:.3f}; {} model(s) put "
                         "back by their recorded transform, {} by the place's move reversed{}",
                a_why, clip.frame, saved.pos.x, saved.pos.y, saved.pos.z, saved.scale, recorded, reversed,
                a_closing ? " (values only: the menu closed)" : "; place/fit waits for a tab with the box");
            if (!test) return;
            const bool managerBack = a_manager.itemPos == saved.pos && a_manager.itemScale == saved.scale && a_manager.itemScaleCopy == saved.scaleCopy;
            const bool pass = managerBack && worst <= kUnplaceSlack;
            (pass ? clip.unplacePass : clip.unplaceFail) += 1;
            SelfTest(std::format("P7.2 {} at PostDisplay {}: itemPos/itemScale {} the saved game place; {} recorded model(s), worst miss {:.2e} of the "
                                 "value (pass <= {:.0e}) ({}){}",
                pass ? "PASS" : "FAIL", clip.frame, managerBack ? "back at" : "NOT at", recorded, worst, kUnplaceSlack, worstWhat,
                reversed > 0 ? std::format("; {} model(s) without a record not tested", reversed) : ""));
        }

        // P7.3 Around our over-pass Render(): every loaded model that is not the selection's (Shown::Form: the tree card's
        // spell while P3 asked for it, else the list's item) is hidden and gets its flag back in the same call (Inventory
        // Reforged DrawUnder, Scene.cpp ~405-465, method only), so a previous spell's effect no longer stays in the box
        // when the new selection has no 3D (video 6, 36.0-36.5 s); with no model for the selection the box is empty.
        // Held by strong references for the call: a load Render() finishes may let a model go. One that left loadedModels
        // meanwhile keeps the flag (it is no longer the game's preview; DrawUnder does the same)
        class OnlySelection
        {
        public:
            OnlySelection(RE::Inventory3DManager& a_manager, const Shown& a_shown) :
                _manager(a_manager)
            {
                for (const auto& loaded : a_manager.GetRuntimeData().loadedModels) {
                    auto* model = loaded.spModel.get();
                    if (!model) continue;
                    if (a_shown.Has(loaded)) {
                        ++_selectionUp;
                        if (!model->GetAppCulled()) ++_selection;
                        continue;
                    }
                    if (model->GetAppCulled()) continue;
                    model->SetAppCulled(true);
                    _names += std::format("{}{} ({})", _names.empty() ? "" : ", ", model->name.c_str(), SpellName(loaded.itemBase));
                    _hidden.emplace_back(model);
                }
            }

            ~OnlySelection()
            {
                const auto& loadedModels = _manager.GetRuntimeData().loadedModels;
                for (const auto& model : _hidden) {
                    if (std::ranges::any_of(loadedModels, [&](const auto& a_loaded) { return a_loaded.spModel.get() == model.get(); })) {
                        model->SetAppCulled(false);
                    }
                }
            }

            OnlySelection(const OnlySelection&) = delete;
            OnlySelection& operator=(const OnlySelection&) = delete;

            [[nodiscard]] std::size_t HiddenCount() const { return _hidden.size(); }
            [[nodiscard]] int Selection() const { return _selection; }      // the selection's models up and not hidden
            [[nodiscard]] int SelectionUp() const { return _selectionUp; }  // the selection's models up
            [[nodiscard]] const std::string& Names() const { return _names; }

        private:
            RE::Inventory3DManager& _manager;
            std::vector<RE::NiPointer<RE::NiAVObject>> _hidden;
            std::string _names;
            int _selection = 0, _selectionUp = 0;
        };

        // Counted for the P7 summary; the first frame of each kind logged
        void LogOnlySelection(const OnlySelection& a_only, const Shown& a_shown)
        {
            auto& clip = g_state.clip;
            if (a_only.HiddenCount() > 0) {
                ++clip.othersFrames;
                if (!std::exchange(clip.othersLogged, true)) {
                    logger::info("Preview: P7 over-pass at PostDisplay {} draws only the selection's model ({:08X}): {} other model(s) hidden for it "
                                 "and given back after it ({}) - later such frames counted only",
                        clip.frame, a_shown.Form(), a_only.HiddenCount(), a_only.Names());
                }
            }
            if (a_only.SelectionUp() == 0) {
                ++clip.emptyFrames;
                if (!std::exchange(clip.emptyLogged, true)) {
                    logger::info("Preview: P7 over-pass at PostDisplay {}: no model of the selection ({:08X}) is up - the box stays empty (later such "
                                 "frames counted only)",
                        clip.frame, a_shown.Form());
                }
            } else if (a_only.Selection() == 0) {
                ++clip.coveredFrames;
                if (!std::exchange(clip.coveredLogged, true)) {
                    logger::info("Preview: P7 over-pass at PostDisplay {}: the selection's model ({:08X}, {} up) is hidden - by the game, or a copy "
                                 "the game replaced - so the box is empty this frame (later such frames counted only)",
                        clip.frame, a_shown.Form(), a_only.SelectionUp());
                }
            }
        }

        // After a selection change: the selection's art objects and every model up (form, art object, hidden), and how it
        // matched - by form, by art only (the shared-art case the art match is for), or not at all (the box stays empty). The
        // first kArtLinesMost per menu; settles whether the game shows a shared-art model under another spell's form
        void LogSelectionArt(RE::Inventory3DManager& a_manager, const Shown& a_shown)
        {
            auto& clip = g_state.clip;
            if (clip.artLines >= kArtLinesMost || !a_shown.Form()) return;
            const auto id = [](const RE::TESForm* a_form) { return a_form ? std::format("{:08X}", a_form->GetFormID()) : std::string("-"); };
            std::string models;
            int byForm = 0, byArt = 0;
            for (const auto& loaded : a_manager.GetRuntimeData().loadedModels) {
                if (!loaded.spModel) continue;
                const bool form = a_shown.Has(loaded.itemBase), art = !form && a_shown.Has(loaded);
                byForm += form ? 1 : 0;
                byArt += art ? 1 : 0;
                models += std::format("{}{} {} art {}{}{}", models.empty() ? "" : "; ", loaded.spModel->name.c_str(), id(loaded.itemBase), id(loaded.modelObj),
                    loaded.spModel->GetAppCulled() ? " hidden" : "", form ? " [form]" : art ? " [art]" : "");
            }
            ++clip.artLines;
            logger::info("Preview: P7 art match for {} ({:08X}, art {} / effect art {}, the art used: the spell's own when set) at PostDisplay {}: {} by form, "
                         "{} by art only (art only counts with none by form) - models up: {}",
                SpellName(RE::TESForm::LookupByID(a_shown.Form())), a_shown.Form(), id(a_shown.art[0]), id(a_shown.art[1]), clip.frame, byForm, byArt,
                models.empty() ? "none" : models);
        }

        // One line at close: what P7 did this menu
        void SummarizeP7(std::uint64_t a_generation)
        {
            const auto& clip = g_state.clip;
            if (g_state.generation != a_generation || clip.frame == 0) return;
            logger::info("Preview: P7 summary - menu {}: backdrop shapes hidden {}, glow shapes hidden {} (bHideBackdrop {}, bHideGlow {}), "
                         "matching shapes found hidden already {}, art shapes shown again after we hid them {}; over-passes drawing only the "
                         "selection's model with others hidden {}, with no model for the selection up {}, with its model up but hidden {}; the "
                         "game's own place restored {} time(s) (P7.2 self-test PASS {} FAIL {})",
                a_generation, clip.backdrops, clip.glows, Settings::Get().hideBackdrop ? 1 : 0, Settings::Get().hideGlow ? 1 : 0, clip.artAlready,
                clip.artShownAgain, clip.othersFrames, clip.emptyFrames, clip.coveredFrames, clip.unplaced, clip.unplacePass, clip.unplaceFail);
        }

        // ---------------------------------------------------------------------
        // Q3 A: the over-pass scissored to the box (spec v5)
        // ---------------------------------------------------------------------
        // The engine keeps its raster states by [fill][cull][depth bias][scissor 0/1] and sets the one the shadow state
        // names when a raster flag is dirty (Nukem9 SkyrimSETest BSGraphicsRenderer.h:51, BSGraphicsRenderer.cpp:711-713,
        // an SE reimplementation; that it still holds in Render() on this runtime is UNVERIFIED: the read-back is the
        // test), and that code sets no scissor rect of its own. So: shadow rasterStateScissorMode 1 with
        // DIRTY_RASTER_CULL_MODE or-ed in (the next draw's state apply picks the scissor raster state; the other bits
        // are never overwritten), and the box as the D3D11 scissor rect. Afterwards everything is put back: the D3D11
        // raster state and scissor rects as they were, the shadow mode, and the raster flag dirty again, so the engine
        // sets its own state again at its next draw. The engine scissor helper Community Shaders found (RelocationID
        // 75564/77365, x/y/width/height) is not used: its signature is not confirmed here.

        struct Target  // the render target bound for output (slot 0)
        {
            std::uint32_t width = 0, height = 0;
            const void* view = nullptr;  // identity only (its reference is released)
            std::uint32_t samples = 1;   // its texture's sample count
        };

        // Its texture's size at the view's mip; none when nothing is bound or it is not a 2D texture. Every COM
        // reference taken here is released (ComPtr)
        std::optional<Target> BoundTarget(REX::W32::ID3D11DeviceContext& a_context)
        {
            REX::W32::ComPtr<REX::W32::ID3D11RenderTargetView> view;
            a_context.OMGetRenderTargets(1, view.GetAddressOf(), nullptr);
            if (!view.Get()) return std::nullopt;
            REX::W32::ComPtr<REX::W32::ID3D11Resource> resource;
            view->GetResource(resource.GetAddressOf());
            // the IID form: COM.h's QueryInterface(T**) template does not compile (a static_cast from T** to void**)
            REX::W32::ComPtr<REX::W32::ID3D11Texture2D> texture;
            if (!resource.Get() ||
                !REX::W32::SUCCESS(resource->QueryInterface(REX::W32::IID_ID3D11Texture2D, reinterpret_cast<void**>(texture.GetAddressOf()))) ||
                !texture.Get()) {
                return std::nullopt;
            }
            REX::W32::D3D11_TEXTURE2D_DESC desc{};
            texture->GetDesc(&desc);
            REX::W32::D3D11_RENDER_TARGET_VIEW_DESC viewDesc{};
            view->GetDesc(&viewDesc);
            const std::uint32_t mip = viewDesc.viewDimension == REX::W32::D3D11_RTV_DIMENSION_TEXTURE2D ? viewDesc.texture2D.mipSlice : 0;
            return Target{ std::max(desc.width >> mip, 1u), std::max(desc.height >> mip, 1u), view.Get(), desc.sampleDesc.count };
        }

        // P8 log names (spec v11 H1/R9): a DXGI format, an RTV dimension, a blend factor and op by name; others by number
        std::string FormatName(REX::W32::DXGI_FORMAT a_format)
        {
            switch (a_format) {
            case REX::W32::DXGI_FORMAT_UNKNOWN: return "UNKNOWN";
            case REX::W32::DXGI_FORMAT_R32G32B32A32_TYPELESS: return "R32G32B32A32_TYPELESS";
            case REX::W32::DXGI_FORMAT_R32G32B32A32_FLOAT: return "R32G32B32A32_FLOAT";
            case REX::W32::DXGI_FORMAT_R16G16B16A16_TYPELESS: return "R16G16B16A16_TYPELESS";
            case REX::W32::DXGI_FORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT";
            case REX::W32::DXGI_FORMAT_R16G16B16A16_UNORM: return "R16G16B16A16_UNORM";
            case REX::W32::DXGI_FORMAT_R10G10B10A2_TYPELESS: return "R10G10B10A2_TYPELESS";
            case REX::W32::DXGI_FORMAT_R10G10B10A2_UNORM: return "R10G10B10A2_UNORM";
            case REX::W32::DXGI_FORMAT_R11G11B10_FLOAT: return "R11G11B10_FLOAT";
            case REX::W32::DXGI_FORMAT_R8G8B8A8_TYPELESS: return "R8G8B8A8_TYPELESS";
            case REX::W32::DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
            case REX::W32::DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return "R8G8B8A8_UNORM_SRGB";
            case REX::W32::DXGI_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
            case REX::W32::DXGI_FORMAT_B8G8R8X8_UNORM: return "B8G8R8X8_UNORM";
            case REX::W32::DXGI_FORMAT_B8G8R8A8_TYPELESS: return "B8G8R8A8_TYPELESS";
            case REX::W32::DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return "B8G8R8A8_UNORM_SRGB";
            case REX::W32::DXGI_FORMAT_B8G8R8X8_TYPELESS: return "B8G8R8X8_TYPELESS";
            case REX::W32::DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return "B8G8R8X8_UNORM_SRGB";
            default: return std::format("DXGI format {}", static_cast<int>(a_format));
            }
        }

        std::string DimensionName(REX::W32::D3D11_RTV_DIMENSION a_dimension)
        {
            using namespace REX::W32;
            switch (a_dimension) {
            case D3D11_RTV_DIMENSION_BUFFER: return "buffer";
            case D3D11_RTV_DIMENSION_TEXTURE1D: return "1D";
            case D3D11_RTV_DIMENSION_TEXTURE1DARRAY: return "1D array";
            case D3D11_RTV_DIMENSION_TEXTURE2D: return "2D";
            case D3D11_RTV_DIMENSION_TEXTURE2DARRAY: return "2D array";
            case D3D11_RTV_DIMENSION_TEXTURE2DMS: return "2D multisampled";
            case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY: return "2D multisampled array";
            case D3D11_RTV_DIMENSION_TEXTURE3D: return "3D";
            default: return std::format("dimension {}", static_cast<int>(a_dimension));
            }
        }

        std::string BlendName(REX::W32::D3D11_BLEND a_blend)
        {
            using namespace REX::W32;
            switch (a_blend) {
            case D3D11_BLEND_ZERO: return "ZERO";
            case D3D11_BLEND_ONE: return "ONE";
            case D3D11_BLEND_SRC_COLOR: return "SRC_COLOR";
            case D3D11_BLEND_INV_SRC_COLOR: return "INV_SRC_COLOR";
            case D3D11_BLEND_SRC_ALPHA: return "SRC_ALPHA";
            case D3D11_BLEND_INV_SRC_ALPHA: return "INV_SRC_ALPHA";
            case D3D11_BLEND_DEST_ALPHA: return "DEST_ALPHA";
            case D3D11_BLEND_INV_DEST_ALPHA: return "INV_DEST_ALPHA";
            case D3D11_BLEND_DEST_COLOR: return "DEST_COLOR";
            case D3D11_BLEND_INV_DEST_COLOR: return "INV_DEST_COLOR";
            case D3D11_BLEND_SRC_ALPHA_SAT: return "SRC_ALPHA_SAT";
            case D3D11_BLEND_BLEND_FACTOR: return "BLEND_FACTOR";
            case D3D11_BLEND_INV_BLEND_FACTOR: return "INV_BLEND_FACTOR";
            case D3D11_BLEND_SRC1_COLOR: return "SRC1_COLOR";
            case D3D11_BLEND_INV_SRC1_COLOR: return "INV_SRC1_COLOR";
            case D3D11_BLEND_SRC1_ALPHA: return "SRC1_ALPHA";
            case D3D11_BLEND_INV_SRC1_ALPHA: return "INV_SRC1_ALPHA";
            default: return std::format("blend {}", static_cast<int>(a_blend));
            }
        }

        std::string BlendOpName(REX::W32::D3D11_BLEND_OP a_op)
        {
            using namespace REX::W32;
            switch (a_op) {
            case D3D11_BLEND_OP_ADD: return "ADD";
            case D3D11_BLEND_OP_SUBTRACT: return "SUBTRACT";
            case D3D11_BLEND_OP_REV_SUBTRACT: return "REV_SUBTRACT";
            case D3D11_BLEND_OP_MIN: return "MIN";
            case D3D11_BLEND_OP_MAX: return "MAX";
            default: return std::format("op {}", static_cast<int>(a_op));
            }
        }

        // The format a view reads its resource with: the RTV's own when it is typed, else the resource's when that is
        // (a typeless resource is read by the RTV's typed format, R8). UNKNOWN when neither names a layout P8 reads
        std::optional<PreviewMath::PixelLayout> LayoutOf(REX::W32::DXGI_FORMAT a_format)
        {
            switch (a_format) {
            case REX::W32::DXGI_FORMAT_R8G8B8A8_UNORM:
            case REX::W32::DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
                return PreviewMath::PixelLayout::kRgba8;
            case REX::W32::DXGI_FORMAT_B8G8R8A8_UNORM:
            case REX::W32::DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
                return PreviewMath::PixelLayout::kBgra8;
            case REX::W32::DXGI_FORMAT_R16G16B16A16_FLOAT:
                return PreviewMath::PixelLayout::kRgba16Float;
            case REX::W32::DXGI_FORMAT_R11G11B10_FLOAT:
                return PreviewMath::PixelLayout::kR11G11B10Float;
            case REX::W32::DXGI_FORMAT_R10G10B10A2_UNORM:  // spec v12 C2: the over-pass's screen target (build 11 game run)
                return PreviewMath::PixelLayout::kRgb10A2;
            default:
                return std::nullopt;
            }
        }

        // H1/R9: what is bound at a Render() boundary (entry or exit), for the P8 log: the output target (slot 0) and the
        // blend state. Boundary state only: what each draw inside Render() used is a GPU capture's question
        struct BoundaryState
        {
            std::string target, blend;
        };

        std::string DescribeTarget(REX::W32::ID3D11DeviceContext& a_context)
        {
            using namespace REX::W32;
            ComPtr<ID3D11RenderTargetView> view;
            a_context.OMGetRenderTargets(1, view.GetAddressOf(), nullptr);
            if (!view.Get()) return "no render target bound (slot 0)";
            D3D11_RENDER_TARGET_VIEW_DESC viewDesc{};
            view->GetDesc(&viewDesc);
            std::string where;
            switch (viewDesc.viewDimension) {
            case D3D11_RTV_DIMENSION_TEXTURE2D:
                where = std::format("mip {}", viewDesc.texture2D.mipSlice);
                break;
            case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
                where = std::format("mip {}, slices {}+{}", viewDesc.texture2DArray.mipSlice, viewDesc.texture2DArray.firstArraySlice,
                    viewDesc.texture2DArray.arraySize);
                break;
            case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:
                where = std::format("slices {}+{}", viewDesc.texture2DMSArray.firstArraySlice, viewDesc.texture2DMSArray.arraySize);
                break;
            default:
                where = "mip 0";
                break;
            }
            const std::string head = std::format("RTV {} ({}, {})", FormatName(viewDesc.format), DimensionName(viewDesc.viewDimension), where);
            ComPtr<ID3D11Resource> resource;
            view->GetResource(resource.GetAddressOf());
            ComPtr<ID3D11Texture2D> texture;
            if (!resource.Get() || !SUCCESS(resource->QueryInterface(IID_ID3D11Texture2D, reinterpret_cast<void**>(texture.GetAddressOf()))) ||
                !texture.Get()) {
                return head + ", its resource is not a 2D texture";
            }
            D3D11_TEXTURE2D_DESC desc{};
            texture->GetDesc(&desc);
            return std::format("{}, resource {} {}x{}, {} sample(s) quality {}, {} mip(s), {} array slice(s)", head, FormatName(desc.format), desc.width,
                desc.height, desc.sampleDesc.count, desc.sampleDesc.quality, desc.mipLevels, desc.arraySize);
        }

        std::string DescribeBlend(REX::W32::ID3D11DeviceContext& a_context)
        {
            using namespace REX::W32;
            ComPtr<ID3D11BlendState> state;
            std::array<float, 4> factor{};
            std::uint32_t sampleMask = 0;
            a_context.OMGetBlendState(state.GetAddressOf(), factor.data(), &sampleMask);
            // the factor and mask are the context's live values whether a state is bound or not
            const std::string tail = std::format("BlendFactor ({:.3f}, {:.3f}, {:.3f}, {:.3f}), SampleMask 0x{:08X}", factor[0], factor[1], factor[2],
                factor[3], sampleMask);
            if (!state.Get()) {
                return "null state = D3D's default (RT0 BlendEnable 0, colour ONE/ZERO ADD, alpha ONE/ZERO ADD, RenderTargetWriteMask 0xF; "
                       "IndependentBlendEnable 0, AlphaToCoverageEnable 0), " +
                       tail;
            }
            D3D11_BLEND_DESC desc{};
            state->GetDesc(&desc);
            const auto& rt = desc.renderTarget[0];
            return std::format("RT0 BlendEnable {}, colour {}/{} {}, alpha {}/{} {}, RenderTargetWriteMask 0x{:X}; IndependentBlendEnable {}, "
                               "AlphaToCoverageEnable {}, {}",
                rt.blendEnable ? 1 : 0, BlendName(rt.srcBlend), BlendName(rt.destBlend), BlendOpName(rt.blendOp), BlendName(rt.srcBlendAlpha),
                BlendName(rt.destBlendAlpha), BlendOpName(rt.blendOpAlpha), rt.renderTargetWriteMask, desc.independentBlendEnable ? 1 : 0,
                desc.alphaToCoverageEnable ? 1 : 0, tail);
        }

        BoundaryState ReadBoundary(REX::W32::ID3D11DeviceContext& a_context)
        {
            return { DescribeTarget(a_context), DescribeBlend(a_context) };
        }

        // P8 readback: the box's pixels as RGBA floats, or why there are none (R8: dimension, array, sample count, format,
        // or the D3D call with its HRESULT; CopySubresourceRegion and ResolveSubresource return nothing to check)
        struct BoxRead
        {
            std::vector<float> pixels;
            PreviewMath::PixelLayout layout = PreviewMath::PixelLayout::kRgba8;
            std::string error;  // empty: read
        };

        BoxRead ReadFailed(std::string a_why)
        {
            BoxRead read;
            read.error = std::move(a_why);
            return read;
        }

        // A render target view's pixels in a_box (H2, R8; ReadBoxPixels reads the bound one by its view, review v13 #6). The RTV's typed format picks the layout (a typeless resource by its view). A multisampled target is first resolved whole to a single-sample USAGE_DEFAULT texture of that
        // format, then the box is copied to STAGING and mapped; the resolve averages each pixel's samples, the same way for
        // both reads. A staging copy and a blocking Map on the main thread: done a few times per menu only (kDarkChecksMost)
        BoxRead ReadViewPixels(REX::W32::ID3D11DeviceContext& a_context, REX::W32::ID3D11RenderTargetView& a_view, const PreviewMath::PixelBox& a_box)
        {
            using namespace REX::W32;
            D3D11_RENDER_TARGET_VIEW_DESC viewDesc{};
            a_view.GetDesc(&viewDesc);
            const bool multisampled = viewDesc.viewDimension == D3D11_RTV_DIMENSION_TEXTURE2DMS;
            if (viewDesc.viewDimension == D3D11_RTV_DIMENSION_TEXTURE2DARRAY || viewDesc.viewDimension == D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY) {
                return ReadFailed(std::format("the RTV is an array ({}): not read", DimensionName(viewDesc.viewDimension)));
            }
            if (viewDesc.viewDimension != D3D11_RTV_DIMENSION_TEXTURE2D && !multisampled) {
                return ReadFailed(std::format("the RTV dimension is {}, not 2D", DimensionName(viewDesc.viewDimension)));
            }
            ComPtr<ID3D11Resource> resource;
            a_view.GetResource(resource.GetAddressOf());
            ComPtr<ID3D11Texture2D> texture;
            if (!resource.Get()) return ReadFailed("the RTV has no resource");
            if (const auto result = resource->QueryInterface(IID_ID3D11Texture2D, reinterpret_cast<void**>(texture.GetAddressOf()));
                !SUCCESS(result) || !texture.Get()) {
                return ReadFailed(std::format("QueryInterface(ID3D11Texture2D) failed, HRESULT 0x{:08X}", static_cast<std::uint32_t>(result)));
            }
            D3D11_TEXTURE2D_DESC desc{};
            texture->GetDesc(&desc);
            if (desc.arraySize != 1) return ReadFailed(std::format("the resource is an array of {} slices: not read", desc.arraySize));
            if ((desc.sampleDesc.count > 1) != multisampled) {
                return ReadFailed(std::format("{} sample(s) on a {} RTV", desc.sampleDesc.count, DimensionName(viewDesc.viewDimension)));
            }
            const auto typed = LayoutOf(viewDesc.format) ? viewDesc.format : desc.format;
            const auto layout = LayoutOf(typed);
            if (!layout) {
                return ReadFailed(std::format("format RTV {} / resource {} is not one P8 reads (8-bit RGBA/BGRA, R16G16B16A16_FLOAT, R11G11B10_FLOAT, "
                                              "R10G10B10A2_UNORM)",
                    FormatName(viewDesc.format), FormatName(desc.format)));
            }
            const std::uint32_t mip = multisampled ? 0 : viewDesc.texture2D.mipSlice;
            const std::uint32_t mipW = std::max(desc.width >> mip, 1u), mipH = std::max(desc.height >> mip, 1u);
            if (a_box.x2 <= a_box.x1 || a_box.y2 <= a_box.y1 || a_box.x1 < 0 || a_box.y1 < 0 || static_cast<std::uint32_t>(a_box.x2) > mipW ||
                static_cast<std::uint32_t>(a_box.y2) > mipH) {
                return ReadFailed(std::format("the box x {}..{} y {}..{} is not inside the {}x{} target", a_box.x1, a_box.x2, a_box.y1, a_box.y2, mipW, mipH));
            }
            const auto width = static_cast<std::uint32_t>(a_box.x2 - a_box.x1), height = static_cast<std::uint32_t>(a_box.y2 - a_box.y1);
            ComPtr<ID3D11Device> device;
            texture->GetDevice(device.GetAddressOf());
            if (!device.Get()) return ReadFailed("the texture has no device");
            ID3D11Texture2D* source = texture.Get();
            std::uint32_t sourceSub = mip;  // one array slice: the subresource index is the view's mip
            ComPtr<ID3D11Texture2D> resolved;
            if (multisampled) {
                D3D11_TEXTURE2D_DESC resolveDesc{};
                resolveDesc.width = desc.width;
                resolveDesc.height = desc.height;
                resolveDesc.mipLevels = resolveDesc.arraySize = 1;
                resolveDesc.format = typed;
                resolveDesc.sampleDesc = { 1, 0 };
                resolveDesc.usage = D3D11_USAGE_DEFAULT;
                if (const auto result = device->CreateTexture2D(&resolveDesc, nullptr, resolved.GetAddressOf()); !SUCCESS(result) || !resolved.Get()) {
                    return ReadFailed(std::format("CreateTexture2D (resolve target {}) failed, HRESULT 0x{:08X}", FormatName(typed),
                        static_cast<std::uint32_t>(result)));
                }
                a_context.ResolveSubresource(resolved.Get(), 0, texture.Get(), 0, typed);
                source = resolved.Get();
                sourceSub = 0;
            }
            D3D11_TEXTURE2D_DESC stagingDesc{};
            stagingDesc.width = width;
            stagingDesc.height = height;
            stagingDesc.mipLevels = stagingDesc.arraySize = 1;
            stagingDesc.format = typed;
            stagingDesc.sampleDesc = { 1, 0 };
            stagingDesc.usage = D3D11_USAGE_STAGING;
            stagingDesc.cpuAccessFlags = static_cast<std::uint32_t>(D3D11_CPU_ACCESS_READ);
            ComPtr<ID3D11Texture2D> staging;
            if (const auto result = device->CreateTexture2D(&stagingDesc, nullptr, staging.GetAddressOf()); !SUCCESS(result) || !staging.Get()) {
                return ReadFailed(std::format("CreateTexture2D (staging {}) failed, HRESULT 0x{:08X}", FormatName(typed), static_cast<std::uint32_t>(result)));
            }
            const D3D11_BOX box{ static_cast<std::uint32_t>(a_box.x1), static_cast<std::uint32_t>(a_box.y1), 0, static_cast<std::uint32_t>(a_box.x2),
                static_cast<std::uint32_t>(a_box.y2), 1 };
            a_context.CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, source, sourceSub, &box);
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (const auto result = a_context.Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped); !SUCCESS(result) || !mapped.data) {
                return ReadFailed(std::format("Map (staging) failed, HRESULT 0x{:08X}", static_cast<std::uint32_t>(result)));
            }
            // a mapped 2D staging subresource is height rows of rowPitch bytes
            auto pixels = PreviewMath::UnpackBox(static_cast<const std::uint8_t*>(mapped.data), static_cast<std::size_t>(mapped.rowPitch) * height,
                mapped.rowPitch, width, height, *layout);
            a_context.Unmap(staging.Get(), 0);
            if (!pixels) return ReadFailed(std::format("the mapped row pitch {} is under a row of {} pixels", mapped.rowPitch, width));
            BoxRead read;
            read.pixels = std::move(*pixels);
            read.layout = *layout;
            return read;
        }

        // The render target bound for output (slot 0), read as ReadViewPixels does
        BoxRead ReadBoxPixels(REX::W32::ID3D11DeviceContext& a_context, const PreviewMath::PixelBox& a_box)
        {
            REX::W32::ComPtr<REX::W32::ID3D11RenderTargetView> view;
            a_context.OMGetRenderTargets(1, view.GetAddressOf(), nullptr);
            if (!view.Get()) return ReadFailed("no render target bound");
            return ReadViewPixels(a_context, *view.Get(), a_box);
        }

        // Scaleform draws the menu movie with blend, depth and raster states of its own, set on the D3D11 context directly; the
        // engine's shadow state still holds what it set last. Build 10 guessed that the over-pass Render() therefore skipped
        // re-applying the effect shaders' additive blend and drew the cloud dark (game run 2026-10-26 01:53: a black cloud
        // behind the healing art once the panel was see-through; GPT diagnosis video9/codex_halo_result.md, "the engine sets a
        // state only when dirty"). That cause is UNPROVEN: these flags were tried in build 10 and the cloud stayed (user,
        // 2026-10-26, spec v11 H4). They stay: marking those states dirty makes the engine set every one again for its next
        // draw; the input layout and topology too (Scaleform sets its own; review v10). Shaders, textures and samplers are not
        // covered by these flags. Build 11 only measures (P8: target format, boundary blend state, colour and alpha change)
        void MarkDrawStatesDirty(RE::BSGraphics::RendererShadowState& a_shadowState)
        {
            using Flag = RE::BSGraphics::ShaderFlags;
            a_shadowState.GetRuntimeData().stateUpdateFlags.set(Flag::DIRTY_ALPHA_BLEND, Flag::DIRTY_ALPHA_ENABLE_TEST, Flag::DIRTY_ALPHA_TEST_REF,
                Flag::DIRTY_DEPTH_MODE, Flag::DIRTY_DEPTH_STENCILREF_MODE, Flag::DIRTY_RASTER_CULL_MODE, Flag::DIRTY_RASTER_DEPTH_BIAS,
                Flag::DIRTY_VERTEX_DESC, Flag::DIRTY_PRIMITIVE_TOPO);
        }

        bool SameRect(const REX::W32::D3D11_RECT& a_one, const PreviewMath::PixelBox& a_box)
        {
            return a_one.x1 == a_box.x1 && a_one.y1 == a_box.y1 && a_one.x2 == a_box.x2 && a_one.y2 == a_box.y2;
        }

        struct ScissorAfter  // what Render() left (not each draw's state: a GPU capture is the full check)
        {
            std::uint32_t mode = 0;          // shadow rasterStateScissorMode
            std::optional<bool> enabled;     // the bound D3D11 raster state's scissorEnable; none: no state bound (default, off)
            std::uint32_t rects = 0;
            REX::W32::D3D11_RECT rect{};
            bool sameRect = false;
        };

        // Sets the box as the scissor for its lifetime; puts everything back when it ends
        class ScissorScope
        {
        public:
            ScissorScope(REX::W32::ID3D11DeviceContext& a_context, RE::BSGraphics::RendererShadowState::FLAT_RUNTIME_DATA& a_shadow,
                const PreviewMath::PixelBox& a_box) :
                _context(a_context), _shadow(a_shadow), _box(a_box)
            {
                _context.RSGetState(_state.GetAddressOf());  // one reference, released by the ComPtr
                _context.RSGetScissorRects(&_count, nullptr);  // how many are bound
                _count = std::min(_count, kRectsMost);
                if (_count > 0) _context.RSGetScissorRects(&_count, _rects.data());
                _mode = _shadow.rasterStateScissorMode;
                _shadow.rasterStateScissorMode = kScissorOn;
                _shadow.stateUpdateFlags.set(RE::BSGraphics::ShaderFlags::DIRTY_RASTER_CULL_MODE);
                const REX::W32::D3D11_RECT rect{ _box.x1, _box.y1, _box.x2, _box.y2 };
                _context.RSSetScissorRects(1, &rect);
            }

            ~ScissorScope()
            {
                _shadow.rasterStateScissorMode = _mode;
                _shadow.stateUpdateFlags.set(RE::BSGraphics::ShaderFlags::DIRTY_RASTER_CULL_MODE);
                _context.RSSetState(_state.Get());
                _context.RSSetScissorRects(_count, _count > 0 ? _rects.data() : nullptr);
            }

            ScissorScope(const ScissorScope&) = delete;
            ScissorScope& operator=(const ScissorScope&) = delete;

            [[nodiscard]] ScissorAfter ReadBack() const
            {
                ScissorAfter after;
                after.mode = _shadow.rasterStateScissorMode;
                REX::W32::ComPtr<REX::W32::ID3D11RasterizerState> state;
                _context.RSGetState(state.GetAddressOf());
                if (state.Get()) {
                    REX::W32::D3D11_RASTERIZER_DESC desc{};
                    state->GetDesc(&desc);
                    after.enabled = desc.scissorEnable != 0;
                }
                std::array<REX::W32::D3D11_RECT, kRectsMost> rects{};
                std::uint32_t count = 0;
                _context.RSGetScissorRects(&count, nullptr);
                count = std::min(count, kRectsMost);
                if (count > 0) _context.RSGetScissorRects(&count, rects.data());
                after.rects = count;
                if (count > 0) after.rect = rects[0];
                after.sameRect = count >= 1 && SameRect(rects[0], _box);
                return after;
            }

        private:
            REX::W32::ID3D11DeviceContext& _context;
            RE::BSGraphics::RendererShadowState::FLAT_RUNTIME_DATA& _shadow;
            PreviewMath::PixelBox _box;
            REX::W32::ComPtr<REX::W32::ID3D11RasterizerState> _state;
            std::array<REX::W32::D3D11_RECT, kRectsMost> _rects{};
            std::uint32_t _count = 0;
            std::uint32_t _mode = 0;
        };

        // Models Render() draws: up and not hidden
        int Drawable(RE::Inventory3DManager& a_manager)
        {
            int drawn = 0;
            for (const auto& loaded : a_manager.GetRuntimeData().loadedModels) drawn += loaded.spModel && !loaded.spModel->GetAppCulled() ? 1 : 0;
            return drawn;
        }

        std::string BoundsText(const PreviewMath::PixelBounds& a_bounds)
        {
            return a_bounds.Empty() ? std::string{ "none" } :
                                      std::format("x {}..{} y {}..{}", a_bounds.left, a_bounds.right, a_bounds.top, a_bounds.bottom);
        }

        std::string RangeText(float a_min, float a_max)
        {
            return a_min > a_max ? std::string{ "none finite" } : std::format("{:.4f}..{:.4f}", a_min, a_max);
        }

        // The tolerance a P8 colour change must pass, for the log (spec v12: R10G10B10A2 keeps the 8-bit values, PreviewMath.h)
        std::string PastText(PreviewMath::PixelLayout a_layout, const PreviewMath::PixelTolerance& a_tolerance)
        {
            if (PreviewMath::IsFloat(a_layout)) {
                return std::format("past max({:.3f}, {:.0f} % of the larger value)", a_tolerance.colour, a_tolerance.colourShare * 100.0f);
            }
            if (a_layout == PreviewMath::PixelLayout::kRgb10A2) return std::format("past {:.4f} (24.5 of 255 steps, about 98 of 1023)", a_tolerance.colour);
            return "past 24 of 255 steps";
        }

        // ---------------------------------------------------------------------
        // A: keep the panel's alpha (spec v13 A1-A4)
        // ---------------------------------------------------------------------
        // Build 12 game run (ledger "Build 12 game run"): the over-pass target is R10G10B10A2_UNORM 2560x1600, one sample,
        // NOT the swap chain's back buffer - it is composited later by its alpha over the world. Over Render() no pixel got
        // darker (P8 dark PASS) but alpha went 0.667 -> 1.0 over the art's silhouette (P8 alpha ALPHA_CHANGED): the world
        // behind is covered there, the dark cloud. Build 12's redirect into our own target could not work: the engine
        // re-binds its own target inside Render(). Spec v13 A2: with [Preview] bKeepPanelAlpha=1 the scissored over-pass
        // copies the box of the bound target into a scratch texture of the same format (the target's size, so the pixel
        // shader Loads at its own pixel: no constant buffer) before Render(), and after it draws one full-screen triangle of
        // our own, scissored to the box, blend off, RenderTargetWriteMask ALPHA: the box's alpha is put back, RGB stays what
        // Render() drew. Off for the rest of the session, with one warn line, when the shaders do not compile (D3DCompile
        // from d3dcompiler_47.dll, loaded at run time: the plugin has no import of it) or a D3D object cannot be made. A
        // target the restore does not handle (multisampled, an array, a format without alpha or one P8 does not read, A1)
        // is skipped, logged once per menu; that over-pass draws as build 11.
        // Review v13 #1: an over-pass without the scissor saves and restores the whole bound target (RenderUnclipped).

        struct AlphaPass  // session-wide; main thread only (OnPostDisplay, the close task)
        {
            bool off = false;    // for the rest of the session (its reason logged once)
            bool built = false;  // shaders and states made
            REX::W32::ComPtr<REX::W32::ID3D11VertexShader> vs;
            REX::W32::ComPtr<REX::W32::ID3D11PixelShader> ps;
            REX::W32::ComPtr<REX::W32::ID3D11BlendState> blend;
            REX::W32::ComPtr<REX::W32::ID3D11DepthStencilState> depth;
            REX::W32::ComPtr<REX::W32::ID3D11RasterizerState> raster;
            // the scratch: made again on a size or format change, released at menu close
            REX::W32::ComPtr<REX::W32::ID3D11Texture2D> texture;
            REX::W32::ComPtr<REX::W32::ID3D11ShaderResourceView> srv;
            std::uint32_t width = 0, height = 0;
            REX::W32::DXGI_FORMAT format = REX::W32::DXGI_FORMAT_UNKNOWN;
            std::uint64_t generation = 0;  // the menu that used the scratch last (its close releases it)
        };
        // Never destroyed, like g_state: a ComPtr released in a static destructor at game exit would call into a device
        // that may be gone by then
        AlphaPass& g_alphaPass = *new AlphaPass{};

        void ReleaseScratch()
        {
            g_alphaPass.srv.Reset();
            g_alphaPass.texture.Reset();
            g_alphaPass.width = g_alphaPass.height = 0;
            g_alphaPass.format = REX::W32::DXGI_FORMAT_UNKNOWN;
        }

        void AlphaPassOff(std::string_view a_why)
        {
            if (std::exchange(g_alphaPass.off, true)) return;
            ReleaseScratch();
            logger::warn("Preview: A alpha restore off for the rest of the session - {}; the over-pass draws without the alpha restore (the art raises the "
                         "panel's alpha: the dark cloud can show)",
                a_why);
            FlushLog();
        }

        // Fullscreen triangle from SV_VertexID (no vertex buffer, no input layout). The PS Loads the saved texel under its
        // pixel (the scratch is the target's size and the viewport maps it 1:1: no sampler slot to save and restore) and
        // outputs its alpha; the blend state writes alpha only. An UNORM Load returns the stored step, so the write lands on
        // the same step (A3). Shader model 4.0 (feature level 10_0 and up)
        constexpr char kAlphaHlsl[] = R"(
Texture2D<float4> saved : register(t0);

float4 VSMain(uint id : SV_VertexID) : SV_Position
{
    float2 corner = float2((id << 1) & 2, id & 2);
    return float4(corner * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 PSMain(float4 position : SV_Position) : SV_Target
{
    return float4(0.0, 0.0, 0.0, saved.Load(int3(position.xy, 0)).a);
}
)";

        // D3DCompile's signature as REX::W32 declares it (D3DCOMPILER.h); called through GetProcAddress, not REX's import
        using D3DCompileFn = REX::W32::HRESULT (*)(const void*, std::size_t, const char*, const REX::W32::D3D_SHADER_MACRO*, REX::W32::ID3DInclude*,
            const char*, const char*, std::uint32_t, std::uint32_t, REX::W32::ID3DBlob**, REX::W32::ID3DBlob**);

        // One entry point compiled; empty on success, else why (with the compiler's own message)
        std::string CompileEntry(D3DCompileFn a_compile, const char* a_entry, const char* a_profile, REX::W32::ComPtr<REX::W32::ID3DBlob>& a_code)
        {
            using namespace REX::W32;
            ComPtr<ID3DBlob> errors;
            const auto result = a_compile(kAlphaHlsl, sizeof(kAlphaHlsl) - 1, "SL_MagicMenu alpha restore", nullptr, nullptr, a_entry, a_profile,
                D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, a_code.GetAddressOf(), errors.GetAddressOf());
            if (SUCCESS(result) && a_code.Get()) return {};
            std::string message = errors.Get() && errors->GetBufferPointer() ?
                                      std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) :
                                      std::string{ "no message" };
            while (!message.empty() && (message.back() == '\0' || message.back() == '\n' || message.back() == '\r')) message.pop_back();
            return std::format("D3DCompile {} ({}) failed, HRESULT 0x{:08X}: {}", a_entry, a_profile, static_cast<std::uint32_t>(result), message);
        }

        // The shaders (compiled once) and the three states of the restore draw (build 12 G8). Empty on success, else why
        std::string BuildAlphaPass(REX::W32::ID3D11Device& a_device)
        {
            using namespace REX::W32;
            const auto module = REX::W32::LoadLibraryW(L"d3dcompiler_47.dll");
            if (!module) return std::format("LoadLibraryW(d3dcompiler_47.dll) failed, error {}", REX::W32::GetLastError());
            std::string why;
            {
                // the blobs are that DLL's objects: released before it is freed
                ComPtr<ID3DBlob> vsCode, psCode;
                const auto compile = reinterpret_cast<D3DCompileFn>(REX::W32::GetProcAddress(module, "D3DCompile"));
                if (!compile) {
                    why = std::format("GetProcAddress(D3DCompile) failed, error {}", REX::W32::GetLastError());
                } else if (why = CompileEntry(compile, "VSMain", "vs_4_0", vsCode); why.empty()) {
                    why = CompileEntry(compile, "PSMain", "ps_4_0", psCode);
                }
                if (why.empty()) {
                    if (const auto vsResult = a_device.CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr,
                            g_alphaPass.vs.ReleaseAndGetAddressOf());
                        !SUCCESS(vsResult)) {
                        why = std::format("CreateVertexShader failed, HRESULT 0x{:08X}", static_cast<std::uint32_t>(vsResult));
                    } else if (const auto psResult = a_device.CreatePixelShader(psCode->GetBufferPointer(), psCode->GetBufferSize(), nullptr,
                                   g_alphaPass.ps.ReleaseAndGetAddressOf());
                               !SUCCESS(psResult)) {
                        why = std::format("CreatePixelShader failed, HRESULT 0x{:08X}", static_cast<std::uint32_t>(psResult));
                    }
                }
            }
            REX::W32::FreeLibrary(module);
            if (!why.empty()) return why;

            // spec v13 A2: blend off (the D3D default state would write all four channels), alpha written only
            D3D11_BLEND_DESC blend{};
            blend.alphaToCoverageEnable = 0;
            blend.independentBlendEnable = 0;
            auto& rt = blend.renderTarget[0];
            rt.blendEnable = 0;
            rt.srcBlend = rt.srcBlendAlpha = D3D11_BLEND_ONE;
            rt.destBlend = rt.destBlendAlpha = D3D11_BLEND_ZERO;
            rt.blendOp = rt.blendOpAlpha = D3D11_BLEND_OP_ADD;
            rt.renderTargetWriteMask = kWriteAlpha;
            if (const auto result = a_device.CreateBlendState(&blend, g_alphaPass.blend.ReleaseAndGetAddressOf()); !SUCCESS(result)) {
                return std::format("CreateBlendState failed, HRESULT 0x{:08X}", static_cast<std::uint32_t>(result));
            }
            D3D11_DEPTH_STENCIL_DESC depth{};
            depth.depthEnable = 0;
            depth.depthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            depth.depthFunc = D3D11_COMPARISON_ALWAYS;
            depth.stencilEnable = 0;
            depth.stencilReadMask = depth.stencilWriteMask = 0xFF;
            depth.frontFace = depth.backFace = { D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_ALWAYS };
            if (const auto result = a_device.CreateDepthStencilState(&depth, g_alphaPass.depth.ReleaseAndGetAddressOf()); !SUCCESS(result)) {
                return std::format("CreateDepthStencilState failed, HRESULT 0x{:08X}", static_cast<std::uint32_t>(result));
            }
            D3D11_RASTERIZER_DESC raster{};
            raster.fillMode = D3D11_FILL_SOLID;
            raster.cullMode = D3D11_CULL_NONE;
            raster.depthClipEnable = 1;
            raster.scissorEnable = 1;  // the box
            if (const auto result = a_device.CreateRasterizerState(&raster, g_alphaPass.raster.ReleaseAndGetAddressOf()); !SUCCESS(result)) {
                return std::format("CreateRasterizerState failed, HRESULT 0x{:08X}", static_cast<std::uint32_t>(result));
            }
            g_alphaPass.built = true;
            logger::info("Preview: A alpha restore shaders compiled (d3dcompiler_47.dll D3DCompile, vs_4_0 / ps_4_0) and states made (blend off, "
                         "write mask ALPHA; depth and stencil off; solid, no cull, scissor on)");
            return {};
        }

        constexpr int kScratchRemakesMost = 8;  // review v13 #5: per menu, then the restore is off for the session

        // The scratch at a_width x a_height of a_format (kept while both hold), shader resource only. Empty on success, else why
        std::string EnsureScratch(REX::W32::ID3D11Device& a_device, std::uint32_t a_width, std::uint32_t a_height, REX::W32::DXGI_FORMAT a_format)
        {
            using namespace REX::W32;
            if (g_alphaPass.texture.Get() && g_alphaPass.width == a_width && g_alphaPass.height == a_height && g_alphaPass.format == a_format) return {};
            // review v13 #5: two targets of different size or format alternating would make a 16 MB texture every frame.
            // Round 2 #5: the count is of creations that succeeded (a failed one turns the restore off anyway)
            if (g_state.clip.scratchCreated >= kScratchRemakesMost) {
                return std::format("the scratch was created {} times this menu (the most: {}; targets of different size or format alternate; "
                                   "now {} {}x{})",
                    g_state.clip.scratchCreated, kScratchRemakesMost, FormatName(a_format), a_width, a_height);
            }
            ReleaseScratch();
            D3D11_TEXTURE2D_DESC desc{};
            desc.width = a_width;
            desc.height = a_height;
            desc.mipLevels = desc.arraySize = 1;
            desc.format = a_format;
            desc.sampleDesc = { 1, 0 };
            desc.usage = D3D11_USAGE_DEFAULT;
            desc.bindFlags = static_cast<std::uint32_t>(D3D11_BIND_SHADER_RESOURCE);
            if (const auto result = a_device.CreateTexture2D(&desc, nullptr, g_alphaPass.texture.ReleaseAndGetAddressOf()); !SUCCESS(result)) {
                ReleaseScratch();
                return std::format("CreateTexture2D (scratch {} {}x{}) failed, HRESULT 0x{:08X}", FormatName(a_format), a_width, a_height,
                    static_cast<std::uint32_t>(result));
            }
            if (const auto result = a_device.CreateShaderResourceView(g_alphaPass.texture.Get(), nullptr, g_alphaPass.srv.ReleaseAndGetAddressOf());
                !SUCCESS(result)) {
                ReleaseScratch();
                return std::format("CreateShaderResourceView (scratch {}) failed, HRESULT 0x{:08X}", FormatName(a_format), static_cast<std::uint32_t>(result));
            }
            g_alphaPass.width = a_width;
            g_alphaPass.height = a_height;
            g_alphaPass.format = a_format;
            ++g_state.clip.scratchCreated;
            logger::info("Preview: A scratch made: {} {}x{}, 1 sample, SRV", FormatName(a_format), a_width, a_height);
            return {};
        }

        // G10 (spec v12 C3): whether the bound render target's resource is the swap chain's buffer 0, by IUnknown identity
        // (Renderer::GetCurrentRenderWindow()->swapChain, CommonLibSSE-NG Renderer.h:35 / Renderer.cpp:190)
        std::string SwapChainCheck(REX::W32::ID3D11DeviceContext& a_context)
        {
            using namespace REX::W32;
            auto* window = RE::BSGraphics::Renderer::GetCurrentRenderWindow();
            if (!window || !window->swapChain) return "no current render window or swap chain - not checked";
            ComPtr<IUnknown> buffer;
            if (const auto result = window->swapChain->GetBuffer(0, REX::W32::IID_IUnknown, reinterpret_cast<void**>(buffer.GetAddressOf()));
                !SUCCESS(result) || !buffer.Get()) {
                return std::format("IDXGISwapChain::GetBuffer(0) failed, HRESULT 0x{:08X} - not checked", static_cast<std::uint32_t>(result));
            }
            ComPtr<ID3D11RenderTargetView> view;
            a_context.OMGetRenderTargets(1, view.GetAddressOf(), nullptr);
            if (!view.Get()) return "no render target bound - not checked";
            ComPtr<ID3D11Resource> resource;
            view->GetResource(resource.GetAddressOf());
            ComPtr<IUnknown> identity;
            if (!resource.Get() || !SUCCESS(resource->QueryInterface(REX::W32::IID_IUnknown, reinterpret_cast<void**>(identity.GetAddressOf()))) || !identity.Get()) {
                return "the bound RTV's resource gave no IUnknown - not checked";
            }
            return identity.Get() == buffer.Get() ? "the bound render target IS the swap chain's back buffer (buffer 0)" :
                                                    "the bound render target is NOT the swap chain's back buffer (buffer 0)";
        }

        // With bKeepPanelAlpha=1 (and the restore not off) an over-pass whose box alpha is not restored draws as build 11;
        // said once per menu, the rest counted for the close line
        void LogRestoreSkipped(std::string_view a_why)
        {
            if (!Settings::Get().keepPanelAlpha || g_alphaPass.off || REL::Module::IsVR()) return;
            ++g_state.clip.restoreSkipped;
            if (std::exchange(g_state.clip.restoreSkipLogged, true)) return;
            logger::info("Preview: A alpha restore skipped at PostDisplay {} - {}; that over-pass draws without the alpha restore (later such frames counted only)",
                g_state.clip.frame, a_why);
        }

        // Review v13 #2: a frame with no render target to save (no context, none bound) is counted apart from the skips
        // above, so the first real skip is the one LogRestoreSkipped logs; once per menu, the rest counted for the close line
        void LogRestoreNoTarget(std::string_view a_why)
        {
            if (!Settings::Get().keepPanelAlpha || g_alphaPass.off || REL::Module::IsVR()) return;
            ++g_state.clip.restoreNoTarget;
            if (std::exchange(g_state.clip.restoreNoTargetLogged, true)) return;
            logger::info("Preview: A alpha restore: no target to save at PostDisplay {} - {}; that over-pass draws without the alpha restore (counted apart "
                         "from the skips; later such frames counted only)",
                g_state.clip.frame, a_why);
        }

        // Spec v13 A2: the box of the render target bound for output (slot 0) copied into the scratch at the same pixels,
        // before Render(); the view is kept for the restore draw. nullopt: bKeepPanelAlpha=0, the restore off, Skyrim VR,
        // or a target it does not handle (A1, logged once per menu). The box is ScissorBox's (clamped to the target) and is
        // checked again against the view's mip: CopySubresourceRegion's source region must lie inside the texture
        struct SavedAlpha
        {
            REX::W32::ComPtr<REX::W32::ID3D11RenderTargetView> view;
            std::uint32_t width = 0, height = 0;  // the view's mip size (the restore draw's viewport)
        };

        std::optional<SavedAlpha> SaveBoxAlpha(REX::W32::ID3D11DeviceContext& a_context, const PreviewMath::PixelBox& a_box)
        {
            using namespace REX::W32;
            if (!Settings::Get().keepPanelAlpha || g_alphaPass.off || REL::Module::IsVR()) return std::nullopt;
            SavedAlpha saved;
            a_context.OMGetRenderTargets(1, saved.view.GetAddressOf(), nullptr);
            if (!saved.view.Get()) {
                LogRestoreSkipped("no render target bound");
                return std::nullopt;
            }
            D3D11_RENDER_TARGET_VIEW_DESC viewDesc{};
            saved.view->GetDesc(&viewDesc);
            if (viewDesc.viewDimension != D3D11_RTV_DIMENSION_TEXTURE2D) {
                LogRestoreSkipped(std::format("the RTV is {} (only a single-sample 2D target is handled, A1)", DimensionName(viewDesc.viewDimension)));
                return std::nullopt;
            }
            ComPtr<ID3D11Resource> resource;
            saved.view->GetResource(resource.GetAddressOf());
            ComPtr<ID3D11Texture2D> texture;
            if (!resource.Get() || !SUCCESS(resource->QueryInterface(IID_ID3D11Texture2D, reinterpret_cast<void**>(texture.GetAddressOf()))) ||
                !texture.Get()) {
                LogRestoreSkipped("the RTV's resource is not a 2D texture");
                return std::nullopt;
            }
            D3D11_TEXTURE2D_DESC desc{};
            texture->GetDesc(&desc);
            if (desc.arraySize != 1 || desc.sampleDesc.count != 1) {
                LogRestoreSkipped(std::format("the target has {} array slice(s) and {} sample(s) (1 and 1 handled, A1)", desc.arraySize, desc.sampleDesc.count));
                return std::nullopt;
            }
            // the format the view reads its resource with, as P8 picks it (a typeless resource by its view)
            const auto typed = LayoutOf(viewDesc.format) ? viewDesc.format : desc.format;
            switch (PreviewMath::AlphaRestoreFor(LayoutOf(typed))) {
            case PreviewMath::AlphaRestore::kRestore:
                break;
            case PreviewMath::AlphaRestore::kNoAlphaStorage:
                LogRestoreSkipped(std::format("format RTV {} / resource {} has no alpha storage: nothing to restore (A1)", FormatName(viewDesc.format),
                    FormatName(desc.format)));
                return std::nullopt;
            case PreviewMath::AlphaRestore::kUnsupportedFormat:
                LogRestoreSkipped(std::format("format RTV {} / resource {} is not one P8 reads (A1)", FormatName(viewDesc.format), FormatName(desc.format)));
                return std::nullopt;
            }
            const std::uint32_t mip = viewDesc.texture2D.mipSlice;
            saved.width = std::max(desc.width >> mip, 1u);
            saved.height = std::max(desc.height >> mip, 1u);
            if (a_box.x2 <= a_box.x1 || a_box.y2 <= a_box.y1 || a_box.x1 < 0 || a_box.y1 < 0 || static_cast<std::uint32_t>(a_box.x2) > saved.width ||
                static_cast<std::uint32_t>(a_box.y2) > saved.height) {
                LogRestoreSkipped(std::format("the box x {}..{} y {}..{} is not inside the {}x{} target", a_box.x1, a_box.x2, a_box.y1, a_box.y2,
                    saved.width, saved.height));
                return std::nullopt;
            }
            ComPtr<ID3D11Device> device;
            a_context.GetDevice(device.GetAddressOf());
            if (!device.Get()) {
                AlphaPassOff("the renderer's context has no device");
                return std::nullopt;
            }
            if (!g_alphaPass.built) {
                if (const auto why = BuildAlphaPass(*device.Get()); !why.empty()) {
                    AlphaPassOff(why);
                    return std::nullopt;
                }
            }
            if (const auto why = EnsureScratch(*device.Get(), saved.width, saved.height, typed); !why.empty()) {
                AlphaPassOff(why);
                return std::nullopt;
            }
            const D3D11_BOX box{ static_cast<std::uint32_t>(a_box.x1), static_cast<std::uint32_t>(a_box.y1), 0, static_cast<std::uint32_t>(a_box.x2),
                static_cast<std::uint32_t>(a_box.y2), 1 };
            // the same pixels in the scratch; the source bound as a render target is read the way P8's readback reads it.
            // review v13 #7: a predicated copy may be skipped (the restore would then write stale scratch alpha), so the
            // predicate is off for the copy, as in the restore draw, and set back after it
            ComPtr<ID3D11Predicate> predicate;
            BOOL predicateValue = 0;
            a_context.GetPredication(predicate.GetAddressOf(), &predicateValue);
            if (predicate.Get()) a_context.SetPredication(nullptr, 0);
            a_context.CopySubresourceRegion(g_alphaPass.texture.Get(), 0, box.left, box.top, 0, texture.Get(), mip, &box);
            if (predicate.Get()) a_context.SetPredication(predicate.Get(), predicateValue);
            g_alphaPass.generation = g_state.generation;  // the menu that uses the scratch releases it
            // review v13 #4: this says only that the alpha is saved; whether it was put back is `P8 alpha restored`
            if (!std::exchange(g_state.clip.restoreLogged, true)) {
                logger::info("Preview: A saving the alpha of x {}..{} y {}..{} of the {} {}x{} target before the over-pass Render(), for the restore "
                             "after it (RGB stays as Render() drew it) - menu {}; whether it was put back: `P8 alpha restored`",
                    a_box.x1, a_box.x2, a_box.y1, a_box.y2, FormatName(typed), saved.width, saved.height, g_state.generation);
            }
            return saved;
        }

        // Every context input the restore draw sets, read when it starts and set back exactly when it ends (build 12 G8; the
        // output targets too, spec v13 A2: the draw binds slot 0 alone). The vertex and index buffers are not touched (no
        // input layout, SV_VertexID) and not saved; shaders are saved without class instances (the engine binds none).
        // Review v13 #8: the output-merger UAVs too (binding our RTV unbinds them). They share the 8 pixel-shader output
        // slots with the RTVs and start right after the last RTV, so they are set back with the real RTV count; slots 8 and
        // up of an 11.1 context (64 UAV slots) are not read (D3D11_PS_CS_UAV_REGISTER_COUNT): such a UAV would be lost
        class PassScope
        {
        public:
            explicit PassScope(REX::W32::ID3D11DeviceContext& a_context) :
                _context(a_context)
            {
                // one reference each, released at the end
                _context.OMGetRenderTargetsAndUnorderedAccessViews(kTargetsMost, _views.data(), &_depthView, 0, kUavsMost, _uavs.data());
                for (std::uint32_t slot = 0; slot < kTargetsMost; ++slot) {
                    if (_views[slot]) _viewCount = slot + 1;
                }
                _context.IAGetInputLayout(_layout.GetAddressOf());
                _context.IAGetPrimitiveTopology(&_topology);
                _context.VSGetShader(_vs.GetAddressOf(), nullptr, nullptr);
                _context.PSGetShader(_ps.GetAddressOf(), nullptr, nullptr);
                _context.GSGetShader(_gs.GetAddressOf(), nullptr, nullptr);
                _context.HSGetShader(_hs.GetAddressOf(), nullptr, nullptr);
                _context.DSGetShader(_ds.GetAddressOf(), nullptr, nullptr);
                _context.PSGetShaderResources(0, 1, _srv.GetAddressOf());
                _context.OMGetBlendState(_blend.GetAddressOf(), _factor.data(), &_sampleMask);
                _context.OMGetDepthStencilState(_depth.GetAddressOf(), &_stencilRef);
                _context.RSGetState(_raster.GetAddressOf());
                _context.RSGetViewports(&_viewportCount, nullptr);
                _viewportCount = std::min(_viewportCount, kRectsMost);
                if (_viewportCount > 0) _context.RSGetViewports(&_viewportCount, _viewports.data());
                _context.RSGetScissorRects(&_rectCount, nullptr);
                _rectCount = std::min(_rectCount, kRectsMost);
                if (_rectCount > 0) _context.RSGetScissorRects(&_rectCount, _rects.data());
                _context.GetPredication(_predicate.GetAddressOf(), &_predicateValue);
            }

            ~PassScope()
            {
                _context.SetPredication(_predicate.Get(), _predicateValue);
                _context.RSSetScissorRects(_rectCount, _rectCount > 0 ? _rects.data() : nullptr);
                _context.RSSetViewports(_viewportCount, _viewportCount > 0 ? _viewports.data() : nullptr);
                _context.RSSetState(_raster.Get());
                _context.OMSetDepthStencilState(_depth.Get(), _stencilRef);
                _context.OMSetBlendState(_blend.Get(), _factor.data(), _sampleMask);
                REX::W32::ID3D11ShaderResourceView* const srv[] = { _srv.Get() };
                _context.PSSetShaderResources(0, 1, srv);
                _context.DSSetShader(_ds.Get(), nullptr, 0);
                _context.HSSetShader(_hs.Get(), nullptr, 0);
                _context.GSSetShader(_gs.Get(), nullptr, 0);
                _context.PSSetShader(_ps.Get(), nullptr, 0);
                _context.VSSetShader(_vs.Get(), nullptr, 0);
                _context.IASetPrimitiveTopology(_topology);
                _context.IASetInputLayout(_layout.Get());
                // initial counts -1: the append/consume counters are kept
                std::array<std::uint32_t, kUavsMost> keepCounters;
                keepCounters.fill(static_cast<std::uint32_t>(-1));
                // review v13 round 2 #3: with 8 RTVs the UAV range would start at slot 8 with 0 UAVs (D3D11 debug error
                // 3146060, GPT on WARP FL11_0); with no UAV saved, RTVs and depth only. Unverified: whether OMSetRenderTargets
                // clears a UAV Render() bound in those slots (the old call nulled them); our own pass binds none
                const bool anyUav = std::any_of(_uavs.begin() + _viewCount, _uavs.end(), [](const auto* a_uav) { return a_uav != nullptr; });
                if (_viewCount >= kTargetsMost || !anyUav) {
                    _context.OMSetRenderTargets(_viewCount, _viewCount > 0 ? _views.data() : nullptr, _depthView);
                } else {
                    _context.OMSetRenderTargetsAndUnorderedAccessViews(_viewCount, _viewCount > 0 ? _views.data() : nullptr, _depthView, _viewCount,
                        kUavsMost - _viewCount, _uavs.data() + _viewCount, keepCounters.data());
                }
                for (auto* view : _views) {
                    if (view) view->Release();
                }
                for (auto* uav : _uavs) {
                    if (uav) uav->Release();
                }
                if (_depthView) _depthView->Release();
            }

            PassScope(const PassScope&) = delete;
            PassScope& operator=(const PassScope&) = delete;

        private:
            REX::W32::ID3D11DeviceContext& _context;
            static constexpr auto kUavsMost = static_cast<std::uint32_t>(REX::W32::D3D11_PS_CS_UAV_REGISTER_COUNT);
            static_assert(kUavsMost == kTargetsMost, "the OM UAVs share the RTV slots");
            std::array<REX::W32::ID3D11RenderTargetView*, kTargetsMost> _views{};
            std::uint32_t _viewCount = 0;  // the last bound RTV slot + 1 (review v13 #8)
            std::array<REX::W32::ID3D11UnorderedAccessView*, kUavsMost> _uavs{};
            REX::W32::ID3D11DepthStencilView* _depthView = nullptr;
            REX::W32::ComPtr<REX::W32::ID3D11InputLayout> _layout;
            REX::W32::D3D11_PRIMITIVE_TOPOLOGY _topology{};
            REX::W32::ComPtr<REX::W32::ID3D11VertexShader> _vs;
            REX::W32::ComPtr<REX::W32::ID3D11PixelShader> _ps;
            REX::W32::ComPtr<REX::W32::ID3D11GeometryShader> _gs;
            REX::W32::ComPtr<REX::W32::ID3D11HullShader> _hs;
            REX::W32::ComPtr<REX::W32::ID3D11DomainShader> _ds;
            REX::W32::ComPtr<REX::W32::ID3D11ShaderResourceView> _srv;
            REX::W32::ComPtr<REX::W32::ID3D11BlendState> _blend;
            std::array<float, 4> _factor{};
            std::uint32_t _sampleMask = 0;
            REX::W32::ComPtr<REX::W32::ID3D11DepthStencilState> _depth;
            std::uint32_t _stencilRef = 0;
            REX::W32::ComPtr<REX::W32::ID3D11RasterizerState> _raster;
            std::array<REX::W32::D3D11_VIEWPORT, kRectsMost> _viewports{};
            std::uint32_t _viewportCount = 0;
            std::array<REX::W32::D3D11_RECT, kRectsMost> _rects{};
            std::uint32_t _rectCount = 0;
            REX::W32::ComPtr<REX::W32::ID3D11Predicate> _predicate;
            REX::W32::BOOL _predicateValue = 0;
        };

        // Spec v13 A2: the saved alpha written back over the box of a_saved's target (bound alone in slot 0, no depth view;
        // depth is off). Our SRV is unbound again before the saved inputs go back
        void DrawAlphaRestore(REX::W32::ID3D11DeviceContext& a_context, const SavedAlpha& a_saved, const PreviewMath::PixelBox& a_box)
        {
            using namespace REX::W32;
            const PassScope scope(a_context);
            ID3D11RenderTargetView* const target[] = { a_saved.view.Get() };
            a_context.OMSetRenderTargets(1, target, nullptr);
            a_context.IASetInputLayout(nullptr);
            a_context.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            a_context.VSSetShader(g_alphaPass.vs.Get(), nullptr, 0);
            a_context.PSSetShader(g_alphaPass.ps.Get(), nullptr, 0);
            a_context.GSSetShader(nullptr, nullptr, 0);
            a_context.HSSetShader(nullptr, nullptr, 0);
            a_context.DSSetShader(nullptr, nullptr, 0);
            a_context.SetPredication(nullptr, 0);
            constexpr std::array<float, 4> kFactor{ 1.0f, 1.0f, 1.0f, 1.0f };  // not used with blending off
            a_context.OMSetBlendState(g_alphaPass.blend.Get(), kFactor.data(), D3D11_DEFAULT_SAMPLE_MASK);  // alpha-to-coverage off in the state
            a_context.OMSetDepthStencilState(g_alphaPass.depth.Get(), 0);
            a_context.RSSetState(g_alphaPass.raster.Get());
            const D3D11_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(a_saved.width), static_cast<float>(a_saved.height), 0.0f, 1.0f };
            a_context.RSSetViewports(1, &viewport);
            const D3D11_RECT rect{ a_box.x1, a_box.y1, a_box.x2, a_box.y2 };  // PassScope's end puts the old rects back (review v13 #6)
            a_context.RSSetScissorRects(1, &rect);
            ID3D11ShaderResourceView* const saved[] = { g_alphaPass.srv.Get() };
            a_context.PSSetShaderResources(0, 1, saved);
            a_context.Draw(kTriangleVertices, 0);
            ID3D11ShaderResourceView* const none[] = { nullptr };
            a_context.PSSetShaderResources(0, 1, none);
        }

        // P8's log for one check (spec v11 H1-H3, R2/R3/R8/R9): the boundary state at Render() entry and exit, then the
        // `P8 alpha` and `P8 dark` verdicts over Render() (a_after: read after Render(), before the alpha restore, spec v13
        // A3), or `P8 dark SKIP <reason>` when the box was not read twice. The alpha line comes first: SelfTest writes the
        // buffer through at the dark verdict, which carries it along. Returns the count (nullopt on SKIP) for the restore
        // line logged with it (review v13 #4)
        std::optional<PreviewMath::DarkCount> ReportDarkCheck(const PreviewMath::PixelBox& a_box, std::uint64_t a_frame, RE::FormID a_form, const BoundaryState& a_entry,
            const BoundaryState& a_exit, const BoxRead& a_before, const BoxRead& a_after, bool a_sameTarget)
        {
            SelfTest(std::format("P8 target at Render() entry (PostDisplay {}): {}", a_frame, a_entry.target));
            SelfTest(std::format("P8 blend at Render() entry (boundary state, not per draw): {}", a_entry.blend));
            SelfTest(std::format("P8 target at Render() exit (PostDisplay {}): {}", a_frame, a_exit.target));
            SelfTest(std::format("P8 blend at Render() exit (boundary state, not per draw): {}", a_exit.blend));
            std::string skip;
            std::optional<PreviewMath::DarkCount> count;
            if (!a_before.error.empty()) {
                skip = "before Render(): " + a_before.error;
            } else if (!a_sameTarget) {
                // the second read is of the target bound after Render(): another one is not the same pixels (review v10)
                skip = "Render() left another render target bound";
            } else if (!a_after.error.empty()) {
                skip = "after Render(): " + a_after.error;
            } else if (a_after.layout != a_before.layout) {
                skip = "the target's format changed during Render()";
            } else if (count = PreviewMath::CountDarker(a_before.pixels, a_after.pixels, a_box.x2 - a_box.x1, a_before.layout); !count) {
                skip = "the two reads are not the same whole rows of pixels";
            }
            if (!skip.empty()) {
                SelfTest(std::format("P8 dark SKIP at PostDisplay {}: {}", a_frame, skip));
                return std::nullopt;
            }
            const auto& c = *count;
            const auto& tol = c.tolerance;
            const std::string invalid = std::format("{} pixel(s) with NaN/Inf left out (before NaN {} Inf {}, after NaN {} Inf {})", c.invalid, c.before.nan,
                c.before.inf, c.after.nan, c.after.inf);
            const std::string alpha = !c.hasAlpha ?
                                          std::format("{} has no alpha storage", PreviewMath::LayoutName(a_before.layout)) :
                                          std::format("alpha up {} ({}), down {} ({}), colour unchanged but alpha up {}, largest alpha change {:+.4f}, alpha "
                                                      "before {} after {}, past {:.4f}",
                                              c.alphaUp, BoundsText(c.alphaUpBounds), c.alphaDown, BoundsText(c.alphaDownBounds), c.alphaOnlyUp,
                                              c.largestAlphaChange, RangeText(c.before.alphaMin, c.before.alphaMax),
                                              RangeText(c.after.alphaMin, c.after.alphaMax), tol.alpha);
            SelfTest(std::format("P8 alpha {} at PostDisplay {}: {}; {} (over Render(), before the alpha restore)", PreviewMath::AlphaVerdict(c), a_frame,
                alpha, invalid));
            SelfTest(std::format("P8 dark {} at PostDisplay {} (selection {:08X}, {}): of {} box pixels, {} got darker ({}) and {} brighter in a colour "
                                 "channel over Render(), {}; colour before {} after {}; {}",
                PreviewMath::DarkVerdict(c), a_frame, a_form, PreviewMath::LayoutName(a_before.layout), c.pixels, c.darker, BoundsText(c.darkerBounds),
                c.brighter, PastText(a_before.layout, tol), RangeText(c.before.colourMin, c.before.colourMax),
                RangeText(c.after.colourMin, c.after.colourMax), invalid));
            return count;
        }

        // Spec v13 A3: `P8 alpha restored PASS|FAIL|INCONCLUSIVE` - the box's alpha after the restore against its value before
        // Render(), to one stored step (R10G10B10A2: 1/3); SKIP with the reason when it was not restored or not read. Review
        // v13 #4: logged right after the same frame's `P8 alpha` (a_overRender, its count; nullopt: P8 dark SKIP), and
        // NOTHING_TO_RESTORE, not PASS, when that frame's Render() did not change the alpha - this line is the in-game evidence
        void ReportAlphaRestored(REX::W32::ID3D11DeviceContext& a_context, const PreviewMath::PixelBox& a_box, std::uint64_t a_frame,
            const BoxRead& a_before, const BoxRead& a_afterRender, std::string_view a_notRestored, const std::optional<PreviewMath::DarkCount>& a_overRender)
        {
            std::string skip;
            BoxRead afterRead;
            std::optional<PreviewMath::AlphaMoved> count;
            // review v13 round 2 #4: "Render() changed the alpha" by the stored step, before vs after Render()
            std::optional<bool> changed;
            if (!a_notRestored.empty()) {
                skip = std::string(a_notRestored);
            } else if (!a_overRender) {
                skip = "the same frame's P8 alpha over Render() was not judged (P8 dark SKIP): whether there was anything to restore is not known";
            } else if (!a_before.error.empty()) {
                skip = "before Render(): " + a_before.error;
            } else if (changed = PreviewMath::RenderChangedAlpha(a_before.pixels, a_afterRender.pixels, a_box.x2 - a_box.x1, a_before.layout); !changed) {
                skip = "the reads before and after Render() are not the same whole rows of pixels with alpha";
            } else if (afterRead = ReadBoxPixels(a_context, a_box); !afterRead.error.empty()) {
                skip = "after the restore: " + afterRead.error;
            } else if (afterRead.layout != a_before.layout) {
                skip = "the target's format changed";
            } else if (count = PreviewMath::CountAlphaMoved(a_before.pixels, afterRead.pixels, a_box.x2 - a_box.x1, a_before.layout); !count) {
                skip = "the two reads are not the same whole rows of pixels with alpha";
            }
            if (!skip.empty()) {
                SelfTest(std::format("P8 alpha restored SKIP at PostDisplay {}: {}", a_frame, skip));
                return;
            }
            const auto& c = *count;
            const auto& over = *a_overRender;
            SelfTest(std::format("P8 alpha restored {} at PostDisplay {}: of {} box pixels, {} have an alpha not back on its before-Render() value ({}; "
                                 "step {:.6f}, 0 = exact), {} with NaN/Inf left out; Render() changed the alpha by a stored step: {}; this frame's "
                                 "P8 alpha over Render(): {} ({} up, {} down past its tolerance)",
                PreviewMath::AlphaRestoredVerdict(c, *changed), a_frame, c.pixels, c.moved, BoundsText(c.movedBounds), c.step, c.invalid,
                *changed ? "yes" : "no", PreviewMath::AlphaVerdict(over), over.alphaUp, over.alphaDown));
        }
        // Review v13 #1: an over-pass without the scissor (clip off, the model not fitted yet - zooming, it stands away from
        // the box -, no frame info, or no box in the target) draws wherever the model is, so the whole bound target's alpha
        // is saved and put back, not the box's: Render() is what changes alpha there (the scratch is the target's size
        // either way). Not when nothing is drawn (#4)
        void RenderUnclipped(RE::Inventory3DManager& a_manager)
        {
            auto& clip = g_state.clip;
            auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
            auto* context = renderer ? renderer->GetRuntimeData().context : nullptr;
            std::optional<Target> target;
            std::optional<SavedAlpha> saved;
            PreviewMath::PixelBox whole;
            if (Settings::Get().keepPanelAlpha && !g_alphaPass.off && !REL::Module::IsVR() && Drawable(a_manager) > 0) {
                if (!context) {
                    LogRestoreNoTarget("no renderer context");
                } else if (target = BoundTarget(*context); !target) {
                    LogRestoreNoTarget("no 2D render target bound");
                } else {
                    whole = { 0, 0, static_cast<std::int32_t>(target->width), static_cast<std::int32_t>(target->height) };
                    saved = SaveBoxAlpha(*context, whole);
                }
            }
            a_manager.Render();
            if (!saved) return;
            const auto targetAfter = BoundTarget(*context);
            if (!targetAfter || targetAfter->view != target->view || targetAfter->width != target->width || targetAfter->height != target->height) {
                LogRestoreSkipped(std::format("Render() left {} bound - the saved alpha is of the target bound before it (an over-pass without the "
                                              "scissor)",
                    !targetAfter ? std::string("no render target") : std::format("another render target ({}x{})", targetAfter->width, targetAfter->height)));
                return;
            }
            DrawAlphaRestore(*context, *saved, whole);
            ++clip.restores;
            ++clip.restoresWhole;
            if (!std::exchange(clip.restoreWholeLogged, true)) {
                logger::info("Preview: A over-pass without the scissor at PostDisplay {}: the whole {}x{} target's alpha put back after Render() "
                             "(later such frames counted only)",
                    clip.frame, target->width, target->height);
            }
        }

        // The over-pass Render(), scissored to the box when a_clip. Judged only on a frame that draws a model (with none
        // drawn Render() applies no state and the read-back shows what was there before) and leaves the render target it
        // found (one of another identity or size, or none, means the scissor was in the wrong pixels: not judged, never
        // PASS, review round 1 #3). The first PASS, FAIL and not-judged frame of a menu are logged with everything read
        // back; the rest are counted for the close summary
        // blocks plan Task 3 (D2): a_stageClip is the stage rect the scissor and the alpha save take - the box, the box cut to its
        // column, or (a frame not fitted) the column's viewport; nullopt draws unclipped as before
        // True when it called Render(): false only when the box cut to its column has no pixel (review fix 1 - nothing shows).
        // a_boxCut: a_stageClip is the box cut to its column (a fitted frame with a column), the frames SelfTest L2 judges
        bool RenderClipped(RE::Inventory3DManager& a_manager, const std::optional<FrameInfo>& a_frame, const std::optional<Settings::Rect>& a_stageClip,
            bool a_boxCut)
        {
            auto* shadowState = RE::BSGraphics::RendererShadowState::GetSingleton();
            if (shadowState) {
                MarkDrawStatesDirty(*shadowState);
                if (!std::exchange(g_state.clip.dirtyLogged, true)) {
                    logger::info("Preview: P8 the over-pass marks the engine's blend, alpha test, depth and raster states dirty before Render() (Scaleform "
                                 "changed them on the context)");
                }
            }
            auto& clip = g_state.clip;
            // review v12 #C4 / GPT second check #2: the swap-chain line before any early return (clip off, no frame info)
            if (!REL::Module::IsVR() && !clip.swapChainLogged) {
                if (auto* r = RE::BSGraphics::Renderer::GetSingleton(); r && r->GetRuntimeData().context) {
                    clip.swapChainLogged = true;
                    logger::info("Preview: C swap chain (G10, once per menu): {}", SwapChainCheck(*r->GetRuntimeData().context));
                }
            }
            if (!a_stageClip || !a_frame) {
                RenderUnclipped(a_manager);  // review v13 #1/#2: not a skip, the whole target is restored
                return true;
            }
            auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
            auto* context = renderer ? renderer->GetRuntimeData().context : nullptr;
            std::optional<Target> target;
            std::optional<PreviewMath::PixelBox> box, alone;  // the scissor rect's pixels, and the box's own (the same mapping)
            BoxOnScreen screen;
            const char* skip = nullptr;
            if (!context || !shadowState) {
                skip = "no renderer context or shadow state";
            } else if (target = BoundTarget(*context); !target) {
                skip = "no 2D render target bound";
            } else {
                screen = StagePixels(*a_frame, *a_stageClip);
                box = PreviewMath::ScissorBox(screen.rect, a_frame->bufferW, a_frame->bufferH, target->width, target->height);
                alone = PreviewMath::ScissorBox(StagePixels(*a_frame, a_frame->box).rect, a_frame->bufferW, a_frame->bufferH, target->width,
                    target->height);
                // review fix 1: a box its column cuts to under one pixel (VisibleBox is not empty, ScissorBox rounds it away) is out
                // of view as a wholly scrolled-out one: no Render(), no alpha save. Final review: only when the box alone has pixels -
                // a buffer or target of size 0 (or a sub-pixel box) leaves none either, and that draws unclipped as build 13 did
                if (PreviewMath::PlanClip(box, alone) == PreviewMath::ClipPlan::kNothingShows) {
                    ++clip.columnOut;
                    if (!std::exchange(clip.columnOutLogged, true)) {
                        logger::info("Preview: L2 the preview box has no pixel left in its column's view at PostDisplay {} (scissored to "
                                     "{:.2f},{:.2f}..{:.2f},{:.2f} stage, {:.2f},{:.2f}..{:.2f},{:.2f} px) - nothing drawn, the game's own pass kept "
                                     "hidden (later such frames counted only)",
                            clip.frame, a_stageClip->left, a_stageClip->top, a_stageClip->right, a_stageClip->bottom, screen.rect.left,
                            screen.rect.top, screen.rect.right, screen.rect.bottom);
                    }
                    return false;
                }
                if (!box) skip = "the box has no pixels in the render target (or the viewport's buffer size is 0)";
            }
            // SelfTest L2 preview clip (blocks plan Task 3; final review: a real test). Judged only on a fitted frame whose column cuts
            // the box in pixels: the scissor must differ from the box's own pixels and lie inside the column's, and the column clip
            // the scissor took must be the column's mask as Scaleform reads it back (FrameInfo::columnMask, within half a stage
            // unit). A frame the column does not cut is counted (not judged, never PASS). The first PASS and the first FAIL of a
            // menu are logged, the rest counted for the Q3 summary
            if (box && a_boxCut && a_frame->columnClip) {
                const auto& f = *a_frame;
                const auto& col = *f.columnClip;
                const auto columnBox = PreviewMath::ScissorBox(StagePixels(f, col).rect, f.bufferW, f.bufferH, target->width, target->height);
                const auto verdict = PreviewMath::JudgeColumnClip(*box, alone, columnBox);
                constexpr double kMaskSlack = 0.5;
                const bool maskAgrees = f.columnMask && std::abs(f.columnMask->left - col.left) <= kMaskSlack && std::abs(f.columnMask->top - col.top) <= kMaskSlack &&
                                        std::abs(f.columnMask->right - col.right) <= kMaskSlack && std::abs(f.columnMask->bottom - col.bottom) <= kMaskSlack;
                if (verdict == PreviewMath::ColumnClipVerdict::kNotCut) {
                    ++clip.columnClipNotCut;
                } else {
                    const bool pass = verdict == PreviewMath::ColumnClipVerdict::kPass && maskAgrees;
                    (pass ? clip.columnClipPass : clip.columnClipFail) += 1;
                    if (!std::exchange(pass ? clip.columnClipPassLogged : clip.columnClipFailLogged, true)) {
                        const auto pixels = [](const std::optional<PreviewMath::PixelBox>& a_box) {
                            return a_box ? std::format("{},{}..{},{}", a_box->x1, a_box->y1, a_box->x2, a_box->y2) : std::string("(none in the target)");
                        };
                        SelfTest(std::format("L2 preview clip {} at PostDisplay {}: the column cuts the box - scissor {} {} the box's own pixels {}, {} the "
                                             "column's pixels {}; the column clip {:.1f},{:.1f}..{:.1f},{:.1f} {} the mask read back {}; box (stage) "
                                             "{:.1f},{:.1f}..{:.1f},{:.1f}, scissored to {:.1f},{:.1f}..{:.1f},{:.1f}",
                            pass ? "PASS" : "FAIL", clip.frame, pixels(box), alone && *alone == *box ? "EQUALS" : "differs from", pixels(alone),
                            columnBox && columnBox->Holds(*box) ? "inside" : "NOT inside", pixels(columnBox), col.left, col.top, col.right, col.bottom,
                            maskAgrees ? "matches" : "does NOT match",
                            f.columnMask ? std::format("{:.1f},{:.1f}..{:.1f},{:.1f}", f.columnMask->left, f.columnMask->top, f.columnMask->right,
                                               f.columnMask->bottom) :
                                           std::string("(not read)"),
                            f.box.left, f.box.top, f.box.right, f.box.bottom, a_stageClip->left, a_stageClip->top, a_stageClip->right, a_stageClip->bottom));
                    }
                }
            }
            if (skip) {
                ++clip.clipSkipped;
                if (!std::exchange(clip.clipSkipLogged, true)) {
                    SelfTest(std::format("Q3 clip FAIL - not set at PostDisplay {}: {}; this over-pass draws unclipped (later ones counted only)",
                        clip.frame, skip));
                }
                RenderUnclipped(a_manager);  // review v13 #1/#2: the whole target restored, or counted as no target
                return true;
            }
            const int drawn = Drawable(a_manager);
            auto& shadow = shadowState->GetRuntimeData();
            const auto engineViewport = shadow.viewPort;
            ScissorAfter after;
            std::optional<Target> targetAfter;
            // P8: a while after a selection change (its particles are up), the box's pixels before and after Render(): additive art
            // only brightens, so a pixel that got darker is either a draw that is not additive or one with the wrong blend
            // (review v10: from the delay on, once per selection, so a frame with nothing drawn yet only postpones it). With
            // it, the target and blend state at Render() entry and exit (spec v11 H1/R9; boundary state, not per draw)
            const bool darkCheck = drawn > 0 && clip.darkChecks < kDarkChecksMost && clip.selectionFrame > 0 &&
                                   clip.frame >= clip.selectionFrame + kDarkCheckDelay && clip.darkCheckedFor != clip.selectionFrame;
            // spec v13 A2: the box's alpha saved before Render() (nullopt: bKeepPanelAlpha=0, the restore off, or a target it
            // does not handle, A1). Every frame, not only P8's: the art raises the panel's alpha whenever it is drawn. Not
            // when no model is drawn: Render() changes nothing then (review v13 #4)
            const auto saved = drawn > 0 ? SaveBoxAlpha(*context, *box) : std::nullopt;
            BoundaryState entryState, exitState;
            std::optional<BoxRead> before;
            if (darkCheck) {
                entryState = ReadBoundary(*context);
                before = ReadBoxPixels(*context, *box);
            }
            {
                const ScissorScope scope(*context, shadow, *box);
                a_manager.Render();
                after = scope.ReadBack();
                targetAfter = BoundTarget(*context);
                if (darkCheck) exitState = ReadBoundary(*context);  // before the scope puts the raster state back (blend is not touched)
            }
            const bool sameTarget = targetAfter && targetAfter->view == target->view && targetAfter->width == target->width &&
                                    targetAfter->height == target->height;
            // P8 over Render() reads before the restore (spec v13 A3): the dark / alpha change Render() itself made
            BoxRead afterRender;
            if (darkCheck && sameTarget) afterRender = ReadBoxPixels(*context, *box);
            std::string notRestored = !saved ? "the box's alpha was not saved (bKeepPanelAlpha=0, the restore off, or a target A1 skips)" : "";
            if (saved && sameTarget) {
                DrawAlphaRestore(*context, *saved, *box);
                ++clip.restores;
            } else if (saved) {
                // what Render() drew went to the target it left bound, not to the one whose alpha was saved: nothing put back
                notRestored = "Render() left another render target bound";
                LogRestoreSkipped(std::format("Render() left {} bound - the saved alpha is of the target bound before it",
                    !targetAfter ? std::string("no render target") : std::format("another render target ({}x{})", targetAfter->width, targetAfter->height)));
            }
            if (darkCheck) {
                ++clip.darkChecks;
                clip.darkCheckedFor = clip.selectionFrame;
                const auto overRender = ReportDarkCheck(*box, clip.frame, clip.shown.Form(), entryState, exitState, *before, afterRender, sameTarget);
                if (Settings::Get().keepPanelAlpha && !REL::Module::IsVR()) {
                    ReportAlphaRestored(*context, *box, clip.frame, *before, afterRender, notRestored, overRender);
                }
            }
            ++clip.clipped;
            if (drawn == 0) {
                ++clip.clipUnjudged;
                return true;
            }
            const bool held = after.mode == kScissorOn && after.enabled.value_or(false) && after.sameRect;
            const char* verdict = !sameTarget ? "not judged" : held ? "PASS" : "FAIL";
            (!sameTarget ? clip.clipTargetChanged : held ? clip.clipHeld : clip.clipDropped) += 1;
            if (std::exchange(!sameTarget ? clip.clipTargetLogged : held ? clip.clipLogged : clip.clipFailLogged, true)) return true;
            const auto& frame = *a_frame;
            const auto& rect = screen.rect;
            const std::string targetText = !targetAfter ? "none bound" :
                                           sameTarget   ? "the same" :
                                                          std::format("another one ({}, {}x{}) - the scissor was in the first one's pixels",
                                                              targetAfter->view == target->view ? "the same view, resized" : "another view",
                                                              targetAfter->width, targetAfter->height);
            const char* tail = !sameTarget ? " - not judged: the render target changed during Render() (counted apart, never PASS)" :
                               held        ? " (what Render() left; whether every pixel stayed inside is the video's check)" :
                                             " - the engine dropped the scissor: the over-pass is NOT clipped (hiding the game's pass alone is "
                                             "not a clipping success)";
            SelfTest(std::format("Q3 clip {} at PostDisplay {}: render target {}x{} (viewport buffer {}x{}, movie viewport {:.0f},{:.0f} {:.0f}x{:.0f} px, "
                                 "engine viewport {:.0f},{:.0f} {:.0f}x{:.0f}), box px {:.1f}..{:.1f} x {:.1f}..{:.1f} ({}) -> scissor {},{}..{},{}; "
                                 "read back after Render(): shadow scissor mode {} (set {}), D3D11 raster scissorEnable {}, scissor rect {}, render "
                                 "target {}; {} model(s) drawn{}",
                verdict, clip.frame, target->width, target->height, frame.bufferW, frame.bufferH, frame.viewport.left, frame.viewport.top,
                frame.viewport.right - frame.viewport.left, frame.viewport.bottom - frame.viewport.top, engineViewport.topLeftX,
                engineViewport.topLeftY, engineViewport.width, engineViewport.height, rect.left, rect.right, rect.top, rect.bottom, screen.source,
                box->x1, box->y1, box->x2, box->y2, after.mode, kScissorOn,
                after.enabled ? (*after.enabled ? "yes" : "no") : "no state bound (default: off)",
                after.sameRect ? "the same" :
                                 std::format("changed ({} rect(s), first {},{}..{},{})", after.rects, after.rect.x1, after.rect.y1, after.rect.x2,
                                     after.rect.y2),
                targetText, drawn, tail));
            return true;
        }

        // Which Q3 parts run (once per menu, at its first over-pass). Both follow place/fit: without it the model stands at
        // the game's own place, away from the box, and would be hidden from the game's pass and cut away by the scissor
        // (review round 1, #2)
        struct Modes
        {
            bool hide = false, clip = false;
        };

        const char* ModesOff()
        {
            const auto& settings = Settings::Get();
            return REL::Module::IsVR()     ? "Skyrim VR" :
                   g_irActive              ? "Inventory Reforged runs its panel in this menu" :
                   !settings.movePreview   ? "bMovePreview=0: the model is not in the box" :
                                             nullptr;
        }

        Modes ModesOf(bool a_overPass)
        {
            const auto& settings = Settings::Get();
            const bool allowed = a_overPass && !ModesOff();
            return { allowed && settings.hideGamePass, allowed && settings.clipPreview };
        }

        void LogModes(const Modes& a_modes)
        {
            const char* off = ModesOff();
            logger::info("Preview: Q3 this menu - hide the selection's model from the game's own pass {} ({}), scissor the over-pass to the box {} ({})",
                a_modes.hide ? "ON" : "off",
                a_modes.hide ? "hidden after the over-pass, given back before the next" : off ? off : "bHideGamePass=0", a_modes.clip ? "ON" : "off",
                a_modes.clip ? "engine shadow state + D3D11 scissor rect, put back after Render(); off on a frame the model is not fitted" :
                off          ? off :
                               "bClipPreview=0");
        }

        // ---------------------------------------------------------------------
        // Menu open / close (main thread)
        // ---------------------------------------------------------------------

        void Reset(std::uint64_t a_generation)
        {
            if (!Current(a_generation)) return;
            // what an earlier menu hid and its close did not give back (its task may not have run on this manager)
            if (auto* manager = RE::Inventory3DManager::GetSingleton()) {
                GiveBackAll(*manager, "a new menu opened, before its reset");
                GiveBackTab(*manager, "a new menu opened, before its reset", true);
            }
            g_state.clip = Clip{};
            g_state.generation = a_generation;
            g_state.placedFor.reset();
            g_state.room.reset();
            g_state.probed = g_state.waitLogged = false;
            g_state.measures = g_state.hiddenLines = 0;
            g_state.logged = g_state.hidden = Tally{};
            g_state.lastLoaded = 0;
            // the per-model cache stays: its entries leave with their models (Forget); a new menu measures them afresh
            // (Entry, by generation), and first-render marks are per menu
        }

        // The close of a menu this state was reset for: what was measured, every P1 verdict counted (also those whose
        // line was not logged, R4), and how many models were up (R5)
        void Summarize(std::uint64_t a_generation)
        {
            if (g_state.generation != a_generation || g_irActive || !Settings::Get().movePreview) return;
            const auto models = std::ranges::count_if(g_state.models, [&](const Model& a_entry) { return a_entry.generation == a_generation && a_entry.measures > 0; });
            std::size_t now = 0;
            if (auto* manager = RE::Inventory3DManager::GetSingleton()) {
                for (const auto& loaded : manager->GetRuntimeData().loadedModels) now += loaded.spModel ? 1 : 0;
            }
            const int fails = g_state.logged.fail + g_state.hidden.fail, passes = g_state.logged.pass + g_state.hidden.pass;
            // from the counts: with bSelfTest=0 measures run but nothing is tested (second review I3)
            const char* verdict = g_state.measures == 0 ? "(no measure)" : !Settings::Get().selfTest ? "(not tested: bSelfTest=0)" :
                                  fails > 0             ? "FAIL" :
                                  passes > 0            ? "PASS" :
                                                          "BOUND";
            const auto line = std::format("P1 summary {} - menu {}: {} measures of {} models ({} measure lines not logged); P1 verdicts "
                                          "logged PASS {} FAIL {} BOUND {}, not logged PASS {} FAIL {} BOUND {}; loadedModels {} at the last "
                                          "fitted frame, {} at close; cache {} entries",
                verdict, a_generation, g_state.measures, models, g_state.hiddenLines, g_state.logged.pass, g_state.logged.fail,
                g_state.logged.bound, g_state.hidden.pass, g_state.hidden.fail, g_state.hidden.bound, g_state.lastLoaded, now,
                g_state.models.size());
            if (Settings::Get().selfTest) {
                SelfTest(line);
            } else {
                logger::info("Preview: {}", line);
            }
            FlushLog();
        }

        // The close of a menu: every over-pass's hide and clip counted, judged apart (review round 2, #2: a hide success
        // made the whole summary PASS while the clip was never judged). hide: off (this menu's mode), FAIL when a model
        // we hid was given back by something else, PASS when one held, "-" when none was judged. clip: off, FAIL when
        // the scissor was dropped or not set, PASS only when it held at least once and never failed, NOT JUDGED
        // otherwise (a frame whose render target changed, or that drew nothing, is never a PASS)
        void SummarizeClip(std::uint64_t a_generation)
        {
            const auto& clip = g_state.clip;
            if (g_state.generation != a_generation || clip.overPasses == 0) return;
            const bool tested = Settings::Get().selfTest;
            const char* hide = !clip.hideOn                          ? "off" :
                               !tested                               ? "(not tested)" :
                               clip.hideFail > 0 || clip.lost > 0    ? "FAIL" :
                               clip.hidePass > 0                     ? "PASS" :
                                                                       "-";
            const char* scissor = !clip.clipOn                                  ? "off" :
                                  !tested                                       ? "(not tested)" :
                                  clip.clipDropped > 0 || clip.clipSkipped > 0  ? "FAIL" :
                                  clip.clipHeld > 0                             ? "PASS" :
                                                                                  "NOT JUDGED";
            const auto line = std::format("Q3 summary hide {}, clip {} - menu {}: {} over-passes in {} PostDisplays; hide: {} hides, held through the game's pass "
                                          "{} times, given back by something else {} times (hide PASS {} FAIL {}), {} changes to a selection's "
                                          "model ({} of them drawn unclipped by the game's pass that frame), {} given back on being selected "
                                          "again, {} not given back (the game showed another model of the form), {} earlier-selection models "
                                          "left hidden, {} models built anew at a known address, {} frames drawing two or more models of one "
                                          "form; clip: {} over-passes scissored, held {}, dropped {}, render target changed {} (not judged), "
                                          "nothing drawn {} (not judged), not set {}, not fitted {} (drawn without the box scissor; {} of them cut to "
                                          "the column's viewport); column: {} frames out of view (nothing drawn), L2 preview clip PASS {} FAIL {} "
                                          "(not cut by the column {}, not judged); "
                                          "anim moved {}, still {}, not judged {}",
                hide, scissor, a_generation, clip.overPasses, clip.frame, clip.hides, clip.held, clip.lost, clip.hidePass, clip.hideFail, clip.sights,
                clip.flashes, clip.again, clip.replaced, clip.left, clip.rebuilt, clip.doubles, clip.clipped, clip.clipHeld, clip.clipDropped,
                clip.clipTargetChanged, clip.clipUnjudged, clip.clipSkipped, clip.clipUnfitted, clip.columnOnly, clip.columnOut, clip.columnClipPass,
                clip.columnClipFail, clip.columnClipNotCut, clip.animMoved, clip.animStill, clip.animNone);
            if (tested) {
                SelfTest(line);
            } else {
                logger::info("Preview: {}", line);
            }
            FlushLog();
        }

        // The close of the menu generation that saved the originals puts them back, once
        void Restore(std::uint64_t a_generation)
        {
            if (!g_state.saved || g_state.saved->generation != a_generation) return;
            if (auto* manager = RE::Inventory3DManager::GetSingleton()) {
                manager->itemPos = g_state.saved->pos;
                manager->itemPosCopy = g_state.saved->posCopy;
                manager->itemScale = g_state.saved->scale;
                manager->itemScaleCopy = g_state.saved->scaleCopy;
                logger::info("Preview: game preview place restored ({:.2f},{:.2f},{:.2f}, scale {:.3f}, menu {})", manager->itemPos.x,
                    manager->itemPos.y, manager->itemPos.z, manager->itemScale, a_generation);
            }
            g_state.saved.reset();
        }
    }

    void OnMenuOpen(bool a_irActive)
    {
        const auto generation = ++g_generation;
        g_closing = false;
        g_irActive = a_irActive;
        if (REL::Module::IsVR()) {
            static bool logged = false;
            if (!std::exchange(logged, true)) logger::info("Preview: Skyrim VR - the preview is left alone (VR's loadedModels layout differs)");
            return;
        }
        // whether place/fit runs this menu (and why not) is logged once by Widget ("preview place/fit ON|off")
        OnMainThread([generation] { Reset(generation); });
    }

    void OnFrame(const FrameInfo& a_frame)
    {
        if (g_closing) return;
        std::scoped_lock lock(g_slot.lock);
        g_slot.frame = a_frame;
        g_slot.generation = g_generation.load();
    }

    // The order the spec sets (v5, "Changes from the GPT design review"): give back -> place/fit -> scissor -> Render
    // -> scissor restored -> hide (the scissor only when this frame's place/fit succeeded). FitAll skips hidden models,
    // so the give-back comes first; the hide comes after our Render, so the game's next own pass (before the next movie)
    // has nothing of the selection to draw
    bool OnPostDisplay(RE::Inventory3DManager& a_manager, bool a_fit, bool a_overPass, bool a_active, bool a_noBox, bool a_hide3D, bool a_blockHidden)
    {
        if (!GameThread::On()) {
            static std::atomic<bool> logged{ false };  // g_state is main-thread only
            if (!logged.exchange(true)) {
                logger::warn("Preview: PostDisplay ran off the main thread ({}) - nothing placed, hidden or clipped", REX::W32::GetCurrentThreadId());
            }
            if (!a_overPass || !a_active) return false;
            a_manager.Render();
            return true;
        }
        auto& clip = g_state.clip;
        const auto generation = g_generation.load();
        const bool current = g_state.generation == generation;  // this menu's reset ran
        // the latest selection first, on every path (review round 1, #5)
        const auto frame = current ? TakeFrame(generation) : std::nullopt;
        if (current) {
            ++clip.frame;
            if (frame) {
                const auto was = clip.shown.Form();
                clip.shown = { frame->selected, frame->treeSpell };
                clip.shown.art = ArtOf(clip.shown.Form());
                if (clip.shown.Form() != was) clip.artPending = clip.selectionFrame = clip.frame;
            }
        }
        if (current && !REL::Module::IsVR()) {
            clip.shown.formUp = std::ranges::any_of(a_manager.GetRuntimeData().loadedModels,
                [&](const auto& a_loaded) { return a_loaded.spModel && clip.shown.Has(a_loaded.itemBase); });
        }
        // a few frames after a selection change (the game's load has had time): what the models up are, for the art match
        if (current && !REL::Module::IsVR() && clip.artPending > 0 && clip.frame - clip.artPending >= kArtLogDelay) {
            clip.artPending = 0;
            LogSelectionArt(a_manager, clip.shown);
        }
        // the touches below read loadedModels (VR's layout differs) and change nothing while Inventory Reforged runs its panel
        const bool ours = current && !REL::Module::IsVR() && !g_irActive;
        // P7.1, on every tab: the art is the same model. Review v13 round 2 #1: not on a call made only for the Powers hide
        // (bMovePreview=0 and bDrawPreviewOver=0), where nothing else of ours ran before round 2
        if (ours && (a_fit || a_overPass)) HideArt(a_manager);
        // review v13 round 2 #2: the Powers hide ends (this menu's movie, a tab shown, not Powers now; Powers at the last such
        // call). The close gives everything back itself
        const bool ourCall = a_active || a_noBox;
        const bool powersHideEnds = current && ourCall && !g_closing && clip.powersHid && !a_hide3D;
        if (current && ourCall && !g_closing) clip.powersHid = a_hide3D;
        // not active (the preview box hidden - nothing chosen, the Powers hide -, the call for another movie). At the boundary the
        // observation history ends (review round 2, #3): back on a tab with the box, the selection's model counts as a
        // change again, and a model the game showed meanwhile is not taken for a lost hide (everHidden keeps the rest).
        // P7.2 (spec v7, replacing build 5's keep-the-hide): on that tab the game's own place comes back (our place kept
        // moving the model there, so the 3D floated over the game view, video 6 48.5-58.5 s) and the selection's model we
        // hid is given back, so the game's pass draws it as without us; an earlier selection's model we hid stays hidden
        // (the same assumption as at every selection change) until the close. Place/fit waits for a tab with the box.
        if (!a_active || g_closing) {
            if (current && !std::exchange(clip.inactive, true)) {
                const auto held = clip.hidden.size();
                const int back = ours ? GiveBack(a_manager, clip.shown) : 0;
                if (held > 0 && !std::exchange(clip.inactiveLogged, true)) {
                    logger::info("Preview: Q3 the preview box is not shown at PostDisplay {} - of {} model(s) hidden at the last over-pass, {} given "
                                 "back (of this tab's selection {:08X}); the rest are an earlier selection's and stay hidden until the close",
                        clip.frame, held, back, clip.shown.Form());
                }
                clip.hidden.clear();
                clip.shownLast.clear();
                if (ours && !g_closing) Unplace(a_manager, "the preview box is not shown", false);
            }
            // this menu without the box (nothing chosen, the Powers hide; Active Effects until build 15): no 3D at all there -
            // the game's own pass drew the effect over the game view at its own place (game run 2026-10-26 01:06; build 16 gives
            // Active Effects the box instead, its full-screen flash per click: ledger "Build 15 game run"); every model up is hidden while it lasts
            // and given back when the box shows again or the menu closes (OnMenuClose)
            // bMovePreview=0 (the way back) changes nothing here either (reviewer: Preview.h promises it), except on the
            // Powers tab (a_hide3D): no 3D there whatever bMovePreview says (spec v13 B1, review v13 #3)
            // fix round 1 (GPT P2): a preview block hidden by [Blocks] sHidden counts as bMovePreview=0 here (PreviewMath::HidesForTab)
            const bool hideForTab = current && ours && a_noBox && !g_closing && PreviewMath::HidesForTab(Settings::Get().movePreview, a_blockHidden, a_hide3D);
            // review v13 round 2 #2: Powers -> a state without the box with bMovePreview=0: no tab hide follows here, so what Powers hid
            // is given back now (both tabs are not active: the give-back below is never reached); with bMovePreview=1 the
            // tab hide goes on and its give-back stays the one below
            if (powersHideEnds && ours && !hideForTab) GiveBackTab(a_manager, "the Powers tab hide ends on a tab without the box", true);
            if (hideForTab) HideForTab(a_manager);
            return false;
        }
        // review v13 round 2 #1: on a call only for the Powers hide (no place/fit, no over-pass) no Q3 follows to give the
        // models handed to it back, so every one is given back here
        if (current && ours) GiveBackTab(a_manager, "the preview box shows again", !a_fit && !a_overPass);
        if (current) clip.inactive = false;
        const auto modes = ModesOf(a_overPass);
        if (current && a_overPass && !std::exchange(clip.modesSet, true)) {
            clip.hideOn = modes.hide;
            clip.clipOn = modes.clip;
            LogModes(modes);
        }
        // P7.2: the models' own transforms, before anything of ours moves them this frame
        if (ours && a_fit && Settings::Get().movePreview) RecordOriginals(a_manager);
        // before this menu's reset the state is an earlier menu's, which its close already gave back
        if (current && modes.hide) Watch(a_manager, clip.shown);
        if (current) GiveBack(a_manager, clip.shown);
        const char* unfitted = a_fit ? Fit(a_manager, a_overPass, generation, frame) : "place/fit is off";
        if (!a_overPass) return false;
        if (current) {
            ++clip.overPasses;
            if (modes.hide) CountDoubles(a_manager);
        }
        // blocks plan Task 3 (D2): the preview column's viewport cuts the scissor, fitted or not. None of the box in its column
        // (scrolled out): no Render() and no alpha save; the game's own pass stays hidden (below), the models stay placed
        const bool column = current && modes.clip && frame && frame->columnClip;
        const auto visibleBox = column ? Layout::VisibleBox(frame->box, *frame->columnClip) : std::nullopt;
        if (column && !visibleBox) {
            ++clip.columnOut;
            if (!std::exchange(clip.columnOutLogged, true)) {
                logger::info("Preview: L2 the preview box is out of its column's view at PostDisplay {} (box {:.0f},{:.0f}..{:.0f},{:.0f}, column "
                             "{:.0f},{:.0f}..{:.0f},{:.0f}) - nothing drawn, the game's own pass kept hidden (later such frames counted only)",
                    clip.frame, frame->box.left, frame->box.top, frame->box.right, frame->box.bottom, frame->columnClip->left, frame->columnClip->top,
                    frame->columnClip->right, frame->columnClip->bottom);
            }
            if (modes.hide) HideSelection(a_manager, clip.shown);
            return false;
        }
        // the box scissor only on a frame the model was fitted to the box: zooming, not placed yet or no frame info would cut a
        // model away from the box (review round 1, #2) - such a frame is cut to its column's viewport alone (D2)
        std::optional<Settings::Rect> scissor;
        if (current && modes.clip && frame) {
            if (!unfitted) {
                scissor = column ? *visibleBox : frame->box;
            } else if (column) {
                scissor = *frame->columnClip;
                ++clip.columnOnly;
            }
        }
        if (current && modes.clip && unfitted) {
            ++clip.clipUnfitted;
            if (!std::exchange(clip.unfittedLogged, true)) {
                logger::info("Preview: Q3 over-pass at PostDisplay {} drawn without the box scissor - {}{} (later such frames counted only)", clip.frame,
                    unfitted, column ? "; scissored to the preview column's viewport (blocks plan D2)" : "");
            }
        }
        bool rendered = false;
        {
            // P7.3: only the selection's model in the over-pass, the others' flags back right after (needs this frame's selection)
            std::optional<OnlySelection> only;
            if (ours && frame) only.emplace(a_manager, clip.shown);
            rendered = RenderClipped(a_manager, frame, scissor, column && !unfitted);
            if (only) LogOnlySelection(*only, clip.shown);
        }
        if (current && modes.hide && frame) HideSelection(a_manager, clip.shown);
        return rendered;
    }

    void OnMenuClose()
    {
        g_closing = true;
        const auto closed = g_generation++;  // the generation that closes; its frames and queued work are dropped from here on
        OnMainThread([closed] {
            // every model we hid that is still up and hidden, whatever the selection: no over-pass follows (review round 2, #4)
            if (g_state.generation == closed) {
                if (auto* manager = RE::Inventory3DManager::GetSingleton()) {
                    GiveBackAll(*manager, "the menu closed");
                    GiveBackTab(*manager, "the menu closed", true);
                }
            }
            // P7.2: the models back at the game's place too (Restore alone reset only the manager's values), so a model kept
            // into the next menu starts from the game's place; then our references to them go (review build 7 #4, GPT #4)
            if (g_state.generation == closed && !REL::Module::IsVR()) {
                if (auto* manager = RE::Inventory3DManager::GetSingleton()) Unplace(*manager, "the menu closed", true);
                g_state.clip.originals.clear();
                g_state.clip.artLogged.clear();
            }
            if (const auto& c = g_state.clip;
                g_state.generation == closed && (c.restores > 0 || c.restoreLogged || c.restoreSkipped > 0 || c.restoreNoTarget > 0)) {
                // review v13 #2: the frames with no target to save apart from the skips
                logger::info("Preview: A alpha restore - menu {}: {} over-pass(es) restored ({} of them the whole target, no scissor), {} skipped, "
                             "{} with no target to save; scratch created {} time(s){}; scratch released",
                    closed, c.restores, c.restoresWhole, c.restoreSkipped, c.restoreNoTarget, c.scratchCreated,
                    g_alphaPass.off ? " (now off for the session)" : "");
            }
            // spec v13 A2: the scratch texture and its SRV go at the close of the menu that used them (made again at the next
            // over-pass; review v13 #6);
            // review v12 #C5: not when a newer menu has used them already (its own close releases them)
            if (g_alphaPass.generation == closed) ReleaseScratch();
            Summarize(closed);
            SummarizeClip(closed);

            SummarizeP7(closed);
            Restore(closed);
        });
    }
}

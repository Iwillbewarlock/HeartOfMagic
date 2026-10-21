// =============================================================================
// GreySelfTestLantern - --self-test for --write-lantern (GreyLantern.h) on made-up sources (no game files)
// =============================================================================
// A frame NIF (BSFadeNode, BSXFlags, collision, one lit BSTriShape with a hook out in +Y at the top), a skeleton with
// NPC Pelvis [Pelv], an atronach stand-in whose FireStrip08 (palette effect shader, float controller) and FireStripWarp
// (lighting shader, float controller) are skinned half and half to two bones (each strip a box plus a stray shorter box),
// a 64 x 64 BC3 diffuse with 7 mips, and body stand-ins (a hip box). The build runs on them; the outputs are read back
// and checked (strap and its leather shader, bracket, ring, upright handle, vertical lantern, one strip piece, bind pose,
// the Vertex_Colors flag, clearances), then the failures and edge cases: a missing source or body, a thigh that lengthens
// the bracket, one no length up to the maximum clears, a body part through the strap, a wrong inverse bind, an addon node
// with the wrong value, a darken factor out of range. LanternPieces' weld and LanternAxisTiltDeg have their own checks.

#include "GreyLantern.h"
#include "GreySelfTest.h"
#include "GreyShapes.h"

#include <Animation.hpp>
#include <ExtraData.hpp>
#include <Geometry.hpp>
#include <NifFile.hpp>
#include <Nodes.hpp>
#include <Shaders.hpp>
#include <Skin.hpp>
#include <bhk.hpp>

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <memory>
#include <unordered_map>

namespace GreyBuild::SelfTest
{
    namespace
    {
        constexpr unsigned kDxgiBc3 = 77;  // DXGI_FORMAT_BC3_UNORM
        constexpr float kTolerance = 1e-3f;
        constexpr float kFrameHalfX = 9.0f;
        constexpr float kFrameHalfY = 8.0f;
        constexpr float kFrameHeight = 30.0f;
        constexpr float kHookY = 15.0f;   // the hook's reach in +Y at the top (the band rule must leave it out)
        constexpr float kPelvisZ = 68.9f;
        constexpr float kHipHalfX = 8.0f;  // the body stand-in's hip box: x -8..8
        constexpr std::uint32_t kFrameBsx = 0xC2;  // candlelantern01.nif's value
        constexpr const char* kSourceNormal = "textures\\clutter\\common\\lampgeneric01_n.dds";
        constexpr const char* kOrangePalette = "textures\\effects\\gradients\\gradfireatronach.dds";
        constexpr std::uint16_t kAlphaNormalBlend = 0x10ED;

        using nifly::MatTransform;
        using nifly::Vector3;

        [[nodiscard]] bool Near(float a_left, float a_right, float a_tolerance = kTolerance) noexcept
        {
            return std::fabs(a_left - a_right) <= a_tolerance;
        }

        /// A closed box from a_lo to a_hi appended to a_verts / a_tris
        void AddBox(std::vector<Vector3>& a_verts, std::vector<nifly::Triangle>& a_tris, const Vector3& a_lo, const Vector3& a_hi)
        {
            const auto base = static_cast<std::uint16_t>(a_verts.size());
            for (int i = 0; i < 8; ++i) {
                a_verts.emplace_back((i & 1) ? a_hi.x : a_lo.x, (i & 2) ? a_hi.y : a_lo.y, (i & 4) ? a_hi.z : a_lo.z);
            }
            constexpr std::uint16_t kFaces[12][3] = { { 0, 1, 3 }, { 0, 3, 2 }, { 4, 6, 7 }, { 4, 7, 5 }, { 0, 4, 5 }, { 0, 5, 1 },
                { 2, 3, 7 }, { 2, 7, 6 }, { 0, 2, 6 }, { 0, 6, 4 }, { 1, 5, 7 }, { 1, 7, 3 } };
            for (const auto& face : kFaces) {
                a_tris.emplace_back(static_cast<std::uint16_t>(base + face[0]), static_cast<std::uint16_t>(base + face[1]),
                    static_cast<std::uint16_t>(base + face[2]));
            }
        }

        /// a_extra: a second, disconnected and shorter box above (the strips' stray piece the build must leave out)
        nifly::NiShape* MakeShape(nifly::NifFile& a_nif, const std::string& a_name, const Vector3& a_lo, const Vector3& a_hi,
            bool a_hook, bool a_extra = false)
        {
            std::vector<Vector3> verts;
            std::vector<nifly::Triangle> tris;
            AddBox(verts, tris, a_lo, a_hi);
            if (a_extra) {
                AddBox(verts, tris, Vector3(-5.0f, -3.0f, a_hi.z + 10.0f), Vector3(5.0f, 3.0f, a_hi.z + 20.0f));
            }
            if (a_hook) {
                // two rings of the cage walls at a third and two thirds of the height, and the hook out in +Y at the top
                for (const float share : { 1.0f / 3.0f, 2.0f / 3.0f }) {
                    const float z = a_lo.z + (a_hi.z - a_lo.z) * share;
                    AddBox(verts, tris, Vector3(a_lo.x, a_lo.y, z - 0.5f), Vector3(a_hi.x, a_hi.y, z + 0.5f));
                }
                AddBox(verts, tris, Vector3(-1.0f, a_hi.y, a_hi.z - 3.0f), Vector3(1.0f, kHookY, a_hi.z - 1.0f));
            }
            std::vector<nifly::Vector2> uvs(verts.size());
            auto* shape = a_nif.CreateShapeFromData(a_name, &verts, &tris, &uvs);
            if (!shape) {
                throw std::runtime_error("nifly made no shape");
            }
            auto* root = a_nif.GetRootNode();
            const std::uint32_t id = a_nif.GetHeader().GetBlockID(shape);
            bool listed = false;
            for (const auto& child : root->childRefs) {
                listed = listed || child.index == id;
            }
            if (!listed) {
                root->childRefs.AddBlockRef(id);
            }
            return shape;
        }

        void MakeFrame(const std::filesystem::path& a_file)
        {
            nifly::NifFile nif;
            nif.Create(nifly::NiVersion::getSSE());
            auto& hdr = nif.GetHeader();
            (void)hdr.ReplaceBlock(0, std::make_unique<nifly::BSFadeNode>());
            auto* root = nif.GetRootNode();
            root->name.get() = "CandleLantern01";
            auto bsx = std::make_unique<nifly::BSXFlags>();
            bsx->name.get() = "BSX";
            bsx->integerData = kFrameBsx;
            root->extraDataRefs.AddBlockRef(hdr.AddBlock(std::move(bsx)));
            root->collisionRef.index = hdr.AddBlock(std::make_unique<nifly::bhkCollisionObject>());
            auto* shape = MakeShape(nif, "CandleLantern01:11", Vector3(-kFrameHalfX, -kFrameHalfY, 0.0f),
                Vector3(kFrameHalfX, kFrameHalfY, kFrameHeight), true);
            auto* shader = hdr.GetBlock<nifly::NiShader>(shape->ShaderPropertyRef());
            auto* set = shader ? hdr.GetBlock<nifly::BSShaderTextureSet>(shader->TextureSetRef()) : nullptr;
            if (!set) {
                throw std::runtime_error("nifly's shape has no texture set");
            }
            set->textures[0].get() = kLanternDiffuseSource;
            set->textures[1].get() = kSourceNormal;
            shape->SetVertexColors(true);
            if (auto* lit = dynamic_cast<nifly::BSShaderProperty*>(shader)) {
                lit->shaderFlags2 |= kSlsf2VertexColors;
            }
            SaveNif(nif, a_file);
        }

        void MakeSkeleton(const std::filesystem::path& a_file)
        {
            nifly::NifFile nif;
            nif.Create(nifly::NiVersion::getSSE());
            MatTransform com;
            com.translation = Vector3(0.0f, 0.0f, kPelvisZ);
            auto* comNode = nif.AddNode("NPC COM [COM ]", com, nif.GetRootNode());
            (void)nif.AddNode(kLanternBone, MatTransform(), comNode);
            SaveNif(nif, a_file);
        }

        void AddFloatController(nifly::NifFile& a_nif, nifly::NiShader& a_shader, std::unique_ptr<nifly::NiFloatInterpController> a_ctrl)
        {
            auto& hdr = a_nif.GetHeader();
            auto data = std::make_unique<nifly::NiFloatData>();
            nifly::NiAnimationKey<float> key;
            key.time = 0.0f;
            key.value = 0.0f;
            data->data.AddKey(key);
            key.time = 1.0f;
            key.value = 1.0f;
            data->data.AddKey(key);
            const std::uint32_t dataId = hdr.AddBlock(std::move(data));
            auto interp = std::make_unique<nifly::NiFloatInterpolator>();
            interp->dataRef.index = dataId;
            const std::uint32_t interpId = hdr.AddBlock(std::move(interp));
            a_ctrl->interpolatorRef.index = interpId;
            a_ctrl->targetRef.index = hdr.GetBlockID(&a_shader);
            a_shader.controllerRef.index = hdr.AddBlock(std::move(a_ctrl));
        }

        /// The stand-in Flame Atronach: two bones and the two strips skinned half and half to them (body part 32)
        void MakeAtronach(const std::filesystem::path& a_file)
        {
            nifly::NifFile nif;
            nif.Create(nifly::NiVersion::getSSE());
            auto& hdr = nif.GetHeader();
            auto* root = nif.GetRootNode();
            MatTransform pelvis;
            pelvis.translation = Vector3(0.0f, 0.0f, 70.0f);
            MatTransform spine;
            spine.translation = Vector3(0.0f, 0.0f, 90.0f);
            const std::uint32_t boneA = hdr.GetBlockID(nif.AddNode("FireAtronach_Pelvis [Pelv]", pelvis, root));
            const std::uint32_t boneB = hdr.GetBlockID(nif.AddNode("FireAtronach_Spine [Spn0]", spine, root));
            for (const char* name : kLanternFlameStrips) {
                const bool effect = std::string(name) == kLanternFlameStrips[0];
                auto* shape = MakeShape(nif, name, Vector3(-20.0f, -10.0f, 0.0f), Vector3(20.0f, 6.0f, 130.0f), false, true);
                if (effect) {
                    nif.DeleteShader(shape);
                    auto prop = std::make_unique<nifly::BSEffectShaderProperty>();
                    prop->shaderFlags1 |= kSlsf1Skinned | kSlsf1GreyscaleToPaletteColor | kSlsf1GreyscaleToPaletteAlpha;
                    prop->sourceTexture.get() = "textures\\effects\\fxfirescrolltile02.dds";
                    prop->greyscaleTexture.get() = kOrangePalette;
                    shape->ShaderPropertyRef()->index = hdr.AddBlock(std::move(prop));
                    auto alpha = std::make_unique<nifly::NiAlphaProperty>();
                    alpha->flags = kAlphaNormalBlend;
                    (void)nif.AssignAlphaProperty(shape, std::move(alpha));
                }
                auto* shader = hdr.GetBlock<nifly::NiShader>(shape->ShaderPropertyRef());
                if (effect) {
                    AddFloatController(nif, *shader, std::make_unique<nifly::BSEffectShaderPropertyFloatController>());
                } else {
                    if (auto* lit = dynamic_cast<nifly::BSShaderProperty*>(shader)) {
                        lit->shaderFlags1 |= kSlsf1Skinned;
                    }
                    AddFloatController(nif, *shader, std::make_unique<nifly::BSLightingShaderPropertyFloatController>());
                }
                nif.CreateSkinning(shape);
                std::vector<int> bones{ static_cast<int>(boneA), static_cast<int>(boneB) };
                nif.SetShapeBoneIDList(shape, bones);
                nif.SetShapeTransformGlobalToSkin(shape, MatTransform());
                nif.SetShapeTransformSkinToBone(shape, 0, pelvis.InverseTransform());
                nif.SetShapeTransformSkinToBone(shape, 1, spine.InverseTransform());
                std::unordered_map<std::uint16_t, float> half;
                for (std::uint16_t i = 0; i < shape->GetNumVertices(); ++i) {
                    std::vector<std::uint8_t> ids{ 0, 1 };
                    std::vector<float> weights{ 0.5f, 0.5f };
                    nif.SetShapeVertWeights(name, i, ids, weights);
                    half[i] = 0.5f;
                }
                nif.SetShapeBoneWeights(name, 0, half);
                nif.SetShapeBoneWeights(name, 1, half);
                nif.SetDefaultPartition(shape);
                nif.UpdateSkinPartitions(shape);
            }
            SaveNif(nif, a_file);
        }

        /// A body stand-in: one unskinned box shape per lo..hi pair
        void MakeBody(const std::filesystem::path& a_file, const std::vector<std::pair<Vector3, Vector3>>& a_boxes)
        {
            nifly::NifFile nif;
            nif.Create(nifly::NiVersion::getSSE());
            int n = 0;
            for (const auto& [lo, hi] : a_boxes) {
                (void)MakeShape(nif, fmt::format("Body{}", n++), lo, hi, false);
            }
            SaveNif(nif, a_file);
        }

        /// Both body stand-ins (male and female paths) as the same boxes
        void MakeBodies(const std::filesystem::path& a_dir, const std::vector<std::pair<Vector3, Vector3>>& a_boxes)
        {
            for (const char* body : kLanternBodySources) {
                MakeBody(a_dir / body, a_boxes);
            }
        }

        [[nodiscard]] bool Mentions(const std::vector<std::string>& a_lines, const std::string& a_text)
        {
            return std::any_of(a_lines.begin(), a_lines.end(), [&](const auto& a_line) { return a_line.find(a_text) != std::string::npos; });
        }

        [[nodiscard]] const LanternShapeInfo* Shape(const LanternNifInfo& a_info, const std::string& a_name)
        {
            const auto it = std::find_if(a_info.shapes.begin(), a_info.shapes.end(), [&](const auto& a_s) { return a_s.name == a_name; });
            return it == a_info.shapes.end() ? nullptr : &*it;
        }
    }

    void CheckLanternRuns(Checker& a_check, const std::filesystem::path& a_dir);

    /// LanternAxisTiltDeg: a pure yaw, scale and move reads 0, a 10 degree lean reads 10 (also with a yaw on top), the handle
    /// above kLanternAxisTopShare may move freely, different vertex counts throw
    void CheckLanternAxis(Checker& a_check)
    {
        std::vector<Vector3> ground;
        std::vector<nifly::Triangle> tris;
        AddBox(ground, tris, Vector3(-9.0f, -8.0f, 0.0f), Vector3(9.0f, 8.0f, 30.0f));
        AddBox(ground, tris, Vector3(-9.0f, -8.0f, 9.5f), Vector3(9.0f, 8.0f, 10.5f));
        AddBox(ground, tris, Vector3(-1.0f, 8.0f, 27.0f), Vector3(1.0f, 15.0f, 29.0f));  // above the top share: the handle
        const auto through = [&](float a_leanDeg, float a_yawDeg, float a_scale, const Vector3& a_move, bool a_moveHandle) {
            constexpr float kRad = 3.14159265358979f / 180.0f;
            std::vector<Vector3> out;
            for (const auto& v : ground) {
                Vector3 p = v;
                if (a_moveHandle && p.z > 30.0f * kLanternAxisTopShare) {
                    p = p + Vector3(3.0f, -7.0f, 2.0f);
                }
                const float cl = std::cos(a_leanDeg * kRad), sl = std::sin(a_leanDeg * kRad);
                p = Vector3(p.x * cl + p.z * sl, p.y, -p.x * sl + p.z * cl);
                const float cy = std::cos(a_yawDeg * kRad), sy = std::sin(a_yawDeg * kRad);
                p = Vector3(p.x * cy - p.y * sy, p.x * sy + p.y * cy, p.z);
                out.push_back(p * a_scale + a_move);
            }
            return out;
        };
        const float still = LanternAxisTiltDeg(ground, through(0.0f, -90.0f, 0.63f, Vector3(-20.0f, 3.0f, 48.0f), true));
        a_check.Check(Near(still, 0.0f, 0.01f), fmt::format("lantern axis: a yaw, scale and move (and a moved handle) read 0 deg ({:.4f})", still));
        const float lean = LanternAxisTiltDeg(ground, through(10.0f, 0.0f, 1.0f, Vector3(), false));
        const float leanYaw = LanternAxisTiltDeg(ground, through(10.0f, 37.0f, 2.0f, Vector3(5.0f, 5.0f, 5.0f), false));
        a_check.Check(Near(lean, 10.0f, 0.01f) && Near(leanYaw, 10.0f, 0.01f),
            fmt::format("lantern axis: a 10 deg lean reads 10 ({:.4f}; with a yaw, scale and move {:.4f})", lean, leanYaw));
        bool threw = false;
        try {
            (void)LanternAxisTiltDeg(ground, std::vector<Vector3>(ground.begin(), ground.end() - 1));
        } catch (const std::runtime_error&) {
            threw = true;
        }
        a_check.Check(threw, "lantern axis: different vertex counts throw");
    }

    /// LanternPieces' weld: two triangles touching only through a vertex pair within kLanternWeld are one piece, also when
    /// the pair straddles a grid cell boundary; a pair two weld distances apart stays two pieces; a bad index throws
    void CheckLanternPieces(Checker& a_check)
    {
        const auto twoTriangles = [](float a_left, float a_right) {
            std::vector<Vector3> verts{ Vector3(a_left, 0.0f, 0.0f), Vector3(-1.0f, 1.0f, 0.0f), Vector3(-1.0f, 0.0f, 1.0f),
                Vector3(a_right, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 0.0f), Vector3(1.0f, 0.0f, 1.0f) };
            std::vector<nifly::Triangle> tris{ nifly::Triangle(0, 1, 2), nifly::Triangle(3, 4, 5) };
            return LanternPieces(verts, tris);
        };
        // 0.99 and 1.01 weld cells: floor puts them in cells 0 and 1, 0.02 weld apart
        const auto straddle = twoTriangles(kLanternWeld * 0.99f, kLanternWeld * 1.01f);
        a_check.Check(straddle.size() == 1 && straddle.front().triangles.size() == 2,
            fmt::format("lantern pieces: a vertex pair within the weld across a cell boundary joins ({} pieces)", straddle.size()));
        const auto same = twoTriangles(0.0f, 0.0f);
        a_check.Check(same.size() == 1 && same.front().vertices.size() == 6, "lantern pieces: a pair at the same position joins");
        const auto apart = twoTriangles(0.0f, kLanternWeld * 2.0f);
        a_check.Check(apart.size() == 2 && apart[0].triangles.size() == 1 && apart[1].triangles.size() == 1,
            fmt::format("lantern pieces: a pair two weld distances apart stays two pieces ({})", apart.size()));
        bool threw = false;
        try {
            (void)LanternPieces({ Vector3() }, { nifly::Triangle(0, 0, 5) });
        } catch (const std::runtime_error&) {
            threw = true;
        }
        a_check.Check(threw, "lantern pieces: a triangle naming a vertex past the list throws");
    }

    void CheckLantern(Checker& a_check, const std::filesystem::path& a_dir)
    {
        try {
            CheckLanternPieces(a_check);
            CheckLanternAxis(a_check);
            CheckLanternRuns(a_check, a_dir);
        } catch (const std::exception& e) {
            a_check.Check(false, fmt::format("lantern: exception {}", e.what()));
        }
    }

    void CheckLanternRuns(Checker& a_check, const std::filesystem::path& a_dir)
    {
        const auto in = a_dir / "lantern" / "in";
        const auto out = a_dir / "lantern" / "out";
        MakeFrame(in / kLanternFrameSource);
        MakeSkeleton(in / kLanternSkeletonSource);
        MakeAtronach(in / kLanternFlameSource);
        a_check.Check(WriteTestTexture(in / kLanternDiffuseSource, kDxgiBc3, true), "lantern: made-up sources written");

        // the bodies: a hip box whose left side is at x -kHipHalfX (both body paths)
        const auto bodies = a_dir / "lantern" / "bodies";
        MakeBodies(bodies, { { Vector3(-kHipHalfX, -6.0f, 30.0f), Vector3(kHipHalfX, 6.0f, 100.0f) } });
        const std::vector<std::filesystem::path> inputs{ in, bodies };

        const auto result = BuildLantern(inputs, out);
        for (const auto& problem : result.problems) {
            std::cout << "      " << problem << "\n";
        }
        a_check.Check(result.ok, fmt::format("lantern: the build passes its own read-back ({} problem(s))", result.problems.size()));
        {
            const auto noBody = BuildLantern({ in }, a_dir / "lantern" / "out_nobody");
            a_check.Check(!noBody.ok && Mentions(noBody.problems, kLanternBodySources[0]),
                "lantern: the worn model needs the bodies (a missing body fails, named)");
        }

        // the ground model
        const auto gnd = InspectLanternNif(out / kLanternGroundNif);
        const auto* frame = gnd.shapes.empty() ? nullptr : &gnd.shapes.front();
        a_check.Check(gnd.shapes.size() == 1 && frame && frame->type == "BSTriShape" && !frame->textures.empty() &&
                          frame->textures.front() == fmt::format("[0] {}", kLanternDiffuse),
            fmt::format("lantern gnd: one frame shape whose diffuse is '{}'", kLanternDiffuse));
        a_check.Check(frame && frame->textures.size() > 1 && frame->textures[1] == fmt::format("[1] {}", kSourceNormal),
            "lantern gnd: the normal map kept as the source names it");
        a_check.Check(gnd.hasCollision && gnd.rootType == "BSFadeNode", "lantern gnd: collision and the BSFadeNode root kept");
        a_check.Check(gnd.hasBsx && gnd.bsx == (kFrameBsx | kBsxAddon), fmt::format("lantern gnd: BSXFlags {} = source | Addon", gnd.bsx));
        const bool gndNode = gnd.valueNodes.size() == 1 && gnd.valueNodes.front().name == "AddOnNode9100" &&
                             gnd.valueNodes.front().value == kLanternFlameAddon && gnd.valueNodes.front().flags == kLanternFlameValueFlags &&
                             gnd.valueNodes.front().parent == gnd.rootName;
        a_check.Check(gndNode, "lantern gnd: exactly one BSValueNode named 'AddOnNode9100' with value 9100, under the root");
        a_check.Check(gndNode && Near(gnd.valueNodes.front().translation[0], 0.0f) && Near(gnd.valueNodes.front().translation[1], 0.0f) &&
                          Near(gnd.valueNodes.front().translation[2], kFrameHeight * 0.5f),
            fmt::format("lantern gnd: the addon node at the cage centre (0, 0, {}), the hook at +Y left out (at {})", kFrameHeight * 0.5f,
                gndNode ? fmt::format("{}, {}, {}", gnd.valueNodes.front().translation[0], gnd.valueNodes.front().translation[1],
                              gnd.valueNodes.front().translation[2]) : std::string("-")));
        a_check.Check(Near(result.frameBounds.max[1], kHookY) && Near(result.frameBounds.Size(2), kFrameHeight),
            "lantern gnd: the frame bounds reported with the hook, for the record's ObjectBounds");

        // the worn model
        const auto worn = InspectLanternNif(out / kLanternWornNif);
        a_check.Check(worn.shapes.size() == 6 && Shape(worn, kLanternWornFrameName) && Shape(worn, "FireStrip08") &&
                          Shape(worn, "FireStripWarp") && Shape(worn, kLanternStrapName) && Shape(worn, kLanternBracketName) &&
                          Shape(worn, kLanternRingName),
            fmt::format("lantern worn: the frame, both strips, the strap, the bracket and the ring ({} shapes)", worn.shapes.size()));
        bool allOneBone = !worn.shapes.empty();
        bool allPart60 = !worn.shapes.empty();
        bool allWeights = !worn.shapes.empty();
        bool allSkinnedFlag = !worn.shapes.empty();
        for (const auto& s : worn.shapes) {
            allOneBone = allOneBone && s.skinType == "BSDismemberSkinInstance" && s.bones.size() == 1 && s.bones.front() == std::string("NPC Pelvis [Pelv]");
            allPart60 = allPart60 && s.partitions.size() == 1 && s.partitions.front() == 60;
            allWeights = allWeights && s.allWeightsOne && s.weightedOnFirstBone == s.vertices && s.skinDataWeights == s.vertices;
            allSkinnedFlag = allSkinnedFlag && s.skinned && (s.flags1 & kSlsf1Skinned) != 0;
        }
        a_check.Check(allOneBone, "lantern worn: every shape skinned to exactly one bone, NPC Pelvis [Pelv], by a BSDismemberSkinInstance");
        a_check.Check(allPart60, "lantern worn: every shape has one partition, body part 60");
        a_check.Check(allWeights, "lantern worn: every vertex weight 1 on the pelvis, none on another bone (vertex data and NiSkinData; "
                                  "the source strips were half and half)");
        a_check.Check(allSkinnedFlag, "lantern worn: vertex skinned flag and shader Skinned flag on every shape");
        a_check.Check(worn.nodeNames.size() == 2 && worn.nodeNames[0] == kLanternWornRootName && worn.nodeNames[1] == kLanternBone &&
                          worn.valueNodes.empty() && !worn.hasCollision,
            fmt::format("lantern worn: nodes are the root and the bone alone ({}), no addon node, no collision", worn.nodeNames.size()));
        const auto* fire = Shape(worn, "FireStrip08");
        const auto* warp = Shape(worn, "FireStripWarp");
        a_check.Check(fire && fire->textures.size() > 1 && fire->textures[1] == std::string("greyscale ") + kPalettePath,
            "lantern worn: FireStrip08's palette is GradBlackFlame.dds");
        a_check.Check(fire && warp && fire->controllers.size() == 1 && warp->controllers.size() == 1 &&
                          fire->controllers[0].find("its shader") != std::string::npos &&
                          warp->controllers[0].find("its shader") != std::string::npos,
            "lantern worn: each strip's float controller copied and aimed at its own shader");
        const auto* wornFrame = Shape(worn, kLanternWornFrameName);
        const float scale = kLanternWornHeight / kFrameHeight;
        // the hook (z 27..29, out to y kHookY) turned up about its hinge (y kFrameHalfY, z 28): its top at z 28 + (kHookY - 8)
        const float turnedTop = (kFrameHeight - 2.0f) + (kHookY - kFrameHalfY);
        a_check.Check(Near(result.wornScale, scale) && Near(result.bracketLength, kLanternBracketLength),
            fmt::format("lantern worn: scale {:.5f} = {} / {}, the bracket at its first length beside the hip box ({}, kLanternBracketLength {})",
                result.wornScale, kLanternWornHeight, kFrameHeight, result.bracketLength, kLanternBracketLength));
        {
            const float tilt = WornAxisTiltDeg(out);
            a_check.Check(tilt <= kLanternVerticalToleranceDeg && Near(tilt, 0.0f, 0.01f),
                fmt::format("lantern worn: the cage axis read back is world Z ({:.4f} deg, tolerance {})", tilt, kLanternVerticalToleranceDeg));
        }
        a_check.Check(Near(result.wornCage[0], result.contact[0]) && Near(result.wornCage[1], result.contact[1]),
            fmt::format("lantern worn: the cage centre hangs straight below the handle's top wire (cage {:.3f}, {:.3f}; contact {:.3f}, {:.3f}: "
                        "the hinge moved onto the cage axis)", result.wornCage[0], result.wornCage[1], result.contact[0], result.contact[1]));
        a_check.Check(wornFrame && Near(wornFrame->bounds.Size(2), turnedTop * scale) &&
                          Near(wornFrame->bounds.max[2], result.contact[2] + result.handleWire),
            fmt::format("lantern worn: the handle stands up (frame {:.3f} tall, expected {:.3f}) and its top wire is the contact (top {:.3f}, "
                        "contact + wire {:.3f})", wornFrame ? wornFrame->bounds.Size(2) : 0.0f, turnedTop * scale,
                wornFrame ? wornFrame->bounds.max[2] : 0.0f, result.contact[2] + result.handleWire));
        a_check.Check(Near(result.contact[0], result.ringCentre[0]) && Near(result.contact[1], result.ringCentre[1]) &&
                          Near(result.contact[2], result.ringCentre[2] - kLanternRingRadius + kLanternRingTube + result.handleWire),
            "lantern worn: the handle's top wire rests on the ring's bottom tube, inside the ring");
        const auto* strap = Shape(worn, kLanternStrapName);
        const auto* ring = Shape(worn, kLanternRingName);
        const auto* bracket = Shape(worn, kLanternBracketName);
        const float tipZ = kLanternStrapBottom - kLanternBracketBelow - kLanternBracketDrop;  // the ring's top tube centre
        a_check.Check(strap && ring && Near(strap->bounds.max[2], kLanternStrapTop) && Near(strap->bounds.min[2], kLanternStrapBottom) &&
                          Near(strap->bounds.Size(1), kLanternStrapWidth) && strap->bounds.max[0] <= -kHipHalfX - kLanternStrapBodyGap + kTolerance &&
                          Near(ring->bounds.max[2], tipZ + kLanternRingTube),
            fmt::format("lantern worn: the strap from z {} to {} ({} wide) outside the hip at x {}, the ring's top tube at the bracket's tip",
                kLanternStrapBottom, kLanternStrapTop, kLanternStrapWidth, -kHipHalfX));
        // the strap's outer layer centreline at its lower end: the hip at -kHipHalfX, the inner layer kLanternStrapBodyGap + half
        // the thickness off it, the outer layer a thickness and kLanternStrapBeltGap further out
        const float outerX = -kHipHalfX - kLanternStrapBodyGap - kLanternStrapThickness * 1.5f - kLanternStrapBeltGap;
        a_check.Check(Near(result.strapOuterBottomX, outerX) && Near(result.ringCentre[0], outerX - kLanternBracketLength) &&
                          Near(result.ringCentre[2], tipZ - kLanternRingRadius),
            fmt::format("lantern worn: the ring centre kLanternBracketLength {} out from the strap's outer layer (x {:.3f} -> {:.3f}) at z {:.3f}",
                kLanternBracketLength, result.strapOuterBottomX, result.ringCentre[0], result.ringCentre[2]));
        a_check.Check(bracket && Near(bracket->bounds.max[2], kLanternStrapBottom + kLanternBracketGrip) && Near(bracket->bounds.min[2], tipZ) &&
                          Near(bracket->bounds.Size(1), kLanternBracketWidth) &&
                          Near(bracket->bounds.max[0], outerX + kLanternBracketThickness * 0.5f) &&
                          Near(bracket->bounds.min[0], result.ringCentre[0] - kLanternBracketThickness * 0.5f),
            fmt::format("lantern worn: the bracket from its plate in the strap's lower end (z {}) out to the ring and down to its tube (z {})",
                kLanternStrapBottom + kLanternBracketGrip, tipZ));
        a_check.Check(bracket && bracket->textures.size() > 1 && bracket->textures[0] == fmt::format("[0] {}", kLanternDiffuse) &&
                          bracket->textures[1] == fmt::format("[1] {}", kSourceNormal),
            "lantern worn: the bracket on the soot lamp texture and the lamp's normal map");
        a_check.Check(bracket && ring && bracket->uvMin[0] >= kLanternIronUvMin[0] - kTolerance && bracket->uvMin[1] >= kLanternIronUvMin[1] - kTolerance &&
                          bracket->uvMax[0] <= kLanternIronUvMax[0] + kTolerance && bracket->uvMax[1] <= kLanternIronUvMax[1] + kTolerance &&
                          ring->uvMin[0] >= kLanternIronUvMin[0] - kTolerance && ring->uvMin[1] >= kLanternIronUvMin[1] - kTolerance &&
                          ring->uvMax[0] <= kLanternIronUvMax[0] + kTolerance && ring->uvMax[1] <= kLanternIronUvMax[1] + kTolerance,
            fmt::format("lantern worn: the bracket's and the ring's UVs in the iron box u {}..{} v {}..{}", kLanternIronUvMin[0],
                kLanternIronUvMax[0], kLanternIronUvMin[1], kLanternIronUvMax[1]));
        a_check.Check(strap && Near(strap->glossiness, kLanternStrapGlossiness) && Near(strap->specularStrength, kLanternStrapSpecularStrength) &&
                          Near(strap->specularColor[0], kLanternStrapSpecularColor[0]) && Near(strap->specularColor[1], kLanternStrapSpecularColor[1]) &&
                          Near(strap->specularColor[2], kLanternStrapSpecularColor[2]),
            fmt::format("lantern worn: the strap's shader has the vanilla leather specular (glossiness {}, strength {})",
                strap ? strap->glossiness : -1.0f, strap ? strap->specularStrength : -1.0f));
        {
            const auto* frameShape = Shape(worn, kLanternWornFrameName);
            bool madeCleared = strap && bracket && ring;
            for (const auto* made : { strap, bracket, ring }) {
                madeCleared = madeCleared && !made->vertexColors && (made->flags2 & kSlsf2VertexColors) == 0;
            }
            a_check.Check(madeCleared && frameShape && frameShape->vertexColors && (frameShape->flags2 & kSlsf2VertexColors) != 0,
                "lantern worn: Vertex_Colors cleared on the made shapes (no colours), kept on the frame (colours)");
        }
        a_check.Check(strap && ring && strap->textures.size() > 1 && strap->textures[0] == fmt::format("[0] {}", kLanternStrapDiffuse) &&
                          strap->textures[1] == fmt::format("[1] {}", kLanternStrapNormal) && ring->textures.size() > 1 &&
                          ring->textures[0] == fmt::format("[0] {}", kLanternDiffuse) && ring->textures[1] == fmt::format("[1] {}", kSourceNormal),
            "lantern worn: strap on the vanilla leather and normal map, ring on the soot lamp texture and the lamp's normal map");
        a_check.Check(fire && warp && fire->vertices == 8 && warp->vertices == 8,
            fmt::format("lantern worn: one connected piece of each strip kept, the stray box left out ({} / {} vertices, expected 8)",
                fire ? fire->vertices : 0, warp ? warp->vertices : 0));
        a_check.Check(warp && Near(warp->refraction, kLanternWarpRefraction, 1e-6f),
            fmt::format("lantern worn: FireStripWarp's refraction strength {} (kLanternWarpRefraction)", warp ? warp->refraction : -1.0f));
        if (fire && warp) {
            LanternBox strips = fire->bounds;
            for (int axis = 0; axis < 3; ++axis) {
                strips.min[axis] = std::min(strips.min[axis], warp->bounds.min[axis]);
                strips.max[axis] = std::max(strips.max[axis], warp->bounds.max[axis]);
            }
            const auto centre = strips.Centre();
            a_check.Check(Near(centre[0], result.wornCage[0]) && Near(centre[1], result.wornCage[1]) && Near(centre[2], result.wornCage[2]),
                fmt::format("lantern worn: the strips centred in the cage ({:.3f}, {:.3f}, {:.3f}; cage {:.3f}, {:.3f}, {:.3f})", centre[0],
                    centre[1], centre[2], result.wornCage[0], result.wornCage[1], result.wornCage[2]));
        } else {
            a_check.Check(false, "lantern worn: the strips centred in the cage (no strips)");
        }
        bool allBind = !worn.shapes.empty();
        for (const auto& s : worn.shapes) {
            allBind = allBind && s.bindRead && s.bindError <= kLanternBindTolerance && s.sphereError <= kLanternBoundsTolerance &&
                      s.vertexError <= kLanternBoundsTolerance;
        }
        a_check.Check(allBind, "lantern worn: on every shape the pelvis bind pose x skin-to-bone is the identity and the bone-space "
                               "bounds and vertices land back on the skin-space ones");
        {
            const auto clearances = WornClearances(inputs, out);
            bool clear = clearances.size() == 8;
            std::string text;
            for (const auto& c : clearances) {
                clear = clear && c.gap.points > 0 && c.gap.inside == 0 && c.gap.nearest >= c.margin;
                text += fmt::format(" {} {:.3f};", c.part, c.gap.nearest);
            }
            a_check.Check(clear, fmt::format("lantern worn: lantern, strap, bracket and ring clear of both bodies by their margins ({})", text));
        }

        // the smoke static
        const auto smoke = InspectLanternNif(out / kLanternSmokeNif);
        a_check.Check(smoke.rootType == "BSFadeNode" && smoke.shapes.empty() && smoke.hasBsx && smoke.bsx == kLanternSmokeBsx,
            fmt::format("lantern smoke: an empty BSFadeNode with BSXFlags {}", kLanternSmokeBsx));
        a_check.Check(smoke.valueNodes.size() == 1 && smoke.valueNodes.front().name == "AddOnNode9123" &&
                          smoke.valueNodes.front().value == kLanternSmokeAddon && smoke.valueNodes.front().parent == kLanternSmokeRootName,
            "lantern smoke: exactly one BSValueNode named 'AddOnNode9123' with value 9123, a child of the root");

        // the texture
        double sourceMean = 0.0;
        double copyMean = 0.0;
        std::size_t sw = 0, sh = 0, sm = 0, cw = 0, ch = 0, cm = 0;
        std::string sf, cf;
        const bool read =
            DdsMean(in / kLanternDiffuseSource, sourceMean, sw, sh, sm, sf) && DdsMean(out / kLanternDiffuse, copyMean, cw, ch, cm, cf);
        a_check.Check(read && cw == sw && ch == sh && cm == sm && cf == sf,
            fmt::format("lantern texture: same size, format and mips as the source ({}x{} format {} {} mips)", cw, ch, cf, cm));
        a_check.Check(read && copyMean < sourceMean &&
                          Near(static_cast<float>(copyMean), static_cast<float>(sourceMean) * kLanternSootDarken, 0.02f),
            fmt::format("lantern texture: darker on average ({:.4f} -> {:.4f}, factor {})", sourceMean, copyMean, kLanternSootDarken));

        // failures
        {
            const auto empty = a_dir / "lantern" / "empty";
            std::filesystem::create_directories(empty);
            const auto missing = BuildLantern({ empty }, a_dir / "lantern" / "out_missing");
            a_check.Check(!missing.ok && Mentions(missing.problems, kLanternFrameSource) && Mentions(missing.problems, kLanternDiffuseSource),
                "lantern: missing sources fail, each named");
        }
        {
            // a thigh box beside the hanging cage: the bracket grows until the lantern clears it, and the read-back agrees. The
            // cage's inboard edge is the ring x + 8 x 20/30 (the frame's half depth, yawed, scaled): -10.4 - L + 5.33; with the
            // thigh's side at -11.4 it clears by 1.0 from L 7.33, so the loop lands on 7.5 (7.0 misses by 0.43)
            const auto thigh = a_dir / "lantern" / "bodies_thigh";
            MakeBodies(thigh, { { Vector3(-kHipHalfX, -6.0f, 30.0f), Vector3(kHipHalfX, 6.0f, 100.0f) },
                                  { Vector3(-11.4f, -20.0f, 0.0f), Vector3(kHipHalfX, 20.0f, 56.0f) } });
            const auto longer = BuildLantern({ in, thigh }, a_dir / "lantern" / "out_longer");
            a_check.Check(longer.ok && Near(longer.bracketLength, 7.5f) && Mentions(longer.log, "bracket length 7 (") &&
                              WornAxisTiltDeg(a_dir / "lantern" / "out_longer") <= kLanternVerticalToleranceDeg,
                fmt::format("lantern: a thigh beside the cage lengthens the bracket in kLanternBracketLengthStep steps (to {}, expected 7.5), "
                            "the lantern still vertical, and the build's read-back passes", longer.bracketLength));
            // a thigh reaching far out: no length up to kLanternBracketLengthMax clears it
            const auto wide = a_dir / "lantern" / "bodies_wide";
            MakeBodies(wide, { { Vector3(-kHipHalfX, -6.0f, 30.0f), Vector3(kHipHalfX, 6.0f, 100.0f) },
                                 { Vector3(-60.0f, -30.0f, 0.0f), Vector3(kHipHalfX, 30.0f, 60.0f) } });
            const auto clash = BuildLantern({ in, wide }, a_dir / "lantern" / "out_clash");
            a_check.Check(!clash.ok && Mentions(clash.problems, "at every bracket length") &&
                              Mentions(clash.log, fmt::format("bracket length {} (", kLanternBracketLengthMax)),
                "lantern: a body no bracket length up to the maximum clears fails the build, each length tried and logged");
            // a body part across the hip window's edge (x pelvis - kLanternHipWindow) in the strap's band: the strap placed on its
            // window-clipped side ends up inside it, which the clearance check must catch
            const auto deep = a_dir / "lantern" / "bodies_deep";
            MakeBodies(deep, { { Vector3(-kHipHalfX, -6.0f, 30.0f), Vector3(kHipHalfX, 6.0f, 100.0f) },
                                 { Vector3(-30.0f, 0.0f, 76.0f), Vector3(-kLanternHipWindow + 1.0f, 6.0f, 80.0f) } });
            const auto buried = BuildLantern({ in, deep }, a_dir / "lantern" / "out_deep");
            a_check.Check(!buried.ok && Mentions(buried.problems, "the strap comes within"),
                "lantern: a body part through the strap fails the build");
        }
        {
            // a wrong inverse bind on the frame: the read-back refuses it
            const auto badBind = a_dir / "lantern" / "out_badbind";
            std::filesystem::remove_all(badBind);
            std::filesystem::copy(out, badBind, std::filesystem::copy_options::recursive);
            nifly::NifFile bad;
            LoadNif(bad, badBind / kLanternWornNif);
            for (auto* shape : bad.GetShapes()) {
                if (shape->name.get() == kLanternWornFrameName) {
                    bad.SetShapeTransformSkinToBone(shape, 0, MatTransform());
                }
            }
            SaveNif(bad, badBind / kLanternWornNif);
            a_check.Check(Mentions(VerifyLantern(inputs, badBind), "bind pose x skin-to-bone"),
                "lantern: the read-back refuses a skin-to-bone transform that is not the bone's inverse bind");
        }
        {
            // the worn frame leaned 5 deg about Y round its first vertex: the read-back's vertical check refuses it
            const auto leaned = a_dir / "lantern" / "out_lean";
            std::filesystem::remove_all(leaned);
            std::filesystem::copy(out, leaned, std::filesystem::copy_options::recursive);
            nifly::NifFile bad;
            LoadNif(bad, leaned / kLanternWornNif);
            constexpr float kLeanDeg = 5.0f;
            const float c = std::cos(kLeanDeg * 3.14159265358979f / 180.0f);
            const float s = std::sin(kLeanDeg * 3.14159265358979f / 180.0f);
            for (auto* shape : bad.GetShapes()) {
                auto* tri = dynamic_cast<nifly::BSTriShape*>(shape);
                if (shape->name.get() == kLanternWornFrameName && tri && !tri->vertData.empty()) {
                    const Vector3 pivot = tri->vertData.front().vert;
                    for (auto& vertex : tri->vertData) {
                        const Vector3 d = vertex.vert - pivot;
                        vertex.vert = pivot + Vector3(d.x * c + d.z * s, d.y, -d.x * s + d.z * c);
                    }
                }
            }
            SaveNif(bad, leaned / kLanternWornNif);
            const float tilt = WornAxisTiltDeg(leaned);
            a_check.Check(Near(tilt, kLeanDeg, 0.01f) && Mentions(VerifyLantern(inputs, leaned), "leans"),
                fmt::format("lantern: the read-back refuses a worn frame leaned {} deg off world Z (read {:.4f})", kLeanDeg, tilt));
        }
        {
            nifly::NifFile bad;
            LoadNif(bad, out / kLanternSmokeNif);
            for (std::uint32_t id = 0; id < bad.GetHeader().GetNumBlocks(); ++id) {
                if (auto* node = bad.GetHeader().GetBlock<nifly::BSValueNode>(id)) {
                    node->value = kLanternSmokeAddon + 1;
                }
            }
            SaveNif(bad, out / kLanternSmokeNif);
            a_check.Check(Mentions(VerifyLantern({ in }, out), "smoke: expected one BSValueNode AddOnNode9123"),
                "lantern: the read-back refuses an addon node whose value is not its index");
        }
        a_check.Check(!DarkenDds(in / kLanternDiffuseSource, a_dir / "lantern" / "x.dds", 1.5f).ok,
            "lantern: a darken factor above 1 is refused");
    }
}

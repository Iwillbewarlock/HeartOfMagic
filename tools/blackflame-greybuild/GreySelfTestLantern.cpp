// =============================================================================
// GreySelfTestLantern - --self-test for --write-lantern (GreyLantern.h) on made-up sources (no game files)
// =============================================================================
// A frame NIF (BSFadeNode, BSXFlags, collision, one lit BSTriShape with a hook out in +Y at the top), a skeleton with
// NPC Pelvis [Pelv], an atronach stand-in whose FireStrip08 (palette effect shader, float controller) and FireStripWarp
// (lighting shader, float controller) are skinned half and half to two bones, and a 64 x 64 BC3 diffuse with 7 mips.
// The build runs on them; the outputs are read back and checked, then the failures: a missing source, a body inside the
// worn frame, an addon node with the wrong value, a darken factor out of range.

#include "GreyLantern.h"
#include "GreySelfTest.h"

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

        nifly::NiShape* MakeShape(nifly::NifFile& a_nif, const std::string& a_name, const Vector3& a_lo, const Vector3& a_hi,
            bool a_hook)
        {
            std::vector<Vector3> verts;
            std::vector<nifly::Triangle> tris;
            AddBox(verts, tris, a_lo, a_hi);
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
                auto* shape = MakeShape(nif, name, Vector3(-20.0f, -10.0f, 0.0f), Vector3(20.0f, 6.0f, 130.0f), false);
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

        /// A body stand-in: one unskinned box at a_lo..a_hi
        void MakeBody(const std::filesystem::path& a_file, const Vector3& a_lo, const Vector3& a_hi)
        {
            nifly::NifFile nif;
            nif.Create(nifly::NiVersion::getSSE());
            (void)MakeShape(nif, "Body", a_lo, a_hi, false);
            SaveNif(nif, a_file);
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

    void CheckLantern(Checker& a_check, const std::filesystem::path& a_dir)
    {
        try {
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

        const auto result = BuildLantern({ in }, out);
        for (const auto& problem : result.problems) {
            std::cout << "      " << problem << "\n";
        }
        a_check.Check(result.ok, fmt::format("lantern: the build passes its own read-back ({} problem(s))", result.problems.size()));
        a_check.Check(Mentions(result.log, "not checked"), "lantern: no body in the --in folders is logged as not checked");

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
        a_check.Check(worn.shapes.size() == 3 && Shape(worn, kLanternWornFrameName) && Shape(worn, "FireStrip08") &&
                          Shape(worn, "FireStripWarp"),
            fmt::format("lantern worn: the frame and both strips ({} shapes)", worn.shapes.size()));
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
        const float top = kPelvisZ + kLanternWornHipOffset[2];
        a_check.Check(wornFrame && Near(wornFrame->bounds.Size(2), kLanternWornHeight) && Near(wornFrame->bounds.max[2], top) &&
                          Near(result.wornScale, kLanternWornHeight / kFrameHeight),
            fmt::format("lantern worn: the frame {} units tall, its top at the pelvis + offset (z {})", kLanternWornHeight, top));
        const float cageZ = top - kFrameHeight * 0.5f * result.wornScale;
        if (fire && warp) {
            LanternBox strips = fire->bounds;
            for (int axis = 0; axis < 3; ++axis) {
                strips.min[axis] = std::min(strips.min[axis], warp->bounds.min[axis]);
                strips.max[axis] = std::max(strips.max[axis], warp->bounds.max[axis]);
            }
            const auto centre = strips.Centre();
            a_check.Check(Near(centre[0], kLanternWornHipOffset[0]) && Near(centre[1], kLanternWornHipOffset[1]) && Near(centre[2], cageZ),
                fmt::format("lantern worn: the strips centred in the cage ({:.3f}, {:.3f}, {:.3f}; expected z {:.3f})", centre[0], centre[1],
                    centre[2], cageZ));
        } else {
            a_check.Check(false, "lantern worn: the strips centred in the cage (no strips)");
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
            const auto bodies = a_dir / "lantern" / "bodies";
            // a body box reaching into the worn frame (which sits outside x = offset + half its width)
            MakeBody(bodies / kLanternBodySources[0], Vector3(-30.0f, -5.0f, 50.0f), Vector3(30.0f, 5.0f, 70.0f));
            const auto clash = BuildLantern({ in, bodies }, a_dir / "lantern" / "out_clash");
            a_check.Check(!clash.ok && Mentions(clash.problems, "inside the worn frame"), "lantern: a body inside the worn frame fails the build");
            MakeBody(bodies / kLanternBodySources[0], Vector3(-8.0f, -5.0f, 50.0f), Vector3(8.0f, 5.0f, 70.0f));
            const auto clear = BuildLantern({ in, bodies }, a_dir / "lantern" / "out_clear");
            a_check.Check(clear.ok && Mentions(clear.log, "0 inside"), "lantern: a body clear of the frame passes and is logged");
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

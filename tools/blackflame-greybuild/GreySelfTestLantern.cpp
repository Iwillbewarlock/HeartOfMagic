// =============================================================================
// GreySelfTestLantern - --self-test for --write-lantern (GreyLantern.h) on made-up sources (no game files)
// =============================================================================
// A frame NIF (BSFadeNode, BSXFlags, collision, one lit BSTriShape with a hook out in +Y at the top, its hinge a little above
// the box's top: the lid, and two hollow rings of the cage walls), a skeleton with NPC Pelvis [Pelv], an atronach stand-in
// whose FireStrip08 (palette effect shader with palette alpha, falloff and the decal bits on, normal blend or additive, float
// controller) is skinned half and half to two bones, a 64 x 64 BC3 diffuse with 7 mips, and body stand-ins (a hip box above
// the lantern's lid and a belt band box named as the vanilla band shape).
// The build runs on them; the outputs are read back and checked (the ground's two flame layers: counts, base heights, on the
// cage axis inside the walls with the clearance, shader flags, blends, base alphas, palettes, vertex alpha, controllers on
// their own shaders and no block shared; no addon node, the measured band, the ring at its top beside the hip, the upright
// handle at its height with a lug under each foot, vertical lantern, the worn layers skinned to the pelvis, bind pose, the
// Vertex_Colors flag, clearances), then the failures and edge cases: an additive template (the layers keep their own blends),
// a missing source, body or band shape, a thigh that walks the ring out, one no step up to the maximum clears, a frame bar
// through the flame, a cage too small for the flame, a handle foot too far above the lid, a wrong inverse bind, a leaned
// frame, an addon node with the wrong value, a darken factor out of range. LanternPieces' weld and LanternAxisTiltDeg have
// their own checks.

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
#include <array>
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
        // the hook: a flat bar at the top, z kHookZ0..kFrameHeight; the frame's box below it ends at kFrameBoxTop, so the
        // hook turned up stands from its hinge (z kHookHinge) kHookHinge - kFrameBoxTop above the lid (like the vanilla
        // handle's feet; the lugs bridge it)
        constexpr float kHookZ0 = 29.6f;
        constexpr float kHookHalfX = 3.0f;  // turned up, its feet stand 2 either side of the cage axis: the lugs do not meet
        constexpr float kHookHinge = (kHookZ0 + kFrameHeight) * 0.5f;
        constexpr float kFrameBoxTop = 29.4f;
        // the cage walls' rings at these shares of the frame's height: inside kLanternCageBand's window and clear of the
        // flame's base (kLanternFlameBaseShare); each ring four bars kRingBar thick round the box's outline (hollow, as the
        // vanilla cage's wire rings: the flame stands inside them)
        constexpr std::array<float, 2> kRingShares{ 0.28f, 0.72f };
        constexpr float kRingBar = 0.5f;
        constexpr float kPelvisZ = 68.9f;
        constexpr float kHipHalfX = 12.5f;  // the body stand-in's hip box: x -12.5..12.5 (the band window is x -17..-12)
        // the body stand-in's belt band: a box kBandProud proud of the hip, z kBandBottom..kBandTop, named as the vanilla
        // band shape of each body
        constexpr float kBandProud = 0.2f;
        constexpr float kBandBottom = 72.0f;
        constexpr float kBandTop = 77.0f;
        constexpr std::uint32_t kFrameBsx = 0xC2;  // candlelantern01.nif's value
        constexpr const char* kSourceNormal = "textures\\clutter\\common\\lampgeneric01_n.dds";
        constexpr const char* kOrangePalette = "textures\\effects\\gradients\\gradfireatronach.dds";
        constexpr std::uint16_t kAlphaNormalBlend = 0x10ED;
        constexpr std::uint16_t kAlphaAdditive = 0x100D;  // blend on, SRC_ALPHA / ONE

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

        /// a_hook: the cage walls' rings at a_rings of the height and the hook; a_barZ (when not negative): a bar kRingBar
        /// thick straight across the cage in X through its axis at that height
        nifly::NiShape* MakeShape(nifly::NifFile& a_nif, const std::string& a_name, const Vector3& a_lo, const Vector3& a_hi,
            bool a_hook, const std::array<float, 2>& a_rings = kRingShares, float a_barZ = -1.0f)
        {
            std::vector<Vector3> verts;
            std::vector<nifly::Triangle> tris;
            AddBox(verts, tris, a_lo, a_hi);
            if (a_hook) {
                // two hollow rings of the cage walls, and the hook out in +Y at the top
                for (const float share : a_rings) {
                    const float z0 = a_lo.z + (a_hi.z - a_lo.z) * share - 0.5f;
                    const float z1 = z0 + 1.0f;
                    AddBox(verts, tris, Vector3(a_lo.x, a_lo.y, z0), Vector3(a_hi.x, a_lo.y + kRingBar, z1));
                    AddBox(verts, tris, Vector3(a_lo.x, a_hi.y - kRingBar, z0), Vector3(a_hi.x, a_hi.y, z1));
                    AddBox(verts, tris, Vector3(a_lo.x, a_lo.y, z0), Vector3(a_lo.x + kRingBar, a_hi.y, z1));
                    AddBox(verts, tris, Vector3(a_hi.x - kRingBar, a_lo.y, z0), Vector3(a_hi.x, a_hi.y, z1));
                }
                if (a_barZ >= 0.0f) {
                    AddBox(verts, tris, Vector3(a_lo.x, -kRingBar * 0.5f, a_barZ - kRingBar * 0.5f),
                        Vector3(a_hi.x, kRingBar * 0.5f, a_barZ + kRingBar * 0.5f));
                }
                AddBox(verts, tris, Vector3(-kHookHalfX, a_hi.y, kHookZ0), Vector3(kHookHalfX, kHookY, kFrameHeight));
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

        /// a_boxTop: the top of the frame's box (the lid under the hook's feet); a_rings: its rings' height shares; a_barZ: a bar
        /// across the cage at that height (none when negative); a_halfX, a_halfY: the box's half sizes (the cage walls)
        void MakeFrame(const std::filesystem::path& a_file, float a_boxTop = kFrameBoxTop, const std::array<float, 2>& a_rings = kRingShares,
            float a_barZ = -1.0f, float a_halfX = kFrameHalfX, float a_halfY = kFrameHalfY)
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
            auto* shape = MakeShape(nif, "CandleLantern01:11", Vector3(-a_halfX, -a_halfY, 0.0f), Vector3(a_halfX, a_halfY, a_boxTop), true,
                a_rings, a_barZ);
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

        /// The stand-in Flame Atronach: two bones and FireStrip08 (the flame layers' shader template: palette alpha, falloff and
        /// the decal bits on as on the vanilla body, no SLSF2 flags) skinned half and half to them; it blends by a_alpha
        void MakeAtronach(const std::filesystem::path& a_file, std::uint16_t a_alpha = kAlphaNormalBlend)
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
            {
                const char* name = kLanternFlameTemplate;
                auto* shape = MakeShape(nif, name, Vector3(-20.0f, -10.0f, 0.0f), Vector3(20.0f, 6.0f, 130.0f), false);
                nif.DeleteShader(shape);
                auto prop = std::make_unique<nifly::BSEffectShaderProperty>();
                prop->shaderFlags1 |= kSlsf1Skinned | kSlsf1GreyscaleToPaletteColor | kSlsf1GreyscaleToPaletteAlpha | kSlsf1Falloff |
                                      kSlsf1Decal | kSlsf1DynamicDecal;
                prop->shaderFlags2 = 0;
                prop->sourceTexture.get() = "textures\\effects\\fxfirescrolltile02.dds";
                prop->greyscaleTexture.get() = kOrangePalette;
                shape->ShaderPropertyRef()->index = hdr.AddBlock(std::move(prop));
                auto alpha = std::make_unique<nifly::NiAlphaProperty>();
                alpha->flags = a_alpha;
                (void)nif.AssignAlphaProperty(shape, std::move(alpha));
                auto* shader = hdr.GetBlock<nifly::NiShader>(shape->ShaderPropertyRef());
                AddFloatController(nif, *shader, std::make_unique<nifly::BSEffectShaderPropertyFloatController>());
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

        /// A body stand-in: one unskinned box shape per lo..hi pair, and the belt band box named a_band (none when empty)
        void MakeBody(const std::filesystem::path& a_file, const std::vector<std::pair<Vector3, Vector3>>& a_boxes, const std::string& a_band)
        {
            nifly::NifFile nif;
            nif.Create(nifly::NiVersion::getSSE());
            int n = 0;
            for (const auto& [lo, hi] : a_boxes) {
                (void)MakeShape(nif, fmt::format("Body{}", n++), lo, hi, false);
            }
            if (!a_band.empty()) {
                (void)MakeShape(nif, a_band, Vector3(-kHipHalfX - kBandProud, -6.0f, kBandBottom), Vector3(kHipHalfX + kBandProud, 6.0f, kBandTop),
                    false);
            }
            SaveNif(nif, a_file);
        }

        /// Both body stand-ins (male and female paths) as the same boxes, each with its belt band shape (a_band false: none)
        void MakeBodies(const std::filesystem::path& a_dir, const std::vector<std::pair<Vector3, Vector3>>& a_boxes, bool a_band = true)
        {
            for (std::size_t b = 0; b < kLanternBodySources.size(); ++b) {
                MakeBody(a_dir / kLanternBodySources[b], a_boxes, a_band ? kLanternBandShapes[b] : "");
            }
        }

        // the default body stand-in: the hip box from the band's bottom up (the lantern's lid hangs below it)
        const std::pair<Vector3, Vector3> kHipBox{ Vector3(-kHipHalfX, -10.0f, kBandBottom), Vector3(kHipHalfX, 10.0f, 100.0f) };

        [[nodiscard]] bool Mentions(const std::vector<std::string>& a_lines, const std::string& a_text)
        {
            return std::any_of(a_lines.begin(), a_lines.end(), [&](const auto& a_line) { return a_line.find(a_text) != std::string::npos; });
        }

        [[nodiscard]] const LanternShapeInfo* Shape(const LanternNifInfo& a_info, const std::string& a_name)
        {
            const auto it = std::find_if(a_info.shapes.begin(), a_info.shapes.end(), [&](const auto& a_s) { return a_s.name == a_name; });
            return it == a_info.shapes.end() ? nullptr : &*it;
        }

        /// The flame layers of a model as the build must write them: names, counts, shader flags (a_worn: Skinned on, else
        /// off), blend, base alpha, palette for the blend, vertex alpha up to 255 with 0 at the tip, one controller each on its
        /// own shader, no shader, alpha property, controller, interpolator or key block shared. "" when it holds, else what failed
        [[nodiscard]] std::string FlameLayersWrong(const LanternNifInfo& a_info, bool a_worn)
        {
            constexpr std::uint32_t kOff = kSlsf1GreyscaleToPaletteAlpha | kSlsf1Falloff | kSlsf1Decal | kSlsf1DynamicDecal;
            constexpr std::uint32_t kOn = kSlsf1GreyscaleToPaletteColor | kSlsf1VertexAlpha;
            constexpr std::uint32_t kOn2 = kSlsf2DoubleSided | kSlsf2VertexColors;
            std::vector<std::uint32_t> blocks;
            for (std::size_t l = 0; l < kLanternFlameLayers.size(); ++l) {
                const auto& layer = kLanternFlameLayers[l];
                const auto* s = Shape(a_info, layer.name);
                if (!s) {
                    return fmt::format("no '{}'", layer.name);
                }
                const char* palette = l == 0 ? kPalettePath : kPaletteAddPath;  // the outer layer normal, the core additive
                const bool skinned = (s->flags1 & kSlsf1Skinned) != 0;
                if (s->vertices != kLanternFlameVertices || s->triangles != kLanternFlameTriangles) {
                    return fmt::format("'{}': {} vertices, {} triangles", s->name, s->vertices, s->triangles);
                }
                if (s->shader != "BSEffectShaderProperty" || (s->flags1 & kOff) != 0 || (s->flags1 & kOn) != kOn || (s->flags2 & kOn2) != kOn2 ||
                    skinned != a_worn) {
                    return fmt::format("'{}': {} SLSF1 0x{:08X} SLSF2 0x{:08X}", s->name, s->shader, s->flags1, s->flags2);
                }
                if (!s->hasAlpha || s->alphaFlags != layer.alphaFlags || s->additive != (l == 1) || !Near(s->baseAlpha, layer.baseAlpha, 1e-6f) ||
                    s->textures.size() < 2 || s->textures[1] != std::string("greyscale ") + palette) {
                    return fmt::format("'{}': alpha 0x{:04X}, base alpha {}, {}", s->name, s->alphaFlags, s->baseAlpha,
                        s->textures.size() > 1 ? s->textures[1] : std::string("no palette"));
                }
                if (!s->vertexColors || s->alphaMax != 255 || s->alphaTop != 0) {
                    return fmt::format("'{}': vertex alpha up to {}, {} at the tip", s->name, s->alphaMax, s->alphaTop);
                }
                if (s->controllers.size() != 1 || s->controllers[0].find("its shader") == std::string::npos) {
                    return fmt::format("'{}': {} controllers", s->name, s->controllers.size());
                }
                for (const auto id : s->blockIds) {
                    if (std::find(blocks.begin(), blocks.end(), id) != blocks.end()) {
                        return fmt::format("'{}': block {} shared", s->name, id);
                    }
                    blocks.push_back(id);
                }
            }
            return "";
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

        // the bodies: the hip box above the lantern's lid and the belt band kBandProud proud of it (both body paths)
        const auto bodies = a_dir / "lantern" / "bodies";
        MakeBodies(bodies, { kHipBox });
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
            const auto bandless = a_dir / "lantern" / "bodies_noband";
            MakeBodies(bandless, { kHipBox }, false);
            const auto noBand = BuildLantern({ in, bandless }, a_dir / "lantern" / "out_noband");
            a_check.Check(!noBand.ok && Mentions(noBand.problems, kLanternBandShapes[0]),
                fmt::format("lantern: a body without its belt band shape fails, the shape named ('{}')", kLanternBandShapes[0]));
        }
        a_check.Check(result.bands.size() == 2 && std::all_of(result.bands.begin(), result.bands.end(), [](const LanternBand& a_band) {
            return a_band.vertices > 0 && Near(a_band.top, kBandTop) && Near(a_band.bottom, kBandBottom);
        }),
            fmt::format("lantern: the belt band measured on both bodies (top z {}, bottom z {}: {})", kBandTop, kBandBottom,
                result.bands.empty() ? std::string("none") : fmt::format("{:.3f} / {:.3f}", result.bands.front().top, result.bands.front().bottom)));

        // the ground model: the frame and the flame layers, no addon node
        const auto gnd = InspectLanternNif(out / kLanternGroundNif);
        const auto* frame = Shape(gnd, "CandleLantern01:11");
        a_check.Check(gnd.shapes.size() == 3 && frame && frame->type == "BSTriShape" && !frame->textures.empty() &&
                          frame->textures.front() == fmt::format("[0] {}", kLanternDiffuse),
            fmt::format("lantern gnd: the frame (diffuse '{}') and two flame layers ({} shapes)", kLanternDiffuse, gnd.shapes.size()));
        a_check.Check(frame && frame->textures.size() > 1 && frame->textures[1] == fmt::format("[1] {}", kSourceNormal),
            "lantern gnd: the normal map kept as the source names it");
        a_check.Check(gnd.hasCollision && gnd.rootType == "BSFadeNode", "lantern gnd: collision and the BSFadeNode root kept");
        a_check.Check(gnd.hasBsx && gnd.bsx == (kFrameBsx | kBsxAnimated) && gnd.valueNodes.empty() && gnd.nodeNames.size() == 1,
            fmt::format("lantern gnd: BSXFlags {} = source | Animated (no Addon), no addon node ({} found), the root the only node (the "
                        "atronach's bones left out; {} nodes)", gnd.bsx, gnd.valueNodes.size(), gnd.nodeNames.size()));
        a_check.Check(Near(result.frameBounds.max[1], kHookY) && Near(result.frameBounds.Size(2), kFrameHeight),
            "lantern gnd: the frame bounds reported with the hook, for the record's ObjectBounds");
        const auto flame = FlameReadBack(out);
        const float gndBase = kFrameHeight * kLanternFlameBaseShare;
        const float gndHeight = kFrameHeight * kLanternFlameHeightShare;
        // the walls 18 x 16 (the rings' outline): the widest radius from the smaller
        const float gndRadius = kLanternFlameRadiusShare * 2.0f * std::min(kFrameHalfX, kFrameHalfY);
        {
            const std::string wrong = FlameLayersWrong(gnd, false);
            a_check.Check(wrong.empty() && Shape(gnd, kLanternFlameLayers[0].name)->skinType.empty() &&
                              Shape(gnd, kLanternFlameLayers[1].name)->skinType.empty(),
                fmt::format("lantern gnd: both flame layers ({} vertices, {} triangles each, not skinned) with palette alpha, falloff, the "
                            "decal bits and Skinned cleared, palette colour, vertex alpha, double-sided and vertex colours on; the outer "
                            "0x{:04X} on GradBlackFlame.dds base alpha {}, the core 0x{:04X} on GradBlackFlameAdd.dds base alpha {}; vertex "
                            "alpha up to 255, 0 at the tip; each controller on its own shader, no block shared ({})",
                    kLanternFlameVertices, kLanternFlameTriangles, kLanternFlameLayers[0].alphaFlags, kLanternFlameLayers[0].baseAlpha,
                    kLanternFlameLayers[1].alphaFlags, kLanternFlameLayers[1].baseAlpha, wrong.empty() ? std::string("ok") : wrong));
            bool placed = true;
            std::string text;
            for (std::size_t l = 0; l < kLanternFlameLayers.size(); ++l) {
                const auto& layer = kLanternFlameLayers[l];
                const auto& box = flame.ground[l];
                const float low = gndBase + layer.lift * gndHeight;
                const float r = layer.radiusShare * gndRadius;
                placed = placed && Near(box.min[2], low, kTolerance) && Near(box.max[2], low + layer.heightShare * gndHeight, kTolerance) &&
                         Near(box.Size(0), 2.0f * r, kTolerance) && Near(box.Size(1), 2.0f * r, kTolerance) &&
                         Near(box.Centre()[0], 0.0f, kTolerance) && Near(box.Centre()[1], 0.0f, kTolerance);
                text += fmt::format(" '{}' z {:.4f}..{:.4f} size ({:.4f}, {:.4f}) centre ({:.4f}, {:.4f});", layer.name, box.min[2], box.max[2],
                    box.Size(0), box.Size(1), box.Centre()[0], box.Centre()[1]);
            }
            a_check.Check(placed && Near(result.flameBase[2], gndBase) && Near(result.flameHeight, gndHeight) && Near(result.flameRadius, gndRadius),
                fmt::format("lantern gnd: the outer layer from z {:.4f} (kLanternFlameBaseShare x the frame's height) {:.4f} tall, radius "
                            "{:.4f}; the core lifted, shorter and narrower by its shares; both on the cage axis ({})", gndBase, gndHeight,
                    gndRadius, text));
            // every flame vertex inside the cage walls (x +-kFrameHalfX, y +-kFrameHalfY) by the worn clearance at the ground scale
            const float clearance = kLanternFlameClearance * kFrameHeight / kLanternWornHeight;
            bool inside = true;
            for (const auto& box : flame.ground) {
                inside = inside && box.max[0] + clearance <= kFrameHalfX && box.min[0] - clearance >= -kFrameHalfX &&
                         box.max[1] + clearance <= kFrameHalfY && box.min[1] - clearance >= -kFrameHalfY;
            }
            a_check.Check(inside, fmt::format("lantern gnd: every flame vertex inside the cage walls by kLanternFlameClearance at the ground "
                                              "scale ({:.4f})", clearance));
        }
        {
            // the layers' normals point out from the axis (0, 0), the triangles wind outward, tangents are there (nifly's
            // CalcTangentSpace)
            nifly::NifFile nif;
            LoadNif(nif, out / kLanternGroundNif);
            int checked = 0;
            int wrong = 0;
            for (const auto& layer : kLanternFlameLayers) {
                auto* tri = dynamic_cast<nifly::BSTriShape*>(nif.FindBlockByName<nifly::NiShape>(layer.name));
                if (!tri) {
                    ++wrong;
                    continue;
                }
                ++checked;
                wrong += tri->HasTangents() ? 0 : 1;
                const auto& normals = tri->UpdateRawNormals();
                for (std::size_t i = 0; i < tri->vertData.size(); ++i) {
                    const Vector3& v = tri->vertData[i].vert;
                    ++checked;
                    wrong += normals[i].dot(Vector3(v.x, v.y, 0.0f)) > 0.0f ? 0 : 1;
                }
                for (const auto& t : tri->triangles) {
                    const Vector3& a = tri->vertData[t.p1].vert;
                    const Vector3& b = tri->vertData[t.p2].vert;
                    const Vector3& c = tri->vertData[t.p3].vert;
                    const Vector3 centre = (a + b + c) / 3.0f;
                    ++checked;
                    wrong += (b - a).cross(c - a).dot(Vector3(centre.x, centre.y, 0.0f)) > 0.0f ? 0 : 1;
                }
            }
            a_check.Check(checked > 0 && wrong == 0,
                fmt::format("lantern gnd: the flame layers' normals point out from the axis, their triangles wind outward, they have "
                            "tangents ({} of {} wrong)", wrong, checked));
        }

        // the worn model
        const auto worn = InspectLanternNif(out / kLanternWornNif);
        a_check.Check(worn.shapes.size() == 5 && Shape(worn, kLanternWornFrameName) && Shape(worn, kLanternFlameLayers[0].name) &&
                          Shape(worn, kLanternFlameLayers[1].name) && Shape(worn, kLanternRingName) && Shape(worn, kLanternLugName),
            fmt::format("lantern worn: the frame, both flame layers, the ring and the lugs, nothing else ({} shapes)", worn.shapes.size()));
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
                                  "the flame template was half and half)");
        a_check.Check(allSkinnedFlag, "lantern worn: vertex skinned flag and shader Skinned flag on every shape");
        a_check.Check(worn.nodeNames.size() == 2 && worn.nodeNames[0] == kLanternWornRootName && worn.nodeNames[1] == kLanternBone &&
                          worn.valueNodes.empty() && !worn.hasCollision,
            fmt::format("lantern worn: nodes are the root and the bone alone ({}), no addon node, no collision", worn.nodeNames.size()));
        {
            const std::string wrong = FlameLayersWrong(worn, true);
            a_check.Check(wrong.empty(), fmt::format("lantern worn: both flame layers as on the ground (counts, flags with Skinned on, "
                                                     "blends, base alphas, palettes, vertex alpha, own controllers, no block shared; {})",
                                             wrong.empty() ? std::string("ok") : wrong));
        }
        const auto* wornFrame = Shape(worn, kLanternWornFrameName);
        const float scale = kLanternWornHeight / kFrameHeight;
        // the hook (z kHookZ0..kFrameHeight, out to y kHookY) turned up about its hinge (y kFrameHalfY, z kHookHinge) and
        // set kLanternHandleHeight tall: the frame's top (worn units, its bottom at 0) at kHookHinge x scale + that
        const float wornTop = kHookHinge * scale + kLanternHandleHeight;
        a_check.Check(Near(result.wornScale, scale), fmt::format("lantern worn: scale {:.5f} = {} / {}", result.wornScale, kLanternWornHeight,
            kFrameHeight));
        {
            const float tilt = WornAxisTiltDeg(out);
            a_check.Check(tilt <= kLanternVerticalToleranceDeg && Near(tilt, 0.0f, 0.01f),
                fmt::format("lantern worn: the cage axis read back is world Z ({:.4f} deg, tolerance {})", tilt, kLanternVerticalToleranceDeg));
        }
        a_check.Check(Near(result.wornCage[0], result.contact[0]) && Near(result.wornCage[1], result.contact[1]),
            fmt::format("lantern worn: the cage centre hangs straight below the handle's top wire (cage {:.3f}, {:.3f}; contact {:.3f}, {:.3f}: "
                        "the hinge moved onto the cage axis)", result.wornCage[0], result.wornCage[1], result.contact[0], result.contact[1]));
        a_check.Check(wornFrame && Near(wornFrame->bounds.Size(2), wornTop) &&
                          Near(wornFrame->bounds.max[2], result.contact[2] + result.handleWire),
            fmt::format("lantern worn: the handle stands up (frame {:.3f} tall, expected {:.3f}) and its top wire is the contact (top {:.3f}, "
                        "contact + wire {:.3f})", wornFrame ? wornFrame->bounds.Size(2) : 0.0f, wornTop,
                wornFrame ? wornFrame->bounds.max[2] : 0.0f, result.contact[2] + result.handleWire));
        {
            // the handle: kLanternHandleHeight tall read back; its top wire's half height squashed by the same Z scale (half the
            // hook's thickness at the worn scale)
            const float handleScale = kLanternHandleHeight / ((kHookY - kFrameHalfY) * scale);
            const auto handle = WornHandle(out);
            a_check.Check(Near(handle.height, kLanternHandleHeight) && Near(result.handleHeight, kLanternHandleHeight) &&
                              Near(result.handleScale, handleScale) &&
                              Near(result.handleWire, (kFrameHeight - kHookZ0) * 0.5f * scale * handleScale),
                fmt::format("lantern worn: the handle {} tall (kLanternHandleHeight; read back {:.4f}, build {:.4f}), Z scale {:.4f}, top "
                            "wire half height {:.4f}", kLanternHandleHeight, handle.height, result.handleHeight, result.handleScale,
                    result.handleWire));
            // its feet stand (kHookHinge - kFrameBoxTop) x scale above the lid; the lugs touch the lid and the feet
            const float gap = (kHookHinge - kFrameBoxTop) * scale;
            bool feet = true;
            std::string text;
            for (const auto& foot : handle.feet) {
                const float contact = std::max(foot.lug.min[2] - foot.lidZ, foot.footZ - foot.lug.max[2]);
                feet = feet && Near(foot.footZ - foot.lidZ, gap) && contact <= kLanternLugContact &&
                       foot.lug.Size(2) >= kLanternLugMinHeight - kTolerance && foot.lug.Size(2) <= kLanternLugMaxHeight + kTolerance &&
                       Near(foot.lug.Size(0), kLanternLugSize[0]) && Near(foot.lug.Size(1), kLanternLugSize[1]);
                text += fmt::format(" foot gap {:.4f}, lug {:.4f} tall, contact {:.4f};", foot.footZ - foot.lidZ, foot.lug.Size(2), contact);
            }
            a_check.Check(feet && std::fabs(handle.feet[0].centre[1] - handle.feet[1].centre[1]) > 1.0f,
                fmt::format("lantern worn: two handle feet {:.4f} above the lid, a lug under each touching lid and foot within {} ({})", gap,
                    kLanternLugContact, text));
        }
        a_check.Check(Near(result.contact[0], result.ringCentre[0]) && Near(result.contact[1], result.ringCentre[1]) &&
                          Near(result.contact[2], result.ringCentre[2] - kLanternRingRadius + kLanternRingTube + result.handleWire),
            "lantern worn: the handle's top wire rests on the ring's bottom tube, inside the ring");

        // the ring: at the band's top beside the hip, as near as kLanternRingMargin allows (its innermost point, x + radius +
        // tube at the band top's height, kLanternRingMargin off the band's face at -(kHipHalfX + kBandProud)), kLanternRingY
        // forward of the pelvis (the hip box leaves the lantern room below it: no step out)
        const auto* ring = Shape(worn, kLanternRingName);
        const float ringX = -kHipHalfX - kBandProud - kLanternRingRadius - kLanternRingTube - kLanternRingMargin;
        a_check.Check(ring && Near(ring->bounds.Centre()[2], kBandTop) && Near(result.ringCentre[2], kBandTop) &&
                          Near(result.ringCentre[0], ringX) && Near(result.ringCentre[1], kLanternRingY) &&
                          result.ringSteps == 1 && Near(result.ringOut, 0.0f),
            fmt::format("lantern worn: the ring centre at the band top z {} (read {:.4f}), x {:.4f} (expected {:.4f}), y {:.4f} "
                        "(kLanternRingY {}) after {} step(s)", kBandTop, ring ? ring->bounds.Centre()[2] : 0.0f, result.ringCentre[0], ringX,
                result.ringCentre[1], kLanternRingY, result.ringSteps));
        a_check.Check(result.ringGap >= kLanternRingMargin - kLanternRingFitTolerance * 0.5f && result.ringGap <= kLanternRingMargin + kTolerance &&
                          result.lanternGap >= kLanternBodyMargin,
            fmt::format("lantern worn: the ring {:.4f} off the bodies (kLanternRingMargin {}, within half of kLanternRingFitTolerance {}), the lantern "
                        "{:.4f} (kLanternBodyMargin {})", result.ringGap, kLanternRingMargin, kLanternRingFitTolerance, result.lanternGap,
                kLanternBodyMargin));
        a_check.Check(ring && ring->textures.size() > 1 && ring->textures[0] == fmt::format("[0] {}", kLanternDiffuse) &&
                          ring->textures[1] == fmt::format("[1] {}", kSourceNormal) && ring->uvMin[0] >= kLanternIronUvMin[0] - kTolerance &&
                          ring->uvMin[1] >= kLanternIronUvMin[1] - kTolerance && ring->uvMax[0] <= kLanternIronUvMax[0] + kTolerance &&
                          ring->uvMax[1] <= kLanternIronUvMax[1] + kTolerance,
            fmt::format("lantern worn: the ring on the soot lamp texture and the lamp's normal map, UVs in the iron box u {}..{} v {}..{}",
                kLanternIronUvMin[0], kLanternIronUvMax[0], kLanternIronUvMin[1], kLanternIronUvMax[1]));
        {
            const auto* lugs = Shape(worn, kLanternLugName);
            bool madeCleared = ring && lugs;
            for (const auto* made : { ring, lugs }) {
                madeCleared = madeCleared && !made->vertexColors && (made->flags2 & kSlsf2VertexColors) == 0;
            }
            a_check.Check(madeCleared && wornFrame && wornFrame->vertexColors && (wornFrame->flags2 & kSlsf2VertexColors) != 0,
                "lantern worn: Vertex_Colors cleared on the made shapes (no colours), kept on the frame (colours)");
        }
        {
            // the fire as on the ground at the worn scale: each layer's base and top above the frame's bottom by the ground
            // heights x scale, on the cage axis below the handle's top wire; clear of the frame by kLanternFlameClearance
            bool placed = wornFrame != nullptr;
            std::string text;
            for (std::size_t l = 0; l < kLanternFlameLayers.size() && wornFrame; ++l) {
                const auto& layer = kLanternFlameLayers[l];
                const auto& box = flame.worn[l];
                const float low = wornFrame->bounds.min[2] + (gndBase + layer.lift * gndHeight) * scale;
                placed = placed && Near(box.min[2], low, kTolerance) &&
                         Near(box.max[2], low + layer.heightShare * gndHeight * scale, kTolerance) &&
                         Near(box.Centre()[0], result.wornCage[0], kTolerance) && Near(box.Centre()[1], result.wornCage[1], kTolerance);
                text += fmt::format(" '{}' z {:.4f}..{:.4f};", layer.name, box.min[2], box.max[2]);
            }
            a_check.Check(placed, fmt::format("lantern worn: the flame layers at the ground heights x scale above the frame's bottom, on the "
                                              "cage axis ({})", text));
            a_check.Check(flame.wornClearance >= kLanternFlameClearance && Near(flame.wornClearance, result.wornFlameClearance) &&
                              flame.groundClearance >= kLanternFlameClearance && Near(flame.groundClearance, result.groundFlameClearance),
                fmt::format("lantern: the flame clear of the frame by kLanternFlameClearance {} (worn {:.4f}, ground {:.4f}; build and read-back "
                            "agree)", kLanternFlameClearance, flame.wornClearance, flame.groundClearance));
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
            bool clear = clearances.size() == 4;
            std::string text;
            for (const auto& c : clearances) {
                clear = clear && c.gap.points > 0 && c.gap.inside == 0 && c.gap.nearest >= c.margin;
                text += fmt::format(" {} {:.3f};", c.part, c.gap.nearest);
            }
            a_check.Check(clear, fmt::format("lantern worn: lantern (with the lugs) and ring clear of both bodies by their margins ({})", text));
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

        // an additive template: the layers keep their own blends (outer normal on GradBlackFlame.dds, core additive on
        // GradBlackFlameAdd.dds) in both models
        {
            const auto additive = a_dir / "lantern" / "in_additive";
            std::filesystem::remove_all(additive);
            std::filesystem::copy(in, additive, std::filesystem::copy_options::recursive);
            MakeAtronach(additive / kLanternFlameSource, kAlphaAdditive);
            const auto addOut = a_dir / "lantern" / "out_additive";
            const auto added = BuildLantern({ additive, bodies }, addOut);
            const std::string gndWrong = FlameLayersWrong(InspectLanternNif(addOut / kLanternGroundNif), false);
            const std::string wornWrong = FlameLayersWrong(InspectLanternNif(addOut / kLanternWornNif), true);
            a_check.Check(added.ok && gndWrong.empty() && wornWrong.empty(),
                fmt::format("lantern: an additive template still gives the outer layer 0x10ED on GradBlackFlame.dds and the core 0x100D on "
                            "GradBlackFlameAdd.dds in both models, and the read-back passes ({} / {})",
                    gndWrong.empty() ? std::string("ok") : gndWrong, wornWrong.empty() ? std::string("ok") : wornWrong));
        }

        // failures
        {
            const auto empty = a_dir / "lantern" / "empty";
            std::filesystem::create_directories(empty);
            const auto missing = BuildLantern({ empty }, a_dir / "lantern" / "out_missing");
            a_check.Check(!missing.ok && Mentions(missing.problems, kLanternFrameSource) && Mentions(missing.problems, kLanternDiffuseSource),
                "lantern: missing sources fail, each named");
        }
        {
            // a thigh box under the hip out to x kThighX: the ring steps out in kLanternRingOutStep steps until the lantern's
            // inboard side (the frame's half depth kFrameHalfY x scale inboard of the ring, the yaw turns Y into X) clears it by
            // kLanternBodyMargin: from out = the start x - kThighX + kFrameHalfY x scale + kLanternBodyMargin = 3.633,
            // so step 8 (4.0; 3.5 misses by 0.133)
            constexpr float kThighX = -12.0f;
            const float needed = ringX - kThighX + kFrameHalfY * scale + kLanternBodyMargin;
            const float settled = std::ceil(needed / kLanternRingOutStep) * kLanternRingOutStep;
            const auto thigh = a_dir / "lantern" / "bodies_thigh";
            MakeBodies(thigh, { kHipBox, { Vector3(kThighX, -20.0f, 0.0f), Vector3(kHipHalfX, 20.0f, 65.0f) } });
            const auto outward = BuildLantern({ in, thigh }, a_dir / "lantern" / "out_outward");
            a_check.Check(outward.ok && Near(settled, 4.0f) && Near(outward.ringOut, settled) && outward.ringSteps == 9 &&
                              Mentions(outward.log, "ring out 3.5 (") && Near(outward.ringCentre[0], ringX - settled) &&
                              Near(outward.ringCentre[1], kLanternRingY) &&
                              WornAxisTiltDeg(a_dir / "lantern" / "out_outward") <= kLanternVerticalToleranceDeg,
                fmt::format("lantern: a thigh under the hip walks the ring out in kLanternRingOutStep steps (to {}, expected {}, {} steps), "
                            "the lantern still vertical, and the build's read-back passes", outward.ringOut, settled, outward.ringSteps));
            // a thigh reaching far out: no step up to kLanternRingOutMax clears it
            const auto wide = a_dir / "lantern" / "bodies_wide";
            MakeBodies(wide, { kHipBox, { Vector3(-60.0f, -30.0f, 0.0f), Vector3(kHipHalfX, 30.0f, 65.0f) } });
            const auto clash = BuildLantern({ in, wide }, a_dir / "lantern" / "out_clash");
            a_check.Check(!clash.ok && Mentions(clash.problems, "at every outward step") &&
                              Mentions(clash.log, fmt::format("ring out {} (", kLanternRingOutMax)),
                fmt::format("lantern: a body no outward step up to the maximum clears fails the build (no tilt), each step tried and logged "
                            "({})", clash.problems.empty() ? std::string("no problem") : clash.problems.front()));
        }
        {
            // a frame bar across the cage through its axis at the outer layer's widest ring: the flame comes within the clearance
            const auto barred = a_dir / "lantern" / "in_barred";
            std::filesystem::remove_all(barred);
            std::filesystem::copy(in, barred, std::filesystem::copy_options::recursive);
            MakeFrame(barred / kLanternFrameSource, kFrameBoxTop, kRingShares, gndBase + kLanternFlameProfile[2][0] * gndHeight);
            const auto touching = BuildLantern({ barred, bodies }, a_dir / "lantern" / "out_barred");
            a_check.Check(!touching.ok && Mentions(touching.problems, "the flame comes within"),
                "lantern: a frame bar through the flame fails the build (kLanternFlameClearance)");
        }
        {
            // a cage 2.4 x 2.4: the widest ring (radius 0.66) and the clearance at the ground scale (0.75) reach past its walls (1.2)
            const auto narrow = a_dir / "lantern" / "in_narrow";
            std::filesystem::remove_all(narrow);
            std::filesystem::copy(in, narrow, std::filesystem::copy_options::recursive);
            MakeFrame(narrow / kLanternFrameSource, kFrameBoxTop, kRingShares, -1.0f, 1.2f, 1.2f);
            std::filesystem::remove_all(a_dir / "lantern" / "out_narrow");
            const auto small = BuildLantern({ narrow, bodies }, a_dir / "lantern" / "out_narrow");
            a_check.Check(!small.ok && Mentions(small.problems, "the cage is too small for the flame") &&
                              !std::filesystem::exists(a_dir / "lantern" / "out_narrow" / kLanternGroundNif),
                fmt::format("lantern: a cage too small for the flame fails the build, nothing written for the ground model ({})",
                    small.problems.empty() ? std::string("no problem") : small.problems.front()));
        }
        {
            // the frame's box 1 lower: the hook's feet stand (kHookHinge - kFrameBoxTop + 1) x scale = 0.93 above the lid, more than
            // a lug bridges
            const auto high = a_dir / "lantern" / "in_high";
            std::filesystem::remove_all(high);
            std::filesystem::copy(in, high, std::filesystem::copy_options::recursive);
            MakeFrame(high / kLanternFrameSource, kFrameBoxTop - 1.0f);
            const auto floating = BuildLantern({ high, bodies }, a_dir / "lantern" / "out_high");
            a_check.Check(!floating.ok && Mentions(floating.problems, "above the lid"),
                "lantern: a handle foot further above the lid than a lug bridges fails the build");
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

// =============================================================================
// GreySelfTestLantern - --self-test for --write-lantern (GreyLantern.h) on made-up sources (no game files)
// =============================================================================
// A frame NIF (BSFadeNode, BSXFlags, collision, one lit BSTriShape with a hook out in +Y at the top, its hinge a little above
// the box's top: the lid, and two rings of the cage walls), a skeleton with NPC Pelvis [Pelv], a stand-in of the vanilla
// wisp's particles (fxwispparticleattach.nif: puffs on an NiParticleSystem and strips on a BSStripParticleSystem, each with
// a mesh emitter on a hidden shape, a gravity modifier on a node, the strips two bomb modifiers on nodes switched by
// NiPSysModifierActiveCtlrs), a 64 x 64 BC3 diffuse with 7 mips, and body stand-ins (a hip box and a belt band box named as
// the vanilla band shape). The build runs on them; the outputs are read back and checked (the measured band, the strap on
// it with its bulge, wobbled edges, its leather shader and its layers lying on each other, the bevelled and tapered bracket,
// ring, the upright handle at its height with a lug under each foot, vertical lantern, the wisp in both models: shaders,
// palettes, blends, scroll and pulse controllers, radii and centre, its clearance in the cage; the ground's bob and turn,
// BSXFlags, particles copied, scaled, on our palette, emitting from the wisp's emitter, their bombs gone; bind pose, the
// Vertex_Colors flag, clearances and the strap's gap), then the failures and edge cases: a missing source (the particles
// too), body or band shape, a thigh that lengthens the bracket, one no length up to the maximum clears, a body part through
// the strap, a handle foot too far above the lid, a cage bar through the wisp, a wrong inverse bind, a wisp palette or
// motion taken away, an addon node with the wrong value, a darken factor out of range. LanternPieces' weld and
// LanternAxisTiltDeg have their own checks.

#include "GreyLantern.h"
#include "GreySelfTest.h"
#include "GreyShapes.h"

#include <Animation.hpp>
#include <ExtraData.hpp>
#include <Geometry.hpp>
#include <NifFile.hpp>
#include <Nodes.hpp>
#include <Particles.hpp>
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
        constexpr float kPelvisZ = 68.9f;
        constexpr float kHipHalfX = 12.5f;  // the body stand-in's hip box: x -12.5..12.5 (the band window is x -17..-12)
        // the body stand-in's belt band: a box kBandProud proud of the hip, z kBandBottom..kBandTop, named as the vanilla
        // band shape of each body
        constexpr float kBandProud = 0.2f;
        constexpr float kBandBottom = 72.0f;
        constexpr float kBandTop = 77.0f;
        constexpr std::uint32_t kFrameBsx = 0xC2;  // candlelantern01.nif's value
        constexpr const char* kSourceNormal = "textures\\clutter\\common\\lampgeneric01_n.dds";
        constexpr const char* kWispPalette = "textures\\effects\\gradients\\GradWisp.dds";
        constexpr std::uint16_t kAlphaNormalBlend = 0x10ED;
        // the cage walls' two rings at these shares of the frame box's height (the halo, 3 + the bob, clears them in both models)
        constexpr std::array<float, 2> kRingShares{ 0.28f, 0.72f };
        constexpr float kPi = 3.14159265358979f;

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
            bool a_hook, bool a_extra = false, const std::vector<float>& a_rings = { kRingShares[0], kRingShares[1] })
        {
            std::vector<Vector3> verts;
            std::vector<nifly::Triangle> tris;
            AddBox(verts, tris, a_lo, a_hi);
            if (a_extra) {
                AddBox(verts, tris, Vector3(-5.0f, -3.0f, a_hi.z + 10.0f), Vector3(5.0f, 3.0f, a_hi.z + 20.0f));
            }
            if (a_hook) {
                // the rings of the cage walls (a_rings: shares of the height), and the hook out in +Y at the top
                for (const float share : a_rings) {
                    const float z = a_lo.z + (a_hi.z - a_lo.z) * share;
                    AddBox(verts, tris, Vector3(a_lo.x, a_lo.y, z - 0.5f), Vector3(a_hi.x, a_hi.y, z + 0.5f));
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

        /// a_boxTop: the top of the frame's box (the lid under the hook's feet); a_rings: the cage walls' rings
        void MakeFrame(const std::filesystem::path& a_file, float a_boxTop = kFrameBoxTop,
            const std::vector<float>& a_rings = { kRingShares[0], kRingShares[1] })
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
                Vector3(kFrameHalfX, kFrameHalfY, a_boxTop), true, false, a_rings);
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

        /// One stand-in particle system of the wisp's (made-up values, small enough that the copy stays in the made-up cage)
        struct StandInSystem
        {
            const char* name;
            bool strip;
            float radius;
            float speed;
            float life;
            float gravity;
            float birth;
            int bombs;
        };
        constexpr std::array<StandInSystem, 2> kStandIns{ { { "PArray02", false, 40.0f, 20.0f, 2.0f, 4.0f, 30.0f, 0 },
            { "PArray01", true, 50.0f, 40.0f, 1.5f, 20.0f, 9.0f, 2 } } };

        /// The stand-in of the vanilla wisp's particles (kLanternWispSource): each system of kStandIns with its NiPSysData (or
        /// BSStripPSysData), a palette effect shader on GradWisp.dds, normal blend, a mesh emitter on a hidden shape of its
        /// own, a gravity modifier on a node, a position modifier, bomb modifiers on nodes of their own each switched by an
        /// NiPSysModifierActiveCtlr, and an NiPSysEmitterCtlr
        void MakeWispParticles(const std::filesystem::path& a_file)
        {
            nifly::NifFile nif;
            nif.Create(nifly::NiVersion::getSSE());
            auto& hdr = nif.GetHeader();
            (void)hdr.ReplaceBlock(0, std::make_unique<nifly::BSFadeNode>());
            auto* root = nif.GetRootNode();
            root->name.get() = "FXWispParticleAttach";
            const auto node = [&](const std::string& a_name, const Vector3& a_at) {
                MatTransform t;
                t.translation = a_at;
                return hdr.GetBlockID(nif.AddNode(a_name, t, root));
            };
            for (const auto& standIn : kStandIns) {
                const std::string name = standIn.name;
                auto* emitShape = MakeShape(nif, "EmitGeo" + name + ":0", Vector3(-30.0f, -30.0f, 50.0f), Vector3(30.0f, 30.0f, 70.0f), false);
                nif.DeleteShader(emitShape);
                emitShape->flags = 0x8000F;
                const std::uint32_t emitId = hdr.GetBlockID(emitShape);
                const std::uint32_t gravityNode = node("Gravity" + name, Vector3(60.0f, -100.0f, 0.0f));
                std::unique_ptr<nifly::NiPSysData> data;
                if (standIn.strip) {
                    data = std::make_unique<nifly::BSStripPSysData>();
                } else {
                    data = std::make_unique<nifly::NiPSysData>();
                }
                const std::uint32_t dataId = hdr.AddBlock(std::move(data));
                auto shader = std::make_unique<nifly::BSEffectShaderProperty>();
                shader->shaderFlags1 = 0x80000000u | kSlsf1GreyscaleToPaletteColor;
                shader->shaderFlags2 = kSlsf2VertexColors;
                shader->sourceTexture.get() = "textures\\effects\\FXSmokeSoftSub01.dds";
                shader->greyscaleTexture.get() = kWispPalette;
                const std::uint32_t shaderId = hdr.AddBlock(std::move(shader));
                auto alpha = std::make_unique<nifly::NiAlphaProperty>();
                alpha->flags = kAlphaNormalBlend;
                const std::uint32_t alphaId = hdr.AddBlock(std::move(alpha));
                std::unique_ptr<nifly::NiParticleSystem> system;
                if (standIn.strip) {
                    system = std::make_unique<nifly::BSStripParticleSystem>();
                } else {
                    system = std::make_unique<nifly::NiParticleSystem>();
                }
                system->name.get() = name;
                system->dataRef.index = dataId;
                system->psysDataRef.index = dataId;
                system->shaderPropertyRef.index = shaderId;
                system->alphaPropertyRef.index = alphaId;
                system->isWorldSpace = true;
                const std::uint32_t systemId = hdr.AddBlock(std::move(system));
                root->childRefs.AddBlockRef(systemId);
                std::vector<std::uint32_t> modifiers;
                const auto modifier = [&](std::unique_ptr<nifly::NiPSysModifier> a_modifier, const std::string& a_name, std::uint32_t a_order) {
                    a_modifier->name.get() = a_name;
                    a_modifier->order = a_order;
                    a_modifier->targetRef.index = systemId;
                    a_modifier->isActive = true;
                    modifiers.push_back(hdr.AddBlock(std::move(a_modifier)));
                };
                auto emitter = std::make_unique<nifly::NiPSysMeshEmitter>();
                emitter->radius = standIn.radius;
                emitter->radiusVariation = standIn.radius * 0.2f;
                emitter->speed = standIn.speed;
                emitter->speedVariation = standIn.speed * 0.5f;
                emitter->lifeSpan = standIn.life;
                emitter->meshRefs.AddBlockRef(emitId);
                emitter->emissionAxis = Vector3(1.0f, 0.0f, 0.0f);
                modifier(std::move(emitter), "NiPSysMeshEmitter:0", 1000);
                auto gravity = std::make_unique<nifly::NiPSysGravityModifier>();
                gravity->gravityObjRef.index = gravityNode;
                gravity->gravityAxis = Vector3(0.0f, 0.0f, 1.0f);
                gravity->strength = standIn.gravity;
                gravity->forceType = nifly::FORCE_PLANAR;
                modifier(std::move(gravity), "NiPSysGravityModifier:9", 4000);
                std::vector<std::string> bombs;
                for (int b = 0; b < standIn.bombs; ++b) {
                    auto bomb = std::make_unique<nifly::NiPSysBombModifier>();
                    bomb->bombNodeRef.index = node(fmt::format("PBomb{}{}", name, b), Vector3(50.0f, 40.0f * static_cast<float>(b), 100.0f));
                    bomb->deltaV = -450.0f;
                    bomb->decay = 60.0f;
                    bombs.push_back(fmt::format("NiPSysBombModifier:1{}", b));
                    modifier(std::move(bomb), bombs.back(), 4000);
                }
                modifier(std::make_unique<nifly::NiPSysPositionModifier>(), "NiPSysPositionModifier:7", 6000);
                auto* made = hdr.GetBlock<nifly::NiParticleSystem>(systemId);
                for (const auto id : modifiers) {
                    made->modifierRefs.AddBlockRef(id);
                }
                // the controller chain: each bomb's switch, then the emitter's
                nifly::NiBlockRef<nifly::NiTimeController>* link = &made->controllerRef;
                const auto chain = [&](std::unique_ptr<nifly::NiPSysModifierCtlr> a_ctrl, const std::string& a_modifier, std::uint32_t a_interp) {
                    a_ctrl->modifierName.get() = a_modifier;
                    a_ctrl->targetRef.index = systemId;
                    a_ctrl->flags = 0x0048;
                    a_ctrl->stopTime = 33.3333f;
                    a_ctrl->startTime = 0.0f;
                    a_ctrl->interpolatorRef.index = a_interp;
                    const std::uint32_t id = hdr.AddBlock(std::move(a_ctrl));
                    link->index = id;
                    link = &hdr.GetBlock<nifly::NiTimeController>(id)->nextControllerRef;
                };
                for (const auto& bomb : bombs) {
                    auto on = std::make_unique<nifly::NiBoolInterpolator>();
                    on->boolValue = 1;
                    chain(std::make_unique<nifly::NiPSysModifierActiveCtlr>(), bomb, hdr.AddBlock(std::move(on)));
                }
                auto rate = std::make_unique<nifly::NiFloatInterpolator>();
                rate->floatValue = standIn.birth;
                chain(std::make_unique<nifly::NiPSysEmitterCtlr>(), "NiPSysMeshEmitter:0", hdr.AddBlock(std::move(rate)));
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

    namespace
    {
        /// One wisp sphere's expected look in a model: its palette (core, halo) or none (shell), its blend, its controllers
        struct WispExpect
        {
            const char* name;
            float radius;
            bool palette;
            std::uint16_t alpha;
            std::vector<std::uint32_t> variables;  // the controllers' controlled variables in chain order
            std::vector<float> stops;              // their cycle lengths
            std::vector<float> ends;               // their last key's value
        };

        [[nodiscard]] std::array<WispExpect, 3> WispExpects()
        {
            const float pulse = 1.0f / kLanternWispPulseHz;
            return { { { kLanternWispCoreName, kLanternWispCoreRadius, true, kLanternWispAdditive, { 6, 8, 0 },
                           { kLanternWispCoreScroll[0], kLanternWispCoreScroll[1], pulse }, { 1.0f, 1.0f, kLanternWispCoreEmissive } },
                { kLanternWispShellName, kLanternWispShellRadius, false, kLanternWispNormal, { 6, 8 },
                    { kLanternWispShellScroll[0], kLanternWispShellScroll[1] }, { -1.0f, -1.0f } },
                { kLanternWispHaloName, kLanternWispHaloRadius, true, kLanternWispAdditive, {}, {}, {} } } };
        }

        /// The three spheres in a_info round a_centre: shader, palette, blend, controllers, radius; a_skinned for the worn copies
        void CheckWispSpheres(Checker& a_check, const std::string& a_what, const LanternNifInfo& a_info, const std::array<float, 3>& a_centre,
            bool a_skinned)
        {
            for (const auto& e : WispExpects()) {
                const auto* s = Shape(a_info, e.name);
                const std::uint32_t flags1 = (e.palette ? kLanternWispFlags1Palette : kLanternWispFlags1Dark) | (a_skinned ? kSlsf1Skinned : 0u);
                const std::string greyscale = std::string("greyscale ") + (e.palette ? kPalettePath : "");
                a_check.Check(s && s->shader == "BSEffectShaderProperty" && s->flags1 == flags1 && s->textures.size() == 2 &&
                                  s->textures[1] == greyscale && s->hasAlpha && s->alphaFlags == e.alpha,
                    fmt::format("lantern {} wisp '{}': effect shader 0x{:08X}, palette '{}', alpha 0x{:04X} ({})", a_what, e.name, flags1, greyscale,
                        e.alpha, e.alpha == kLanternWispAdditive ? "additive" : "normal blend"));
                bool round = s != nullptr;
                for (int a = 0; round && a < 3; ++a) {
                    round = Near(s->bounds.Size(a) * 0.5f, e.radius, 0.01f) && Near(s->bounds.Centre()[a], a_centre[a], 0.01f);
                }
                a_check.Check(round, fmt::format("lantern {} wisp '{}': a sphere of radius {} round ({:.3f}, {:.3f}, {:.3f}) ({})", a_what, e.name,
                                         e.radius, a_centre[0], a_centre[1], a_centre[2], s ? fmt::format("size {:.4f}, centre ({:.3f}, {:.3f}, {:.3f})",
                                                                                                s->bounds.Size(0), s->bounds.Centre()[0],
                                                                                                s->bounds.Centre()[1], s->bounds.Centre()[2]) :
                                                                                            std::string("missing")));
                bool controllers = s && s->controllerInfo.size() == e.variables.size();
                for (std::size_t c = 0; controllers && c < e.variables.size(); ++c) {
                    const auto& got = s->controllerInfo[c];
                    controllers = got.variable == e.variables[c] && got.flags == kLanternWispControllerFlags && Near(got.stop, e.stops[c]) &&
                                  got.keys.size() >= 2 && Near(got.keys.back()[1], e.ends[c]) && Near(got.keys.front()[1], e.variables[c] == 0 ? e.ends[c] : 0.0f) &&
                                  s->controllers[c].find("its shader") != std::string::npos;
                }
                a_check.Check(controllers, fmt::format("lantern {} wisp '{}': {} looping controllers on its own shader (U / V offset over {} s, pulse "
                                                       "{} Hz)", a_what, e.name, e.variables.size(),
                                               e.stops.empty() ? std::string("-") : fmt::format("{} / {}", e.stops[0], e.stops.size() > 1 ? e.stops[1] : 0.0f),
                                               kLanternWispPulseHz));
            }
        }

        /// The ground model's wisp: the spheres under HoMBF_Wisp, the hidden emitter, the bob and turn, the particles copied
        void CheckLanternGroundWisp(Checker& a_check, const LanternNifInfo& a_gnd, const std::filesystem::path& a_out)
        {
            const std::array<float, 3> centre{ 0.0f, 0.0f, kFrameHeight * 0.5f };
            CheckWispSpheres(a_check, "gnd", a_gnd, centre, false);
            bool parents = true;
            for (const auto& e : WispExpects()) {
                const auto* s = Shape(a_gnd, e.name);
                parents = parents && s && s->parent == kLanternWispNode && s->skinType.empty();
            }
            const auto* emitter = Shape(a_gnd, kLanternWispEmitterName);
            a_check.Check(parents && emitter && emitter->parent == kLanternWispNode && emitter->shader.empty() && (emitter->avFlags & 1u) != 0,
                "lantern gnd: the spheres (unskinned) and a hidden emitter without a shader under the wisp node");
            const float cycle = static_cast<float>(kLanternWispBobsPerTurn) / kLanternWispBobHz;
            const LanternController* motion = nullptr;
            int controllers = 0;
            for (const auto& [node, controller] : a_gnd.nodeControllers) {
                if (node == kLanternWispNode) {
                    motion = &controller;
                    ++controllers;
                }
            }
            bool bob = controllers == 1 && motion->type == "NiTransformController" && motion->flags == kLanternWispControllerFlags &&
                       Near(motion->stop, cycle) && motion->translations.size() == static_cast<std::size_t>(std::lround(cycle * kLanternWispKeyRate)) + 1;
            float low = 1e9f;
            float high = -1e9f;
            if (bob) {
                for (const auto& t : motion->translations) {
                    low = std::min(low, t[3]);
                    high = std::max(high, t[3]);
                    bob = bob && Near(t[1], centre[0]) && Near(t[2], centre[1]);
                }
                bob = bob && Near(low, centre[2] - kLanternWispBobAmplitude) && Near(high, centre[2] + kLanternWispBobAmplitude) &&
                      Near(motion->translations.front()[3], motion->translations.back()[3]) && motion->zRotations.size() == 2 &&
                      Near(motion->zRotations.back()[1], 2.0f * kPi) && Near(motion->zRotations.back()[0], cycle);
            }
            a_check.Check(bob, fmt::format("lantern gnd: the wisp node bobs {} round z {} (z {:.3f}..{:.3f}) and turns once about Z in one looping "
                                           "{:.3f} s cycle ({} controllers)", kLanternWispBobAmplitude, centre[2], low, high, cycle, controllers));
            const auto wisp = WispReadBack(a_out);
            a_check.Check(wisp.groundClearance >= kLanternWispClearance && Near(wisp.groundCentre[2], centre[2]) &&
                              Near(wisp.bobLow, centre[2] - kLanternWispBobAmplitude) && Near(wisp.bobHigh, centre[2] + kLanternWispBobAmplitude),
                fmt::format("lantern gnd: the halo clears the frame by {:.4f} anywhere along the bob (kLanternWispClearance {})", wisp.groundClearance,
                    kLanternWispClearance));
            a_check.Check(a_gnd.particles.size() == kStandIns.size(), fmt::format("lantern gnd: {} particle systems copied", a_gnd.particles.size()));
            for (const auto& standIn : kStandIns) {
                const auto it = std::find_if(a_gnd.particles.begin(), a_gnd.particles.end(), [&](const auto& a_p) { return a_p.name == standIn.name; });
                if (it == a_gnd.particles.end()) {
                    a_check.Check(false, fmt::format("lantern gnd: no particle system '{}'", standIn.name));
                    continue;
                }
                const auto& p = *it;
                const bool bombs = std::any_of(p.modifiers.begin(), p.modifiers.end(),
                    [](const std::string& a_m) { return a_m.find("NiPSysBombModifier") != std::string::npos; });
                const bool switches = std::any_of(p.lines.begin(), p.lines.end(),
                    [](const std::string& a_l) { return a_l.find("NiPSysModifierActiveCtlr") != std::string::npos; });
                a_check.Check(p.parent == kLanternWispNode && p.type == (standIn.strip ? "BSStripParticleSystem" : "NiParticleSystem") &&
                                  p.greyscale == kPalettePath && (p.flags1 & kSlsf1GreyscaleToPaletteColor) != 0 && p.emitterMeshes.size() == 1 &&
                                  p.emitterMeshes.front() == kLanternWispEmitterName && !bombs && !switches && p.outside.size() == 2,
                    fmt::format("lantern gnd particles '{}': under the wisp node, on GradBlackFlame.dds, emitting from '{}', no bombs nor their "
                                "switches, naming only the emitter and the node outside itself ({} outside)", standIn.name, kLanternWispEmitterName,
                        p.outside.size()));
                a_check.Check(Near(p.radius, standIn.radius * kLanternWispParticleScale, 1e-5f) &&
                                  Near(p.speed, standIn.speed * kLanternWispParticleScale, 1e-5f) && Near(p.birthRate, standIn.birth, 1e-5f),
                    fmt::format("lantern gnd particles '{}': radius {:.4f}, speed {:.4f} (x kLanternWispParticleScale {}), birth rate {} kept", standIn.name,
                        p.radius, p.speed, kLanternWispParticleScale, p.birthRate));
            }
        }

        /// The worn model's wisp: the spheres skinned round the cage centre, clear of the frame
        void CheckLanternWornWisp(Checker& a_check, const LanternNifInfo& a_worn, const LanternBuildResult& a_result, const std::filesystem::path& a_out)
        {
            CheckWispSpheres(a_check, "worn", a_worn, a_result.wornCage, true);
            const auto wisp = WispReadBack(a_out);
            a_check.Check(wisp.wornClearance >= kLanternWispClearance && Near(wisp.wornClearance, a_result.wispWornClearance) &&
                              Near(wisp.wornCentre[0], a_result.wornCage[0]) && Near(wisp.wornCentre[1], a_result.wornCage[1]) &&
                              Near(wisp.wornCentre[2], a_result.wornCage[2]),
                fmt::format("lantern worn: the wisp at the cage centre, its halo {:.4f} off the frame (build {:.4f}; kLanternWispClearance {})",
                    wisp.wornClearance, a_result.wispWornClearance, kLanternWispClearance));
        }
    }

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
        MakeWispParticles(in / kLanternWispSource);
        a_check.Check(WriteTestTexture(in / kLanternDiffuseSource, kDxgiBc3, true), "lantern: made-up sources written");

        // the bodies: a hip box whose left side is at x -kHipHalfX and the belt band kBandProud proud of it (both body paths)
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
            const auto bandless = a_dir / "lantern" / "bodies_noband";
            MakeBodies(bandless, { { Vector3(-kHipHalfX, -6.0f, 30.0f), Vector3(kHipHalfX, 6.0f, 100.0f) } }, false);
            const auto noBand = BuildLantern({ in, bandless }, a_dir / "lantern" / "out_noband");
            a_check.Check(!noBand.ok && Mentions(noBand.problems, kLanternBandShapes[0]),
                fmt::format("lantern: a body without its belt band shape fails, the shape named ('{}')", kLanternBandShapes[0]));
        }
        a_check.Check(result.bands.size() == 2 && std::all_of(result.bands.begin(), result.bands.end(), [](const LanternBand& a_band) {
            return a_band.vertices > 0 && Near(a_band.top, kBandTop) && Near(a_band.bottom, kBandBottom);
        }),
            fmt::format("lantern: the belt band measured on both bodies (top z {}, bottom z {}: {})", kBandTop, kBandBottom,
                result.bands.empty() ? std::string("none") : fmt::format("{:.3f} / {:.3f}", result.bands.front().top, result.bands.front().bottom)));

        // the ground model
        const auto gnd = InspectLanternNif(out / kLanternGroundNif);
        const auto* frame = gnd.shapes.empty() ? nullptr : &gnd.shapes.front();
        a_check.Check(gnd.shapes.size() == 5 && frame && frame->type == "BSTriShape" && frame->shader == "BSLightingShaderProperty" &&
                          !frame->textures.empty() && frame->textures.front() == fmt::format("[0] {}", kLanternDiffuse),
            fmt::format("lantern gnd: the frame (first, diffuse '{}') and the wisp's four shapes ({} shapes)", kLanternDiffuse, gnd.shapes.size()));
        a_check.Check(frame && frame->textures.size() > 1 && frame->textures[1] == fmt::format("[1] {}", kSourceNormal),
            "lantern gnd: the normal map kept as the source names it");
        a_check.Check(gnd.hasCollision && gnd.rootType == "BSFadeNode", "lantern gnd: collision and the BSFadeNode root kept");
        a_check.Check(gnd.hasBsx && gnd.bsx == (kFrameBsx | kBsxAnimated) && gnd.valueNodes.empty(),
            fmt::format("lantern gnd: BSXFlags 0x{:X} = source | Animated (no Addon bit), no addon node ({})", gnd.bsx, gnd.valueNodes.size()));
        CheckLanternGroundWisp(a_check, gnd, out);
        a_check.Check(Near(result.frameBounds.max[1], kHookY) && Near(result.frameBounds.Size(2), kFrameHeight),
            "lantern gnd: the frame bounds reported with the hook, for the record's ObjectBounds");

        // the worn model
        const auto worn = InspectLanternNif(out / kLanternWornNif);
        a_check.Check(worn.shapes.size() == 8 && Shape(worn, kLanternWornFrameName) && Shape(worn, kLanternWispCoreName) &&
                          Shape(worn, kLanternWispShellName) && Shape(worn, kLanternWispHaloName) && Shape(worn, kLanternStrapName) &&
                          Shape(worn, kLanternBracketName) && Shape(worn, kLanternRingName) && Shape(worn, kLanternLugName),
            fmt::format("lantern worn: the frame, the wisp's core, shell and halo, the strap, the bracket, the ring and the lugs ({} shapes)",
                worn.shapes.size()));
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
        CheckLanternWornWisp(a_check, worn, result, out);
        const auto* wornFrame = Shape(worn, kLanternWornFrameName);
        const float scale = kLanternWornHeight / kFrameHeight;
        // the hook (z kHookZ0..kFrameHeight, out to y kHookY) turned up about its hinge (y kFrameHalfY, z kHookHinge) and
        // set kLanternHandleHeight tall: the frame's top (worn units, its bottom at 0) at kHookHinge x scale + that
        const float wornTop = kHookHinge * scale + kLanternHandleHeight;
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
        const auto* strap = Shape(worn, kLanternStrapName);
        const auto* ring = Shape(worn, kLanternRingName);
        const auto* bracket = Shape(worn, kLanternBracketName);
        const float tipZ = result.strapBottom - kLanternBracketBelow - kLanternBracketDrop;  // the ring's top tube centre
        // the strap on the band: its fold's top kLanternStrapOverBand over the band's top, its inner end kLanternStrapUnderBand
        // under the band's bottom (the end cap leans with the inner layer's step onto the band: within 0.15), its outer end
        // kLanternStrapOuterLength under the top
        const float strapTop = kBandTop + kLanternStrapOverBand;
        a_check.Check(strap && Near(strap->bounds.max[2], strapTop) && Near(result.strapTop, strapTop) &&
                          Near(result.strapInnerBottom, kBandBottom - kLanternStrapUnderBand) &&
                          Near(strap->bounds.min[2], kBandBottom - kLanternStrapUnderBand, 0.15f) &&
                          Near(result.strapBottom, strapTop - kLanternStrapOuterLength),
            fmt::format("lantern worn: the strap's top z {:.3f} = band top {} + {}, its inner end under the band at z {:.3f} (band bottom {} - {}), "
                        "its outer end z {:.3f}", strap ? strap->bounds.max[2] : 0.0f, kBandTop, kLanternStrapOverBand,
                strap ? strap->bounds.min[2] : 0.0f, kBandBottom, kLanternStrapUnderBand, result.strapBottom));
        // the edges wobble across the width (up to kLanternStrapEdgeWobble each, not mirrored); the inner layer's body side
        // no nearer the hip than kLanternStrapBodyGap
        a_check.Check(strap && ring && strap->bounds.Size(1) <= kLanternStrapWidth + 2.0f * kLanternStrapEdgeWobble + kTolerance &&
                          strap->bounds.Size(1) >= kLanternStrapWidth - 2.0f * kLanternStrapEdgeWobble - kTolerance &&
                          std::fabs((strap->bounds.max[1] - kLanternStrapY) - (kLanternStrapY - strap->bounds.min[1])) > 1e-3f &&
                          strap->bounds.max[0] <= -kHipHalfX - kLanternStrapBodyGap + kTolerance && Near(ring->bounds.max[2], tipZ + kLanternRingTube),
            fmt::format("lantern worn: the strap {:.3f} wide (y {:.3f}..{:.3f}, wobbled edges), its body side at x {:.3f}, the ring's top tube "
                        "at the bracket's tip", strap ? strap->bounds.Size(1) : 0.0f, strap ? strap->bounds.min[1] : 0.0f,
                strap ? strap->bounds.max[1] : 0.0f, strap ? strap->bounds.max[0] : 0.0f));
        // the strap's outer layer centreline at its lower end: the band at -(kHipHalfX + kBandProud), the inner layer
        // kLanternStrapBodyGap + half the thickness off it, the outer layer a thickness and kLanternStrapBeltGap further out
        const float outerX = -kHipHalfX - kBandProud - kLanternStrapBodyGap - kLanternStrapThickness * 1.5f - kLanternStrapBeltGap;
        a_check.Check(strap && Near(strap->bounds.min[0], outerX - kLanternStrapThickness * 0.5f - kLanternStrapBulge),
            fmt::format("lantern worn: the outer layer's outer face bulges {} (strap min x {:.4f}, expected {:.4f})", kLanternStrapBulge,
                strap ? strap->bounds.min[0] : 0.0f, outerX - kLanternStrapThickness * 0.5f - kLanternStrapBulge));
        a_check.Check(Near(result.strapOuterBottomX, outerX) && Near(result.ringCentre[0], outerX - kLanternBracketLength) &&
                          Near(result.ringCentre[2], tipZ - kLanternRingRadius),
            fmt::format("lantern worn: the ring centre kLanternBracketLength {} out from the strap's outer layer (x {:.3f} -> {:.3f}) at z {:.3f}",
                kLanternBracketLength, result.strapOuterBottomX, result.ringCentre[0], result.ringCentre[2]));
        a_check.Check(bracket && Near(bracket->bounds.max[2], result.strapBottom + kLanternBracketGrip) && Near(bracket->bounds.min[2], tipZ) &&
                          Near(bracket->bounds.Size(1), kLanternBracketWidth) &&
                          Near(bracket->bounds.max[0], outerX + kLanternBracketThickness * 0.5f) &&
                          Near(bracket->bounds.min[0], result.ringCentre[0] - kLanternBracketThickness * 0.5f),
            fmt::format("lantern worn: the bracket from its plate in the strap's lower end (z {}) out to the ring and down to its tube (z {})",
                result.strapBottom + kLanternBracketGrip, tipZ));
        {
            // the bracket's bevel (normals at 45 degrees between a broad face and an edge face) and its taper (the tip
            // kLanternBracketTaper narrower than the plate's top)
            nifly::NifFile file;
            LoadNif(file, out / kLanternWornNif);
            nifly::BSTriShape* shape = nullptr;
            for (auto* s : file.GetShapes()) {
                if (s->name.get() == kLanternBracketName) {
                    shape = dynamic_cast<nifly::BSTriShape*>(s);
                }
            }
            int bevelled = 0;
            float tipLo = 1e9f, tipHi = -1e9f, plateLo = 1e9f, plateHi = -1e9f;
            if (shape && bracket) {
                for (const auto& n : shape->UpdateRawNormals()) {
                    bevelled += std::fabs(std::fabs(n.y) - std::sqrt(0.5f)) < 0.05f ? 1 : 0;
                }
                for (const auto& vertex : shape->vertData) {
                    if (Near(vertex.vert.z, bracket->bounds.min[2])) {
                        tipLo = std::min(tipLo, vertex.vert.y);
                        tipHi = std::max(tipHi, vertex.vert.y);
                    }
                    if (Near(vertex.vert.z, bracket->bounds.max[2])) {
                        plateLo = std::min(plateLo, vertex.vert.y);
                        plateHi = std::max(plateHi, vertex.vert.y);
                    }
                }
            }
            // normals within 0.05 (they are stored in 8 bits), the widths within 0.01
            a_check.Check(bevelled > 0 && Near(plateHi - plateLo, kLanternBracketWidth, 0.01f) &&
                              Near(tipHi - tipLo, kLanternBracketWidth * (1.0f - kLanternBracketTaper), 0.01f),
                fmt::format("lantern worn: the bracket's edges bevelled ({} vertex normals at 45 degrees) and its width tapered from {:.4f} at "
                            "the plate to {:.4f} at the tip (expected {:.4f})", bevelled, plateHi - plateLo, tipHi - tipLo,
                    kLanternBracketWidth * (1.0f - kLanternBracketTaper)));
        }
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
            bool innerGap = clear;
            std::string text;
            for (const auto& c : clearances) {
                clear = clear && c.gap.points > 0 && c.gap.inside == 0 && c.gap.nearest >= c.margin;
                if (c.part == "strap") {
                    innerGap = innerGap && c.gap.nearest >= 0.2f && c.gap.nearest <= 0.4f;
                }
                text += fmt::format(" {} {:.3f};", c.part, c.gap.nearest);
            }
            a_check.Check(clear, fmt::format("lantern worn: lantern (with the lugs), strap, bracket and ring clear of both bodies by their "
                                             "margins ({})", text));
            a_check.Check(innerGap, fmt::format("lantern worn: the strap's inner layer 0.2..0.4 off the band and the hip on both bodies "
                                                "(kLanternStrapBodyGap {}; {})", kLanternStrapBodyGap, text));
            // round 5: the outer layer lies on the inner one (the made-up hip is flat: the read-back is exact on it)
            const auto gap = StrapLayerGap(out);
            a_check.Check(Near(gap[0], kLanternStrapBeltGap) && Near(gap[1], kLanternStrapBeltGap) && gap[0] >= kLanternStrapLayerGapMin &&
                              gap[0] <= kLanternStrapLayerGapMax,
                fmt::format("lantern worn: the strap's layers {:.4f}..{:.4f} apart (kLanternStrapBeltGap {}, within {}..{})", gap[0], gap[1],
                    kLanternStrapBeltGap, kLanternStrapLayerGapMin, kLanternStrapLayerGapMax));
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
            // the wisp's particles missing: the ground fails, the source named
            const auto noWisp = a_dir / "lantern" / "in_nowisp";
            std::filesystem::remove_all(noWisp);
            std::filesystem::copy(in, noWisp, std::filesystem::copy_options::recursive);
            std::filesystem::remove(noWisp / kLanternWispSource);
            const auto wispless = BuildLantern({ noWisp, bodies }, a_dir / "lantern" / "out_nowisp");
            a_check.Check(!wispless.ok && Mentions(wispless.problems, kLanternWispSource),
                "lantern: the wisp's particle source missing fails the ground model, named");
        }
        {
            // a thigh box beside the hanging cage: the bracket grows until the lantern clears it, and the read-back agrees. The
            // cage's inboard edge is the ring x + kFrameHalfY x 20/30 (the frame's half depth, yawed, scaled) = outerX - L + 5.333
            // = -8.917 - L (round 5: the strap's layers 0.3 apart); with the thigh's side at kThighX it clears by kLanternBodyMargin
            // from L = -7.917 - kThighX = 7.333, so the loop lands on 7.5 (7.0 misses by 0.333)
            constexpr float kThighX = -15.25f;
            const float thighLength = std::ceil((kLanternBodyMargin - kThighX + outerX + kFrameHalfY * scale) / kLanternBracketLengthStep) *
                                      kLanternBracketLengthStep;
            const auto thigh = a_dir / "lantern" / "bodies_thigh";
            MakeBodies(thigh, { { Vector3(-kHipHalfX, -6.0f, 30.0f), Vector3(kHipHalfX, 6.0f, 100.0f) },
                                  { Vector3(kThighX, -20.0f, 0.0f), Vector3(kHipHalfX, 20.0f, 56.0f) } });
            const auto longer = BuildLantern({ in, thigh }, a_dir / "lantern" / "out_longer");
            a_check.Check(longer.ok && Near(thighLength, 7.5f) && Near(longer.bracketLength, thighLength) && Mentions(longer.log, "bracket length 7 (") &&
                              WornAxisTiltDeg(a_dir / "lantern" / "out_longer") <= kLanternVerticalToleranceDeg,
                fmt::format("lantern: a thigh beside the cage lengthens the bracket in kLanternBracketLengthStep steps (to {}, expected {}), "
                            "the lantern still vertical, and the build's read-back passes", longer.bracketLength, thighLength));
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
            // a ring of the cage walls through the middle of the cage: the halo would touch it, the build refuses it
            const auto crowded = a_dir / "lantern" / "in_crowded";
            std::filesystem::remove_all(crowded);
            std::filesystem::copy(in, crowded, std::filesystem::copy_options::recursive);
            MakeFrame(crowded / kLanternFrameSource, kFrameBoxTop, { 0.45f, 0.72f });
            const auto bar = BuildLantern({ crowded, bodies }, a_dir / "lantern" / "out_crowded");
            a_check.Check(!bar.ok && Mentions(bar.problems, "wisp's halo comes within"),
                "lantern: a cage bar within kLanternWispClearance of the wisp's halo fails the build");
        }
        {
            // the core's palette and the node's motion taken away in a copy: the read-back refuses each
            const auto bare = a_dir / "lantern" / "out_bare";
            std::filesystem::remove_all(bare);
            std::filesystem::copy(out, bare, std::filesystem::copy_options::recursive);
            nifly::NifFile bareGnd;
            LoadNif(bareGnd, bare / kLanternGroundNif);
            auto& hdr = bareGnd.GetHeader();
            for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
                if (auto* node = hdr.GetBlock<nifly::NiNode>(id); node && node->name.get() == kLanternWispNode) {
                    node->controllerRef.index = nifly::NIF_NPOS;
                }
            }
            for (auto* shape : bareGnd.GetShapes()) {
                if (shape->name.get() == kLanternWispCoreName) {
                    if (auto* effect = hdr.GetBlock<nifly::BSEffectShaderProperty>(shape->ShaderPropertyRef())) {
                        effect->greyscaleTexture.get() = kPaletteAddPath;
                    }
                }
            }
            SaveNif(bareGnd, bare / kLanternGroundNif);
            const auto problems = VerifyLantern(inputs, bare);
            a_check.Check(Mentions(problems, fmt::format("gnd '{}': textures", kLanternWispCoreName)) &&
                              Mentions(problems, fmt::format("gnd: '{}' has 0 controllers", kLanternWispNode)),
                "lantern: the read-back refuses a wisp core off GradBlackFlame.dds and a wisp node without its motion");
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

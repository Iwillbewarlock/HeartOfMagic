#pragma once

// =============================================================================
// GreyLantern - the Black Flame lantern's meshes and texture (--write-lantern; lantern spec section 6)
// =============================================================================
// docs/superpowers/specs/2026-10-21-black-flame-lantern-design.md, section 6 and records 0x80D / 0x80E / 0x815. From
// vanilla files in the --in folders (loose, Data layout; extract Skyrim - Meshes0.bsa / Skyrim - Textures*.bsa first):
//   lantern_gnd.nif    candlelantern01.nif's frame and collision, diffuse = the soot copy, normal map kept, and the fire
//                      (round 7): two closed spindles made here on the cage axis (kLanternFlameLayers), the outer layer's
//                      base at kLanternFlameBaseShare of the frame's height; BSXFlags Animated (the layers' controllers), no
//                      Addon
//   lantern_worn.nif   at the left hip, every shape 100% on NPC Pelvis [Pelv] (BSDismemberSkinInstance, one partition, body
//                      part 60): an iron ring (made here, the soot lamp texture) beside the hip at the belt band's height, and
//                      the frame without collision (handle turned upright and shortened, iron lugs under its feet) hanging
//                      vertically from it, with the same fire as the ground model in its cage. Nothing lies on the body
//                      (round 6, user: no clipping with whatever the wearer has on)
// The fire's shaders: each layer a copy of the Flame Atronach's FireStrip08 effect shader and its UV scroll controller
// (kLanternFlameTemplate, the atronach NIF is read for nothing else), palette colour on and palette alpha off (with it on,
// the opacity is the palette's alpha alone and our palettes' rows are all alike) and falloff on, so the opacity is texture
// alpha x vertex alpha x base alpha x falloff (fading the outline edge-on); U texture scale kLanternFlameUScale; the outer layer blends normally and takes GradBlackFlame.dds through GreyEffects, the core is
// additive and takes GradBlackFlameAdd.dds
//   lantern_smoke.nif  an empty BSFadeNode with BSXFlags and one AddOnNode9123 (grey ADDN 0x809 MPSFireSmSmoke01)
//   lantern_empty.nif  a root BSFadeNode HoMBF_LanternEmpty and nothing else (no geometry, no BSXFlags): the ARMA's biped model
//                      while Immersive Equipment Displays shows the lantern (plugins/BlackFlame/src/LanternIed.h)
//   LampGenericSoot01.dds  LampGeneric01.dds with RGB x kLanternSootDarken, alpha kept, same format and mips
// An AddOnNode is a BSValueNode whose value is the ADDN index (what the engine reads; vanilla candlelanternwithcandle01.nif
// AddOnNode49 = BSValueNode value 49), named AddOnNode<index> as the vanilla files name theirs.

#include "GreyBuild.h"
#include "GreyShapes.h"

#include <Object3d.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nifly
{
    class NifFile;
}

namespace GreyBuild
{
    // Sources (Data-relative, as FindSource looks them up in the --in folders)
    inline constexpr const char* kLanternFrameSource = "meshes\\clutter\\common\\candlelantern01.nif";
    inline constexpr const char* kLanternSkeletonSource = "meshes\\actors\\character\\character assets\\skeleton.nif";
    inline constexpr const char* kLanternFlameSource = "meshes\\actors\\atronachflame\\character assets\\atronachflame.nif";
    inline constexpr const char* kLanternDiffuseSource = "textures\\clutter\\common\\lampgeneric01.dds";

    // Outputs (Data-relative, under the --write-lantern folder)
    inline constexpr const char* kLanternGroundNif = "meshes\\heartofmagic\\blackflame\\lantern\\lantern_gnd.nif";
    inline constexpr const char* kLanternWornNif = "meshes\\heartofmagic\\blackflame\\lantern\\lantern_worn.nif";
    inline constexpr const char* kLanternSmokeNif = "meshes\\heartofmagic\\blackflame\\lantern\\lantern_smoke.nif";
    inline constexpr const char* kLanternEmptyNif = "meshes\\heartofmagic\\blackflame\\lantern\\lantern_empty.nif";
    // The texture's file and the text the NIF slot names (the slot keeps the case the other addon paths use)
    inline constexpr const char* kLanternDiffuse = "textures\\HeartOfMagic\\BlackFlame\\Lantern\\LampGenericSoot01.dds";

    // The grey ADDN index the smoke static's addon node names (shipped record 0x809 MPSFireSmSmoke01). The ground model no
    // longer names 0x804 MPSFireSmall01 (round 6: that grey copy is all additive on GradBlackFlame.dds without the dark
    // layer; the fire is the flame layers instead)
    inline constexpr int kLanternSmokeAddon = 9123;
    // BSValueNode value flags as the precedent has them: the vanilla smoke static fxsmokechimney01.nif's AddOnNode56 0
    inline constexpr std::uint8_t kLanternSmokeValueFlags = 0;
    // BSXFlags bit 0, Animated (nif.xml BSXFlags): the ground model's flame layers run shader controllers, as the animated vanilla
    // effect static fxfirewithembers01_lite.nif (0x1); bit 4, Addon: a model that carries addon nodes (the ground no longer)
    inline constexpr std::uint32_t kBsxAnimated = 1u << 0;
    inline constexpr std::uint32_t kBsxAddon = 1u << 4;
    // lantern_smoke.nif's BSXFlags: the value the vanilla effect statics with an AddOnNode carry, fxsmokechimney01.nif and
    // fxfire01new.nif (read 2026-10-21 from Skyrim - Meshes0.bsa): 0x31 = Animated | Addon | EditorMarker. Copied as it is;
    // the EditorMarker bit names a shape this model does not have, which leaves nothing for it to hide
    inline constexpr std::uint32_t kLanternSmokeBsx = 0x31;
    inline constexpr const char* kLanternSmokeRootName = "HoMBF_LanternSmoke";
    inline constexpr const char* kLanternEmptyRootName = "HoMBF_LanternEmpty";
    inline constexpr const char* kLanternWornRootName = "HoMBF_LanternWorn";
    inline constexpr const char* kLanternWornFrameName = "HoMBF_LanternFrame";

    // The worn mesh: the bone and its body part
    inline constexpr const char* kLanternBone = "NPC Pelvis [Pelv]";
    inline constexpr std::uint16_t kLanternBodyPart = 60;
    // SLSF1 bit 1, Skinned (CommonLibSSE-NG BSShaderProperty.h EShaderPropertyFlag kSkinned = 1)
    inline constexpr std::uint32_t kSlsf1Skinned = 1u << 1;
    // SLSF1 bit 3, Vertex_Alpha (EShaderPropertyFlag kVertexAlpha = 3)
    inline constexpr std::uint32_t kSlsf1VertexAlpha = 1u << 3;
    // SLSF1 bit 6, Use_Falloff (EShaderPropertyFlag kFalloff = 6)
    inline constexpr std::uint32_t kSlsf1Falloff = 1u << 6;
    // SLSF1 bits 26 and 27, Decal and Dynamic_Decal (EShaderPropertyFlag kDecal = 26, kDynamicDecal = 27): the atronach body's
    inline constexpr std::uint32_t kSlsf1Decal = 1u << 26;
    inline constexpr std::uint32_t kSlsf1DynamicDecal = 1u << 27;
    // SLSF2 bit 4, Double_Sided (EShaderPropertyFlag kTwoSided = 36, the second word's bit 4)
    inline constexpr std::uint32_t kSlsf2DoubleSided = 1u << 4;

    // The cage centre: X and Y from the vertex bounds of the frame's vertices in this middle share of its height (the
    // cage walls; candlelantern01's hook at the top reaches out in +Y and would pull a whole-bounds centre off the cage),
    // Z the middle of the whole bounds' height
    inline constexpr float kLanternCageBand = 0.5f;

    // ---- The worn model (round 6, 2026-10-21: ring and lantern only). Character space in the skeleton's reference pose: +Y
    // forward, -X the wearer's left, Z up. An iron ring sits beside the left hip at the belt band's height (hooked on the
    // wearer's own belt) and the lantern hangs vertically (its cage axis world Z) from it by its upright handle. Every part
    // is placed on the vanilla male and female bodies (kLanternBodySources, required) and checked against both.
    inline constexpr std::array<const char*, 2> kLanternBodySources{ "meshes\\actors\\character\\character assets\\malebody_1.nif",
        "meshes\\actors\\character\\character assets\\femalebody_1.nif" };

    // The frame: scaled so its height before the handle turn is this (0.8 x round 1's 25)
    inline constexpr float kLanternWornHeight = 20.0f;
    // The handle: the one frame piece (LanternPieces) that reaches past the cage walls in +Y (candlelantern01's flat
    // loop, z 27.2-28.3), turned about +X by this around its hinge (the piece's -Y edge at its middle height) so it stands
    // up. Its hinge is first moved in Y onto the cage axis (candlelantern01's sits 2.5 units off it): a lantern hanging
    // vertically under gravity has its centre of mass, the cage axis, straight below the handle's top
    inline constexpr float kLanternHandleTurnDeg = 90.0f;
    // Then the whole lantern turned about Z (around the cage's vertical axis) by this: the upright loop lies along the body
    // side (the YZ plane; across X it would reach 3.6 units into the hip) and the cage sits outboard of its hinge
    inline constexpr float kLanternWornYawDeg = -90.0f;
    // The upright handle scaled along Z about its feet (its lowest point) to this height (worn units; candlelantern01's
    // stands 7.86 at the worn scale and read as a stem). The scale squashes the top wire's section along Z by the same
    // share; LanternBuildResult::handleWire is that squashed half height
    inline constexpr float kLanternHandleHeight = 6.0f;

    // The handle's feet: its vertices within kLanternFootBand (worn units) of its lowest point, split in two at the middle
    // of its wider horizontal extent. Under each a lug (a hinge mount, an iron block kLanternLugSize X x Y) stands on the
    // lid: from kLanternLugSink below the lid's lowest point under its footprint up to kLanternLugOverlap into the foot,
    // sunk further when that is under kLanternLugMinHeight; a foot more than kLanternLugMaxHeight - kLanternLugSink -
    // kLanternLugOverlap above the lid fails the build. The lid: over a kLanternLugSamples x kLanternLugSamples grid of the
    // footprint, the frame's highest surface straight below each point (the handle left out); a point with nothing within
    // kLanternLugMaxHeight below the foot is a hole the lug bridges (candlelantern01's handle bar runs through a half-round
    // hinge tunnel on the lid and the plate is notched under each bar end, x +-0.92 by 0.6 at the worn scale; the lug's Y
    // length reaches past the notch onto the plate). The lug's UVs: the lid's UV at its highest point at that triangle's
    // texel density
    inline constexpr float kLanternFootBand = 0.3f;
    inline constexpr std::array<float, 2> kLanternLugSize{ 0.8f, 1.4f };
    inline constexpr int kLanternLugSamples = 5;
    inline constexpr float kLanternLugSink = 0.05f;
    inline constexpr float kLanternLugOverlap = 0.05f;
    inline constexpr float kLanternLugMinHeight = 0.25f;
    inline constexpr float kLanternLugMaxHeight = 0.4f;
    inline constexpr float kLanternLugContact = 0.02f;  // the read-back: lug to lid and lug to foot within this
    inline constexpr const char* kLanternLugName = "HoMBF_LanternLugs";

    // The belt band (round 4; round 6: it sets the ring's height): the vanilla bodies' underwear band at the left hip,
    // measured on each body's band shape
    // (kLanternBandShapes, paired with kLanternBodySources; required) in the reference pose: its vertices with X between
    // kLanternBandXMin and kLanternBandXMax and Z within kLanternBandZWindow of the pelvis (FemaleUnderwear also holds the
    // breast band, far above). The band top is the highest of them, the band bottom the lowest. malebody_1 measured in
    // Blender 2026-10-21: top z 77.15, bottom 71.58.
    inline constexpr std::array<const char*, 2> kLanternBandShapes{ "MaleUnderwear_1", "FemaleUnderwear" };
    inline constexpr float kLanternBandXMin = -17.0f;          // relative to the pelvis' X
    inline constexpr float kLanternBandXMax = -12.0f;
    inline constexpr std::array<float, 2> kLanternBandZWindow{ -15.0f, 15.0f };  // relative to the pelvis' Z

    // The ring: a torus in the XZ plane (its hole along Y, the handle's loop through it), its centre at the highest belt band
    // top (the male body's) beside the left hip, kLanternRingY forward of the pelvis (round 4's strap Y). It starts with its
    // innermost point kLanternRingMargin off the bodies' leftmost surface within its reach (SurfaceLeft over the ring's Y
    // and Z span grown by the margin, X within kLanternHipWindow of the pelvis; where its nearest body gap then reads under
    // the margin less half of kLanternRingFitTolerance it moves out by the shortfall, at most kLanternRingFitRounds rounds)
    // and steps out (-X) by kLanternRingOutStep while the lantern hung from it comes nearer than kLanternBodyMargin to a body
    // (or the ring nearer than its margin), up to kLanternRingOutMax past which the build fails (no tilt). Round 6: hung
    // from a ring hugging the hip, the cage (about 11 units across, centred under the ring) reaches into the hip and the
    // thigh; walking the ring forward (+Y) instead does not clear it before the ring runs off the belly (y 11, logged
    // 2026-10-21)
    inline constexpr float kLanternRingRadius = 1.4f;
    inline constexpr float kLanternRingTube = 0.3f;
    inline constexpr int kLanternRingSegments = 24;
    inline constexpr int kLanternRingSides = 8;
    inline constexpr const char* kLanternRingName = "HoMBF_LanternRing";

    // The ring's UVs on LampGeneric01.dds (the soot copy): the plainest iron of the atlas, no rod or rim
    // crossing it in the diffuse or the normal map (luminance std 10.5 of 0..255, normal map std <= 8; the handle's box
    // round 2 reused, 0.05-0.85 x 0.66-0.98, crosses four embossed rods)
    inline constexpr std::array<float, 2> kLanternIronUvMin{ 0.80f, 0.60f };
    inline constexpr std::array<float, 2> kLanternIronUvMax{ 0.98f, 0.80f };

    // The lantern hangs vertically, its handle's top wire resting on the ring's bottom tube, no tilt
    inline constexpr float kLanternBodyMargin = 1.0f;          // the lantern: frame, flame and lugs
    inline constexpr float kLanternRingMargin = 0.3f;          // the ring
    inline constexpr float kLanternRingY = 3.0f;
    inline constexpr float kLanternRingOutStep = 0.5f;
    inline constexpr float kLanternRingOutMax = 12.0f;
    // The body's left side is read within this X of the pelvis origin (the hanging forearm at x -21..-29 is outside it)
    inline constexpr float kLanternHipWindow = 20.0f;
    inline constexpr float kLanternRingFitTolerance = 0.02f;
    inline constexpr int kLanternRingFitRounds = 64;
    // The read-back's vertical check: the worn frame's cage axis (LanternAxisTiltDeg) within this of world Z
    inline constexpr float kLanternVerticalToleranceDeg = 0.5f;
    // LanternAxisTiltDeg's reference vertices: the frame's lowest kLanternAxisLowShare of its height, and the vertices
    // above that up to kLanternAxisTopShare (below candlelantern01's handle, which starts at 86% of the height)
    inline constexpr float kLanternAxisLowShare = 0.3f;
    inline constexpr float kLanternAxisTopShare = 0.8f;

    // The flame (round 7, 2026-10-21, user: two layers, seen from every angle, the same on the ground, worn and in the
    // inventory): two closed spindles (lathe shapes, Z up) on the cage axis in the ground model's root space, the worn model
    // taking them through the lantern's own transform. The outer layer kLanternFlameHeightShare of the frame's height tall and
    // kLanternFlameRadiusShare x the smaller of the cage walls' X and Y sizes at its widest, its base kLanternFlameBaseShare
    // of the frame's height above the frame's bottom (the fire rises inside the cage from about a third of its height); each
    // layer scales that by its own shares. Every flame vertex at least kLanternFlameClearance (worn units) from the worn
    // frame's triangles, and the outer layer's widest ring that much (at the ground scale) inside the cage walls
    inline constexpr float kLanternFlameHeightShare = 0.40f;
    inline constexpr float kLanternFlameRadiusShare = 0.5f * 0.55f;
    inline constexpr float kLanternFlameBaseShare = 1.0f / 3.0f;
    inline constexpr float kLanternFlameClearance = 0.5f;
    // Around the axis: this many segments, the seam duplicated so U runs 0..1 (it wraps on the scrolling tile)
    inline constexpr int kLanternFlameSegments = 12;
    // The profile, bottom to top: { share of the height (V = 1 - it), share of the widest radius, vertex alpha }. The 0.60
    // ring's alpha is the linear blend of its neighbours; the tip is a ring (no degenerate point); no bottom cap, and the open
    // bottom ring at alpha 0 (round 8: at 0.5 its rim showed as a hard horizontal line), the 0.15 ring 0.7 so the fade is
    // short and the body stays full
    inline constexpr std::array<std::array<float, 3>, 6> kLanternFlameProfile{ { { 0.00f, 0.55f, 0.0f }, { 0.15f, 0.90f, 0.7f },
        { 0.30f, 1.00f, 1.0f }, { 0.60f, 0.70f, 0.782f }, { 0.85f, 0.35f, 0.6f }, { 1.00f, 0.02f, 0.0f } } };
    inline constexpr std::uint16_t kLanternFlameVertices =
        static_cast<std::uint16_t>((kLanternFlameSegments + 1) * kLanternFlameProfile.size());
    inline constexpr std::uint32_t kLanternFlameTriangles =
        static_cast<std::uint32_t>(2 * kLanternFlameSegments * (kLanternFlameProfile.size() - 1));
    // The shape the layers' shader and its controller are copied from (the V-offset UV scroll)
    inline constexpr const char* kLanternFlameTemplate = "FireStrip08";

    /// One flame layer: its shape name, height and widest radius as shares of the outer layer's, its base this share of the
    /// outer layer's height above the outer base, its NiAlphaProperty flags, its shader's base alpha and its falloff
    /// { start angle, stop angle, start opacity, stop opacity } (the angles as cosines of the view-to-normal angle)
    struct LanternFlameLayer
    {
        const char* name;
        float heightShare;
        float radiusShare;
        float lift;
        std::uint16_t alphaFlags;
        float baseAlpha;
        std::array<float, 4> falloff;
    };
    // Alpha flags: 0x10ED blend on, SRC_ALPHA / INV_SRC_ALPHA (normal); 0x100D SRC_ALPHA / ONE (additive); no alpha test.
    // Base alpha 1 on both: FXFireScrollTile02's alpha averages 0.34 (p90 0.60), which already keeps the outer layer thin.
    // Falloff (round 8, user: the hard lathe silhouette read as a glass spindle holding the fire): opacity 1 facing the
    // camera down to 0 edge-on, so the outline fades instead of ending in a line; the core reaches 0 earlier (cos 0.2, 78
    // degrees) so it stays a glow inside the outer layer. Effect.hlsl: smoothstep of (|N.V| - start) / (stop - start)
    inline constexpr std::array<LanternFlameLayer, 2> kLanternFlameLayers{ {
        { "HoMBF_FlameOuter", 1.0f, 1.0f, 0.0f, 0x10ED, 1.0f, { 1.0f, 0.0f, 1.0f, 0.0f } },
        { "HoMBF_FlameCore", 0.65f, 0.45f, 0.05f, 0x100D, 1.0f, { 1.0f, 0.2f, 1.0f, 0.0f } },
    } };
    // The layers' U texture scale (round 8): the scrolling fire tile twice round the spindle, more and narrower tongues; U runs
    // 0..1 over the segments and the template's clamp mode is wrap (FireStrip08's 0x0000FF03, low byte 3 = wrap S wrap T,
    // checked by the read-back), so the seam still meets itself. V scale kept (the template's 1)
    inline constexpr float kLanternFlameUScale = 2.0f;
    // the texture clamp mode word's low byte: wrap S wrap T
    inline constexpr std::uint32_t kLanternClampWrap = 3;
    // The layers' shader flags (the template's others kept): SLSF1 palette colour, vertex alpha and falloff on; palette alpha
    // off (Community Shaders' Effect.hlsl, as vanilla: with it on the alpha is palette.a(texture alpha, accumulated alpha), and
    // our palettes' rows are all alike, so vertex and base alpha would do nothing), the atronach body's decal bits off,
    // Skinned off (SkinToBone sets it again on the worn model); SLSF2 double-sided and vertex colours on
    inline constexpr std::uint32_t kLanternFlameFlags1On = kSlsf1GreyscaleToPaletteColor | kSlsf1VertexAlpha | kSlsf1Falloff;
    inline constexpr std::uint32_t kLanternFlameFlags1Off =
        kSlsf1GreyscaleToPaletteAlpha | kSlsf1Decal | kSlsf1DynamicDecal | kSlsf1Skinned;
    inline constexpr std::uint32_t kLanternFlameFlags2On = kSlsf2DoubleSided | kSlsf2VertexColors;

    // The read-back's skin checks: bone bind pose x skin-to-bone = identity within this, and the bone-space bounds and
    // vertices back through the bind pose within kLanternBoundsTolerance of the skin-space ones
    inline constexpr float kLanternBindTolerance = 1e-4f;
    inline constexpr float kLanternBoundsTolerance = 1e-3f;

    // The soot texture: RGB x this, alpha kept
    inline constexpr float kLanternSootDarken = 0.4f;

    struct LanternBox
    {
        std::array<float, 3> min{};
        std::array<float, 3> max{};

        [[nodiscard]] std::array<float, 3> Centre() const noexcept
        {
            return { (min[0] + max[0]) * 0.5f, (min[1] + max[1]) * 0.5f, (min[2] + max[2]) * 0.5f };
        }
        [[nodiscard]] float Size(int a_axis) const noexcept { return max[a_axis] - min[a_axis]; }
    };

    /// What one shape of a NIF holds, as a read-back prints it
    struct LanternShapeInfo
    {
        std::string name;
        std::string type;    // block type
        std::string shader;  // shader block type, "" when none
        std::uint32_t flags1 = 0;
        std::uint32_t flags2 = 0;
        bool vertexColors = false;          // the shape's vertex data has colours
        // a lighting shader's specular (glossiness -1: not a lighting shader)
        float glossiness = -1.0f;
        float specularStrength = 0.0f;
        std::array<float, 3> specularColor{};
        // the UV bounds over every vertex (u, v), empty (min > max) when the shape has none
        std::array<float, 2> uvMin{ 1.0f, 1.0f };
        std::array<float, 2> uvMax{ 0.0f, 0.0f };
        std::vector<std::string> textures;  // the texture set's slots, or an effect shader's source and greyscale
        bool skinned = false;               // a skin instance and the vertex skinned flag
        std::string skinType;               // skin instance block type
        std::uint32_t skinRoot = 0xFFFFFFFF;  // the skin instance's skeleton root (block id; NIF_NPOS when none)
        std::uint32_t fileRoot = 0;          // the file's root node block id
        std::vector<std::string> bones;
        std::vector<std::uint16_t> partitions;  // BSDismember body parts
        std::uint32_t vertices = 0;
        std::uint32_t triangles = 0;
        std::uint32_t weightedOnFirstBone = 0;  // vertices with a weight on bone 0 (the shape's vertex data)
        std::uint32_t skinDataWeights = 0;      // NiSkinData's weight list of bone 0
        bool allWeightsOne = false;             // every vertex: bone 0 weight 1 and no other (vertex data and NiSkinData)
        std::vector<std::string> controllers;   // the shader's controller chain, "type -> target ok|wrong"
        // the shader's, the alpha property's, the controllers' and their interpolators' and key data's block ids
        std::vector<std::uint32_t> blockIds;
        float baseAlpha = -1.0f;                // an effect shader's base colour alpha (-1: not an effect shader)
        // an effect shader's falloff: start angle, stop angle (cosines), start opacity, stop opacity; and its texture clamp
        // mode word (the low byte the mode, 3 = wrap S wrap T)
        std::array<float, 4> falloff{};
        std::uint32_t clampMode = 0;
        std::array<float, 2> uvScale{ 1.0f, 1.0f };  // a BSShaderProperty's UV scale (u, v)
        // the vertex colours' alpha (0..255; -1: no colours): the smallest, the largest, the largest at the highest vertices
        // and at the lowest
        int alphaMin = -1;
        int alphaMax = -1;
        int alphaTop = -1;
        int alphaBottom = -1;
        bool hasAlpha = false;                  // an NiAlphaProperty on the shape: its flags and whether it blends additively
        std::uint16_t alphaFlags = 0;
        bool additive = false;
        LanternBox bounds;                      // vertex bounds in the root's space (skinned: skin space)
        float refraction = -1.0f;               // a lighting shader's refraction strength (-1: not a lighting shader)
        // skinned on one bone: B = the bone node's transform to the root, S = the skin-to-bone transform of bone 0
        bool bindRead = false;
        float bindError = 0.0f;    // the largest |element| of B x S - identity (rotation, translation, scale - 1)
        float sphereError = 0.0f;  // |B(bone-space bounds centre) - the shape's bounds centre| + |radius difference|
        float vertexError = 0.0f;  // the largest |B(S(v)) - v| over the vertices
    };

    struct LanternValueNode
    {
        std::string name;
        int value = 0;
        std::uint8_t flags = 0;
        std::array<float, 3> translation{};
        std::string parent;
    };

    struct LanternNifInfo
    {
        std::string rootType;
        std::string rootName;
        std::vector<std::string> blockTypes;  // "type x count", in first-seen order
        std::vector<std::string> nodeNames;   // every NiNode-derived block's name
        std::vector<LanternValueNode> valueNodes;
        std::vector<LanternShapeInfo> shapes;
        bool hasBsx = false;
        std::uint32_t bsx = 0;
        bool hasCollision = false;            // any NiAVObject with a collision object
        bool sseCompatible = false;
        LanternBox bounds;                    // every shape's vertices
        int unknownBlocks = 0;
    };

    /// Vertices closer than this count as one when pieces are joined (game units)
    inline constexpr float kLanternWeld = 1e-4f;

    /// One connected piece of a triangle list: its triangle indices, its vertex indices (sorted) and their bounds
    struct LanternPiece
    {
        std::vector<std::uint32_t> triangles;
        std::vector<std::uint16_t> vertices;
        LanternBox bounds;
    };

    /// The connected pieces of a_tris over a_verts (shared vertex index or the same position within kLanternWeld), in
    /// the order of their first triangle. Throws std::runtime_error on a triangle naming a vertex past the list.
    [[nodiscard]] std::vector<LanternPiece> LanternPieces(const std::vector<nifly::Vector3>& a_verts,
        const std::vector<nifly::Triangle>& a_tris);

    /// A body's triangles in its reference pose (every shape; a skinned one through its global-to-skin transform inverted)
    struct LanternSurface
    {
        std::string name;
        std::vector<nifly::Vector3> verts;
        std::vector<std::array<std::uint32_t, 3>> tris;
        float outward = 1.0f;  // +1 when (b - a) x (c - a) points out of the mesh (its signed volume is positive), else -1
    };

    /// Throws std::runtime_error when the NIF does not load or has no triangles
    [[nodiscard]] LanternSurface LoadLanternSurface(const std::filesystem::path& a_body);

    struct LanternGap
    {
        int points = 0;
        int inside = 0;          // points behind the surface (see SurfaceGap)
        float nearest = 0.0f;    // the smallest distance from a point to the surface's triangles
        std::array<float, 3> nearestPoint{};  // the point that has it
    };

    /// Every point against a_surface: the exact point-to-triangle distance, and inside = the point lies behind the outward
    /// normal at its closest point (the triangles tied for nearest add their unit normals, so an edge or a vertex counts
    /// both sides; oriented by LanternSurface::outward). Unlike a ray test it does not misfire through the body's open
    /// parts (neck, wrists, the crotch seam), which a ray from the left can pass through to reach an inner face.
    /// Limit: only the points are tested, so a body bump narrower than the spacing of a part's vertices (the ring's 24
    /// segments, 0.37 apart) could pass between them; the vanilla bodies are smooth at that scale. Upgrade path: test the
    /// triangles against each other.
    [[nodiscard]] LanternGap SurfaceGap(const LanternSurface& a_surface, const std::vector<nifly::Vector3>& a_points);

    /// How far the lantern's cage axis in a_worn leans off world Z (degrees). a_ground and a_worn are the frame's vertices
    /// in the same order, a_ground as the source has them (its axis +Z), a_worn after any rotation, uniform scale and
    /// translation of them (the handle's vertices, above kLanternAxisTopShare, may move otherwise). The rotation is found
    /// exactly from two reference vectors over the means of index-matched vertex groups (low against high, -X half against
    /// +X half, all at or below kLanternAxisTopShare of the height): a mean maps exactly through such a transform.
    /// Throws std::runtime_error on different sizes or reference vectors too short or parallel.
    [[nodiscard]] float LanternAxisTiltDeg(const std::vector<nifly::Vector3>& a_ground, const std::vector<nifly::Vector3>& a_worn);

    /// The smallest distance from any of a_points to a_tris over a_verts (exact point-to-triangle). Throws
    /// std::runtime_error on a triangle naming a vertex past the list.
    /// Limit: only the points are tested (a frame wire passing between two flame vertices is not seen); the flame layers'
    /// vertices are a ring every 15-30% of their height, 12 round. Upgrade path: test the triangles against each other.
    [[nodiscard]] float LanternNearestTriangle(const std::vector<nifly::Vector3>& a_points, const std::vector<nifly::Vector3>& a_verts,
        const std::vector<nifly::Triangle>& a_tris);

    /// The flame read back: in each model the layers' bounds (kLanternFlameLayers order) and their vertices' nearest frame
    /// triangle (the ground in root space, the worn in character space). Throws std::runtime_error when a file, its frame or
    /// a layer is missing.
    struct LanternFlameReadBack
    {
        std::array<LanternBox, 2> ground{};
        float groundClearance = 0.0f;
        std::array<LanternBox, 2> worn{};
        float wornClearance = 0.0f;
    };
    [[nodiscard]] LanternFlameReadBack FlameReadBack(const std::filesystem::path& a_dataDir);

    /// Sampling step of SurfaceLeft (game units)
    inline constexpr float kLanternSurfaceStep = 0.2f;

    /// The smallest X of a_surface within a_window (X, Y and Z ranges); FLT_MAX when none
    [[nodiscard]] float SurfaceLeft(const LanternSurface& a_surface, const LanternBox& a_window);

    /// LanternAxisTiltDeg of the written files in a_dataDir: lantern_gnd.nif's frame (root space) against
    /// lantern_worn.nif's (skin space = character space). Throws std::runtime_error when either does not read.
    [[nodiscard]] float WornAxisTiltDeg(const std::filesystem::path& a_dataDir);

    /// Reads a NIF the way the read-back prints it. Throws std::runtime_error when nifly cannot load it.
    [[nodiscard]] LanternNifInfo InspectLanternNif(const std::filesystem::path& a_file);

    /// Prints a LanternNifInfo (every line indented by two spaces)
    void PrintLanternNif(const std::string& a_title, const LanternNifInfo& a_info);

    struct LanternDarkenResult
    {
        bool ok = false;
        std::string reason;
        std::string format;
        std::size_t width = 0;
        std::size_t height = 0;
        std::size_t mips = 0;
        double sourceMean = 0.0;  // mean RGB (0..1) of mip 0
        double copyMean = 0.0;
    };

    /// a_source's RGB x a_factor (alpha kept) written to a_target in the same format, size and mip count (each mip
    /// darkened as it is, no new chain); the means of mip 0 before and after, read back from the written file
    [[nodiscard]] LanternDarkenResult DarkenDds(const std::filesystem::path& a_source, const std::filesystem::path& a_target,
        float a_factor);

    /// Mean RGB (0..1) of mip 0 and the size of a DDS file; false when it does not read
    [[nodiscard]] bool DdsMean(const std::filesystem::path& a_file, double& a_mean, std::size_t& a_width, std::size_t& a_height,
        std::size_t& a_mips, std::string& a_format);

    /// One body's belt band at the left hip (kLanternBandShapes, kLanternBandXMin/XMax, kLanternBandZWindow)
    struct LanternBand
    {
        std::string body;
        std::string shape;
        int vertices = 0;   // the band shape's vertices in the window
        float top = 0.0f;
        float bottom = 0.0f;
    };

    /// a_shape's vertices of a_body in the reference pose (global-to-skin inverted) within the band window round a_pelvis.
    /// Throws std::runtime_error when the NIF does not load, has no such shape or no vertex in the window.
    [[nodiscard]] LanternBand MeasureLanternBand(const std::filesystem::path& a_body, const std::string& a_shape,
        const nifly::Vector3& a_pelvis);

    /// One foot of the upright handle and the lug under it (character space)
    struct LanternFoot
    {
        std::array<float, 3> centre{};  // the foot vertices' mean
        float footZ = 0.0f;             // the foot's lowest point
        float lidZ = 0.0f;              // the lid's highest point under the lug's footprint
        float lidMin = 0.0f;            // the lid's lowest point under the lug's footprint
        int lidHoles = 0;               // footprint points over a hole in the lid (kLanternLugSamples squared in all)
        LanternBox lug;                 // the lug's vertex bounds (read back: the lug vertices nearest this foot)
    };

    /// The worn handle read back from lantern_worn.nif: the frame piece holding the frame's highest vertex, its height
    /// and its two feet with the lid under them and the lugs (kLanternLugName). Throws std::runtime_error when the file
    /// does not read, has no frame or lugs, or the feet or the lid under them are not found.
    struct LanternHandleReadBack
    {
        float height = 0.0f;
        std::array<LanternFoot, 2> feet{};
    };
    [[nodiscard]] LanternHandleReadBack WornHandle(const std::filesystem::path& a_dataDir);

    struct LanternBuildResult
    {
        bool ok = false;
        std::vector<std::string> problems;  // what failed (nothing written for that file)
        std::vector<std::string> log;       // the constants used and what was measured
        LanternBox frameBounds;             // the ground frame's vertex bounds (root space)
        std::array<float, 3> cageCentre{};
        std::array<float, 3> flameBase{};   // the outer layer's base centre on the cage axis (ground root space)
        float flameHeight = 0.0f;           // the outer layer's height and widest radius (ground units)
        float flameRadius = 0.0f;
        float groundFlameClearance = 0.0f;  // the layers' nearest ground frame triangle
        float wornFlameClearance = 0.0f;    // ... worn frame triangle
        float wornScale = 0.0f;
        std::array<float, 3> pelvis{};      // the pelvis bone's origin (character space)
        LanternBox wornFrame;               // the worn frame's vertex bounds (character space)
        std::array<float, 3> wornCage{};    // the cage centre placed (character space)
        std::array<float, 3> ringCentre{};
        std::array<float, 3> contact{};     // the handle's top wire centre (on the ring's bottom tube)
        float handleWire = 0.0f;            // the handle's top wire half height (scaled, squashed by handleScale)
        float handleScale = 0.0f;           // the handle's Z scale about its feet (kLanternHandleHeight)
        float handleHeight = 0.0f;          // the placed handle's height
        std::array<LanternFoot, 2> feet{};  // the handle's feet and their lugs (placed)
        float ringOut = 0.0f;               // how far out (-X) from its start the outward walk settled
        int ringSteps = 0;                  // the outward steps tried (the settled one included)
        float ringGap = 0.0f;               // the ring's nearest body gap there
        float lanternGap = 0.0f;            // the lantern's nearest body gap there
        std::vector<LanternBand> bands;     // each body's belt band
        float axisTiltDeg = -1.0f;          // the worn frame's cage axis off world Z, read back (LanternAxisTiltDeg)
        std::vector<std::string> summary;   // the lines RunWriteLantern prints after the read-back
    };

    /// One worn part against one body
    struct LanternClearance
    {
        std::string part;   // "lantern" (frame + flame + lugs), "ring"
        std::string body;
        float margin = 0.0f;
        LanternGap gap;
    };

    /// The worn model's parts read back from a_dataDir against the bodies in a_inputs (each found one; none = empty)
    [[nodiscard]] std::vector<LanternClearance> WornClearances(const std::vector<std::filesystem::path>& a_inputs,
        const std::filesystem::path& a_dataDir);

    /// Builds the five outputs from the --in folders into a_dataDir (Data layout), reading each NIF back and checking it.
    /// Never writes into an --in folder (the caller checks).
    [[nodiscard]] LanternBuildResult BuildLantern(const std::vector<std::filesystem::path>& a_inputs,
        const std::filesystem::path& a_dataDir);

    /// The checks the build and the self-test run on the written outputs: frame shape and texture path, the flame layers in
    /// both models (shader flags, blend, base alpha, palette, vertex alpha, controllers, base height, clearance), the smoke's addon node, the empty model's lone root node, every worn shape skinned to exactly kLanternBone
    /// with weight 1 and body part kLanternBodyPart, the ring's height, the clearances, the BSXFlags. Empty = it holds.
    /// a_inputs: the sources, for what must match them (texture size and mips, darker; the kept normal map).
    [[nodiscard]] std::vector<std::string> VerifyLantern(const std::vector<std::filesystem::path>& a_inputs,
        const std::filesystem::path& a_dataDir);

    /// --write-lantern <data folder> --in <dir> [...]: builds, verifies and prints the read-back of every output. 0/1.
    [[nodiscard]] int RunWriteLantern(const std::filesystem::path& a_dataDir, const std::vector<std::filesystem::path>& a_inputs);

    /// --inspect-lantern <file.nif> [...]: the read-back print of any NIF (vanilla sources included). 0/1.
    [[nodiscard]] int RunInspectLantern(const std::vector<std::filesystem::path>& a_files);
}

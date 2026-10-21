#pragma once

// =============================================================================
// GreyLantern - the Black Flame lantern's meshes and texture (--write-lantern; lantern spec section 6)
// =============================================================================
// docs/superpowers/specs/2026-10-21-black-flame-lantern-design.md, section 6 and records 0x80D / 0x80E / 0x815. From
// vanilla files in the --in folders (loose, Data layout; extract Skyrim - Meshes0.bsa / Skyrim - Textures*.bsa first):
//   lantern_gnd.nif    candlelantern01.nif's frame and collision, diffuse = the soot copy, normal map kept, and the wisp
//                      (round 5): a NiNode HoMBF_Wisp at the cage centre (the frame's vertex bounds) holding the black
//                      flame orb (core, shell, halo, see the wisp constants), a hidden emitter sphere and a scaled-down
//                      copy of the vanilla wisp's trailing particles (fxwispparticleattach.nif's puffs and strips, on
//                      GradBlackFlame.dds), bobbing and turning slowly through a looping NiTransformController (the
//                      vanilla hover light apohoverlight01.nif's pattern; BSXFlags Animated)
//   lantern_worn.nif   belt-mounted at the left hip, every shape 100% on NPC Pelvis [Pelv] (BSDismemberSkinInstance, one
//                      partition, body part 60): a leather strap looped over the vanilla belt band (made here, vanilla
//                      leather textures), an iron hook bracket out of its lower end with an iron ring welded to its tip
//                      (made here, the soot lamp texture), and the frame without collision (handle turned upright and
//                      shortened, iron lugs under its feet) hanging vertically from the ring, with the wisp's core, shell
//                      and halo (round 5) floating at the cage centre: no node animation (skinned), the shaders' own
//                      UV-scroll and pulse controllers
//   lantern_smoke.nif  an empty BSFadeNode with BSXFlags and one AddOnNode9123 (grey ADDN 0x809 MPSFireSmSmoke01)
//   LampGenericSoot01.dds  LampGeneric01.dds with RGB x kLanternSootDarken, alpha kept, same format and mips
// An AddOnNode is a BSValueNode whose value is the ADDN index (what the engine reads; vanilla candlelanternwithcandle01.nif
// AddOnNode49 = BSValueNode value 49), named AddOnNode<index> as the vanilla files name theirs.

#include "GreyBuild.h"

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
    // the vanilla wisp's trailing particles (Skyrim - Meshes0.bsa), copied scaled down into the ground model
    inline constexpr const char* kLanternWispSource = "meshes\\actors\\wisp\\character assets\\fxwispparticleattach.nif";
    inline constexpr const char* kLanternDiffuseSource = "textures\\clutter\\common\\lampgeneric01.dds";

    // Outputs (Data-relative, under the --write-lantern folder)
    inline constexpr const char* kLanternGroundNif = "meshes\\heartofmagic\\blackflame\\lantern\\lantern_gnd.nif";
    inline constexpr const char* kLanternWornNif = "meshes\\heartofmagic\\blackflame\\lantern\\lantern_worn.nif";
    inline constexpr const char* kLanternSmokeNif = "meshes\\heartofmagic\\blackflame\\lantern\\lantern_smoke.nif";
    // The texture's file and the text the NIF slot names (the slot keeps the case the other addon paths use)
    inline constexpr const char* kLanternDiffuse = "textures\\HeartOfMagic\\BlackFlame\\Lantern\\LampGenericSoot01.dds";

    // The grey ADDN index the smoke static's addon node names (shipped record 0x809 MPSFireSmSmoke01; 0x804 stays in the
    // ESP for other features, the lantern no longer names it)
    inline constexpr int kLanternSmokeAddon = 9123;
    // BSValueNode value flags as the precedent has them: the vanilla smoke static fxsmokechimney01.nif's AddOnNode56 0
    inline constexpr std::uint8_t kLanternSmokeValueFlags = 0;
    // BSXFlags bit 0, Animated, and bit 4, Addon (nif.xml BSXFlags)
    inline constexpr std::uint32_t kBsxAnimated = 1u << 0;
    inline constexpr std::uint32_t kBsxAddon = 1u << 4;
    // lantern_smoke.nif's BSXFlags: the value the vanilla effect statics with an AddOnNode carry, fxsmokechimney01.nif and
    // fxfire01new.nif (read 2026-10-21 from Skyrim - Meshes0.bsa): 0x31 = Animated | Addon | EditorMarker. Copied as it is;
    // the EditorMarker bit names a shape this model does not have, which leaves nothing for it to hide
    inline constexpr std::uint32_t kLanternSmokeBsx = 0x31;
    inline constexpr const char* kLanternSmokeRootName = "HoMBF_LanternSmoke";
    inline constexpr const char* kLanternWornRootName = "HoMBF_LanternWorn";
    inline constexpr const char* kLanternWornFrameName = "HoMBF_LanternFrame";

    // The worn mesh: the bone and its body part
    inline constexpr const char* kLanternBone = "NPC Pelvis [Pelv]";
    inline constexpr std::uint16_t kLanternBodyPart = 60;
    // SLSF1 bit 1, Skinned (CommonLibSSE-NG BSShaderProperty.h EShaderPropertyFlag kSkinned = 1)
    inline constexpr std::uint32_t kSlsf1Skinned = 1u << 1;

    // The cage centre: X and Y from the vertex bounds of the frame's vertices in this middle share of its height (the
    // cage walls; candlelantern01's hook at the top reaches out in +Y and would pull a whole-bounds centre off the cage),
    // Z the middle of the whole bounds' height
    inline constexpr float kLanternCageBand = 0.5f;

    // ---- The worn model (round 4, 2026-10-21: belt-mounted on an iron hook). Character space in the skeleton's reference
    // pose: +Y forward, -X the wearer's left, Z up. A leather strap loops over the belt band at the left hip, a forged iron
    // bracket comes out of its lower end and reaches out sideways (-X), its down-turned tip welded to an iron ring, and the
    // lantern hangs vertically (its cage axis world Z) from the ring by its upright handle. Every part is placed on the
    // vanilla male and female bodies (kLanternBodySources, required) and checked against both.
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

    // The belt band (round 4): the vanilla bodies' underwear band at the left hip, measured on each body's band shape
    // (kLanternBandShapes, paired with kLanternBodySources; required) in the reference pose: its vertices with X between
    // kLanternBandXMin and kLanternBandXMax and Z within kLanternBandZWindow of the pelvis (FemaleUnderwear also holds the
    // breast band, far above). The band top is the highest of them, the band bottom the lowest. malebody_1 measured in
    // Blender 2026-10-21: top z 77.15, bottom 71.58.
    inline constexpr std::array<const char*, 2> kLanternBandShapes{ "MaleUnderwear_1", "FemaleUnderwear" };
    inline constexpr float kLanternBandXMin = -17.0f;          // relative to the pelvis' X
    inline constexpr float kLanternBandXMax = -12.0f;
    inline constexpr std::array<float, 2> kLanternBandZWindow{ -15.0f, 15.0f };  // relative to the pelvis' Z

    // The strap: a centreline path in the XZ plane at Y = pelvis + kLanternStrapY, swept with a kLanternStrapWidth (Y) x
    // kLanternStrapThickness (across) cross-section whose four edges are rounded (radius kLanternStrapEdgeRadius,
    // kLanternStrapEdgeSegments segments each). It sits on the belt band of both bodies: the inner layer rises on the body
    // (kLanternStrapBodyGap off it) from kLanternStrapUnderBand below the lowest band bottom (wrapped round the belt), folds
    // over the top with its top kLanternStrapOverBand above the highest band top, and the outer layer hangs down
    // kLanternStrapBeltGap off the inner layer kLanternStrapOuterLength below the top (round 3's length; the bracket comes
    // out of its lower end), draping (never moving back in towards the body as it goes down). Round 5: the gap between
    // the layers 0.3 (was 1.0, "the belt's room", which left the outer layer floating 2.25 off the band at its centreline):
    // the vanilla belt band is the body's own surface, so the outer layer lies on the inner one, which lies on the band;
    // StrapLayerGap reads it back (kLanternStrapLayerGapMin..Max).
    inline constexpr float kLanternStrapY = 3.0f;              // a little forward of the pelvis; the arm hangs at y <= -2 from z 76
    inline constexpr float kLanternStrapOverBand = 1.2f;
    inline constexpr float kLanternStrapUnderBand = 1.0f;
    inline constexpr float kLanternStrapOuterLength = 6.75f;   // round 3: top 82.75, lower end 76
    inline constexpr float kLanternStrapWidth = 3.0f;
    inline constexpr float kLanternStrapThickness = 0.6f;
    inline constexpr float kLanternStrapEdgeRadius = 0.25f;    // below half the thickness: the broad faces stay flat
    inline constexpr int kLanternStrapEdgeSegments = 3;
    inline constexpr float kLanternStrapBodyGap = 0.35f;       // the inner layer off the band (0.2-0.4; kLanternStrapMargin 0.3)
    inline constexpr float kLanternStrapBeltGap = 0.3f;
    inline constexpr float kLanternStrapLayerGapMin = 0.2f;   // the read-back's range for the narrowest gap between the layers
    inline constexpr float kLanternStrapLayerGapMax = 0.4f;
    inline constexpr float kLanternStrapStep = 0.5f;           // path samples along Z
    inline constexpr int kLanternStrapFoldSegments = 8;        // the half circle over the top
    // The outer face (the loop's outside) bulges by this at the middle of the width (a parabola across it, 0 at the
    // rounded edges), on the outer layer, ramping in over the fold's second half (none on the inner layer, which faces
    // the body; none at the fold's top, which stays at the band top + kLanternStrapOverBand); kLanternStrapFaceSegments
    // points across each broad face carry it
    inline constexpr float kLanternStrapBulge = 0.25f;
    inline constexpr int kLanternStrapFaceSegments = 6;
    // Each edge moves in or out across the width by up to this along the strap (cut by hand, not a die): two sines per
    // edge of kLanternStrapWobbleWaves wavelengths, weights 0.6 / 0.4, a different phase per edge. Deterministic
    inline constexpr float kLanternStrapEdgeWobble = 0.1f;
    inline constexpr std::array<float, 2> kLanternStrapWobbleWaves{ 7.3f, 2.9f };
    // The strap's lighting shader as the vanilla mesh of the same texture has it: meshes\clutter\common\leatherstrips01.nif
    // (Skyrim - Meshes0.bsa, read 2026-10-21 with houseCARL). Vanilla leather dims its highlight by the specular colour,
    // not the strength (the lamp frame whose shader the strap copies: glossiness 90, strength 1, white)
    inline constexpr float kLanternStrapGlossiness = 80.0f;
    inline constexpr float kLanternStrapSpecularStrength = 1.0f;
    inline constexpr std::array<float, 3> kLanternStrapSpecularColor{ 0.4078432f, 0.3843138f, 0.3647059f };
    // The body's left side is read within this X of the pelvis origin (the hanging forearm at x -21..-29 is outside it)
    inline constexpr float kLanternHipWindow = 20.0f;
    // Vanilla leather (Skyrim - Textures3.bsa): leatherstrips01 is rows of strap leather; the strap's width takes the
    // plain brown row V kLanternStrapBandV0..V1 (read from the image's row means), its length runs along U
    inline constexpr const char* kLanternStrapDiffuse = "textures\\clutter\\common\\leatherstrips01.dds";
    inline constexpr const char* kLanternStrapNormal = "textures\\clutter\\common\\leatherstrips01_n.dds";
    inline constexpr float kLanternStrapBandV0 = 0.203f;
    inline constexpr float kLanternStrapBandV1 = 0.336f;
    inline constexpr float kLanternStrapUvSpan = 45.0f;        // strap length per U repeat (the row is 256 x 17 px: 15:1)
    inline constexpr const char* kLanternStrapName = "HoMBF_LanternStrap";

    // The bracket: a flat forged iron bar (kLanternBracketWidth along Y, kLanternBracketThickness across) swept along a
    // path in the XZ plane at the strap's Y. Its inboard end is a plate held in the strap's lower end (on the outer
    // layer's centreline, kLanternBracketGrip up into it); it runs down to kLanternBracketBelow under the strap's end, bends
    // out (radius kLanternBracketBend) and runs out in -X, bends down again and its tip runs down kLanternBracketDrop to the
    // ring's top tube centre, where the ring is welded on. Welded, not hung: the ring's hole is along Y (the handle's loop,
    // which lies along the body side, runs through it), so no bar running out in X can pass through it to hang it on.
    // The ring centre sits kLanternBracketLength out from the strap's outer layer at its lower end (round 2's ring
    // position); where the lantern would come nearer than kLanternBodyMargin to a body the length grows by
    // kLanternBracketLengthStep up to kLanternBracketLengthMax, past which the build fails
    // Its four long edges are bevelled kLanternBracketBevel (a 45 degree chamfer), and its width narrows by
    // kLanternBracketTaper (a share) from the plate to the tip, the edges moving in, the bevel kept
    inline constexpr float kLanternBracketWidth = 1.2f;
    inline constexpr float kLanternBracketThickness = 0.5f;
    inline constexpr float kLanternBracketBevel = 0.12f;
    inline constexpr float kLanternBracketTaper = 0.08f;
    inline constexpr float kLanternBracketGrip = 1.0f;
    inline constexpr float kLanternBracketBelow = 1.2f;        // the bar's centreline below the strap's lower end
    inline constexpr float kLanternBracketBend = 0.5f;         // centreline radius of both bends
    inline constexpr int kLanternBracketBendSegments = 4;
    inline constexpr float kLanternBracketDrop = 1.5f;         // the bar's centreline to the ring's top tube centre
    inline constexpr float kLanternBracketLength = 6.0f;
    inline constexpr float kLanternBracketLengthStep = 0.5f;
    inline constexpr float kLanternBracketLengthMax = 9.0f;
    inline constexpr const char* kLanternBracketName = "HoMBF_LanternBracket";

    // The ring: a torus in the XZ plane (its hole along Y, the handle's loop through it), welded to the bracket's tip
    inline constexpr float kLanternRingRadius = 1.4f;
    inline constexpr float kLanternRingTube = 0.3f;
    inline constexpr int kLanternRingSegments = 24;
    inline constexpr int kLanternRingSides = 8;
    inline constexpr const char* kLanternRingName = "HoMBF_LanternRing";

    // The ring's and the bracket's UVs on LampGeneric01.dds (the soot copy): the plainest iron of the atlas, no rod or rim
    // crossing it in the diffuse or the normal map (luminance std 10.5 of 0..255, normal map std <= 8; the handle's box
    // round 2 reused, 0.05-0.85 x 0.66-0.98, crosses four embossed rods)
    inline constexpr std::array<float, 2> kLanternIronUvMin{ 0.80f, 0.60f };
    inline constexpr std::array<float, 2> kLanternIronUvMax{ 0.98f, 0.80f };

    // The lantern hangs vertically, its handle's top wire resting on the ring's bottom tube, no tilt
    inline constexpr float kLanternBodyMargin = 1.0f;          // the lantern: frame, wisp and lugs
    inline constexpr float kLanternStrapMargin = 0.3f;         // the strap (its inner layer is the nearest), the bracket, the ring
    // The read-back's vertical check: the worn frame's cage axis (LanternAxisTiltDeg) within this of world Z
    inline constexpr float kLanternVerticalToleranceDeg = 0.5f;
    // LanternAxisTiltDeg's reference vertices: the frame's lowest kLanternAxisLowShare of its height, and the vertices
    // above that up to kLanternAxisTopShare (below candlelantern01's handle, which starts at 86% of the height)
    inline constexpr float kLanternAxisLowShare = 0.3f;
    inline constexpr float kLanternAxisTopShare = 0.8f;

    // ---- The wisp (round 5, 2026-10-21: "a will-o'-the-wisp floating inside"): a ball of black flame at the cage centre,
    // three UV spheres round one point. The black flame look (the addon's rule): additive alone cannot paint dark, so a
    // bright additive core on GradBlackFlame.dds (kPalettePath) is paired with a darker alpha-blended shell, as the hand
    // marble copies pair their additive fire with a near-black normal-blend companion (GreyMarble.cpp: palette off, base
    // colour 0.006, the texture's alpha). The same radii in both models (the task's game units; the ground cage is 1/0.63
    // of the worn one, so the ground orb fills less of it).
    inline constexpr const char* kLanternWispNode = "HoMBF_Wisp";            // the ground model's node
    inline constexpr const char* kLanternWispCoreName = "HoMBF_WispCore";
    inline constexpr const char* kLanternWispShellName = "HoMBF_WispShell";
    inline constexpr const char* kLanternWispHaloName = "HoMBF_WispHalo";
    inline constexpr const char* kLanternWispEmitterName = "HoMBF_WispEmitter";  // the ground's hidden particle emitter
    inline constexpr float kLanternWispCoreRadius = 1.6f;
    inline constexpr float kLanternWispShellRadius = 2.1f;
    inline constexpr float kLanternWispHaloRadius = 3.0f;
    inline constexpr int kLanternWispSegments = 24;          // round the axis
    inline constexpr int kLanternWispRings = 16;             // pole to pole
    inline constexpr int kLanternWispEmitterSegments = 12;   // the hidden emitter sphere (core radius)
    inline constexpr int kLanternWispEmitterRings = 8;
    // The frame (its triangles) at least this far from every point of the halo (the ground: anywhere along its bob)
    inline constexpr float kLanternWispClearance = 0.5f;
    // The sources the shaders scroll: the vanilla wisp's own glow noise (wisp.nif ArmMagic01 / SideViewMagic) on the core
    // and the shell, its soft puffs (wisp.nif SkirtMagic) on the halo
    inline constexpr const char* kLanternWispNoise = "textures\\effects\\DarkSwirls.dds";
    inline constexpr const char* kLanternWispHaloTexture = "textures\\effects\\FXpuffsTile.dds";
    // Shader flags. SLSF1: 31 ZBuffer_Test, 30 Soft_Effect, 6 Use_Falloff, 5 palette alpha, 4 palette colour (the vanilla
    // wisp glows' 0xE000007A without Vertex_Alpha and External_Emittance: these spheres have no vertex colours and emit
    // themselves; the worn copies add Skinned); SLSF2 0: no ZBuffer_Write (blended), no vertex colours, no effect lighting
    inline constexpr std::uint32_t kLanternWispFlags1Palette = 0xC0000070;   // core and halo
    inline constexpr std::uint32_t kLanternWispFlags1Dark = 0xC0000040;      // the shell: palette off (the marble's rule)
    inline constexpr std::uint32_t kLanternWispFlags2 = 0;
    inline constexpr float kLanternWispSoftDepth = 2.0f;                     // the vanilla wisp glows' soft falloff depth
    // NiAlphaProperty flags: additive (blend on, source SRC_ALPHA, destination ONE; 4109, as the hand copies' tests) and
    // normal (SRC_ALPHA / INV_SRC_ALPHA, the vanilla wisp glows' 0x10ED)
    inline constexpr std::uint16_t kLanternWispAdditive = 0x100D;
    inline constexpr std::uint16_t kLanternWispNormal = 0x10ED;
    // Falloff (start angle cosine, stop angle cosine, start opacity, stop opacity): the core fades from 30 to 75 degrees off
    // the view, the shell as the vanilla wisp glows (75 to 85 degrees), the halo over the whole half sphere
    inline constexpr std::array<float, 4> kLanternWispCoreFalloff{ 0.8660f, 0.2588f, 1.0f, 0.0f };
    inline constexpr std::array<float, 4> kLanternWispShellFalloff{ 0.2588f, 0.0872f, 1.0f, 0.0f };
    inline constexpr std::array<float, 4> kLanternWispHaloFalloff{ 1.0f, 0.0f, 1.0f, 0.0f };
    // The core: GradBlackFlame.dds tops out at grey 0.5, so its emissive multiple carries the brightness, pulsing as a
    // sine about kLanternWispCoreEmissive by kLanternWispPulseAmplitude at kLanternWispPulseHz (kLanternWispPulseKeys
    // linear keys a period)
    inline constexpr float kLanternWispCoreEmissive = 2.5f;
    inline constexpr float kLanternWispPulseAmplitude = 0.6f;
    inline constexpr float kLanternWispPulseHz = 0.6f;
    inline constexpr int kLanternWispPulseKeys = 16;
    // The shell: near black (the hand marble's 0.006) at this alpha
    inline constexpr float kLanternWispShellColour = 0.006f;
    inline constexpr float kLanternWispShellAlpha = 0.9f;
    // The halo: faint
    inline constexpr float kLanternWispHaloAlpha = 0.15f;
    inline constexpr float kLanternWispHaloEmissive = 1.0f;
    // UV scroll: seconds per U and V repeat (U offset and V offset 0 -> 1, linear, looping). The shell runs the other way
    // (0 -> -1) and slower, so its dark parts drift across the core
    inline constexpr std::array<float, 2> kLanternWispCoreScroll{ 6.0f, 10.0f };
    inline constexpr std::array<float, 2> kLanternWispShellScroll{ 9.0f, 15.0f };
    // BSEffectShaderPropertyFloatController controlled variables (nif.xml EffectShaderControlledVariable; vanilla
    // apohoverlight01.nif's keys sit round 0 on 6 / 8 and round 1 on 7 / 9): 0 emissive multiple, 6 U offset, 8 V offset
    inline constexpr std::uint32_t kLanternWispVarEmissive = 0;
    inline constexpr std::uint32_t kLanternWispVarUOffset = 6;
    inline constexpr std::uint32_t kLanternWispVarVOffset = 8;
    // Every wisp controller's flags: vanilla apohoverlight01.nif's and wisp.nif's 0x0048 (cycle loop, active)
    inline constexpr std::uint16_t kLanternWispControllerFlags = 0x0048;
    // The ground's motion (one NiTransformController on HoMBF_Wisp, as apohoverlight01.nif's on 'lLight01'): a bob of
    // kLanternWispBobAmplitude up and down at kLanternWispBobHz and one turn about Z every kLanternWispBobsPerTurn bobs (both
    // in one looping cycle), translation keys linear at kLanternWispKeyRate a second (the precedent's 1/30 s)
    inline constexpr float kLanternWispBobAmplitude = 0.6f;
    inline constexpr float kLanternWispBobHz = 0.3f;
    inline constexpr int kLanternWispBobsPerTurn = 4;
    inline constexpr float kLanternWispKeyRate = 30.0f;
    // The particles: fxwispparticleattach.nif's puffs (NiParticleSystem 'PArray02') and strips (BSStripParticleSystem
    // 'PArray01'), their sizes, speeds and gravity x kLanternWispParticleScale, emitting from the hidden emitter sphere, their
    // gravity about HoMBF_Wisp, their bomb modifiers (and the controllers that switch them) left out (their nodes stand 60-190
    // units round the vanilla wisp), their palette GradBlackFlame.dds (normal blend: the dark part of the flame)
    inline constexpr std::array<const char*, 2> kLanternWispParticles{ "PArray02", "PArray01" };
    inline constexpr float kLanternWispParticleScale = 0.015f;

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

    /// One controller as a read-back prints it (a shader's, a node's or a particle system's)
    struct LanternController
    {
        std::string type;
        std::uint32_t variable = 0xFFFFFFFF;  // a shader float controller's controlled variable (else 0xFFFFFFFF)
        std::uint16_t flags = 0;
        float frequency = 0.0f;
        float phase = 0.0f;
        float start = 0.0f;
        float stop = 0.0f;
        std::uint32_t target = 0xFFFFFFFF;
        std::string interpolator;                    // the interpolator's block type, "" when none
        float value = 0.0f;                          // an NiFloatInterpolator's value
        std::vector<std::array<float, 2>> keys;      // its NiFloatData's keys (time, value)
        std::vector<std::array<float, 4>> translations;  // an NiTransformData's translation keys (time, x, y, z)
        std::vector<std::array<float, 2>> zRotations;    // ... its Z rotation keys (time, radians; XYZ rotation)
        std::uint32_t rotationType = 0;              // ... its rotation type (nifly NiKeyType)
        std::string text;                            // the printed line
    };

    /// One particle system as a read-back prints it
    struct LanternParticleInfo
    {
        std::string name;
        std::string type;
        std::string parent;
        bool worldSpace = false;
        std::uint32_t pool = 0;             // its NiPSysData's particle pool
        std::uint32_t flags1 = 0;
        std::uint32_t flags2 = 0;
        std::string source;
        std::string greyscale;
        std::array<float, 4> baseColor{};
        std::uint16_t alphaFlags = 0;
        bool hasAlpha = false;
        float radius = 0.0f;                // the first emitter's particle radius, its variation, speed, life span
        float radiusVariation = 0.0f;
        float speed = 0.0f;
        float lifeSpan = 0.0f;
        float birthRate = -1.0f;            // the emitter controller's interpolator value (-1: none read)
        std::vector<std::string> emitterMeshes;  // a mesh emitter's meshes by name
        std::vector<std::string> modifiers;      // "type 'name'" per modifier
        std::vector<std::string> outside;        // a modifier or controller reference naming a block outside the system's graph
        std::vector<std::string> lines;          // the printed detail lines
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
        std::uint32_t weightedOnFirstBone = 0;  // vertices with a weight on bone 0 (the shape's vertex data)
        std::uint32_t skinDataWeights = 0;      // NiSkinData's weight list of bone 0
        bool allWeightsOne = false;             // every vertex: bone 0 weight 1 and no other (vertex data and NiSkinData)
        std::vector<std::string> controllers;   // the shader's controller chain, "type -> target ok|wrong"
        std::vector<LanternController> controllerInfo;  // the same chain, read out
        // an effect shader's values (effect true)
        bool effect = false;
        std::array<float, 4> baseColor{};
        float emissiveMultiple = 0.0f;          // baseColorScale
        std::array<float, 4> falloff{};         // start angle, stop angle, start opacity, stop opacity
        float softFalloffDepth = 0.0f;
        std::uint32_t clampMode = 0;            // the texture clamp mode (3: wrap S and T)
        std::array<float, 2> uvOffset{};
        std::array<float, 2> uvScale{};
        bool hasAlpha = false;                  // an NiAlphaProperty on the shape
        std::uint16_t alphaFlags = 0;
        std::string parent;                     // the node whose children name the shape
        std::uint32_t avFlags = 0;              // the shape's NiAVObject flags (bit 0: hidden)
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
        std::vector<LanternParticleInfo> particles;
        std::vector<std::pair<std::string, LanternController>> nodeControllers;  // node name, each controller on it
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

    /// Sampling step of SurfaceLeft (game units)
    inline constexpr float kLanternSurfaceStep = 0.2f;

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
    /// Limit: only the points are tested, so a body bump narrower than the spacing of a part's vertices (the strap's
    /// width, 3 units; its length, kLanternStrapStep) could pass between them; the vanilla bodies are smooth at that scale.
    /// Upgrade path: test the triangles against each other.
    [[nodiscard]] LanternGap SurfaceGap(const LanternSurface& a_surface, const std::vector<nifly::Vector3>& a_points);

    /// The smallest X of a_surface within a_window (X, Y and Z ranges); FLT_MAX when none
    [[nodiscard]] float SurfaceLeft(const LanternSurface& a_surface, const LanternBox& a_window);

    /// How far the lantern's cage axis in a_worn leans off world Z (degrees). a_ground and a_worn are the frame's vertices
    /// in the same order, a_ground as the source has them (its axis +Z), a_worn after any rotation, uniform scale and
    /// translation of them (the handle's vertices, above kLanternAxisTopShare, may move otherwise). The rotation is found
    /// exactly from two reference vectors over the means of index-matched vertex groups (low against high, -X half against
    /// +X half, all at or below kLanternAxisTopShare of the height): a mean maps exactly through such a transform.
    /// Throws std::runtime_error on different sizes or reference vectors too short or parallel.
    [[nodiscard]] float LanternAxisTiltDeg(const std::vector<nifly::Vector3>& a_ground, const std::vector<nifly::Vector3>& a_worn);

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

    /// The wisp read back from both files: its centre (the shapes' common bounds centre; the ground's node translation) and
    /// the frame's nearest triangle to the halo's sphere (radius kLanternWispHaloRadius about the centre; the ground's at
    /// every bob key's height). Throws std::runtime_error when a file, the frame or a wisp shape is missing.
    struct LanternWispReadBack
    {
        std::array<float, 3> wornCentre{};
        float wornClearance = 0.0f;
        std::array<float, 3> groundCentre{};
        float groundClearance = 0.0f;
        float bobLow = 0.0f;    // the lowest and highest bob key (node Z)
        float bobHigh = 0.0f;
    };
    [[nodiscard]] LanternWispReadBack WispReadBack(const std::filesystem::path& a_dataDir);

    /// The gap between the strap's layers read back: at each kLanternStrapStep height both layers cross (the strap's
    /// vertices within a quarter of its width of its centre Y), the largest jump in X between the inner layer's outer face
    /// and the outer layer's inner face; the narrowest and the widest. Throws std::runtime_error when no height has both.
    [[nodiscard]] std::array<float, 2> StrapLayerGap(const std::filesystem::path& a_dataDir);

    struct LanternBuildResult
    {
        bool ok = false;
        std::vector<std::string> problems;  // what failed (nothing written for that file)
        std::vector<std::string> log;       // the constants used and what was measured
        LanternBox frameBounds;             // the ground frame's vertex bounds (root space)
        std::array<float, 3> cageCentre{};
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
        float bracketLength = 0.0f;         // the length the clearance loop settled on
        LanternBox bracket;                 // the bracket's vertex bounds (character space)
        std::vector<LanternBand> bands;     // each body's belt band
        float strapTop = 0.0f;              // the fold's top: the highest band top + kLanternStrapOverBand
        float strapInnerBottom = 0.0f;      // the inner layer's lower end: the lowest band bottom - kLanternStrapUnderBand
        float strapBottom = 0.0f;           // the outer layer's lower end: strapTop - kLanternStrapOuterLength
        float strapOuterBottomX = 0.0f;     // the strap's outer layer centreline X at strapBottom
        float axisTiltDeg = -1.0f;          // the worn frame's cage axis off world Z, read back (LanternAxisTiltDeg)
        std::array<float, 3> wispWorn{};    // the worn wisp's centre (character space)
        float wispWornClearance = 0.0f;     // the worn frame's nearest triangle to the halo's sphere
        float wispGroundClearance = 0.0f;   // the ground frame's nearest triangle to the halo's sphere anywhere along the bob
        std::array<float, 2> particleReach{};  // puffs, strips: the emitter sphere + speed x life + gravity x life^2 / 2 + radius
        std::vector<std::string> summary;   // the lines RunWriteLantern prints after the read-back
    };

    /// One worn part against one body
    struct LanternClearance
    {
        std::string part;   // "lantern" (frame + wisp + lugs), "strap", "bracket", "ring"
        std::string body;
        float margin = 0.0f;
        LanternGap gap;
    };

    /// The worn model's parts read back from a_dataDir against the bodies in a_inputs (each found one; none = empty)
    [[nodiscard]] std::vector<LanternClearance> WornClearances(const std::vector<std::filesystem::path>& a_inputs,
        const std::filesystem::path& a_dataDir);

    /// Builds the four outputs from the --in folders into a_dataDir (Data layout), reading each NIF back and checking it.
    /// Never writes into an --in folder (the caller checks).
    [[nodiscard]] LanternBuildResult BuildLantern(const std::vector<std::filesystem::path>& a_inputs,
        const std::filesystem::path& a_dataDir);

    /// The checks the build and the self-test run on the written outputs: frame shape and texture path, addon node names
    /// and values, every worn shape skinned to exactly kLanternBone with weight 1 and body part kLanternBodyPart, the
    /// palette on the effect strips, the BSXFlags. Empty = it holds.
    /// a_inputs: the sources, for what must match them (texture size and mips, darker; the kept normal map).
    [[nodiscard]] std::vector<std::string> VerifyLantern(const std::vector<std::filesystem::path>& a_inputs,
        const std::filesystem::path& a_dataDir);

    /// --write-lantern <data folder> --in <dir> [...]: builds, verifies and prints the read-back of every output. 0/1.
    [[nodiscard]] int RunWriteLantern(const std::filesystem::path& a_dataDir, const std::vector<std::filesystem::path>& a_inputs);

    /// --inspect-lantern <file.nif> [...]: the read-back print of any NIF (vanilla sources included). 0/1.
    [[nodiscard]] int RunInspectLantern(const std::vector<std::filesystem::path>& a_files);
}

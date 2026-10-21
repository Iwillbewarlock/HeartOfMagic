#pragma once

// =============================================================================
// GreyLantern - the Black Flame lantern's meshes and texture (--write-lantern; lantern spec section 6)
// =============================================================================
// docs/superpowers/specs/2026-10-21-black-flame-lantern-design.md, section 6 and records 0x80D / 0x80E / 0x815. From
// vanilla files in the --in folders (loose, Data layout; extract Skyrim - Meshes0.bsa / Skyrim - Textures*.bsa first):
//   lantern_gnd.nif    candlelantern01.nif's frame and collision, diffuse = the soot copy, normal map kept, and an
//                      AddOnNode9100 (grey ADDN 0x804 MPSFireSmall01) at the cage centre (the frame's vertex bounds)
//   lantern_worn.nif   belt-mounted at the left hip, every shape 100% on NPC Pelvis [Pelv] (BSDismemberSkinInstance, one
//                      partition, body part 60): a leather strap looped over the belt line (made here, vanilla leather
//                      textures), an iron ring in its lower end (made here, the soot lamp texture), and the frame
//                      without collision (handle turned upright) hanging from the ring, with one connected piece of each
//                      of the Flame Atronach's fire strips FireStrip08 and FireStripWarp (with their shader controllers)
//                      upright in the cage; FireStrip08's palette shader takes GradBlackFlame.dds through GreyEffects
//                      (FireStripWarp is a lighting-shader refraction strip on a normal map: no colour; its refraction
//                      strength lowered to kLanternWarpRefraction)
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
    inline constexpr const char* kLanternFlameSource = "meshes\\actors\\atronachflame\\character assets\\atronachflame.nif";
    inline constexpr const char* kLanternDiffuseSource = "textures\\clutter\\common\\lampgeneric01.dds";

    // Outputs (Data-relative, under the --write-lantern folder)
    inline constexpr const char* kLanternGroundNif = "meshes\\heartofmagic\\blackflame\\lantern\\lantern_gnd.nif";
    inline constexpr const char* kLanternWornNif = "meshes\\heartofmagic\\blackflame\\lantern\\lantern_worn.nif";
    inline constexpr const char* kLanternSmokeNif = "meshes\\heartofmagic\\blackflame\\lantern\\lantern_smoke.nif";
    // The texture's file and the text the NIF slot names (the slot keeps the case the other addon paths use)
    inline constexpr const char* kLanternDiffuse = "textures\\HeartOfMagic\\BlackFlame\\Lantern\\LampGenericSoot01.dds";

    // The grey ADDN indices the lantern's addon nodes name (shipped records 0x804 MPSFireSmall01, 0x809 MPSFireSmSmoke01)
    inline constexpr int kLanternFlameAddon = 9100;
    inline constexpr int kLanternSmokeAddon = 9123;
    // BSValueNode value flags as the precedent of each role has them: the vanilla candle lantern's flame nodes 1
    // (BSVN_BILLBOARD_WORLD_Z), the vanilla smoke static fxsmokechimney01.nif's AddOnNode56 0
    inline constexpr std::uint8_t kLanternFlameValueFlags = 1;
    inline constexpr std::uint8_t kLanternSmokeValueFlags = 0;
    // BSXFlags bit 4, Addon (nif.xml BSXFlags): set on a model that carries addon nodes
    inline constexpr std::uint32_t kBsxAddon = 1u << 4;
    // lantern_smoke.nif's BSXFlags: the value the vanilla effect statics with an AddOnNode carry, fxsmokechimney01.nif and
    // fxfire01new.nif (read 2026-10-21 from Skyrim - Meshes0.bsa): 0x31 = Animated | Addon | EditorMarker. Copied as it is;
    // the EditorMarker bit names a shape this model does not have, which leaves nothing for it to hide
    inline constexpr std::uint32_t kLanternSmokeBsx = 0x31;
    inline constexpr const char* kLanternSmokeRootName = "HoMBF_LanternSmoke";
    inline constexpr const char* kLanternWornRootName = "HoMBF_LanternWorn";
    inline constexpr const char* kLanternWornFrameName = "HoMBF_LanternFrame";

    // The worn mesh: the bone, its body part and the shapes copied from the Flame Atronach
    inline constexpr const char* kLanternBone = "NPC Pelvis [Pelv]";
    inline constexpr std::uint16_t kLanternBodyPart = 60;
    inline constexpr std::array<const char*, 2> kLanternFlameStrips{ "FireStrip08", "FireStripWarp" };
    // SLSF1 bit 1, Skinned (CommonLibSSE-NG BSShaderProperty.h EShaderPropertyFlag kSkinned = 1)
    inline constexpr std::uint32_t kSlsf1Skinned = 1u << 1;

    // The cage centre: X and Y from the vertex bounds of the frame's vertices in this middle share of its height (the
    // cage walls; candlelantern01's hook at the top reaches out in +Y and would pull a whole-bounds centre off the cage),
    // Z the middle of the whole bounds' height
    inline constexpr float kLanternCageBand = 0.5f;

    // ---- The worn model (round 2, 2026-10-21: belt-mounted). Character space in the skeleton's reference pose: +Y
    // forward, -X the wearer's left, Z up. A leather strap loops over the belt line at the left hip, an iron ring hangs
    // in its lower end, and the lantern hangs from the ring by its handle. Every part is placed on the vanilla male and
    // female bodies (kLanternBodySources, required) and checked against both.
    inline constexpr std::array<const char*, 2> kLanternBodySources{ "meshes\\actors\\character\\character assets\\malebody_1.nif",
        "meshes\\actors\\character\\character assets\\femalebody_1.nif" };

    // The frame: scaled so its height before the handle turn is this (0.8 x round 1's 25)
    inline constexpr float kLanternWornHeight = 20.0f;
    // The handle: the one frame piece (LanternPieces) that reaches past the cage walls in +Y (candlelantern01's flat
    // loop, z 27.2-28.3), turned about +X by this around its hinge (the piece's -Y edge at its middle height) so it stands up
    inline constexpr float kLanternHandleTurnDeg = 90.0f;
    // Then the whole lantern turned about Z (around the cage's vertical axis) by this: the upright loop lies along the body
    // side (the YZ plane; across X it would reach 3.6 units into the hip) and the cage sits outboard of its hinge
    inline constexpr float kLanternWornYawDeg = -90.0f;

    // The strap: a centreline path in the XZ plane at Y = pelvis + kLanternStrapY, extruded kLanternStrapWidth along Y
    // and kLanternStrapThickness across. The inner layer rises from kLanternStrapInnerBottom on the body
    // (kLanternStrapBodyGap off it), folds over the top at kLanternStrapTop, and the outer layer hangs down
    // kLanternStrapBeltGap further out (the belt's room) to kLanternStrapBottom, draping (never moving back in towards
    // the body as it goes down). Z values are absolute in the reference pose (malebody_1's belt top at the hip ~z 81.25).
    inline constexpr float kLanternStrapY = 3.0f;              // a little forward of the pelvis; the arm hangs at y <= -2 from z 76
    inline constexpr float kLanternStrapTop = 86.0f;
    inline constexpr float kLanternStrapInnerBottom = 79.0f;
    inline constexpr float kLanternStrapBottom = 73.0f;        // round 1's frame top (72.9)
    inline constexpr float kLanternStrapWidth = 3.0f;
    inline constexpr float kLanternStrapThickness = 0.4f;
    inline constexpr float kLanternStrapBodyGap = 0.5f;
    inline constexpr float kLanternStrapBeltGap = 1.0f;
    inline constexpr float kLanternStrapStep = 0.5f;           // path samples along Z
    inline constexpr int kLanternStrapFoldSegments = 8;        // the half circle over the top
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

    // The ring: a torus in the XZ plane (its hole along Y, the handle's loop through it), its top tube in the strap's
    // lower end (the tube centre kLanternRingTube above kLanternStrapBottom), on the soot lamp texture at the handle's UVs
    inline constexpr float kLanternRingRadius = 1.4f;
    inline constexpr float kLanternRingTube = 0.3f;
    inline constexpr int kLanternRingSegments = 24;
    inline constexpr int kLanternRingSides = 8;
    inline constexpr const char* kLanternRingName = "HoMBF_LanternRing";

    // The lantern hangs with its handle's top wire resting on the ring's bottom tube. When it would come nearer than
    // kLanternBodyMargin to a body, it is tilted about Y around that contact (bottom outwards, as a hanging lantern rests
    // against a thigh) in kLanternTiltStepDeg steps up to kLanternTiltMaxDeg; more than that fails the build. The flame does
    // not tilt: it moves with the cage centre and stays upright.
    inline constexpr float kLanternBodyMargin = 1.0f;          // the lantern: frame and flame
    inline constexpr float kLanternStrapMargin = 0.3f;         // the strap (its inner layer is the nearest) and the ring
    inline constexpr float kLanternTiltStepDeg = 1.0f;
    inline constexpr float kLanternTiltMaxDeg = 45.0f;

    // The flame: one connected piece of each strip (the one with the most triangles, then the tallest), the FireStripWarp
    // piece the one whose rest-pose bounds overlap the chosen FireStrip08 piece most; both under one transform scaled to
    // kLanternFlameFill of the cage (the smallest of width, depth, height wins) and centred on it
    inline constexpr float kLanternFlameFill = 0.5f;
    // FireStripWarp's refraction strength (0.15 on the atronach, whose flame is ~10x this one)
    inline constexpr float kLanternWarpRefraction = 0.05f;

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
        float handleWire = 0.0f;            // the handle wire's radius (scaled)
        float tiltDeg = 0.0f;
        std::vector<std::string> summary;   // the lines RunWriteLantern prints after the read-back
    };

    /// One worn part against one body
    struct LanternClearance
    {
        std::string part;   // "lantern" (frame + flame), "strap", "ring"
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

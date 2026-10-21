#pragma once

// =============================================================================
// GreyLantern - the Black Flame lantern's meshes and texture (--write-lantern; lantern spec section 6)
// =============================================================================
// docs/superpowers/specs/2026-10-21-black-flame-lantern-design.md, section 6 and records 0x80D / 0x80E / 0x815. From
// vanilla files in the --in folders (loose, Data layout; extract Skyrim - Meshes0.bsa / Skyrim - Textures*.bsa first):
//   lantern_gnd.nif    candlelantern01.nif's frame and collision, diffuse = the soot copy, normal map kept, and an
//                      AddOnNode9100 (grey ADDN 0x804 MPSFireSmall01) at the cage centre (the frame's vertex bounds)
//   lantern_worn.nif   the same frame without collision, 100% on NPC Pelvis [Pelv] (BSDismemberSkinInstance, one
//                      partition, body part 60) at the left hip, with the Flame Atronach's fire strips FireStrip08 and
//                      FireStripWarp (with their shader controllers) moved into the cage and skinned the same way;
//                      FireStrip08's palette shader takes GradBlackFlame.dds through GreyEffects (FireStripWarp is a
//                      lighting-shader refraction strip on a normal map: no colour, left as it is)
//   lantern_smoke.nif  an empty BSFadeNode with BSXFlags and one AddOnNode9123 (grey ADDN 0x809 MPSFireSmSmoke01)
//   LampGenericSoot01.dds  LampGeneric01.dds with RGB x kLanternSootDarken, alpha kept, same format and mips
// An AddOnNode is a BSValueNode whose value is the ADDN index (what the engine reads; vanilla candlelanternwithcandle01.nif
// AddOnNode49 = BSValueNode value 49), named AddOnNode<index> as the vanilla files name theirs.

#include "GreyBuild.h"

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

    // Worn placement, in the skeleton's reference pose (character space: +Y forward, -X the wearer's left, Z up).
    // The frame is scaled so its height is kLanternWornHeight, then moved so its top above the cage centre (the handle
    // ring) sits at the pelvis bone's origin + kLanternWornHipOffset: outside the left hip, hanging from belt height, a
    // little behind the hip line. The offline check is the body clearance below; how it looks is judged in game.
    inline constexpr float kLanternWornHeight = 25.0f;  // game units (the record asks 22-28)
    inline constexpr std::array<float, 3> kLanternWornHipOffset{ -24.5f, -3.0f, 4.0f };
    // The flame strips: both together scaled uniformly to this share of the cage's width, depth and height (the
    // smallest of the three wins) and centred on the cage centre
    inline constexpr float kLanternFlameFill = 0.5f;
    // Body clearance (optional inputs: checked when an --in folder has them): no vertex of these vanilla bodies, in their
    // reference pose, may lie inside the worn frame's bounds grown by kLanternBodyMargin on every side
    inline constexpr std::array<const char*, 2> kLanternBodySources{ "meshes\\actors\\character\\character assets\\malebody_1.nif",
        "meshes\\actors\\character\\character assets\\femalebody_1.nif" };
    inline constexpr float kLanternBodyMargin = 1.0f;

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
        std::vector<std::string> bones;
        std::vector<std::uint16_t> partitions;  // BSDismember body parts
        std::uint32_t vertices = 0;
        std::uint32_t weightedOnFirstBone = 0;  // vertices with a weight on bone 0 (the shape's vertex data)
        std::uint32_t skinDataWeights = 0;      // NiSkinData's weight list of bone 0
        bool allWeightsOne = false;             // every vertex: bone 0 weight 1 and no other (vertex data and NiSkinData)
        std::vector<std::string> controllers;   // the shader's controller chain, "type -> target ok|wrong"
        LanternBox bounds;                      // vertex bounds in the root's space (skinned: skin space)
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
        std::array<float, 3> wornTop{};     // where the frame's top centre was placed (character space)
        std::array<float, 3> pelvis{};      // the pelvis bone's origin (character space)
        LanternBox wornFrame;               // the worn frame's vertex bounds (character space)
    };

    struct LanternClearance
    {
        int vertices = 0;        // body vertices read
        int inside = 0;          // of those, inside the box grown by the margin
        float nearestGap = 0.0f;  // the smallest distance from the box (ungrown) to a body vertex
        float leftmost = 0.0f;    // the smallest X of the body vertices within the box's Y and Z ranges (grown)
        float rightmost = 0.0f;   // the largest X of those
    };

    /// Every shape of a_body at its reference pose (each skinned shape's global-to-skin transform inverted) against a_box
    [[nodiscard]] LanternClearance BodyClearance(const std::filesystem::path& a_body, const LanternBox& a_box, float a_margin);

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

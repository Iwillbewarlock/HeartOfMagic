// =============================================================================
// GreyLantern - the lantern's meshes and soot texture (GreyLantern.h, --write-lantern / --inspect-lantern)
// =============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif

#include "GreyLantern.h"
#include "GreyTexture.h"

#include <Animation.hpp>
#include <ExtraData.hpp>
#include <Geometry.hpp>
#include <NifFile.hpp>
#include <Nodes.hpp>
#include <Shaders.hpp>
#include <Skin.hpp>

#include <DirectXTex.h>

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace GreyBuild
{
    namespace
    {
        constexpr int kNiflyOk = 0;
        constexpr float kWeightOne = 1.0f;
        constexpr float kWeightTolerance = 1e-3f;  // half-float weights: 1.0 is exact, the tolerance only guards rounding
        constexpr float kFloatMax = std::numeric_limits<float>::max();
        constexpr std::uint32_t kNormalSlot = 1;

        using nifly::MatTransform;
        using nifly::Vector3;

        [[nodiscard]] std::array<float, 3> ToArray(const Vector3& a_v) noexcept { return { a_v.x, a_v.y, a_v.z }; }
        [[nodiscard]] Vector3 ToVector(const std::array<float, 3>& a_v) noexcept { return Vector3(a_v[0], a_v[1], a_v[2]); }

        [[nodiscard]] LanternBox EmptyBox() noexcept
        {
            LanternBox box;
            box.min = { kFloatMax, kFloatMax, kFloatMax };
            box.max = { -kFloatMax, -kFloatMax, -kFloatMax };
            return box;
        }

        void Grow(LanternBox& a_box, const Vector3& a_v) noexcept
        {
            const auto v = ToArray(a_v);
            for (int axis = 0; axis < 3; ++axis) {
                a_box.min[axis] = std::min(a_box.min[axis], v[axis]);
                a_box.max[axis] = std::max(a_box.max[axis], v[axis]);
            }
        }

        void Grow(LanternBox& a_box, const LanternBox& a_other) noexcept
        {
            for (int axis = 0; axis < 3; ++axis) {
                a_box.min[axis] = std::min(a_box.min[axis], a_other.min[axis]);
                a_box.max[axis] = std::max(a_box.max[axis], a_other.max[axis]);
            }
        }

        [[nodiscard]] bool IsEmpty(const LanternBox& a_box) noexcept { return a_box.min[0] > a_box.max[0]; }

        [[nodiscard]] std::string Text(const std::array<float, 3>& a_v)
        {
            return fmt::format("({:.4f}, {:.4f}, {:.4f})", a_v[0], a_v[1], a_v[2]);
        }

        [[nodiscard]] std::string BoxText(const LanternBox& a_box)
        {
            return fmt::format("min {} max {} size ({:.4f}, {:.4f}, {:.4f})", Text(a_box.min), Text(a_box.max), a_box.Size(0),
                a_box.Size(1), a_box.Size(2));
        }

        /// The node whose children name a_id, or NIF_NPOS
        [[nodiscard]] std::uint32_t ParentOf(const nifly::NifFile& a_nif, std::uint32_t a_id)
        {
            const auto& hdr = a_nif.GetHeader();
            for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
                if (auto* node = hdr.GetBlock<nifly::NiNode>(id)) {
                    for (const auto& child : node->childRefs) {
                        if (child.index == a_id) {
                            return id;
                        }
                    }
                }
            }
            return nifly::NIF_NPOS;
        }

        /// a_id's coordinates to the root node's own space: its transform and every ancestor's below the root
        [[nodiscard]] MatTransform ToRootSpace(const nifly::NifFile& a_nif, std::uint32_t a_id)
        {
            const auto& hdr = a_nif.GetHeader();
            const std::uint32_t root = hdr.GetBlockID(a_nif.GetRootNode());
            MatTransform out;
            std::set<std::uint32_t> seen;
            for (std::uint32_t id = a_id; id != nifly::NIF_NPOS && id != root; id = ParentOf(a_nif, id)) {
                if (!seen.insert(id).second) {
                    throw std::runtime_error("the node tree has a cycle");
                }
                auto* object = hdr.GetBlock<nifly::NiAVObject>(id);
                if (!object) {
                    break;
                }
                out = object->GetTransformToParent().ComposeTransforms(out);
            }
            return out;
        }

        [[nodiscard]] nifly::NiShape* FindShape(const nifly::NifFile& a_nif, const std::string& a_name)
        {
            for (auto* shape : a_nif.GetShapes()) {
                if (shape->name.get() == a_name) {
                    return shape;
                }
            }
            return nullptr;
        }

        [[nodiscard]] std::filesystem::path Need(const std::vector<std::filesystem::path>& a_inputs, const char* a_rel)
        {
            auto found = FindSource(a_inputs, a_rel);
            if (!found) {
                throw std::runtime_error(fmt::format("'{}' is in no --in folder (the builder reads loose files: extract it from "
                                                     "the vanilla BSA into an --in folder first)", a_rel));
            }
            return *found;
        }

        void SaveNew(nifly::NifFile& a_nif, const std::filesystem::path& a_file)
        {
            std::ostringstream out(std::ios::binary);
            const int result = a_nif.Save(out);  // nifly's defaults: unreferenced blocks removed, blocks sorted
            if (result != kNiflyOk) {
                throw std::runtime_error(fmt::format("nifly could not write '{}' (code {})", PathText(a_file), result));
            }
            const std::string bytes = out.str();
            std::filesystem::create_directories(a_file.parent_path());
            std::ofstream file(a_file, std::ios::binary | std::ios::trunc);
            file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            file.close();
            if (!file) {
                throw std::runtime_error(fmt::format("writing '{}' failed", PathText(a_file)));
            }
        }

        std::uint32_t AddValueNode(nifly::NifFile& a_nif, nifly::NiNode& a_parent, int a_index, std::uint8_t a_flags,
            const Vector3& a_at)
        {
            auto node = std::make_unique<nifly::BSValueNode>();
            node->name.get() = fmt::format("AddOnNode{}", a_index);
            node->value = a_index;
            node->valueFlags = static_cast<nifly::BSValueNodeFlags>(a_flags);
            MatTransform at;
            at.translation = a_at;
            node->SetTransformToParent(at);
            const std::uint32_t id = a_nif.GetHeader().AddBlock(std::move(node));
            a_parent.childRefs.AddBlockRef(id);
            return id;
        }

        [[nodiscard]] nifly::BSXFlags* FindBsx(nifly::NifFile& a_nif)
        {
            auto& hdr = a_nif.GetHeader();
            for (const auto& ref : a_nif.GetRootNode()->extraDataRefs) {
                if (auto* bsx = hdr.GetBlock<nifly::BSXFlags>(ref)) {
                    return bsx;
                }
            }
            return nullptr;
        }

        /// The shape's vertices through a_point, its normals through a_rotation (tangents made again from them when it
        /// is not the identity), its own transform cleared. BSTriShape only (what both sources hold).
        void BakeShape(nifly::NiShape& a_shape, const std::function<Vector3(const Vector3&)>& a_point,
            const nifly::Matrix3& a_rotation)
        {
            auto* tri = dynamic_cast<nifly::BSTriShape*>(&a_shape);
            if (!tri) {
                throw std::runtime_error(fmt::format("shape '{}' is a {}, not a BSTriShape", a_shape.name.get(), a_shape.GetBlockName()));
            }
            for (auto& vertex : tri->vertData) {
                vertex.vert = a_point(vertex.vert);
            }
            nifly::Matrix3 identity;
            if (!a_rotation.IsNearlyEqualTo(identity) && tri->HasNormals()) {
                auto normals = tri->UpdateRawNormals();
                for (auto& normal : normals) {
                    normal = a_rotation * normal;
                    normal.Normalize();
                }
                tri->SetNormals(normals);
                if (tri->HasTangents()) {
                    tri->CalcTangentSpace();
                }
            }
            a_shape.transform.Clear();
        }

        /// Every controller on a_shape's shader aimed at that shader again (a clone keeps the source file's index)
        std::vector<std::string> RetargetShaderControllers(nifly::NifFile& a_nif, nifly::NiShape& a_shape)
        {
            std::vector<std::string> lines;
            auto& hdr = a_nif.GetHeader();
            const std::uint32_t shaderId = a_shape.ShaderPropertyRef()->index;
            auto* shader = hdr.GetBlock<nifly::NiShader>(shaderId);
            if (!shader) {
                return lines;
            }
            std::set<std::uint32_t> seen;
            for (std::uint32_t id = shader->controllerRef.index; id != nifly::NIF_NPOS; ) {
                auto* controller = hdr.GetBlock<nifly::NiTimeController>(id);
                if (!controller || !seen.insert(id).second) {
                    break;
                }
                lines.push_back(fmt::format("{} target {} -> {}", controller->GetBlockName(), controller->targetRef.index, shaderId));
                controller->targetRef.index = shaderId;
                id = controller->nextControllerRef.index;
            }
            return lines;
        }

        /// a_shape skinned 100% to a_boneId (the file's bone node at a_boneGlobal), global-to-skin identity, one
        /// BSDismember partition kLanternBodyPart, bone bounds round its vertices, the shader's Skinned flag on
        void SkinToBone(nifly::NifFile& a_nif, nifly::NiShape& a_shape, std::uint32_t a_boneId, const MatTransform& a_boneGlobal)
        {
            auto* tri = dynamic_cast<nifly::BSTriShape*>(&a_shape);
            if (!tri) {
                throw std::runtime_error(fmt::format("shape '{}' is not a BSTriShape", a_shape.name.get()));
            }
            const std::string name = a_shape.name.get();
            a_nif.CreateSkinning(&a_shape);
            std::vector<int> bones{ static_cast<int>(a_boneId) };
            a_nif.SetShapeBoneIDList(&a_shape, bones);
            a_nif.SetShapeTransformGlobalToSkin(&a_shape, MatTransform());
            const MatTransform skinToBone = a_boneGlobal.InverseTransform();
            a_nif.SetShapeTransformSkinToBone(&a_shape, 0, skinToBone);

            std::unordered_map<std::uint16_t, float> weights;
            std::vector<Vector3> inBone;
            const auto count = static_cast<std::uint16_t>(tri->vertData.size());
            for (std::uint16_t i = 0; i < count; ++i) {
                std::vector<std::uint8_t> ids{ 0 };
                std::vector<float> values{ kWeightOne };
                a_nif.SetShapeVertWeights(name, i, ids, values);
                weights[i] = kWeightOne;
                inBone.push_back(skinToBone.ApplyTransform(tri->vertData[i].vert));
            }
            a_nif.SetShapeBoneWeights(name, 0, weights);
            nifly::BoundingSphere boneBounds(inBone);
            a_nif.SetShapeBoneBounds(name, 0, boneBounds);

            std::vector<Vector3> verts;
            verts.reserve(tri->vertData.size());
            for (const auto& vertex : tri->vertData) {
                verts.push_back(vertex.vert);
            }
            a_shape.SetBounds(nifly::BoundingSphere(verts));

            nifly::NiVector<nifly::BSDismemberSkinInstance::PartitionInfo> parts;
            nifly::BSDismemberSkinInstance::PartitionInfo part;
            part.flags = static_cast<nifly::PartitionFlags>(nifly::PF_EDITOR_VISIBLE | nifly::PF_START_NET_BONESET);
            part.partID = kLanternBodyPart;
            parts.push_back(part);
            const std::vector<int> triParts(a_shape.GetNumTriangles(), 0);
            a_nif.SetShapePartitions(&a_shape, parts, triParts, true);
            a_nif.UpdateSkinPartitions(&a_shape);

            if (auto* shader = a_nif.GetHeader().GetBlock<nifly::BSShaderProperty>(a_shape.ShaderPropertyRef())) {
                shader->shaderFlags1 |= kSlsf1Skinned;
            }
        }

        [[nodiscard]] std::string HrText(HRESULT a_result)
        {
            return fmt::format("HRESULT 0x{:08X}", static_cast<std::uint32_t>(a_result));
        }

        /// Mean RGB of one image (any format TransformImage / EvaluateImage reads)
        [[nodiscard]] double MeanRgb(const DirectX::Image& a_image)
        {
            double sum = 0.0;
            std::uint64_t count = 0;
            (void)DirectX::EvaluateImage(a_image, [&](const DirectX::XMVECTOR* a_pixels, std::size_t a_width, std::size_t) {
                for (std::size_t x = 0; x < a_width; ++x) {
                    DirectX::XMFLOAT4 p;
                    DirectX::XMStoreFloat4(&p, a_pixels[x]);
                    sum += (static_cast<double>(p.x) + p.y + p.z) / 3.0;
                    ++count;
                }
            });
            return count > 0 ? sum / static_cast<double>(count) : 0.0;
        }

        /// a_source decoded to 8-bit RGBA when it is compressed (same colour space), else as it is
        [[nodiscard]] HRESULT Decoded(const DirectX::ScratchImage& a_source, DirectX::ScratchImage& a_out, bool& a_compressed)
        {
            const auto& meta = a_source.GetMetadata();
            a_compressed = DirectX::IsCompressed(meta.format);
            if (!a_compressed) {
                return S_OK;
            }
            const DXGI_FORMAT work = DirectX::IsSRGB(meta.format) ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
            return DirectX::Decompress(a_source.GetImages(), a_source.GetImageCount(), meta, work, a_out);
        }

        void Check(std::vector<std::string>& a_problems, bool a_ok, const std::string& a_what)
        {
            if (!a_ok) {
                a_problems.push_back(a_what);
            }
        }

        [[nodiscard]] std::string Lower(std::string a_text)
        {
            std::transform(a_text.begin(), a_text.end(), a_text.begin(),
                [](unsigned char a_c) { return static_cast<char>(std::tolower(a_c)); });
            return a_text;
        }
    }

    // =========================================================================
    // Read-back
    // =========================================================================

    LanternNifInfo InspectLanternNif(const std::filesystem::path& a_file)
    {
        nifly::NifFile nif;
        LoadNif(nif, a_file);
        auto& hdr = nif.GetHeader();
        LanternNifInfo info;
        info.unknownBlocks = CountUnknownBlocks(nif);
        info.sseCompatible = nif.IsSSECompatible();
        std::vector<std::pair<std::string, int>> counts;
        for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
            const std::string type = hdr.GetBlockTypeStringById(id);
            auto it = std::find_if(counts.begin(), counts.end(), [&](const auto& a_pair) { return a_pair.first == type; });
            if (it == counts.end()) {
                counts.emplace_back(type, 1);
            } else {
                ++it->second;
            }
            if (auto* av = hdr.GetBlock<nifly::NiAVObject>(id); av && !av->collisionRef.IsEmpty()) {
                info.hasCollision = true;
            }
            if (auto* bsx = hdr.GetBlock<nifly::BSXFlags>(id)) {
                info.hasBsx = true;
                info.bsx = bsx->integerData;
            }
            if (auto* node = hdr.GetBlock<nifly::NiNode>(id)) {
                info.nodeNames.push_back(node->name.get());
                if (auto* value = dynamic_cast<nifly::BSValueNode*>(node)) {
                    LanternValueNode entry;
                    entry.name = value->name.get();
                    entry.value = value->value;
                    entry.flags = static_cast<std::uint8_t>(value->valueFlags);
                    entry.translation = ToArray(value->GetTransformToParent().translation);
                    const std::uint32_t parent = ParentOf(nif, id);
                    auto* parentNode = hdr.GetBlock<nifly::NiNode>(parent);
                    entry.parent = parentNode ? parentNode->name.get() : std::string("<none>");
                    info.valueNodes.push_back(entry);
                }
            }
        }
        for (const auto& [type, count] : counts) {
            info.blockTypes.push_back(fmt::format("{} x{}", type, count));
        }
        auto* root = nif.GetRootNode();
        info.rootType = root ? root->GetBlockName() : "<none>";
        info.rootName = root ? root->name.get() : "";
        info.bounds = EmptyBox();

        for (auto* shape : nif.GetShapes()) {
            LanternShapeInfo s;
            s.name = shape->name.get();
            s.type = shape->GetBlockName();
            s.vertices = shape->GetNumVertices();
            const std::uint32_t shapeId = hdr.GetBlockID(shape);
            const std::uint32_t shaderId = shape->ShaderPropertyRef() ? shape->ShaderPropertyRef()->index : nifly::NIF_NPOS;
            if (auto* shader = hdr.GetBlock<nifly::NiShader>(shaderId)) {
                s.shader = shader->GetBlockName();
                if (auto* bs = dynamic_cast<nifly::BSShaderProperty*>(shader)) {
                    s.flags1 = bs->shaderFlags1;
                }
                if (auto* effect = dynamic_cast<nifly::BSEffectShaderProperty*>(shader)) {
                    s.textures.push_back("source " + effect->sourceTexture.get());
                    s.textures.push_back("greyscale " + effect->greyscaleTexture.get());
                } else if (auto* set = hdr.GetBlock<nifly::BSShaderTextureSet>(shader->TextureSetRef())) {
                    for (std::uint32_t slot = 0; slot < set->textures.size(); ++slot) {
                        if (!set->textures[slot].get().empty()) {
                            s.textures.push_back(fmt::format("[{}] {}", slot, set->textures[slot].get()));
                        }
                    }
                }
                std::set<std::uint32_t> seen;
                for (std::uint32_t id = shader->controllerRef.index; id != nifly::NIF_NPOS;) {
                    auto* controller = hdr.GetBlock<nifly::NiTimeController>(id);
                    if (!controller || !seen.insert(id).second) {
                        break;
                    }
                    s.controllers.push_back(fmt::format("{} -> target {} ({})", controller->GetBlockName(), controller->targetRef.index,
                        controller->targetRef.index == shaderId ? "its shader" : "WRONG"));
                    id = controller->nextControllerRef.index;
                }
            }
            if (shape->HasSkinInstance()) {
                auto* skin = hdr.GetBlock<nifly::NiBoneContainer>(shape->SkinInstanceRef());
                s.skinType = skin ? skin->GetBlockName() : "<missing>";
                s.fileRoot = hdr.GetBlockID(nif.GetRootNode());
                if (auto* instance = dynamic_cast<nifly::NiSkinInstance*>(skin)) {
                    s.skinRoot = instance->targetRef.index;
                }
                s.skinned = shape->IsSkinned();
                (void)nif.GetShapeBoneList(shape, s.bones);
                if (auto* dismember = dynamic_cast<nifly::BSDismemberSkinInstance*>(skin)) {
                    for (const auto& part : dismember->partitions) {
                        s.partitions.push_back(part.partID);
                    }
                }
                std::unordered_map<std::uint16_t, float> weights;
                s.weightedOnFirstBone = nif.GetShapeBoneWeights(shape, 0, weights);
                bool allOne = !s.bones.empty();
                if (auto* tri = dynamic_cast<nifly::BSTriShape*>(shape)) {
                    for (const auto& vertex : tri->vertData) {
                        allOne = allOne && vertex.weightBones[0] == 0 && std::fabs(vertex.weights[0] - kWeightOne) <= kWeightTolerance &&
                                 vertex.weights[1] == 0.0f && vertex.weights[2] == 0.0f && vertex.weights[3] == 0.0f;
                    }
                }
                if (auto* instance = dynamic_cast<nifly::NiSkinInstance*>(skin)) {
                    if (auto* data = hdr.GetBlock<nifly::NiSkinData>(instance->dataRef); data && !data->bones.empty()) {
                        s.skinDataWeights = static_cast<std::uint32_t>(data->bones[0].vertexWeights.size());
                        for (const auto& weight : data->bones[0].vertexWeights) {
                            allOne = allOne && std::fabs(weight.weight - kWeightOne) <= kWeightTolerance;
                        }
                    }
                }
                s.allWeightsOne = allOne && s.skinDataWeights == s.vertices;
            }
            s.bounds = EmptyBox();
            std::vector<Vector3> verts;
            if (nif.GetVertsForShape(shape, verts)) {
                // a skinned shape's vertices are in skin space (its own transform is not used); others go to the root's space
                const MatTransform toRoot = shape->HasSkinInstance() ? MatTransform() : ToRootSpace(nif, shapeId);
                for (const auto& v : verts) {
                    Grow(s.bounds, toRoot.ApplyTransform(v));
                }
                Grow(info.bounds, s.bounds);
            }
            info.shapes.push_back(std::move(s));
        }
        return info;
    }

    void PrintLanternNif(const std::string& a_title, const LanternNifInfo& a_info)
    {
        std::cout << a_title << "\n";
        std::cout << fmt::format("  root {} '{}', SSE-compatible {}, unknown blocks {}, collision {}\n", a_info.rootType, a_info.rootName,
            a_info.sseCompatible ? "yes" : "no", a_info.unknownBlocks, a_info.hasCollision ? "yes" : "no");
        std::string types;
        for (const auto& line : a_info.blockTypes) {
            types += (types.empty() ? "" : ", ") + line;
        }
        std::cout << "  blocks: " << types << "\n";
        std::string nodes;
        for (const auto& name : a_info.nodeNames) {
            nodes += (nodes.empty() ? "'" : ", '") + name + "'";
        }
        std::cout << "  nodes: " << nodes << "\n";
        if (a_info.hasBsx) {
            std::cout << fmt::format("  BSXFlags {} (0x{:X})\n", a_info.bsx, a_info.bsx);
        } else {
            std::cout << "  BSXFlags none\n";
        }
        for (const auto& value : a_info.valueNodes) {
            std::cout << fmt::format("  value node '{}' value {} flags {} at {} under '{}'\n", value.name, value.value, value.flags,
                Text(value.translation), value.parent);
        }
        for (const auto& s : a_info.shapes) {
            std::cout << fmt::format("  shape '{}' {} vertices {} shader {} SLSF1 0x{:08X}\n", s.name, s.type, s.vertices,
                s.shader.empty() ? "<none>" : s.shader, s.flags1);
            for (const auto& texture : s.textures) {
                std::cout << "    texture " << texture << "\n";
            }
            for (const auto& controller : s.controllers) {
                std::cout << "    controller " << controller << "\n";
            }
            if (!s.skinType.empty()) {
                std::string bones;
                for (const auto& bone : s.bones) {
                    bones += (bones.empty() ? "'" : ", '") + bone + "'";
                }
                std::string parts;
                for (const auto part : s.partitions) {
                    parts += (parts.empty() ? "" : ", ") + std::to_string(part);
                }
                std::cout << fmt::format("    skin {} (vertex skinned flag {}, skeleton root block {}{}), bones [{}], partitions [{}], "
                                         "weighted on bone 0: {} (vertex data), {} (NiSkinData); all weights 1 on bone 0: {}\n",
                    s.skinType, s.skinned ? "on" : "off", s.skinRoot, s.skinRoot == s.fileRoot ? " = the file's root" : " NOT the root",
                    bones, parts, s.weightedOnFirstBone, s.skinDataWeights, s.allWeightsOne ? "yes" : "no");
            } else {
                std::cout << "    not skinned\n";
            }
            if (!IsEmpty(s.bounds)) {
                std::cout << "    vertex bounds " << BoxText(s.bounds) << "\n";
            }
        }
        if (!IsEmpty(a_info.bounds)) {
            std::cout << "  all shapes' vertex bounds " << BoxText(a_info.bounds) << "\n";
        }
    }

    // =========================================================================
    // Texture
    // =========================================================================

    bool DdsMean(const std::filesystem::path& a_file, double& a_mean, std::size_t& a_width, std::size_t& a_height, std::size_t& a_mips,
        std::string& a_format)
    {
        DirectX::TexMetadata meta{};
        DirectX::ScratchImage image;
        if (FAILED(DirectX::LoadFromDDSFile(a_file.c_str(), DirectX::DDS_FLAGS_NONE, &meta, image))) {
            return false;
        }
        DirectX::ScratchImage decoded;
        bool compressed = false;
        if (FAILED(Decoded(image, decoded, compressed))) {
            return false;
        }
        a_mean = MeanRgb(*(compressed ? decoded : image).GetImage(0, 0, 0));
        a_width = meta.width;
        a_height = meta.height;
        a_mips = meta.mipLevels;
        a_format = std::to_string(static_cast<int>(meta.format));
        return true;
    }

    LanternDarkenResult DarkenDds(const std::filesystem::path& a_source, const std::filesystem::path& a_target, float a_factor)
    {
        LanternDarkenResult result;
        if (!(a_factor >= 0.0f && a_factor <= 1.0f)) {
            result.reason = fmt::format("darken factor {} is not in 0..1", a_factor);
            return result;
        }
        DirectX::TexMetadata meta{};
        DirectX::ScratchImage source;
        if (const HRESULT hr = DirectX::LoadFromDDSFile(a_source.c_str(), DirectX::DDS_FLAGS_NONE, &meta, source); FAILED(hr)) {
            result.reason = fmt::format("'{}' is not a readable DDS: {}", PathText(a_source), HrText(hr));
            return result;
        }
        result.format = std::to_string(static_cast<int>(meta.format));
        result.width = meta.width;
        result.height = meta.height;
        result.mips = meta.mipLevels;
        if (DirectX::FormatDataType(meta.format) == DirectX::FORMAT_TYPE_FLOAT || DirectX::IsTypeless(meta.format) ||
            DirectX::IsPlanar(meta.format) || DirectX::IsPalettized(meta.format) || DirectX::IsVideo(meta.format)) {
            result.reason = fmt::format("format {} is not darkened (float, typeless, planar, palettized or video)", result.format);
            return result;
        }
        DirectX::ScratchImage decoded;
        bool compressed = false;
        if (const HRESULT hr = Decoded(source, decoded, compressed); FAILED(hr)) {
            result.reason = fmt::format("decoding format {} failed: {}", result.format, HrText(hr));
            return result;
        }
        const DirectX::ScratchImage& pixels = compressed ? decoded : source;
        result.sourceMean = MeanRgb(*pixels.GetImage(0, 0, 0));
        DirectX::ScratchImage dark;
        if (const HRESULT hr = DirectX::TransformImage(pixels.GetImages(), pixels.GetImageCount(), pixels.GetMetadata(),
                [a_factor](DirectX::XMVECTOR* a_out, const DirectX::XMVECTOR* a_in, std::size_t a_width, std::size_t) {
                    for (std::size_t x = 0; x < a_width; ++x) {
                        DirectX::XMFLOAT4 p;
                        DirectX::XMStoreFloat4(&p, a_in[x]);
                        a_out[x] = DirectX::XMVectorSet(p.x * a_factor, p.y * a_factor, p.z * a_factor, p.w);
                    }
                },
                dark);
            FAILED(hr)) {
            result.reason = fmt::format("darkening the pixels failed: {}", HrText(hr));
            return result;
        }
        DirectX::ScratchImage encoded;
        const DirectX::ScratchImage* out = &dark;
        if (compressed) {
            const auto flags = static_cast<DirectX::TEX_COMPRESS_FLAGS>(DirectX::TEX_COMPRESS_DEFAULT | DirectX::TEX_COMPRESS_BC7_QUICK |
                                                                        DirectX::TEX_COMPRESS_PARALLEL);
            if (const HRESULT hr = DirectX::Compress(dark.GetImages(), dark.GetImageCount(), dark.GetMetadata(), meta.format, flags,
                    DirectX::TEX_THRESHOLD_DEFAULT, encoded);
                FAILED(hr)) {
                result.reason = fmt::format("encoding format {} failed: {}", result.format, HrText(hr));
                return result;
            }
            out = &encoded;
        }
        if (out->GetMetadata().format != meta.format || out->GetMetadata().mipLevels != meta.mipLevels) {
            result.reason = "the encoded copy's format or mip count is not the source's";
            return result;
        }
        std::error_code ec;
        std::filesystem::create_directories(a_target.parent_path(), ec);
        if (ec) {
            result.reason = fmt::format("making the folder of '{}' failed: {}", PathText(a_target), ec.message());
            return result;
        }
        if (const HRESULT hr = DirectX::SaveToDDSFile(out->GetImages(), out->GetImageCount(), out->GetMetadata(), DirectX::DDS_FLAGS_NONE,
                a_target.c_str());
            FAILED(hr)) {
            result.reason = fmt::format("writing '{}' failed: {}", PathText(a_target), HrText(hr));
            return result;
        }
        std::size_t width = 0;
        std::size_t height = 0;
        std::size_t mips = 0;
        std::string format;
        if (!DdsMean(a_target, result.copyMean, width, height, mips, format) || width != meta.width || height != meta.height ||
            mips != meta.mipLevels || format != result.format) {
            result.reason = fmt::format("'{}' does not read back as {}x{} format {} with {} mips", PathText(a_target), meta.width, meta.height,
                result.format, meta.mipLevels);
            return result;
        }
        result.ok = true;
        return result;
    }

    // =========================================================================
    // Build
    // =========================================================================

    LanternBuildResult BuildLantern(const std::vector<std::filesystem::path>& a_inputs, const std::filesystem::path& a_dataDir)
    {
        LanternBuildResult result;
        auto& log = result.log;
        const auto fail = [&](const char* a_what, const std::exception& a_error) {
            result.problems.push_back(fmt::format("{}: {}", a_what, a_error.what()));
        };

        // 1. the soot texture
        try {
            const auto dark = DarkenDds(Need(a_inputs, kLanternDiffuseSource), a_dataDir / kLanternDiffuse, kLanternSootDarken);
            if (!dark.ok) {
                throw std::runtime_error(dark.reason);
            }
            log.push_back(fmt::format("texture {}: {}x{}, format {}, {} mips, RGB x {} (kLanternSootDarken), mean RGB {:.4f} -> {:.4f}",
                kLanternDiffuse, dark.width, dark.height, dark.format, dark.mips, kLanternSootDarken, dark.sourceMean, dark.copyMean));
        } catch (const std::exception& e) {
            fail(kLanternDiffuse, e);
        }

        // 2. the ground model (kept in memory: the worn model takes its frame from it)
        nifly::NifFile ground;
        nifly::NiShape* frame = nullptr;
        bool groundOk = false;
        try {
            LoadNif(ground, Need(a_inputs, kLanternFrameSource));
            const auto shapes = ground.GetShapes();
            if (shapes.size() != 1 || !dynamic_cast<nifly::BSTriShape*>(shapes.front())) {
                throw std::runtime_error(fmt::format("{} has {} shapes, the frame expected as its only BSTriShape", kLanternFrameSource,
                    shapes.size()));
            }
            frame = shapes.front();
            auto& hdr = ground.GetHeader();
            auto* lighting = hdr.GetBlock<nifly::BSLightingShaderProperty>(frame->ShaderPropertyRef());
            auto* set = lighting ? hdr.GetBlock<nifly::BSShaderTextureSet>(lighting->TextureSetRef()) : nullptr;
            if (!set || set->textures.size() <= kNormalSlot) {
                throw std::runtime_error(fmt::format("frame '{}' has no lighting shader with a texture set", frame->name.get()));
            }
            log.push_back(fmt::format("ground frame '{}': diffuse '{}' -> '{}', normal map kept '{}'", frame->name.get(),
                set->textures[kSlotDiffuse].get(), kLanternDiffuse, set->textures[kNormalSlot].get()));
            set->textures[kSlotDiffuse].get() = kLanternDiffuse;

            std::vector<Vector3> verts;
            if (!ground.GetVertsForShape(frame, verts) || verts.empty()) {
                throw std::runtime_error("the frame has no vertices");
            }
            const MatTransform toRoot = ToRootSpace(ground, hdr.GetBlockID(frame));
            result.frameBounds = EmptyBox();
            for (const auto& v : verts) {
                Grow(result.frameBounds, toRoot.ApplyTransform(v));
            }
            {
                const float bandLow = result.frameBounds.min[2] + result.frameBounds.Size(2) * (1.0f - kLanternCageBand) * 0.5f;
                const float bandHigh = result.frameBounds.max[2] - result.frameBounds.Size(2) * (1.0f - kLanternCageBand) * 0.5f;
                LanternBox walls = EmptyBox();
                for (const auto& v : verts) {
                    const Vector3 p = toRoot.ApplyTransform(v);
                    if (p.z >= bandLow && p.z <= bandHigh) {
                        Grow(walls, p);
                    }
                }
                if (IsEmpty(walls)) {
                    log.push_back("cage walls: no frame vertex in the middle of its height, the whole bounds' centre used");
                    walls = result.frameBounds;
                }
                const auto middle = walls.Centre();
                result.cageCentre = { middle[0], middle[1], result.frameBounds.Centre()[2] };
                log.push_back(fmt::format("cage walls (vertices at z {:.4f}..{:.4f}, kLanternCageBand {}) {}", bandLow, bandHigh,
                    kLanternCageBand, BoxText(walls)));
            }
            {
                constexpr int kBands = 10;
                std::array<LanternBox, kBands> bands;
                std::array<int, kBands> counts{};
                bands.fill(EmptyBox());
                for (const auto& v : verts) {
                    const Vector3 p = toRoot.ApplyTransform(v);
                    const int band = std::clamp(static_cast<int>((p.z - result.frameBounds.min[2]) / result.frameBounds.Size(2) * kBands), 0,
                        kBands - 1);
                    Grow(bands[band], p);
                    ++counts[band];
                }
                for (int band = 0; band < kBands; ++band) {
                    log.push_back(fmt::format("frame profile band {}: {} vertices, {}", band, counts[band],
                        counts[band] ? BoxText(bands[band]) : std::string("-")));
                }
            }
            log.push_back(fmt::format("ground frame vertex bounds (root space) {}", BoxText(result.frameBounds)));
            log.push_back(fmt::format("cage centre (walls' X/Y, bounds' middle Z) {} -> AddOnNode{} (value flags {})", Text(result.cageCentre),
                kLanternFlameAddon, kLanternFlameValueFlags));

            auto* root = ground.GetRootNode();
            (void)AddValueNode(ground, *root, kLanternFlameAddon, kLanternFlameValueFlags, ToVector(result.cageCentre));
            if (auto* bsx = FindBsx(ground)) {
                const std::uint32_t before = bsx->integerData;
                bsx->integerData |= kBsxAddon;
                log.push_back(fmt::format("ground BSXFlags {} -> {} (Addon bit 0x{:X})", before, bsx->integerData, kBsxAddon));
            } else {
                throw std::runtime_error(fmt::format("{} has no BSXFlags on its root", kLanternFrameSource));
            }
            nifly::NifFile copy = ground;  // SaveNif's string-table pass writes through the file: the worn step reads the original
            SaveNif(copy, a_dataDir / kLanternGroundNif);
            groundOk = true;
        } catch (const std::exception& e) {
            fail(kLanternGroundNif, e);
        }

        // 3. the worn model
        if (groundOk) {
            try {
                nifly::NifFile skeleton;
                LoadNif(skeleton, Need(a_inputs, kLanternSkeletonSource));
                MatTransform pelvis;
                if (!skeleton.GetNodeTransformToGlobal(kLanternBone, pelvis)) {
                    throw std::runtime_error(fmt::format("{} has no node '{}'", kLanternSkeletonSource, kLanternBone));
                }
                result.pelvis = ToArray(pelvis.translation);

                const LanternBox& box = result.frameBounds;
                if (box.Size(2) <= 0.0f) {
                    throw std::runtime_error("the frame has no height");
                }
                result.wornScale = kLanternWornHeight / box.Size(2);
                const Vector3 top(result.cageCentre[0], result.cageCentre[1], box.max[2]);
                const Vector3 anchor = pelvis.translation + ToVector(kLanternWornHipOffset);
                result.wornTop = ToArray(anchor);
                const float scale = result.wornScale;
                const auto place = [anchor, top, scale](const Vector3& a_rootSpace) { return anchor + (a_rootSpace - top) * scale; };
                log.push_back(fmt::format("worn: pelvis '{}' at {} (skeleton.nif), frame height {:.4f} -> {} units "
                                          "(kLanternWornHeight), scale {:.5f}; frame top centre {} placed at pelvis + {} "
                                          "(kLanternWornHipOffset) = {}",
                    kLanternBone, Text(result.pelvis), box.Size(2), kLanternWornHeight, scale, Text(ToArray(top)),
                    Text(kLanternWornHipOffset), Text(result.wornTop)));

                nifly::NifFile worn;
                worn.Create(nifly::NiVersion::getSSE());
                auto& hdr = worn.GetHeader();
                auto* root = worn.GetRootNode();
                root->name.get() = kLanternWornRootName;
                auto* bone = worn.AddNode(kLanternBone, pelvis, root);
                const std::uint32_t boneId = hdr.GetBlockID(bone);

                std::vector<nifly::NiShape*> wornShapes;
                const auto adopt = [&](nifly::NiShape* a_shape) {
                    if (!a_shape) {
                        throw std::runtime_error("nifly did not clone a shape");
                    }
                    const std::uint32_t id = hdr.GetBlockID(a_shape);
                    if (ParentOf(worn, id) == nifly::NIF_NPOS) {
                        root->childRefs.AddBlockRef(id);
                    }
                    wornShapes.push_back(a_shape);
                };

                // the frame, from the ground model in memory (soot diffuse; the collision stays on the ground root)
                adopt(worn.CloneShape(frame, kLanternWornFrameName, &ground));
                {
                    const MatTransform toRoot = ToRootSpace(ground, ground.GetHeader().GetBlockID(frame));
                    BakeShape(*wornShapes.back(), [&](const Vector3& a_v) { return place(toRoot.ApplyTransform(a_v)); },
                        toRoot.rotation);
                }

                // the flame strips: rest-pose global positions (the skin's global-to-skin transform inverted), both strips'
                // bounds together scaled into the cage and centred on the cage centre
                nifly::NifFile flame;
                LoadNif(flame, Need(a_inputs, kLanternFlameSource));
                LanternBox strips = EmptyBox();
                std::vector<std::pair<nifly::NiShape*, MatTransform>> sources;
                for (const char* name : kLanternFlameStrips) {
                    auto* shape = FindShape(flame, name);
                    if (!shape || !shape->HasSkinInstance()) {
                        throw std::runtime_error(fmt::format("{} has no skinned shape '{}'", kLanternFlameSource, name));
                    }
                    MatTransform globalToSkin;
                    if (!flame.CalcShapeTransformGlobalToSkin(shape, globalToSkin)) {
                        throw std::runtime_error(fmt::format("'{}': no global-to-skin transform", name));
                    }
                    const MatTransform skinToGlobal = globalToSkin.InverseTransform();
                    std::vector<Vector3> verts;
                    if (!flame.GetVertsForShape(shape, verts) || verts.empty()) {
                        throw std::runtime_error(fmt::format("'{}' has no vertices", name));
                    }
                    LanternBox one = EmptyBox();
                    for (const auto& v : verts) {
                        Grow(one, skinToGlobal.ApplyTransform(v));
                    }
                    log.push_back(fmt::format("flame strip '{}': {} vertices, {} triangles, rest-pose bounds {}", name, verts.size(),
                        shape->GetNumTriangles(), BoxText(one)));
                    Grow(strips, one);
                    sources.emplace_back(shape, skinToGlobal);
                }
                float fit = kFloatMax;
                for (int axis = 0; axis < 3; ++axis) {
                    if (strips.Size(axis) > 0.0f) {
                        fit = std::min(fit, kLanternFlameFill * box.Size(axis) / strips.Size(axis));
                    }
                }
                const Vector3 stripCentre = ToVector(strips.Centre());
                const Vector3 cage = ToVector(result.cageCentre);
                log.push_back(fmt::format("flame strips: together {}, scaled x {:.5f} (kLanternFlameFill {} of the cage) onto the cage "
                                          "centre, then placed with the frame",
                    BoxText(strips), fit, kLanternFlameFill));
                for (auto& [shape, skinToGlobal] : sources) {
                    const MatTransform toGlobal = skinToGlobal;
                    const std::string name = shape->name.get();
                    BakeShape(*shape, [&](const Vector3& a_v) { return place(cage + (toGlobal.ApplyTransform(a_v) - stripCentre) * fit); },
                        toGlobal.rotation);
                    flame.DeleteSkinning(shape);
                    auto* clone = worn.CloneShape(shape, name, &flame);
                    adopt(clone);
                    for (const auto& line : RetargetShaderControllers(worn, *clone)) {
                        log.push_back(fmt::format("flame strip '{}': controller {}", name, line));
                    }
                }

                for (auto* shape : wornShapes) {
                    SkinToBone(worn, *shape, boneId, pelvis);
                }
                // CloneShape brings nodes of the source file along as root children (the atronach's bones, the ground's
                // addon node): only the bone and the shapes stay; the save drops what is then unreferenced
                {
                    std::set<std::uint32_t> keep{ boneId };
                    for (auto* shape : wornShapes) {
                        keep.insert(hdr.GetBlockID(shape));
                    }
                    std::vector<std::uint32_t> children;
                    root->childRefs.GetIndices(children);
                    std::vector<std::uint32_t> kept;
                    for (const auto id : children) {
                        if (keep.count(id) != 0) {
                            kept.push_back(id);
                        }
                    }
                    log.push_back(fmt::format("worn: {} root children the clones brought along dropped", children.size() - kept.size()));
                    root->childRefs.SetIndices(kept);
                }
                // the frame in character space, against the vanilla bodies at rest
                {
                    auto* tri = dynamic_cast<nifly::BSTriShape*>(wornShapes.front());
                    result.wornFrame = EmptyBox();
                    for (const auto& vertex : tri->vertData) {
                        Grow(result.wornFrame, vertex.vert);
                    }
                    log.push_back(fmt::format("worn frame (character space) {}", BoxText(result.wornFrame)));
                    for (const char* body : kLanternBodySources) {
                        const auto file = FindSource(a_inputs, body);
                        if (!file) {
                            log.push_back(fmt::format("body clearance: {} in no --in folder, not checked", body));
                            continue;
                        }
                        const auto clearance = BodyClearance(*file, result.wornFrame, kLanternBodyMargin);
                        log.push_back(fmt::format("body clearance {}: {} vertices, {} inside the frame's bounds grown by {} "
                                                  "(kLanternBodyMargin), nearest {:.3f} units; the body spans x {:.3f}..{:.3f} within the frame's height and depth",
                            body, clearance.vertices, clearance.inside, kLanternBodyMargin, clearance.nearestGap, clearance.leftmost,
                            clearance.rightmost));
                        if (clearance.inside > 0) {
                            throw std::runtime_error(fmt::format("{} body vertices of {} are inside the worn frame's bounds (grown by "
                                                                 "{}): move kLanternWornHipOffset", clearance.inside, body, kLanternBodyMargin));
                        }
                    }
                }
                std::vector<std::string> notes;
                const GreyStats stats = GreyEffects(worn, kDefaultTint, kDefaultTintAdd, &notes);
                log.push_back(fmt::format("worn effect shaders {}: palette normal {} (GradBlackFlame.dds), additive {}, left {}; "
                                          "lighting shaders {} left alone; base colour x tint {}",
                    stats.effectShaders, stats.paletted, stats.palettedAdd, stats.paletteLeft, stats.lightingShaders, kDefaultTint));
                for (const auto& note : notes) {
                    log.push_back("worn: " + note);
                }
                SaveNew(worn, a_dataDir / kLanternWornNif);
            } catch (const std::exception& e) {
                fail(kLanternWornNif, e);
            }
        }

        // 4. the smoke static
        try {
            nifly::NifFile smoke;
            smoke.Create(nifly::NiVersion::getSSE());
            auto& hdr = smoke.GetHeader();
            (void)hdr.ReplaceBlock(0, std::make_unique<nifly::BSFadeNode>());
            auto* root = smoke.GetRootNode();
            if (!root || hdr.GetBlockID(root) != 0 || std::string(root->GetBlockName()) != "BSFadeNode") {
                throw std::runtime_error("the root could not be made a BSFadeNode");
            }
            root->name.get() = kLanternSmokeRootName;
            auto bsx = std::make_unique<nifly::BSXFlags>();
            bsx->name.get() = "BSX";
            bsx->integerData = kLanternSmokeBsx;
            root->extraDataRefs.AddBlockRef(hdr.AddBlock(std::move(bsx)));
            (void)AddValueNode(smoke, *root, kLanternSmokeAddon, kLanternSmokeValueFlags, Vector3());
            log.push_back(fmt::format("smoke: BSFadeNode '{}', BSXFlags {} (kLanternSmokeBsx), AddOnNode{} (value flags {})",
                kLanternSmokeRootName, kLanternSmokeBsx, kLanternSmokeAddon, kLanternSmokeValueFlags));
            SaveNew(smoke, a_dataDir / kLanternSmokeNif);
        } catch (const std::exception& e) {
            fail(kLanternSmokeNif, e);
        }

        if (result.problems.empty()) {
            for (const auto& problem : VerifyLantern(a_inputs, a_dataDir)) {
                result.problems.push_back("read-back: " + problem);
            }
        }
        result.ok = result.problems.empty();
        return result;
    }

    LanternClearance BodyClearance(const std::filesystem::path& a_body, const LanternBox& a_box, float a_margin)
    {
        nifly::NifFile nif;
        LoadNif(nif, a_body);
        LanternClearance out;
        out.nearestGap = kFloatMax;
        out.leftmost = kFloatMax;
        out.rightmost = -kFloatMax;
        for (auto* shape : nif.GetShapes()) {
            MatTransform toGlobal = ToRootSpace(nif, nif.GetHeader().GetBlockID(shape));
            if (shape->HasSkinInstance()) {
                MatTransform globalToSkin;
                if (!nif.CalcShapeTransformGlobalToSkin(shape, globalToSkin)) {
                    throw std::runtime_error(fmt::format("{}: shape '{}' has no global-to-skin transform", PathText(a_body), shape->name.get()));
                }
                toGlobal = globalToSkin.InverseTransform();
            }
            std::vector<Vector3> verts;
            if (!nif.GetVertsForShape(shape, verts)) {
                continue;
            }
            for (const auto& v : verts) {
                const auto p = ToArray(toGlobal.ApplyTransform(v));
                bool inside = true;
                float distance = 0.0f;
                for (int axis = 0; axis < 3; ++axis) {
                    inside = inside && p[axis] >= a_box.min[axis] - a_margin && p[axis] <= a_box.max[axis] + a_margin;
                    const float outside = std::max({ a_box.min[axis] - p[axis], 0.0f, p[axis] - a_box.max[axis] });
                    distance += outside * outside;
                }
                if (p[1] >= a_box.min[1] - a_margin && p[1] <= a_box.max[1] + a_margin && p[2] >= a_box.min[2] - a_margin &&
                    p[2] <= a_box.max[2] + a_margin) {
                    out.leftmost = std::min(out.leftmost, p[0]);
                    out.rightmost = std::max(out.rightmost, p[0]);
                }
                ++out.vertices;
                out.inside += inside ? 1 : 0;
                out.nearestGap = std::min(out.nearestGap, std::sqrt(distance));
            }
        }
        return out;
    }

    std::vector<std::string> VerifyLantern(const std::vector<std::filesystem::path>& a_inputs, const std::filesystem::path& a_dataDir)
    {
        std::vector<std::string> problems;
        try {
            // the ground model
            const auto gnd = InspectLanternNif(a_dataDir / kLanternGroundNif);
            Check(problems, gnd.shapes.size() == 1, fmt::format("gnd: {} shapes, expected the frame alone", gnd.shapes.size()));
            if (!gnd.shapes.empty()) {
                const auto& frame = gnd.shapes.front();
                Check(problems, frame.type == "BSTriShape" && frame.shader == "BSLightingShaderProperty",
                    fmt::format("gnd: frame '{}' is {} with {}", frame.name, frame.type, frame.shader));
                const std::string diffuse = fmt::format("[{}] {}", kSlotDiffuse, kLanternDiffuse);
                Check(problems, !frame.textures.empty() && frame.textures.front() == diffuse,
                    fmt::format("gnd: diffuse is '{}', expected '{}'", frame.textures.empty() ? "" : frame.textures.front(), diffuse));
                Check(problems, frame.skinType.empty(), "gnd: the frame is skinned");
                // the normal map as the source names it
                nifly::NifFile source;
                LoadNif(source, Need(a_inputs, kLanternFrameSource));
                const auto shapes = source.GetShapes();
                if (!shapes.empty()) {
                    auto& shdr = source.GetHeader();
                    auto* shader = shdr.GetBlock<nifly::NiShader>(shapes.front()->ShaderPropertyRef());
                    auto* set = shader ? shdr.GetBlock<nifly::BSShaderTextureSet>(shader->TextureSetRef()) : nullptr;
                    const std::string normal = set && set->textures.size() > kNormalSlot ?
                                                   fmt::format("[{}] {}", kNormalSlot, set->textures[kNormalSlot].get()) : "";
                    Check(problems, frame.textures.size() > 1 && frame.textures[1] == normal,
                        fmt::format("gnd: normal map is '{}', the source's '{}'", frame.textures.size() > 1 ? frame.textures[1] : "", normal));
                }
            }
            Check(problems, gnd.hasCollision, "gnd: the collision is gone");
            Check(problems, gnd.valueNodes.size() == 1 && gnd.valueNodes.front().name == fmt::format("AddOnNode{}", kLanternFlameAddon) &&
                                gnd.valueNodes.front().value == kLanternFlameAddon && gnd.valueNodes.front().parent == gnd.rootName,
                fmt::format("gnd: expected one BSValueNode AddOnNode{} value {} under the root ({} found)", kLanternFlameAddon,
                    kLanternFlameAddon, gnd.valueNodes.size()));
            Check(problems, gnd.hasBsx && (gnd.bsx & kBsxAddon) != 0, fmt::format("gnd: BSXFlags {} without the Addon bit", gnd.bsx));

            // the worn model
            const auto worn = InspectLanternNif(a_dataDir / kLanternWornNif);
            Check(problems, !worn.hasCollision, "worn: has a collision object");
            Check(problems, worn.valueNodes.empty(), "worn: has an addon node (the flame is the strips)");
            Check(problems, worn.sseCompatible, "worn: nifly says it is not SSE-compatible");
            std::vector<std::string> expected{ kLanternWornFrameName };
            expected.insert(expected.end(), kLanternFlameStrips.begin(), kLanternFlameStrips.end());
            Check(problems, worn.shapes.size() == expected.size(),
                fmt::format("worn: {} shapes, expected {}", worn.shapes.size(), expected.size()));
            for (const auto& name : expected) {
                const auto it = std::find_if(worn.shapes.begin(), worn.shapes.end(), [&](const auto& a_s) { return a_s.name == name; });
                Check(problems, it != worn.shapes.end(), fmt::format("worn: no shape '{}'", name));
            }
            for (const auto& s : worn.shapes) {
                Check(problems, s.skinType == "BSDismemberSkinInstance" && s.skinned,
                    fmt::format("worn '{}': skin '{}', vertex skinned flag {}", s.name, s.skinType, s.skinned));
                Check(problems, s.bones.size() == 1 && s.bones.front() == kLanternBone,
                    fmt::format("worn '{}': {} bones, expected '{}' alone", s.name, s.bones.size(), kLanternBone));
                Check(problems, s.partitions.size() == 1 && s.partitions.front() == kLanternBodyPart,
                    fmt::format("worn '{}': {} partitions, expected one body part {}", s.name, s.partitions.size(), kLanternBodyPart));
                Check(problems, s.vertices > 0 && s.weightedOnFirstBone == s.vertices && s.allWeightsOne,
                    fmt::format("worn '{}': {} of {} vertices weighted 1 on the bone (NiSkinData {})", s.name, s.weightedOnFirstBone,
                        s.vertices, s.skinDataWeights));
                Check(problems, (s.flags1 & kSlsf1Skinned) != 0, fmt::format("worn '{}': shader without the Skinned flag", s.name));
                Check(problems, s.skinRoot == s.fileRoot,
                    fmt::format("worn '{}': the skin instance's skeleton root is block {}, not the file's root {}", s.name, s.skinRoot,
                        s.fileRoot));
                for (const auto& controller : s.controllers) {
                    Check(problems, controller.find("WRONG") == std::string::npos,
                        fmt::format("worn '{}': controller {}", s.name, controller));
                }
                if (s.shader == "BSEffectShaderProperty" && (s.flags1 & kSlsf1GreyscaleToPaletteColor) != 0) {
                    const std::string palette = std::string("greyscale ") + kPalettePath;
                    Check(problems, s.textures.size() > 1 && s.textures[1] == palette,
                        fmt::format("worn '{}': palette '{}', expected '{}'", s.name, s.textures.size() > 1 ? s.textures[1] : "", palette));
                }
            }

            // the smoke static
            const auto smoke = InspectLanternNif(a_dataDir / kLanternSmokeNif);
            Check(problems, smoke.rootType == "BSFadeNode" && smoke.shapes.empty(),
                fmt::format("smoke: root {} with {} shapes, expected an empty BSFadeNode", smoke.rootType, smoke.shapes.size()));
            Check(problems, smoke.valueNodes.size() == 1 && smoke.valueNodes.front().name == fmt::format("AddOnNode{}", kLanternSmokeAddon) &&
                                smoke.valueNodes.front().value == kLanternSmokeAddon && smoke.valueNodes.front().parent == smoke.rootName,
                fmt::format("smoke: expected one BSValueNode AddOnNode{} value {} under the root", kLanternSmokeAddon, kLanternSmokeAddon));
            Check(problems, smoke.hasBsx && smoke.bsx == kLanternSmokeBsx, fmt::format("smoke: BSXFlags {} (expected {})", smoke.bsx, kLanternSmokeBsx));
            Check(problems, smoke.nodeNames.size() == 2, fmt::format("smoke: {} nodes, expected the root and the addon node", smoke.nodeNames.size()));

            // the texture
            double sourceMean = 0.0;
            double copyMean = 0.0;
            std::size_t sw = 0, sh = 0, sm = 0, cw = 0, ch = 0, cm = 0;
            std::string sf, cf;
            const bool sourceRead = DdsMean(Need(a_inputs, kLanternDiffuseSource), sourceMean, sw, sh, sm, sf);
            const bool copyRead = DdsMean(a_dataDir / kLanternDiffuse, copyMean, cw, ch, cm, cf);
            Check(problems, sourceRead && copyRead, "texture: the source or the copy does not read");
            Check(problems, sw == cw && sh == ch && sm == cm && sf == cf,
                fmt::format("texture: copy {}x{} format {} {} mips, source {}x{} format {} {} mips", cw, ch, cf, cm, sw, sh, sf, sm));
            Check(problems, copyMean < sourceMean, fmt::format("texture: copy mean {:.4f} not below the source's {:.4f}", copyMean, sourceMean));
        } catch (const std::exception& e) {
            problems.push_back(e.what());
        }
        return problems;
    }

    int RunWriteLantern(const std::filesystem::path& a_dataDir, const std::vector<std::filesystem::path>& a_inputs)
    {
        const auto result = BuildLantern(a_inputs, a_dataDir);
        for (const auto& line : result.log) {
            std::cout << "lantern  " << line << "\n";
        }
        for (const auto& line : result.problems) {
            std::cout << "FAIL  " << line << "\n";
        }
        if (!result.ok) {
            return kExitFail;
        }
        std::cout << "\nread-back (each output opened again with nifly):\n";
        for (const char* rel : { kLanternGroundNif, kLanternWornNif, kLanternSmokeNif }) {
            PrintLanternNif(rel, InspectLanternNif(a_dataDir / rel));
        }
        double mean = 0.0;
        std::size_t width = 0, height = 0, mips = 0;
        std::string format;
        if (DdsMean(a_dataDir / kLanternDiffuse, mean, width, height, mips, format)) {
            std::cout << fmt::format("{}\n  {}x{}, DXGI format {}, {} mips, mean RGB of mip 0 {:.4f}\n", kLanternDiffuse, width, height, format,
                mips, mean);
        }
        const auto& b = result.frameBounds;
        std::cout << fmt::format("\nground vertex bounds (ARMO ObjectBounds): min {} max {}; as whole units X1 {} Y1 {} Z1 {} X2 {} Y2 {} Z2 {}\n",
            Text(b.min), Text(b.max), static_cast<int>(std::floor(b.min[0])), static_cast<int>(std::floor(b.min[1])),
            static_cast<int>(std::floor(b.min[2])), static_cast<int>(std::ceil(b.max[0])), static_cast<int>(std::ceil(b.max[1])),
            static_cast<int>(std::ceil(b.max[2])));
        std::cout << "lantern: 3 NIFs and 1 texture written and read back\n";
        return kExitOk;
    }

    int RunInspectLantern(const std::vector<std::filesystem::path>& a_files)
    {
        int result = kExitOk;
        for (const auto& file : a_files) {
            try {
                PrintLanternNif(PathText(file), InspectLanternNif(file));
            } catch (const std::exception& e) {
                std::cout << "FAIL  " << e.what() << "\n";
                result = kExitFail;
            }
        }
        return result;
    }
}

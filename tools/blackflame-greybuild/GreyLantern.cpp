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
#include "GreyShapes.h"
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

        /// a_name is one of kLanternFlameLayers
        [[nodiscard]] bool IsFlameLayer(const std::string& a_name)
        {
            return std::any_of(kLanternFlameLayers.begin(), kLanternFlameLayers.end(),
                [&](const LanternFlameLayer& a_layer) { return a_name == a_layer.name; });
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
            const Vector3& a_at, float a_scale = 1.0f)
        {
            auto node = std::make_unique<nifly::BSValueNode>();
            node->name.get() = fmt::format("AddOnNode{}", a_index);
            node->value = a_index;
            node->valueFlags = static_cast<nifly::BSValueNodeFlags>(a_flags);
            MatTransform at;
            at.translation = a_at;
            at.scale = a_scale;
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

    std::vector<LanternPiece> LanternPieces(const std::vector<Vector3>& a_verts, const std::vector<nifly::Triangle>& a_tris)
    {
        // union-find over vertex indices; vertices at the same position (within kLanternWeld) count as one, so a UV seam
        // does not split a piece
        std::vector<std::uint32_t> parent(a_verts.size());
        for (std::uint32_t i = 0; i < parent.size(); ++i) {
            parent[i] = i;
        }
        const std::function<std::uint32_t(std::uint32_t)> find = [&](std::uint32_t a_i) {
            while (parent[a_i] != a_i) {
                parent[a_i] = parent[parent[a_i]];
                a_i = parent[a_i];
            }
            return a_i;
        };
        const auto join = [&](std::uint32_t a_a, std::uint32_t a_b) {
            const auto ra = find(a_a);
            const auto rb = find(a_b);
            if (ra != rb) {
                parent[std::max(ra, rb)] = std::min(ra, rb);
            }
        };
        // a grid of kLanternWeld cells; two vertices within the weld distance can sit in neighbouring cells, so each vertex
        // is compared by distance with every vertex of its own cell and the 26 round it
        std::map<std::array<long long, 3>, std::vector<std::uint32_t>> cells;
        for (std::uint32_t i = 0; i < a_verts.size(); ++i) {
            const std::array<long long, 3> key{ static_cast<long long>(std::floor(a_verts[i].x / kLanternWeld)),
                static_cast<long long>(std::floor(a_verts[i].y / kLanternWeld)),
                static_cast<long long>(std::floor(a_verts[i].z / kLanternWeld)) };
            for (long long dx = -1; dx <= 1; ++dx) {
                for (long long dy = -1; dy <= 1; ++dy) {
                    for (long long dz = -1; dz <= 1; ++dz) {
                        const auto it = cells.find({ key[0] + dx, key[1] + dy, key[2] + dz });
                        if (it == cells.end()) {
                            continue;
                        }
                        for (const auto other : it->second) {
                            if (a_verts[i].DistanceTo(a_verts[other]) <= kLanternWeld) {
                                join(i, other);
                            }
                        }
                    }
                }
            }
            cells[key].push_back(i);
        }
        for (const auto& t : a_tris) {
            if (t.p1 >= a_verts.size() || t.p2 >= a_verts.size() || t.p3 >= a_verts.size()) {
                throw std::runtime_error(fmt::format("a triangle names vertex {} / {} / {} of {}", t.p1, t.p2, t.p3, a_verts.size()));
            }
            join(t.p1, t.p2);
            join(t.p1, t.p3);
        }
        std::map<std::uint32_t, std::size_t> slot;
        std::vector<LanternPiece> pieces;
        for (std::uint32_t i = 0; i < a_tris.size(); ++i) {
            const auto root = find(a_tris[i].p1);
            auto [it, added] = slot.emplace(root, pieces.size());
            if (added) {
                pieces.emplace_back();
                pieces.back().bounds = EmptyBox();
            }
            auto& piece = pieces[it->second];
            piece.triangles.push_back(i);
            for (const auto v : { a_tris[i].p1, a_tris[i].p2, a_tris[i].p3 }) {
                if (std::find(piece.vertices.begin(), piece.vertices.end(), v) == piece.vertices.end()) {
                    piece.vertices.push_back(v);
                    Grow(piece.bounds, a_verts[v]);
                }
            }
        }
        for (auto& piece : pieces) {
            std::sort(piece.vertices.begin(), piece.vertices.end());
        }
        return pieces;
    }

    LanternSurface LoadLanternSurface(const std::filesystem::path& a_body)
    {
        nifly::NifFile nif;
        LoadNif(nif, a_body);
        LanternSurface out;
        out.name = a_body.filename().string();
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
            std::vector<nifly::Triangle> tris;
            if (!nif.GetVertsForShape(shape, verts) || !shape->GetTriangles(tris)) {
                continue;
            }
            const auto base = out.verts.size();
            for (const auto& v : verts) {
                out.verts.push_back(toGlobal.ApplyTransform(v));
            }
            for (const auto& t : tris) {
                if (t.p1 >= verts.size() || t.p2 >= verts.size() || t.p3 >= verts.size()) {
                    throw std::runtime_error(fmt::format("{}: shape '{}' has a triangle past its vertices", PathText(a_body), shape->name.get()));
                }
                out.tris.push_back({ static_cast<std::uint32_t>(base + t.p1), static_cast<std::uint32_t>(base + t.p2),
                    static_cast<std::uint32_t>(base + t.p3) });
            }
        }
        if (out.tris.empty()) {
            throw std::runtime_error(fmt::format("{} has no triangles", PathText(a_body)));
        }
        double volume = 0.0;
        for (const auto& t : out.tris) {
            volume += static_cast<double>(out.verts[t[0]].dot(out.verts[t[1]].cross(out.verts[t[2]])));
        }
        out.outward = volume >= 0.0 ? 1.0f : -1.0f;
        return out;
    }

    LanternBand MeasureLanternBand(const std::filesystem::path& a_body, const std::string& a_shape, const Vector3& a_pelvis)
    {
        nifly::NifFile nif;
        LoadNif(nif, a_body);
        auto* shape = FindShape(nif, a_shape);
        if (!shape) {
            throw std::runtime_error(fmt::format("{} has no shape '{}' (the belt band, kLanternBandShapes): use the vanilla body",
                PathText(a_body), a_shape));
        }
        MatTransform toGlobal = ToRootSpace(nif, nif.GetHeader().GetBlockID(shape));
        if (shape->HasSkinInstance()) {
            MatTransform globalToSkin;
            if (!nif.CalcShapeTransformGlobalToSkin(shape, globalToSkin)) {
                throw std::runtime_error(fmt::format("{}: shape '{}' has no global-to-skin transform", PathText(a_body), a_shape));
            }
            toGlobal = globalToSkin.InverseTransform();
        }
        std::vector<Vector3> verts;
        if (!nif.GetVertsForShape(shape, verts)) {
            throw std::runtime_error(fmt::format("{}: shape '{}' has no vertices", PathText(a_body), a_shape));
        }
        LanternBand band;
        band.body = a_body.filename().string();
        band.shape = a_shape;
        band.top = -kFloatMax;
        band.bottom = kFloatMax;
        for (const auto& v : verts) {
            const Vector3 p = toGlobal.ApplyTransform(v);
            const float dx = p.x - a_pelvis.x;
            const float dz = p.z - a_pelvis.z;
            if (dx >= kLanternBandXMin && dx <= kLanternBandXMax && dz >= kLanternBandZWindow[0] && dz <= kLanternBandZWindow[1]) {
                ++band.vertices;
                band.top = std::max(band.top, p.z);
                band.bottom = std::min(band.bottom, p.z);
            }
        }
        if (band.vertices == 0) {
            throw std::runtime_error(fmt::format("{}: shape '{}' has no vertex at x {}..{} and z {}..{} from the pelvis (kLanternBandXMin/XMax, "
                                                 "kLanternBandZWindow)", PathText(a_body), a_shape, kLanternBandXMin, kLanternBandXMax,
                kLanternBandZWindow[0], kLanternBandZWindow[1]));
        }
        return band;
    }

    namespace
    {
        /// The closest point of triangle a, b, c to p (Ericson, Real-Time Collision Detection 5.1.5)
        [[nodiscard]] Vector3 ClosestOnTriangle(const Vector3& p, const Vector3& a, const Vector3& b, const Vector3& c)
        {
            const Vector3 ab = b - a;
            const Vector3 ac = c - a;
            const Vector3 ap = p - a;
            const float d1 = ab.dot(ap);
            const float d2 = ac.dot(ap);
            if (d1 <= 0.0f && d2 <= 0.0f) {
                return a;
            }
            const Vector3 bp = p - b;
            const float d3 = ab.dot(bp);
            const float d4 = ac.dot(bp);
            if (d3 >= 0.0f && d4 <= d3) {
                return b;
            }
            const float vc = d1 * d4 - d3 * d2;
            if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
                return a + ab * (d1 / (d1 - d3));
            }
            const Vector3 cp = p - c;
            const float d5 = ab.dot(cp);
            const float d6 = ac.dot(cp);
            if (d6 >= 0.0f && d5 <= d6) {
                return c;
            }
            const float vb = d5 * d2 - d1 * d6;
            if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
                return a + ac * (d2 / (d2 - d6));
            }
            const float va = d3 * d6 - d5 * d4;
            if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
                return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
            }
            const float denom = 1.0f / (va + vb + vc);
            return a + ab * (vb * denom) + ac * (vc * denom);
        }

        /// Where the vertical line through (x, y) meets a triangle list highest at or below a given Z
        struct SurfaceHit
        {
            float z = -kFloatMax;  // -kFloatMax: no triangle
            std::size_t triangle = 0;
            float u = 0.0f;  // the weights of the triangle's second and third vertex at the hit
            float v = 0.0f;
        };

        /// a_tris' triangles not touching an a_skip vertex, against the vertical line through (a_x, a_y): the highest hit at
        /// or below a_zMax. O(triangles).
        [[nodiscard]] SurfaceHit SurfaceBelow(const std::vector<Vector3>& a_verts, const std::vector<nifly::Triangle>& a_tris,
            const std::vector<bool>& a_skip, float a_x, float a_y, float a_zMax)
        {
            constexpr float kEdge = 1e-6f;  // a point on a shared edge counts for both triangles
            SurfaceHit best;
            for (std::size_t i = 0; i < a_tris.size(); ++i) {
                const auto& t = a_tris[i];
                if (a_skip[t.p1] || a_skip[t.p2] || a_skip[t.p3]) {
                    continue;
                }
                const Vector3& a = a_verts[t.p1];
                const Vector3& b = a_verts[t.p2];
                const Vector3& c = a_verts[t.p3];
                const float d = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
                if (std::fabs(d) < 1e-12f) {
                    continue;  // edge-on from above
                }
                const float u = ((a_x - a.x) * (c.y - a.y) - (c.x - a.x) * (a_y - a.y)) / d;
                const float v = ((b.x - a.x) * (a_y - a.y) - (a_x - a.x) * (b.y - a.y)) / d;
                if (u < -kEdge || v < -kEdge || u + v > 1.0f + kEdge) {
                    continue;
                }
                const float z = a.z + (b.z - a.z) * u + (c.z - a.z) * v;
                if (z <= a_zMax && z > best.z) {
                    best = { z, i, u, v };
                }
            }
            return best;
        }

        /// The upright handle's two feet among a_verts (where a_handle): its vertices within kLanternFootBand of its lowest
        /// point, split at the middle of its wider horizontal extent. Throws when a side has none.
        struct HandleFeet
        {
            std::array<Vector3, 2> centre;
            std::array<float, 2> footZ{ kFloatMax, kFloatMax };
            std::array<int, 2> vertices{};
            LanternBox handle;  // the handle's vertex bounds
            int spread = 0;     // the axis the feet are split along
        };

        [[nodiscard]] HandleFeet FindHandleFeet(const std::vector<Vector3>& a_verts, const std::vector<bool>& a_handle)
        {
            HandleFeet feet;
            feet.handle = EmptyBox();
            for (std::size_t i = 0; i < a_verts.size(); ++i) {
                if (a_handle[i]) {
                    Grow(feet.handle, a_verts[i]);
                }
            }
            if (IsEmpty(feet.handle)) {
                throw std::runtime_error("the handle has no vertices");
            }
            feet.spread = feet.handle.Size(0) >= feet.handle.Size(1) ? 0 : 1;
            const float split = feet.handle.Centre()[feet.spread];
            std::array<Vector3, 2> sums;
            std::array<float, 2> outermost{ kFloatMax, -kFloatMax };  // along the spread: side 0 its least, side 1 its most
            for (std::size_t i = 0; i < a_verts.size(); ++i) {
                const Vector3& p = a_verts[i];
                if (!a_handle[i] || p.z > feet.handle.min[2] + kLanternFootBand) {
                    continue;
                }
                const float along = ToArray(p)[feet.spread];
                const int side = along < split ? 0 : 1;
                sums[side] += p;
                ++feet.vertices[side];
                feet.footZ[side] = std::min(feet.footZ[side], p.z);
                outermost[side] = side == 0 ? std::min(outermost[side], along) : std::max(outermost[side], along);
            }
            for (int side = 0; side < 2; ++side) {
                if (feet.vertices[side] == 0) {
                    throw std::runtime_error(fmt::format("the handle has no foot on its {} side (no vertex within kLanternFootBand {} of its "
                                                         "lowest point z {:.4f})", side == 0 ? "lower" : "upper", kLanternFootBand,
                        feet.handle.min[2]));
                }
                // across the spread the vertices' mean, along it the outermost (where the bottom bar ends: candlelantern01's
                // runs on between the feet inside the lid's hinge tunnel)
                feet.centre[side] = sums[side] * (1.0f / static_cast<float>(feet.vertices[side]));
                (feet.spread == 0 ? feet.centre[side].x : feet.centre[side].y) = outermost[side];
            }
            return feet;
        }

        /// The lid under a foot, over a kLanternLugSamples x kLanternLugSamples grid of the lug's footprint: at each point the
        /// highest frame surface below the foot; a point with none within kLanternLugMaxHeight below the foot is a hole the
        /// lug bridges. lidZ the highest of the rest (a_lidHit its hit), lidMin the lowest. Throws when every point is a hole.
        void LidUnder(const std::vector<Vector3>& a_verts, const std::vector<nifly::Triangle>& a_tris, const std::vector<bool>& a_handle,
            LanternFoot& a_foot, SurfaceHit* a_lidHit)
        {
            a_foot.lidZ = -kFloatMax;
            a_foot.lidMin = kFloatMax;
            int holes = 0;
            for (int i = 0; i < kLanternLugSamples; ++i) {
                for (int j = 0; j < kLanternLugSamples; ++j) {
                    const float dx = kLanternLugSize[0] * (static_cast<float>(i) / static_cast<float>(kLanternLugSamples - 1) - 0.5f);
                    const float dy = kLanternLugSize[1] * (static_cast<float>(j) / static_cast<float>(kLanternLugSamples - 1) - 0.5f);
                    const auto hit = SurfaceBelow(a_verts, a_tris, a_handle, a_foot.centre[0] + dx, a_foot.centre[1] + dy, a_foot.footZ);
                    if (hit.z < a_foot.footZ - kLanternLugMaxHeight) {
                        ++holes;
                        continue;
                    }
                    if (hit.z > a_foot.lidZ) {
                        a_foot.lidZ = hit.z;
                        if (a_lidHit) {
                            *a_lidHit = hit;
                        }
                    }
                    a_foot.lidMin = std::min(a_foot.lidMin, hit.z);
                }
            }
            if (a_foot.lidZ == -kFloatMax) {
                throw std::runtime_error(fmt::format("the handle foot at {} has no lid within kLanternLugMaxHeight {} below it under the lug's "
                                                     "{} x {} footprint: it stands further above the lid than a lug bridges",
                    Text(a_foot.centre), kLanternLugMaxHeight, kLanternLugSize[0], kLanternLugSize[1]));
            }
            a_foot.lidHoles = holes;
        }
    }

    LanternGap SurfaceGap(const LanternSurface& a_surface, const std::vector<Vector3>& a_points)
    {
        // the closest point's side: every triangle as near as the nearest (within kTie: an edge or a vertex is shared)
        // adds its outward unit normal, and the point is inside when it lies behind that sum (an angle-free pseudo-normal)
        constexpr float kTie = 1e-4f;
        LanternGap out;
        out.nearest = kFloatMax;
        for (const auto& p : a_points) {
            float best = kFloatMax;
            Vector3 closest;
            Vector3 normalSum;
            for (const auto& t : a_surface.tris) {
                const Vector3& a = a_surface.verts[t[0]];
                const Vector3& b = a_surface.verts[t[1]];
                const Vector3& c = a_surface.verts[t[2]];
                // cheap reject on the bounding box grown by the best distance so far (and the tie), then the exact distance
                const float reach = best + kTie;
                if (p.x < std::min({ a.x, b.x, c.x }) - reach || p.x > std::max({ a.x, b.x, c.x }) + reach ||
                    p.y < std::min({ a.y, b.y, c.y }) - reach || p.y > std::max({ a.y, b.y, c.y }) + reach ||
                    p.z < std::min({ a.z, b.z, c.z }) - reach || p.z > std::max({ a.z, b.z, c.z }) + reach) {
                    continue;
                }
                const Vector3 q = ClosestOnTriangle(p, a, b, c);
                const float distance = q.DistanceTo(p);
                Vector3 normal = (b - a).cross(c - a) * a_surface.outward;
                if (normal.length() <= 0.0f) {
                    continue;  // a degenerate triangle has no side
                }
                normal.Normalize();
                if (distance < best - kTie) {
                    best = distance;
                    closest = q;
                    normalSum = normal;
                } else if (distance <= best + kTie) {
                    best = std::min(best, distance);
                    normalSum += normal;
                }
            }
            if (best < out.nearest) {
                out.nearest = best;
                out.nearestPoint = ToArray(p);
            }
            out.inside += best < kFloatMax && (p - closest).dot(normalSum) < 0.0f ? 1 : 0;
        }
        out.points = static_cast<int>(a_points.size());
        return out;
    }

    float SurfaceLeft(const LanternSurface& a_surface, const LanternBox& a_window)
    {
        // the smallest X of the surface inside the window's Y and Z ranges: every triangle cut to the window by sampling it
        // at kLanternSurfaceStep (the triangle's own vertices included)
        float left = kFloatMax;
        for (const auto& t : a_surface.tris) {
            const Vector3& a = a_surface.verts[t[0]];
            const Vector3& b = a_surface.verts[t[1]];
            const Vector3& c = a_surface.verts[t[2]];
            if (std::max({ a.y, b.y, c.y }) < a_window.min[1] || std::min({ a.y, b.y, c.y }) > a_window.max[1] ||
                std::max({ a.z, b.z, c.z }) < a_window.min[2] || std::min({ a.z, b.z, c.z }) > a_window.max[2]) {
                continue;
            }
            const float edge = std::max({ a.DistanceTo(b), b.DistanceTo(c), c.DistanceTo(a) });
            const int steps = std::max(1, static_cast<int>(std::ceil(edge / kLanternSurfaceStep)));
            for (int i = 0; i <= steps; ++i) {
                for (int j = 0; i + j <= steps; ++j) {
                    const float u = static_cast<float>(i) / static_cast<float>(steps);
                    const float v = static_cast<float>(j) / static_cast<float>(steps);
                    const Vector3 p = a + (b - a) * u + (c - a) * v;
                    if (p.x >= a_window.min[0] && p.x <= a_window.max[0] && p.y >= a_window.min[1] && p.y <= a_window.max[1] &&
                        p.z >= a_window.min[2] && p.z <= a_window.max[2]) {
                        left = std::min(left, p.x);
                    }
                }
            }
        }
        return left;
    }

    float LanternNearestTriangle(const std::vector<Vector3>& a_points, const std::vector<Vector3>& a_verts,
        const std::vector<nifly::Triangle>& a_tris)
    {
        float nearest = kFloatMax;
        for (const auto& t : a_tris) {
            if (t.p1 >= a_verts.size() || t.p2 >= a_verts.size() || t.p3 >= a_verts.size()) {
                throw std::runtime_error("a triangle names a vertex past the list");
            }
            for (const auto& p : a_points) {
                nearest = std::min(nearest, ClosestOnTriangle(p, a_verts[t.p1], a_verts[t.p2], a_verts[t.p3]).DistanceTo(p));
            }
        }
        return nearest;
    }

    float LanternAxisTiltDeg(const std::vector<Vector3>& a_ground, const std::vector<Vector3>& a_worn)
    {
        if (a_ground.empty() || a_ground.size() != a_worn.size()) {
            throw std::runtime_error(fmt::format("the axis check needs the same vertices twice ({} ground, {} worn)", a_ground.size(),
                a_worn.size()));
        }
        LanternBox box = EmptyBox();
        for (const auto& v : a_ground) {
            Grow(box, v);
        }
        const float lowZ = box.min[2] + box.Size(2) * kLanternAxisLowShare;
        const float topZ = box.min[2] + box.Size(2) * kLanternAxisTopShare;
        double centreX = 0.0;
        int reference = 0;
        for (const auto& v : a_ground) {
            if (v.z <= topZ) {
                centreX += v.x;
                ++reference;
            }
        }
        centreX /= std::max(reference, 1);
        // the four groups: low, high, -X half, +X half (all at or below topZ)
        using D3 = std::array<double, 3>;
        std::array<D3, 4> ground{};
        std::array<D3, 4> worn{};
        std::array<int, 4> counts{};
        for (std::size_t i = 0; i < a_ground.size(); ++i) {
            const Vector3& g = a_ground[i];
            if (g.z > topZ) {
                continue;
            }
            for (const int group : { g.z <= lowZ ? 0 : 1, g.x < centreX ? 2 : 3 }) {
                ground[group] = { ground[group][0] + g.x, ground[group][1] + g.y, ground[group][2] + g.z };
                worn[group] = { worn[group][0] + a_worn[i].x, worn[group][1] + a_worn[i].y, worn[group][2] + a_worn[i].z };
                ++counts[group];
            }
        }
        if (std::find(counts.begin(), counts.end(), 0) != counts.end()) {
            throw std::runtime_error("the axis check found an empty reference group (low, high, -X or +X)");
        }
        const auto sub = [](const D3& a, const D3& b) { return D3{ a[0] - b[0], a[1] - b[1], a[2] - b[2] }; };
        const auto dot = [](const D3& a, const D3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
        const auto scaled = [](const D3& a, double k) { return D3{ a[0] * k, a[1] * k, a[2] * k }; };
        const auto cross = [](const D3& a, const D3& b) {
            return D3{ a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
        };
        const auto mean = [&](const std::array<D3, 4>& a_sums, int a_group) { return scaled(a_sums[a_group], 1.0 / counts[a_group]); };
        // an orthonormal frame from two vectors (Gram-Schmidt): a rotation and a uniform scale carry it exactly
        const auto frame = [&](const D3& a_up, const D3& a_side) {
            constexpr double kShortest = 1e-6;
            const double upLength = std::sqrt(dot(a_up, a_up));
            if (upLength < kShortest) {
                throw std::runtime_error("the axis check's low-to-high vector is too short");
            }
            const D3 e1 = scaled(a_up, 1.0 / upLength);
            const D3 side = sub(a_side, scaled(e1, dot(e1, a_side)));
            const double sideLength = std::sqrt(dot(side, side));
            if (sideLength < kShortest * std::max(1.0, upLength)) {
                throw std::runtime_error("the axis check's reference vectors are parallel");
            }
            const D3 e2 = scaled(side, 1.0 / sideLength);
            return std::array<D3, 3>{ e1, e2, cross(e1, e2) };
        };
        const auto g = frame(sub(mean(ground, 1), mean(ground, 0)), sub(mean(ground, 3), mean(ground, 2)));
        const auto w = frame(sub(mean(worn, 1), mean(worn, 0)), sub(mean(worn, 3), mean(worn, 2)));
        // R = W G^T; R applied to +Z: the worn image of the ground's vertical
        D3 up{};
        for (int k = 0; k < 3; ++k) {
            up = { up[0] + w[k][0] * g[k][2], up[1] + w[k][1] * g[k][2], up[2] + w[k][2] * g[k][2] };
        }
        constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
        return static_cast<float>(std::atan2(std::hypot(up[0], up[1]), up[2]) * kRadToDeg);
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
                    entry.scale = value->GetTransformToParent().scale;
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
            s.triangles = shape->GetNumTriangles();
            const std::uint32_t shapeId = hdr.GetBlockID(shape);
            const std::uint32_t shaderId = shape->ShaderPropertyRef() ? shape->ShaderPropertyRef()->index : nifly::NIF_NPOS;
            if (auto* shader = hdr.GetBlock<nifly::NiShader>(shaderId)) {
                s.shader = shader->GetBlockName();
                s.blockIds.push_back(shaderId);
                if (auto* bs = dynamic_cast<nifly::BSShaderProperty*>(shader)) {
                    s.flags1 = bs->shaderFlags1;
                    s.flags2 = bs->shaderFlags2;
                    s.uvScale = { bs->uvScale.u, bs->uvScale.v };
                }
                if (auto* lit = dynamic_cast<nifly::BSLightingShaderProperty*>(shader)) {
                    s.refraction = lit->refractionStrength;
                    s.glossiness = lit->glossiness;
                    s.specularStrength = lit->specularStrength;
                    s.specularColor = ToArray(lit->specularColor);
                }
                if (auto* effect = dynamic_cast<nifly::BSEffectShaderProperty*>(shader)) {
                    s.baseAlpha = effect->baseColor.a;
                    s.falloff = { effect->falloffStartAngle, effect->falloffStopAngle, effect->falloffStartOpacity,
                        effect->falloffStopOpacity };
                    s.clampMode = effect->textureClampMode;
                    s.colourScale = effect->baseColorScale;
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
                    // an effect shader float controller's variable (nif.xml EffectShaderControlledVariable: 6 U offset, 7 U scale,
                    // 8 V offset, 9 V scale)
                    const auto* effectFloat = dynamic_cast<nifly::BSEffectShaderPropertyFloatController*>(controller);
                    s.controllers.push_back(fmt::format("{}{} -> target {} ({})", controller->GetBlockName(),
                        effectFloat ? fmt::format(" variable {}", effectFloat->typeOfControlledVariable) : std::string(),
                        controller->targetRef.index, controller->targetRef.index == shaderId ? "its shader" : "WRONG"));
                    s.blockIds.push_back(id);
                    // its interpolator and the interpolator's keys
                    if (auto* single = dynamic_cast<nifly::NiSingleInterpController*>(controller)) {
                        const std::uint32_t interpId = single->interpolatorRef.index;
                        if (interpId != nifly::NIF_NPOS) {
                            s.blockIds.push_back(interpId);
                            if (auto* interp = hdr.GetBlock<nifly::NiFloatInterpolator>(interpId); interp && interp->dataRef.index != nifly::NIF_NPOS) {
                                s.blockIds.push_back(interp->dataRef.index);
                            }
                        }
                    }
                    id = controller->nextControllerRef.index;
                }
            }
            if (auto* alpha = shape->AlphaPropertyRef() ? hdr.GetBlock<nifly::NiAlphaProperty>(*shape->AlphaPropertyRef()) : nullptr) {
                s.hasAlpha = true;
                s.alphaFlags = alpha->flags;
                s.additive = BlendOfAlpha(alpha) == ShapeBlend::kAdditive;
                s.blockIds.push_back(shape->AlphaPropertyRef()->index);
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
                // the bind: the bone node's transform to the root (B) against the skin-to-bone transform (S)
                std::uint32_t boneNode = nifly::NIF_NPOS;
                for (std::uint32_t id = 0; !s.bones.empty() && id < hdr.GetNumBlocks(); ++id) {
                    if (auto* node = hdr.GetBlock<nifly::NiNode>(id); node && node->name.get() == s.bones.front()) {
                        boneNode = id;
                        break;
                    }
                }
                MatTransform skinToBone;
                nifly::BoundingSphere boneBounds;
                std::vector<Vector3> skinVerts;
                if (boneNode != nifly::NIF_NPOS && nif.GetShapeTransformSkinToBone(shape, 0u, skinToBone) &&
                    nif.GetShapeBoneBounds(shape, 0u, boneBounds) && nif.GetVertsForShape(shape, skinVerts)) {
                    const MatTransform bind = ToRootSpace(nif, boneNode);
                    const MatTransform both = bind.ComposeTransforms(skinToBone);
                    float error = std::fabs(both.scale - 1.0f);
                    error = std::max({ error, std::fabs(both.translation.x), std::fabs(both.translation.y), std::fabs(both.translation.z) });
                    const nifly::Matrix3 identity;
                    for (int r = 0; r < 3; ++r) {
                        const Vector3 d = both.rotation[r] - identity[r];
                        error = std::max({ error, std::fabs(d.x), std::fabs(d.y), std::fabs(d.z) });
                    }
                    s.bindRead = true;
                    s.bindError = error;
                    const auto own = shape->GetBounds();
                    s.sphereError = bind.ApplyTransform(boneBounds.center).DistanceTo(own.center) +
                                    std::fabs(boneBounds.radius * bind.scale - own.radius);
                    for (const auto& v : skinVerts) {
                        s.vertexError = std::max(s.vertexError, bind.ApplyTransform(skinToBone.ApplyTransform(v)).DistanceTo(v));
                    }
                }
            }
            s.vertexColors = shape->HasVertexColors();
            if (auto* tri = dynamic_cast<nifly::BSTriShape*>(shape); tri && s.vertexColors && !tri->vertData.empty()) {
                constexpr float kEndTolerance = 1e-3f;  // the highest and the lowest ring
                float top = -kFloatMax;
                float bottom = kFloatMax;
                for (const auto& vertex : tri->vertData) {
                    top = std::max(top, vertex.vert.z);
                    bottom = std::min(bottom, vertex.vert.z);
                }
                s.alphaMin = 255;
                s.alphaMax = 0;
                s.alphaTop = 0;
                s.alphaBottom = 0;
                for (const auto& vertex : tri->vertData) {
                    const int a = vertex.colorData[3];
                    s.alphaMin = std::min(s.alphaMin, a);
                    s.alphaMax = std::max(s.alphaMax, a);
                    if (vertex.vert.z >= top - kEndTolerance) {
                        s.alphaTop = std::max(s.alphaTop, a);
                    }
                    if (vertex.vert.z <= bottom + kEndTolerance) {
                        s.alphaBottom = std::max(s.alphaBottom, a);
                    }
                }
                // the jag (GreyLantern.h kLanternFlameJagRing): that ring's vertices at its share of the height, each put in
                // the segment its angle round the ring's centre (the bounds' middle; the seam's copy would pull a mean) is nearest
                if (IsFlameLayer(s.name) && top > bottom) {
                    constexpr double kTurn = 6.283185307179586;
                    const float ringZ = bottom + kLanternFlameProfile[kLanternFlameJagRing][0] * (top - bottom);
                    std::vector<const nifly::BSVertexData*> ring;
                    LanternBox box = EmptyBox();
                    for (const auto& vertex : tri->vertData) {
                        if (std::fabs(vertex.vert.z - ringZ) <= kEndTolerance) {
                            ring.push_back(&vertex);
                            Grow(box, vertex.vert);
                        }
                    }
                    std::array<double, 2> sum{};
                    std::array<int, 2> count{};
                    const auto centre = box.Centre();
                    for (const auto* vertex : ring) {
                        const double angle = std::atan2(vertex->vert.y - centre[1], vertex->vert.x - centre[0]);
                        const long segment = std::lround(angle / (kTurn / kLanternFlameSegments));
                        const std::size_t parity = static_cast<std::size_t>(((segment % 2) + 2) % 2);
                        sum[parity] += vertex->colorData[3];
                        ++count[parity];
                    }
                    if (count[0] > 0 && count[1] > 0) {
                        const double even = sum[0] / count[0];
                        const double odd = sum[1] / count[1];
                        const double brighter = std::max(even, odd);
                        s.alphaJag = brighter > 0.0 ? static_cast<float>(std::min(even, odd) / brighter) : -1.0f;
                    }
                }
            }
            if (auto* tri = dynamic_cast<nifly::BSTriShape*>(shape); tri && tri->HasUVs() && !tri->vertData.empty()) {
                s.uvMin = { kFloatMax, kFloatMax };
                s.uvMax = { -kFloatMax, -kFloatMax };
                for (const auto& uv : tri->UpdateRawUvs()) {
                    s.uvMin = { std::min(s.uvMin[0], uv.u), std::min(s.uvMin[1], uv.v) };
                    s.uvMax = { std::max(s.uvMax[0], uv.u), std::max(s.uvMax[1], uv.v) };
                }
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
            std::cout << fmt::format("  value node '{}' value {} flags {} at {} scale {} under '{}'\n", value.name, value.value, value.flags,
                Text(value.translation), value.scale, value.parent);
        }
        for (const auto& s : a_info.shapes) {
            std::cout << fmt::format("  shape '{}' {} vertices {} triangles {} shader {} SLSF1 0x{:08X} SLSF2 0x{:08X}, vertex colours {}\n",
                s.name, s.type, s.vertices, s.triangles, s.shader.empty() ? "<none>" : s.shader, s.flags1, s.flags2,
                s.vertexColors ? "yes" : "no");
            if (s.alphaMax >= 0) {
                std::cout << fmt::format("    vertex alpha {}..{}, at the highest vertices {}, at the lowest {}\n", s.alphaMin, s.alphaMax,
                    s.alphaTop, s.alphaBottom);
            }
            if (s.alphaJag >= 0.0f) {
                std::cout << fmt::format("    jag: on the {:.2f} ring the dimmer alternate segments' mean vertex alpha {:.4f} x the brighter's\n",
                    kLanternFlameProfile[kLanternFlameJagRing][0], s.alphaJag);
            }
            if (s.baseAlpha >= 0.0f) {
                std::cout << fmt::format("    effect shader base alpha {:.4f}, base colour scale {:.4f}, falloff start angle {:.4f} opacity "
                                         "{:.4f}, stop angle {:.4f} opacity {:.4f}, UV scale {:.4f} x {:.4f}, clamp mode 0x{:08X}\n",
                    s.baseAlpha, s.colourScale, s.falloff[0], s.falloff[2], s.falloff[1], s.falloff[3], s.uvScale[0], s.uvScale[1],
                    s.clampMode);
            }
            for (const auto& texture : s.textures) {
                std::cout << "    texture " << texture << "\n";
            }
            if (s.refraction >= 0.0f) {
                std::cout << fmt::format("    refraction strength {:.4f}\n", s.refraction);
            }
            if (s.glossiness >= 0.0f) {
                std::cout << fmt::format("    specular strength {:.4f}, glossiness {:.4f}, specular colour {}\n", s.specularStrength,
                    s.glossiness, Text(s.specularColor));
            }
            if (s.uvMin[0] <= s.uvMax[0]) {
                std::cout << fmt::format("    UVs u {:.4f}..{:.4f} v {:.4f}..{:.4f}\n", s.uvMin[0], s.uvMax[0], s.uvMin[1], s.uvMax[1]);
            }
            if (s.hasAlpha) {
                std::cout << fmt::format("    alpha property flags 0x{:04X} ({})\n", s.alphaFlags, s.additive ? "additive" : "not additive");
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
                if (s.bindRead) {
                    std::cout << fmt::format("    bind: |bone bind x skin-to-bone - identity| {:.2e}; bone-space bounds back through the "
                                             "bind {:.2e} off the shape's; vertices back through the bind {:.2e} off\n",
                        s.bindError, s.sphereError, s.vertexError);
                }
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

    namespace
    {
        constexpr float kPi = 3.14159265358979f;
        constexpr float kDegToRad = kPi / 180.0f;

        /// a_v turned by a_deg about the X, Y or Z axis (right-handed)
        [[nodiscard]] Vector3 TurnX(const Vector3& a_v, float a_deg)
        {
            const float c = std::cos(a_deg * kDegToRad);
            const float s = std::sin(a_deg * kDegToRad);
            return Vector3(a_v.x, a_v.y * c - a_v.z * s, a_v.y * s + a_v.z * c);
        }
        [[nodiscard]] Vector3 TurnY(const Vector3& a_v, float a_deg)
        {
            const float c = std::cos(a_deg * kDegToRad);
            const float s = std::sin(a_deg * kDegToRad);
            return Vector3(a_v.x * c + a_v.z * s, a_v.y, -a_v.x * s + a_v.z * c);
        }
        [[nodiscard]] Vector3 TurnZ(const Vector3& a_v, float a_deg)
        {
            const float c = std::cos(a_deg * kDegToRad);
            const float s = std::sin(a_deg * kDegToRad);
            return Vector3(a_v.x * c - a_v.y * s, a_v.x * s + a_v.y * c, a_v.z);
        }

        /// A triangle list made in the builder (the ring and the lugs)
        struct LanternMesh
        {
            std::vector<Vector3> verts;
            std::vector<Vector3> normals;
            std::vector<nifly::Vector2> uvs;
            std::vector<nifly::Triangle> tris;

            std::uint16_t Add(const Vector3& a_at, const Vector3& a_normal, float a_u, float a_v)
            {
                if (verts.size() >= 0xFFFF) {
                    throw std::runtime_error("a made mesh has more than 65535 vertices");
                }
                verts.push_back(a_at);
                Vector3 n = a_normal;
                n.Normalize();
                normals.push_back(n);
                uvs.emplace_back(a_u, a_v);
                return static_cast<std::uint16_t>(verts.size() - 1);
            }

            /// One triangle, wound so its face normal agrees with its vertices' normals (outward)
            void Face(std::uint16_t a_a, std::uint16_t a_b, std::uint16_t a_c)
            {
                const Vector3 face = (verts[a_b] - verts[a_a]).cross(verts[a_c] - verts[a_a]);
                const Vector3 normal = normals[a_a] + normals[a_b] + normals[a_c];
                if (face.dot(normal) >= 0.0f) {
                    tris.emplace_back(a_a, a_b, a_c);
                } else {
                    tris.emplace_back(a_a, a_c, a_b);
                }
            }

            void Quad(std::uint16_t a_a, std::uint16_t a_b, std::uint16_t a_c, std::uint16_t a_d)
            {
                Face(a_a, a_b, a_c);
                Face(a_a, a_c, a_d);
            }

            [[nodiscard]] LanternBox Bounds() const
            {
                LanternBox box = EmptyBox();
                for (const auto& v : verts) {
                    Grow(box, v);
                }
                return box;
            }
        };

        /// The smallest X of every body's surface in the hip window (Y a_y +- a_halfY, Z a_zLo..a_zHi)
        [[nodiscard]] float HipLeft(const std::vector<LanternSurface>& a_bodies, const Vector3& a_pelvis, float a_y, float a_halfY,
            float a_zLo, float a_zHi)
        {
            LanternBox window;
            window.min = { a_pelvis.x - kLanternHipWindow, a_y - a_halfY, a_zLo };
            window.max = { a_pelvis.x, a_y + a_halfY, a_zHi };
            float left = kFloatMax;
            for (const auto& body : a_bodies) {
                left = std::min(left, SurfaceLeft(body, window));
            }
            if (left == kFloatMax) {
                throw std::runtime_error(fmt::format("no body surface at the left hip at y {:.2f}..{:.2f}, z {:.2f}..{:.2f} (x within "
                                                     "kLanternHipWindow {} of the pelvis)", window.min[1], window.max[1], a_zLo, a_zHi,
                    kLanternHipWindow));
            }
            return left;
        }

        /// One point of a cross-section: s along the path's side normal, a along +Y, and its outward normal there
        struct SectionPoint
        {
            float s = 0.0f;
            float a = 0.0f;
            float ns = 0.0f;
            float na = 0.0f;
        };

        /// A cross-section: runs of points joined in order (a hard edge is where one run ends and the next begins) and the
        /// outline the end caps fan over
        struct Section
        {
            std::vector<std::vector<SectionPoint>> runs;
            std::vector<SectionPoint> outline;
        };

        /// a_halfS x a_halfA with hard edges: four flat runs of two points
        [[nodiscard]] Section BoxSection(float a_halfS, float a_halfA)
        {
            Section section;
            const SectionPoint c0{ a_halfS, -a_halfA }, c1{ a_halfS, a_halfA }, c2{ -a_halfS, a_halfA }, c3{ -a_halfS, -a_halfA };
            const auto face = [&](SectionPoint a_a, SectionPoint a_b, float a_ns, float a_na) {
                a_a.ns = a_b.ns = a_ns;
                a_a.na = a_b.na = a_na;
                section.runs.push_back({ a_a, a_b });
            };
            face(c0, c1, 1.0f, 0.0f);
            face(c1, c2, 0.0f, 1.0f);
            face(c2, c3, -1.0f, 0.0f);
            face(c3, c0, 0.0f, -1.0f);
            section.outline = { c0, c1, c2, c3 };
            return section;
        }

        /// a_section swept along a_path (in an XZ plane: the section's s axis is the path's side normal there, its tangent
        /// turned a quarter about Y; its a axis +Y), both ends capped. U from a_u(arc length), V from a_v(a). The path's length.
        float Sweep(LanternMesh& a_mesh, const std::vector<Vector3>& a_path, const Section& a_section,
            const std::function<float(float)>& a_u, const std::function<float(float)>& a_v)
        {
            const std::size_t count = a_path.size();
            if (count < 2) {
                throw std::runtime_error("a swept path needs two points");
            }
            std::vector<Vector3> tangent(count);
            std::vector<Vector3> side(count);
            std::vector<float> arc(count, 0.0f);
            for (std::size_t k = 0; k < count; ++k) {
                tangent[k] = a_path[std::min(k + 1, count - 1)] - a_path[k == 0 ? 0 : k - 1];
                tangent[k].Normalize();
                side[k] = Vector3(tangent[k].z, 0.0f, -tangent[k].x);
                if (k > 0) {
                    arc[k] = arc[k - 1] + a_path[k].DistanceTo(a_path[k - 1]);
                }
            }
            const Vector3 across(0.0f, 1.0f, 0.0f);
            const auto at = [&](std::size_t a_k, const SectionPoint& a_p) { return a_path[a_k] + side[a_k] * a_p.s + across * a_p.a; };
            for (const auto& run : a_section.runs) {
                std::vector<std::vector<std::uint16_t>> rows;
                for (std::size_t k = 0; k < count; ++k) {
                    std::vector<std::uint16_t> row;
                    for (const auto& p : run) {
                        row.push_back(a_mesh.Add(at(k, p), side[k] * p.ns + across * p.na, a_u(arc[k]), a_v(p.a)));
                    }
                    rows.push_back(std::move(row));
                }
                for (std::size_t k = 0; k + 1 < count; ++k) {
                    for (std::size_t j = 0; j + 1 < run.size(); ++j) {
                        a_mesh.Quad(rows[k][j], rows[k + 1][j], rows[k + 1][j + 1], rows[k][j + 1]);
                    }
                }
            }
            for (const std::size_t k : { std::size_t{ 0 }, count - 1 }) {
                const Vector3 normal = k == 0 ? tangent[k] * -1.0f : tangent[k];
                const std::uint16_t centre = a_mesh.Add(a_path[k], normal, a_u(arc[k]), a_v(0.0f));
                std::vector<std::uint16_t> rim;
                for (const auto& p : a_section.outline) {
                    rim.push_back(a_mesh.Add(at(k, p), normal, a_u(arc[k]), a_v(p.a)));
                }
                for (std::size_t j = 0; j < rim.size(); ++j) {
                    a_mesh.Face(centre, rim[j], rim[(j + 1) % rim.size()]);
                }
            }
            return arc.back();
        }

        /// A torus in the XZ plane round a_centre, UVs spread over a_uvMin..a_uvMax
        [[nodiscard]] LanternMesh MakeRing(const Vector3& a_centre, const nifly::Vector2& a_uvMin, const nifly::Vector2& a_uvMax)
        {
            LanternMesh mesh;
            const Vector3 hole(0.0f, 1.0f, 0.0f);
            for (int i = 0; i <= kLanternRingSegments; ++i) {
                const float a = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(kLanternRingSegments);
                const Vector3 out(std::cos(a), 0.0f, std::sin(a));
                for (int j = 0; j <= kLanternRingSides; ++j) {
                    const float b = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(kLanternRingSides);
                    const Vector3 normal = out * std::cos(b) + hole * std::sin(b);
                    (void)mesh.Add(a_centre + out * kLanternRingRadius + normal * kLanternRingTube, normal,
                        a_uvMin.u + (a_uvMax.u - a_uvMin.u) * static_cast<float>(i) / static_cast<float>(kLanternRingSegments),
                        a_uvMin.v + (a_uvMax.v - a_uvMin.v) * static_cast<float>(j) / static_cast<float>(kLanternRingSides));
                }
            }
            const auto at = [](int a_i, int a_j) { return static_cast<std::uint16_t>(a_i * (kLanternRingSides + 1) + a_j); };
            for (int i = 0; i < kLanternRingSegments; ++i) {
                for (int j = 0; j < kLanternRingSides; ++j) {
                    mesh.Quad(at(i, j), at(i + 1, j), at(i + 1, j + 1), at(i, j + 1));
                }
            }
            return mesh;
        }

        struct LanternLugs
        {
            LanternMesh mesh;
            std::array<LanternFoot, 2> feet{};
            float handleHeight = 0.0f;
            std::vector<std::string> log;
        };

        /// A lug under each foot of the upright handle (GreyLantern.h, the lug constants): a_frame the placed frame's
        /// vertices, a_handle its handle's, a_uvs its UVs. Throws when a foot or the lid under it is not found or a lug's
        /// height leaves kLanternLugMinHeight..kLanternLugMaxHeight.
        [[nodiscard]] LanternLugs MakeLugs(const std::vector<Vector3>& a_frame, const std::vector<nifly::Triangle>& a_tris,
            const std::vector<bool>& a_handle, const std::vector<nifly::Vector2>& a_uvs)
        {
            LanternLugs lugs;
            const auto feet = FindHandleFeet(a_frame, a_handle);
            lugs.handleHeight = feet.handle.Size(2);
            const float maxGap = kLanternLugMaxHeight - kLanternLugSink - kLanternLugOverlap;
            for (int side = 0; side < 2; ++side) {
                LanternFoot& foot = lugs.feet[side];
                foot.centre = ToArray(feet.centre[side]);
                foot.footZ = feet.footZ[side];
                SurfaceHit hit;
                LidUnder(a_frame, a_tris, a_handle, foot, &hit);
                const float gap = foot.footZ - foot.lidZ;
                if (gap > maxGap) {
                    throw std::runtime_error(fmt::format("the handle foot at {} stands {:.4f} above the lid (z {:.4f}): more than a lug "
                                                         "bridges (kLanternLugMaxHeight {} - kLanternLugSink {} - kLanternLugOverlap {})",
                        Text(foot.centre), gap, foot.lidZ, kLanternLugMaxHeight, kLanternLugSink, kLanternLugOverlap));
                }
                const float top = foot.footZ + kLanternLugOverlap;
                const float bottom = std::min(foot.lidMin - kLanternLugSink, top - kLanternLugMinHeight);
                if (top - bottom > kLanternLugMaxHeight + 1e-5f) {
                    throw std::runtime_error(fmt::format("the lug under the handle foot at {} would be {:.4f} tall (the lid under its "
                                                         "footprint falls to z {:.4f}): more than kLanternLugMaxHeight {}",
                        Text(foot.centre), top - bottom, foot.lidMin, kLanternLugMaxHeight));
                }
                // UVs: the lid's UV under the foot centre, spread at the lid triangle's texel density
                const auto& t = a_tris[hit.triangle];
                const nifly::Vector2& ua = a_uvs[t.p1];
                const nifly::Vector2& ub = a_uvs[t.p2];
                const nifly::Vector2& uc = a_uvs[t.p3];
                const nifly::Vector2 uv(ua.u + (ub.u - ua.u) * hit.u + (uc.u - ua.u) * hit.v, ua.v + (ub.v - ua.v) * hit.u + (uc.v - ua.v) * hit.v);
                const float uvArea = std::fabs((ub.u - ua.u) * (uc.v - ua.v) - (uc.u - ua.u) * (ub.v - ua.v)) * 0.5f;
                const float worldArea = (a_frame[t.p2] - a_frame[t.p1]).cross(a_frame[t.p3] - a_frame[t.p1]).length() * 0.5f;
                const float density = worldArea > 0.0f ? std::sqrt(uvArea / worldArea) : 0.0f;
                const float height = top - bottom;
                const auto first = lugs.mesh.verts.size();
                (void)Sweep(lugs.mesh, { Vector3(foot.centre[0], foot.centre[1], bottom), Vector3(foot.centre[0], foot.centre[1], top) },
                    BoxSection(kLanternLugSize[0] * 0.5f, kLanternLugSize[1] * 0.5f),
                    [uv, density, height](float a_arc) { return uv.u + density * (a_arc - height * 0.5f); },
                    [uv, density](float a_a) { return uv.v + density * a_a; });
                foot.lug = EmptyBox();
                for (auto i = first; i < lugs.mesh.verts.size(); ++i) {
                    Grow(foot.lug, lugs.mesh.verts[i]);
                }
                lugs.log.push_back(fmt::format("handle foot {}: {} vertices, centre {}, lowest z {:.4f}; the lid under the lug's footprint "
                                               "tops out at z {:.4f} ({:.4f} under the foot), lowest z {:.4f}, {} of {} points over holes; lug "
                                               "{} x {} (kLanternLugSize) from z {:.4f} to {:.4f}, {:.4f} tall; UVs round ({:.4f}, {:.4f}) at "
                                               "{:.4f} per unit (lid triangle {})",
                    side, feet.vertices[side], Text(foot.centre), foot.footZ, foot.lidZ, gap, foot.lidMin, foot.lidHoles,
                    kLanternLugSamples * kLanternLugSamples, kLanternLugSize[0], kLanternLugSize[1], bottom, top, height, uv.u, uv.v, density,
                    hit.triangle));
            }
            return lugs;
        }

        /// A new lit shape in a_nif from a_mesh, with a copy of a_template's shader and texture set naming a_diffuse and
        /// a_normal, tangents made from the UVs
        nifly::NiShape* AddMeshShape(nifly::NifFile& a_nif, const std::string& a_name, const LanternMesh& a_mesh,
            nifly::NiShape& a_template, const std::string& a_diffuse, const std::string& a_normal)
        {
            auto& hdr = a_nif.GetHeader();
            auto* shape = a_nif.CreateShapeFromData(a_name, &a_mesh.verts, &a_mesh.tris, &a_mesh.uvs, &a_mesh.normals);
            auto* tri = dynamic_cast<nifly::BSTriShape*>(shape);
            if (!tri) {
                throw std::runtime_error(fmt::format("nifly made no BSTriShape for '{}'", a_name));
            }
            tri->SetNormals(true);
            tri->SetNormals(a_mesh.normals);
            tri->SetTangents(true);
            tri->CalcTangentSpace();
            auto* shader = hdr.GetBlock<nifly::BSLightingShaderProperty>(a_template.ShaderPropertyRef());
            auto* set = shader ? hdr.GetBlock<nifly::BSShaderTextureSet>(shader->TextureSetRef()) : nullptr;
            if (!set || set->textures.size() <= kNormalSlot) {
                throw std::runtime_error(fmt::format("the template '{}' has no lighting shader with a texture set", a_template.name.get()));
            }
            auto shaderCopy = shader->Clone();
            auto setCopy = set->Clone();
            setCopy->textures[kSlotDiffuse].get() = a_diffuse;
            setCopy->textures[kNormalSlot].get() = a_normal;
            shaderCopy->TextureSetRef()->index = hdr.AddBlock(std::move(setCopy));
            shaderCopy->controllerRef.index = nifly::NIF_NPOS;
            if (!tri->HasVertexColors()) {
                // the template's Vertex_Colors flag (the lamp frame has colours) on a shape without them: cleared
                shaderCopy->shaderFlags2 &= ~kSlsf2VertexColors;
            }
            shape->ShaderPropertyRef()->index = hdr.AddBlock(std::move(shaderCopy));
            return shape;
        }

        /// a_shape's vertices through a_point(index, position), its normals through a_normal(index, normal), tangents made
        /// again, its own transform cleared
        void BakeEach(nifly::NiShape& a_shape, const std::function<Vector3(std::size_t, const Vector3&)>& a_point,
            const std::function<Vector3(std::size_t, const Vector3&)>& a_normal)
        {
            auto* tri = dynamic_cast<nifly::BSTriShape*>(&a_shape);
            if (!tri) {
                throw std::runtime_error(fmt::format("shape '{}' is a {}, not a BSTriShape", a_shape.name.get(), a_shape.GetBlockName()));
            }
            std::vector<Vector3> normals;
            if (tri->HasNormals()) {
                normals = tri->UpdateRawNormals();
            }
            for (std::size_t i = 0; i < tri->vertData.size(); ++i) {
                tri->vertData[i].vert = a_point(i, tri->vertData[i].vert);
            }
            if (!normals.empty()) {
                for (std::size_t i = 0; i < normals.size(); ++i) {
                    normals[i] = a_normal(i, normals[i]);
                    normals[i].Normalize();
                }
                tri->SetNormals(normals);
                if (tri->HasTangents()) {
                    tri->CalcTangentSpace();
                }
            }
            a_shape.transform.Clear();
        }

        [[nodiscard]] std::string GapText(const LanternGap& a_gap)
        {
            return fmt::format("{} points, {} inside, nearest {:.3f} at {}", a_gap.points, a_gap.inside, a_gap.nearest,
                Text(a_gap.nearestPoint));
        }

        /// One flame layer made here (GreyLantern.h, the flame constants) in the ground model's root space: its mesh (normals
        /// outward from the axis with the profile's slope, U round the axis, V up) and each vertex's alpha
        struct LanternFlameMesh
        {
            const LanternFlameLayer* layer = nullptr;
            LanternMesh mesh;
            std::vector<std::uint8_t> alpha;
            float base = 0.0f;    // its lowest ring's z
            float height = 0.0f;
            float radius = 0.0f;  // its widest ring's
        };

        /// a_layer's spindle round the vertical line through a_axis (its X and Y), its lowest ring at z a_base
        [[nodiscard]] LanternFlameMesh MakeFlameLayer(const LanternFlameLayer& a_layer, const Vector3& a_axis, float a_base, float a_height,
            float a_radius)
        {
            LanternFlameMesh out;
            out.layer = &a_layer;
            out.base = a_base;
            out.height = a_height;
            out.radius = a_radius;
            const std::size_t rings = kLanternFlameProfile.size();
            for (std::size_t k = 0; k < rings; ++k) {
                const auto& ring = kLanternFlameProfile[k];
                // the profile's slope dr/dz at this ring, from its neighbours (one-sided at the ends)
                const auto& below = kLanternFlameProfile[k == 0 ? 0 : k - 1];
                const auto& above = kLanternFlameProfile[std::min(k + 1, rings - 1)];
                const float slope = (above[1] - below[1]) * a_radius / ((above[0] - below[0]) * a_height);
                const float ringAlpha = std::clamp(ring[2], 0.0f, 1.0f);
                for (int s = 0; s <= kLanternFlameSegments; ++s) {
                    // the jagged top: odd segments dimmed from kLanternFlameJagRing up (the seam column is even)
                    const float jag = k >= kLanternFlameJagRing && s % 2 == 1 ? kLanternFlameJagFactor : 1.0f;
                    const auto alpha = static_cast<std::uint8_t>(std::lround(ringAlpha * jag * 255.0f));
                    const float u = static_cast<float>(s) / static_cast<float>(kLanternFlameSegments);
                    const float angle = 2.0f * kPi * u;
                    const Vector3 radial(std::cos(angle), std::sin(angle), 0.0f);
                    // V falls with the height, as on vanilla FireStrip08 (1.23 at its base, 0.94 at its top): the template's
                    // controller raises the V offset, so the fire then scrolls up
                    (void)out.mesh.Add(Vector3(a_axis.x, a_axis.y, a_base + ring[0] * a_height) + radial * (ring[1] * a_radius),
                        radial - Vector3(0.0f, 0.0f, slope), u, 1.0f - ring[0]);
                    out.alpha.push_back(alpha);
                }
            }
            const auto at = [](std::size_t a_k, int a_s) { return static_cast<std::uint16_t>(a_k * (kLanternFlameSegments + 1) + a_s); };
            for (std::size_t k = 0; k + 1 < rings; ++k) {
                for (int s = 0; s < kLanternFlameSegments; ++s) {
                    out.mesh.Quad(at(k, s), at(k, s + 1), at(k + 1, s + 1), at(k + 1, s));
                }
            }
            return out;
        }

        /// The fire (GreyLantern.h, the flame constants): the atronach NIF holding the shader template (its skinning removed)
        /// and the two layers made for the cage. AddFlame clones the template from it: one LanternFlame per model.
        struct LanternFlame
        {
            nifly::NifFile nif;
            nifly::NiShape* shader = nullptr;  // kLanternFlameTemplate
            std::array<LanternFlameMesh, 2> layers;
        };

        /// The fire's base in ground root space: on the cage axis (a_cageCentre's X and Y), kLanternFlameBaseShare of the frame's
        /// height above its bottom. The ground model's torch fire node stands here, the worn outer layer's base before the worn
        /// transform
        [[nodiscard]] Vector3 FlameBaseOf(const LanternBox& a_frameBounds, const std::array<float, 3>& a_cageCentre)
        {
            return Vector3(a_cageCentre[0], a_cageCentre[1], a_frameBounds.min[2] + kLanternFlameBaseShare * a_frameBounds.Size(2));
        }

        /// The ground model's torch fire node: on the cage axis, kLanternTorchBaseShare of the frame's height above its bottom
        /// (round 11, at kLanternTorchRound11Scale), lowered by what the smaller scale takes off the emitter sphere's radius, so its
        /// bottom (the fire's foot) stays where round 11 had it (round 12)
        [[nodiscard]] Vector3 TorchBaseOf(const LanternBox& a_frameBounds, const std::array<float, 3>& a_cageCentre)
        {
            const float lowered = kLanternTorchEmitterRadius * (kLanternTorchRound11Scale - kLanternTorchFireScale);
            return Vector3(a_cageCentre[0], a_cageCentre[1],
                a_frameBounds.min[2] + kLanternTorchBaseShare * a_frameBounds.Size(2) - lowered);
        }

        /// The torch fire copy (round 11): a_source's every blending alpha property made additive (destination ONE), then
        /// GreyEffects as the spells' fire copies, so its palette shaders take GradBlackFlameAdd.dds and kDefaultTintAdd. Throws
        /// when the source has no blending alpha property or no palette shader
        void BuildTorchFire(const std::filesystem::path& a_source, const std::filesystem::path& a_target, std::vector<std::string>& a_log)
        {
            nifly::NifFile nif;
            LoadNif(nif, a_source);
            auto& hdr = nif.GetHeader();
            int blending = 0;
            for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
                if (auto* alpha = hdr.GetBlock<nifly::NiAlphaProperty>(id); alpha && (alpha->flags & kLanternAlphaBlendOn) != 0) {
                    const std::uint16_t before = alpha->flags;
                    alpha->flags = static_cast<std::uint16_t>(alpha->flags & ~kLanternDestBlendBits);
                    a_log.push_back(fmt::format("torch fire: alpha property block {} 0x{:04X} -> 0x{:04X} (destination ONE: additive, as "
                                                "the spells' fire)", id, before, alpha->flags));
                    ++blending;
                }
            }
            if (blending == 0) {
                throw std::runtime_error(fmt::format("{} has no alpha property with blending on", kLanternTorchSource));
            }
            std::vector<std::string> notes;
            const GreyStats stats = GreyEffects(nif, kDefaultTint, kDefaultTintAdd, &notes);
            for (auto& note : notes) {
                a_log.push_back("torch fire: " + note);
            }
            if (stats.palettedAdd == 0) {
                throw std::runtime_error(fmt::format("{}: no palette shader took the additive palette ({} effect shaders, {} normal-blend "
                                                     "palette shaders)", kLanternTorchSource, stats.effectShaders, stats.paletted));
            }
            a_log.push_back(fmt::format("torch fire: {} effect shaders, {} on {} (base colour x {}, kDefaultTintAdd), {} normal palette, "
                                        "{} left; {} colour modifiers to grey",
                stats.effectShaders, stats.palettedAdd, kPaletteAddPath, kDefaultTintAdd, stats.paletted, stats.paletteLeft,
                stats.colorModifiers));
            SaveNif(nif, a_target);
        }

        /// What is wrong with a written torch fire copy: an alpha property with blending on that is not additive, a palette shader
        /// not on GradBlackFlameAdd.dds, or none at all; empty when it holds
        [[nodiscard]] std::vector<std::string> TorchFireWrong(const std::filesystem::path& a_file)
        {
            std::vector<std::string> problems;
            nifly::NifFile nif;
            LoadNif(nif, a_file);
            auto& hdr = nif.GetHeader();
            int palettes = 0;
            for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
                if (const auto* alpha = hdr.GetBlock<nifly::NiAlphaProperty>(id);
                    alpha && (alpha->flags & kLanternAlphaBlendOn) != 0 && BlendOfAlpha(alpha) != ShapeBlend::kAdditive) {
                    problems.push_back(fmt::format("torch fire: alpha property block {} flags 0x{:04X}, expected additive (destination ONE)",
                        id, alpha->flags));
                }
                if (const auto* shader = hdr.GetBlock<nifly::BSEffectShaderProperty>(id); shader && UsesPalette(*shader)) {
                    ++palettes;
                    if (Lower(shader->greyscaleTexture.get()) != Lower(kPaletteAddPath)) {
                        problems.push_back(fmt::format("torch fire: effect shader block {} palette '{}', expected '{}'", id,
                            shader->greyscaleTexture.get(), kPaletteAddPath));
                    }
                }
            }
            if (palettes == 0) {
                problems.push_back("torch fire: no palette shader");
            }
            return problems;
        }

        /// The cage walls: the bounds of a_rootVerts (the frame in root space, a_bounds their bounds) in the middle
        /// kLanternCageBand of its height; empty when no vertex is there
        [[nodiscard]] LanternBox CageWallsOf(const std::vector<Vector3>& a_rootVerts, const LanternBox& a_bounds)
        {
            const float bandLow = a_bounds.min[2] + a_bounds.Size(2) * (1.0f - kLanternCageBand) * 0.5f;
            const float bandHigh = a_bounds.max[2] - a_bounds.Size(2) * (1.0f - kLanternCageBand) * 0.5f;
            LanternBox walls = EmptyBox();
            for (const auto& p : a_rootVerts) {
                if (p.z >= bandLow && p.z <= bandHigh) {
                    Grow(walls, p);
                }
            }
            return walls;
        }

        /// Loads the template into a_flame and makes the layers in the cage (a_result's frame bounds and cage centre, a_walls);
        /// a_log (when set) gets their sizes and where they stand. Throws when the template is not a BSTriShape with an effect
        /// shader and a float controller on it, or when the outer layer's widest ring is not kLanternFlameClearance (at the
        /// ground scale) inside the cage walls.
        void LoadFlame(LanternFlame& a_flame, const std::vector<std::filesystem::path>& a_inputs, const LanternBox& a_walls,
            LanternBuildResult& a_result, std::vector<std::string>* a_log)
        {
            LoadNif(a_flame.nif, Need(a_inputs, kLanternFlameSource));
            auto& hdr = a_flame.nif.GetHeader();
            a_flame.shader = FindShape(a_flame.nif, kLanternFlameTemplate);
            auto* effect = a_flame.shader ? hdr.GetBlock<nifly::BSEffectShaderProperty>(a_flame.shader->ShaderPropertyRef()) : nullptr;
            if (!dynamic_cast<nifly::BSTriShape*>(a_flame.shader) || !effect) {
                throw std::runtime_error(fmt::format("{} has no BSTriShape '{}' with a BSEffectShaderProperty", kLanternFlameSource,
                    kLanternFlameTemplate));
            }
            bool scroll = false;
            std::set<std::uint32_t> seen;
            for (std::uint32_t id = effect->controllerRef.index; id != nifly::NIF_NPOS && !scroll && seen.insert(id).second;) {
                auto* controller = hdr.GetBlock<nifly::NiTimeController>(id);
                if (!controller) {
                    break;
                }
                scroll = dynamic_cast<nifly::BSEffectShaderPropertyFloatController*>(controller) != nullptr;
                id = controller->nextControllerRef.index;
            }
            if (!scroll) {
                throw std::runtime_error(fmt::format("'{}' in {} has no BSEffectShaderPropertyFloatController (the UV scroll)",
                    kLanternFlameTemplate, kLanternFlameSource));
            }
            a_flame.nif.DeleteSkinning(a_flame.shader);

            const float frameHeight = a_result.frameBounds.Size(2);
            const float wallSize = std::min(a_walls.Size(0), a_walls.Size(1));
            const float height = kLanternFlameHeightShare * frameHeight;
            const float radius = kLanternFlameRadiusShare * wallSize;
            const Vector3 baseCentre = FlameBaseOf(a_result.frameBounds, a_result.cageCentre);
            const float base = baseCentre.z;
            const Vector3 axis(baseCentre.x, baseCentre.y, 0.0f);
            // the worn clearance at the ground scale; the cage centre is the walls' centre
            const float clearance = kLanternFlameClearance * frameHeight / kLanternWornHeight;
            if (radius + clearance > wallSize * 0.5f) {
                throw std::runtime_error(fmt::format("the cage is too small for the flame: its widest ring (radius {:.4f} = "
                                                     "kLanternFlameRadiusShare {:.4f} x the walls' smaller size {:.4f}) and "
                                                     "kLanternFlameClearance {} ({:.4f} at the ground scale) reach {:.4f} from the cage "
                                                     "axis, the walls {:.4f}",
                    radius, kLanternFlameRadiusShare, wallSize, kLanternFlameClearance, clearance, radius + clearance, wallSize * 0.5f));
            }
            for (std::size_t l = 0; l < kLanternFlameLayers.size(); ++l) {
                const auto& layer = kLanternFlameLayers[l];
                a_flame.layers[l] = MakeFlameLayer(layer, axis, base + layer.lift * height, layer.heightShare * height, layer.radiusShare * radius);
            }
            a_result.flameBase = { axis.x, axis.y, base };
            a_result.flameHeight = height;
            a_result.flameRadius = radius;
            if (a_log) {
                a_log->push_back(fmt::format("flame: the outer layer {:.4f} tall (kLanternFlameHeightShare {} x the frame's height {:.4f}), "
                                             "widest radius {:.4f} (kLanternFlameRadiusShare {:.4f} x the walls' smaller size {:.4f}; "
                                             "{:.4f} inside the walls, kLanternFlameClearance at the ground scale {:.4f}); its base on "
                                             "the cage axis at {} = the frame's bottom z {:.4f} + {:.4f} (kLanternFlameBaseShare) x its "
                                             "height",
                    height, kLanternFlameHeightShare, frameHeight, radius, kLanternFlameRadiusShare, wallSize, wallSize * 0.5f - radius,
                    clearance, Text(a_result.flameBase), a_result.frameBounds.min[2], kLanternFlameBaseShare));
                for (const auto& layer : a_flame.layers) {
                    a_log->push_back(fmt::format("flame layer '{}': {} vertices, {} triangles (kLanternFlameSegments {}, {} rings), z {:.4f}"
                                                 "..{:.4f}, widest radius {:.4f}, tip radius {:.4f}; alpha flags 0x{:04X}, base alpha {}",
                        layer.layer->name, layer.mesh.verts.size(), layer.mesh.tris.size(), kLanternFlameSegments,
                        kLanternFlameProfile.size(), layer.base, layer.base + layer.height, layer.radius,
                        layer.radius * kLanternFlameProfile.back()[1], layer.layer->alphaFlags, layer.layer->baseAlpha));
                }
            }
        }

        using FlameMap = std::function<Vector3(const Vector3&)>;

        /// a_flame's layers as shapes in a_dst: each a clone of the template (its shader with the controllers aimed at that
        /// shader, its alpha property) carrying the layer's mesh, the vertices through a_point and the normals through a_normal
        /// (both given the ground root-space value), tangents made from the UVs, and the shader flags, base alpha and blend of
        /// GreyLantern.h's flame constants. The clones (not yet in a_dst's scene: the caller places them)
        std::vector<nifly::NiShape*> AddFlame(nifly::NifFile& a_dst, LanternFlame& a_flame, const FlameMap& a_point, const FlameMap& a_normal,
            std::vector<std::string>& a_log, const char* a_model)
        {
            constexpr std::uint8_t kColourFull = 255;
            auto& hdr = a_dst.GetHeader();
            std::vector<nifly::NiShape*> clones;
            for (const auto& layer : a_flame.layers) {
                const char* name = layer.layer->name;
                auto* clone = a_dst.CloneShape(a_flame.shader, name, &a_flame.nif);
                auto* tri = dynamic_cast<nifly::BSTriShape*>(clone);
                if (!tri) {
                    throw std::runtime_error(fmt::format("nifly did not clone '{}' into the {} model as '{}'", kLanternFlameTemplate, a_model, name));
                }
                const auto& mesh = layer.mesh;
                std::vector<nifly::BSVertexData> data(mesh.verts.size());
                std::vector<Vector3> normals;
                for (std::size_t i = 0; i < mesh.verts.size(); ++i) {
                    Vector3 n = a_normal(mesh.normals[i]);
                    n.Normalize();
                    data[i].vert = a_point(mesh.verts[i]);
                    data[i].uv = mesh.uvs[i];
                    data[i].colorData[0] = data[i].colorData[1] = data[i].colorData[2] = kColourFull;
                    data[i].colorData[3] = layer.alpha[i];
                    normals.push_back(n);
                }
                tri->SetSkinned(false);
                tri->SetVertices(true);
                tri->SetUVs(true);
                tri->SetVertexColors(true);
                tri->SetVertexData(data);
                tri->SetTriangles(mesh.tris);
                tri->SetNormals(normals);
                tri->CalcTangentSpace();  // from the UVs, as the ring and the lugs (AddMeshShape)
                clone->transform.Clear();
                tri->UpdateBounds();
                for (const auto& line : RetargetShaderControllers(a_dst, *clone)) {
                    a_log.push_back(fmt::format("{} flame layer '{}': controller {}", a_model, name, line));
                }
                auto* effect = hdr.GetBlock<nifly::BSEffectShaderProperty>(clone->ShaderPropertyRef());
                if (!effect) {
                    throw std::runtime_error(fmt::format("the {} flame layer '{}' has no effect shader", a_model, name));
                }
                const std::uint32_t flags1 = effect->shaderFlags1;
                const std::uint32_t flags2 = effect->shaderFlags2;
                effect->shaderFlags1 = (flags1 & ~kLanternFlameFlags1Off) | kLanternFlameFlags1On;
                effect->shaderFlags2 = flags2 | kLanternFlameFlags2On;
                effect->baseColor.a = layer.layer->baseAlpha;
                effect->falloffStartAngle = layer.layer->falloff[0];
                effect->falloffStopAngle = layer.layer->falloff[1];
                effect->falloffStartOpacity = layer.layer->falloff[2];
                effect->falloffStopOpacity = layer.layer->falloff[3];
                effect->uvScale.u = kLanternFlameUvScale[0];
                effect->uvScale.v = kLanternFlameUvScale[1];
                const float templateScale = effect->baseColorScale;
                effect->baseColorScale = layer.layer->colourScale;
                auto* alpha = clone->AlphaPropertyRef() ? hdr.GetBlock<nifly::NiAlphaProperty>(*clone->AlphaPropertyRef()) : nullptr;
                if (!alpha) {
                    auto made = std::make_unique<nifly::NiAlphaProperty>();
                    alpha = made.get();
                    (void)a_dst.AssignAlphaProperty(clone, std::move(made));
                }
                alpha->flags = layer.layer->alphaFlags;
                a_log.push_back(fmt::format("{} flame layer '{}': {} vertices, {} triangles; SLSF1 0x{:08X} -> 0x{:08X} (palette colour, "
                                            "vertex alpha and falloff on; palette alpha, decal, dynamic decal, skinned off), SLSF2 0x{:08X} -> "
                                            "0x{:08X} (double-sided, vertex colours on); base alpha {}, base colour scale {} -> {}, falloff "
                                            "start angle {} opacity {}, stop angle {} opacity {}, UV scale {} x {} (clamp mode 0x{:08X}), "
                                            "alpha flags 0x{:04X} ({})",
                    a_model, name, data.size(), mesh.tris.size(), flags1, effect->shaderFlags1, flags2, effect->shaderFlags2,
                    effect->baseColor.a, templateScale, effect->baseColorScale, effect->falloffStartAngle, effect->falloffStartOpacity, effect->falloffStopAngle,
                    effect->falloffStopOpacity, effect->uvScale.u, effect->uvScale.v, effect->textureClampMode, alpha->flags,
                    BlendOfAlpha(alpha) == ShapeBlend::kAdditive ? "additive" : "normal blend"));
                clones.push_back(clone);
            }
            return clones;
        }

        /// GreyEffects on a_nif (the black flame palettes; it reads each layer's blend from its alpha property), then one line
        /// per layer: its blend and the palette it took. GreyEffects changes no shader flags; throws when a layer's were changed.
        void BlackFlameLayers(nifly::NifFile& a_nif, const std::vector<nifly::NiShape*>& a_layers, std::vector<std::string>& a_log,
            const char* a_model)
        {
            auto& hdr = a_nif.GetHeader();
            std::vector<std::pair<std::uint32_t, std::uint32_t>> flags;
            for (auto* shape : a_layers) {
                const auto* effect = hdr.GetBlock<nifly::BSEffectShaderProperty>(shape->ShaderPropertyRef());
                flags.emplace_back(effect ? effect->shaderFlags1 : 0u, effect ? effect->shaderFlags2 : 0u);
            }
            std::vector<std::string> notes;
            const GreyStats stats = GreyEffects(a_nif, kDefaultTint, kDefaultTintAdd, &notes);
            a_log.push_back(fmt::format("{} effect shaders {}: palette normal {} ({}), additive {} ({}), left {}; lighting shaders {} left "
                                        "alone; base colour x tint {} (additive {})",
                a_model, stats.effectShaders, stats.paletted, kPalettePath, stats.palettedAdd, kPaletteAddPath, stats.paletteLeft,
                stats.lightingShaders, kDefaultTint, kDefaultTintAdd));
            for (const auto& note : notes) {
                a_log.push_back(fmt::format("{}: {}", a_model, note));
            }
            for (std::size_t i = 0; i < a_layers.size(); ++i) {
                auto* shape = a_layers[i];
                auto* effect = hdr.GetBlock<nifly::BSEffectShaderProperty>(shape->ShaderPropertyRef());
                auto* alpha = shape->AlphaPropertyRef() ? hdr.GetBlock<nifly::NiAlphaProperty>(*shape->AlphaPropertyRef()) : nullptr;
                if (!effect) {
                    throw std::runtime_error(fmt::format("the {} flame layer '{}' lost its effect shader", a_model, shape->name.get()));
                }
                if (effect->shaderFlags1 != flags[i].first || effect->shaderFlags2 != flags[i].second) {
                    throw std::runtime_error(fmt::format("GreyEffects changed the {} flame layer '{}' shader flags: SLSF1 0x{:08X} -> 0x{:08X}, "
                                                         "SLSF2 0x{:08X} -> 0x{:08X}",
                        a_model, shape->name.get(), flags[i].first, effect->shaderFlags1, flags[i].second, effect->shaderFlags2));
                }
                a_log.push_back(fmt::format("{} '{}': alpha 0x{:04X} ({}), base colour ({:.4f}, {:.4f}, {:.4f}, {:.4f}), palette '{}'; "
                                            "SLSF1 0x{:08X} SLSF2 0x{:08X} kept by GreyEffects",
                    a_model, shape->name.get(), alpha ? alpha->flags : 0, BlendOfAlpha(alpha) == ShapeBlend::kAdditive ? "additive" : "normal blend",
                    effect->baseColor.r, effect->baseColor.g, effect->baseColor.b, effect->baseColor.a, effect->greyscaleTexture.get(),
                    effect->shaderFlags1, effect->shaderFlags2));
            }
        }

        /// The worn model (GreyLantern.h, the worn constants) into a_dataDir; throws on any failure
        void BuildWorn(const std::vector<std::filesystem::path>& a_inputs, const std::filesystem::path& a_dataDir,
            nifly::NifFile& a_ground, nifly::NiShape& a_frame, const LanternBox& a_walls, LanternBuildResult& a_result)
        {
            auto& log = a_result.log;
            nifly::NifFile skeleton;
            LoadNif(skeleton, Need(a_inputs, kLanternSkeletonSource));
            MatTransform pelvis;
            if (!skeleton.GetNodeTransformToGlobal(kLanternBone, pelvis)) {
                throw std::runtime_error(fmt::format("{} has no node '{}'", kLanternSkeletonSource, kLanternBone));
            }
            a_result.pelvis = ToArray(pelvis.translation);
            std::vector<LanternSurface> bodies;
            float bandTop = -kFloatMax;
            for (std::size_t b = 0; b < kLanternBodySources.size(); ++b) {
                const auto file = Need(a_inputs, kLanternBodySources[b]);
                bodies.push_back(LoadLanternSurface(file));
                log.push_back(fmt::format("body {}: {} triangles, winding {}", kLanternBodySources[b], bodies.back().tris.size(),
                    bodies.back().outward > 0.0f ? "outward" : "inward"));
                const auto band = MeasureLanternBand(file, kLanternBandShapes[b], pelvis.translation);
                log.push_back(fmt::format("belt band {} '{}': {} vertices at x {}..{} and z {}..{} from the pelvis (kLanternBandXMin/XMax, "
                                          "kLanternBandZWindow): top z {:.4f}, bottom z {:.4f}",
                    band.body, band.shape, band.vertices, kLanternBandXMin, kLanternBandXMax, kLanternBandZWindow[0], kLanternBandZWindow[1],
                    band.top, band.bottom));
                bandTop = std::max(bandTop, band.top);
                a_result.bands.push_back(band);
            }

            // the frame in the ground's root space, and its handle
            auto* frameTri = dynamic_cast<nifly::BSTriShape*>(&a_frame);
            if (!frameTri) {
                throw std::runtime_error("the frame is not a BSTriShape");
            }
            const MatTransform frameToRoot = ToRootSpace(a_ground, a_ground.GetHeader().GetBlockID(&a_frame));
            std::vector<Vector3> frameRoot;
            for (const auto& vertex : frameTri->vertData) {
                frameRoot.push_back(frameToRoot.ApplyTransform(vertex.vert));
            }
            const auto pieces = LanternPieces(frameRoot, frameTri->triangles);
            std::vector<std::size_t> handles;
            for (std::size_t p = 0; p < pieces.size(); ++p) {
                if (pieces[p].bounds.max[1] > a_walls.max[1] + kLanternWeld) {
                    handles.push_back(p);
                }
            }
            if (handles.size() != 1) {
                throw std::runtime_error(fmt::format("{} frame pieces of {} reach past the cage walls (y {:.4f}) in +Y, expected the "
                                                     "handle alone", handles.size(), pieces.size(), a_walls.max[1]));
            }
            const LanternPiece& handle = pieces[handles.front()];
            std::vector<bool> isHandle(frameRoot.size(), false);
            for (const auto v : handle.vertices) {
                isHandle[v] = true;
            }
            const Vector3 hinge(0.0f, handle.bounds.min[1], handle.bounds.Centre()[2]);
            const Vector3 hingeShift(0.0f, a_result.cageCentre[1] - hinge.y, 0.0f);
            log.push_back(fmt::format("handle: frame piece {} of {} ({} triangles, {} vertices) {}; hinge y {:.4f} z {:.4f} moved {:.4f} in "
                                      "Y onto the cage axis (y {:.4f}), turned {} deg about X (kLanternHandleTurnDeg)",
                handles.front(), pieces.size(), handle.triangles.size(), handle.vertices.size(), BoxText(handle.bounds), hinge.y, hinge.z,
                hingeShift.y, a_result.cageCentre[1], kLanternHandleTurnDeg));

            // the lantern's own transform: handle turn, yaw round the cage axis, scale (then only a translation onto the ring)
            const float scale = kLanternWornHeight / a_result.frameBounds.Size(2);
            a_result.wornScale = scale;
            // the turned handle stands from its hinge (its lowest point, the piece's -Y edge) up by its Y length; scaled along
            // Z about the hinge to kLanternHandleHeight at the worn scale
            const float handleLength = handle.bounds.Size(1);
            if (!(handleLength > 0.0f)) {
                throw std::runtime_error("the handle piece has no length in Y");
            }
            const float handleScale = kLanternHandleHeight / (handleLength * scale);
            a_result.handleScale = handleScale;
            const Vector3 axis(a_result.cageCentre[0], a_result.cageCentre[1], 0.0f);
            const auto shapeA = [&](Vector3 a_p, bool a_handle) {
                if (a_handle) {
                    a_p = hinge + hingeShift + TurnX(a_p - hinge, kLanternHandleTurnDeg);
                    a_p.z = hinge.z + (a_p.z - hinge.z) * handleScale;
                }
                return (axis + TurnZ(a_p - axis, kLanternWornYawDeg)) * scale;
            };
            const auto normalA = [&](Vector3 a_n, bool a_handle) {
                if (a_handle) {
                    a_n = TurnX(a_n, kLanternHandleTurnDeg);
                    a_n.z /= handleScale;  // a scale along Z: the normals by its inverse (BakeEach normalises)
                }
                return TurnZ(a_n, kLanternWornYawDeg);
            };
            const float roundWire = handle.bounds.Size(2) * 0.5f * scale;
            const float wire = roundWire * handleScale;  // the top wire's half height after the Z scale
            log.push_back(fmt::format("handle: {:.4f} tall at the worn scale, scaled x {:.5f} along Z about its feet to {} "
                                      "(kLanternHandleHeight); its wire radius {:.4f}, the top wire's half height {:.4f}",
                handleLength * scale, handleScale, kLanternHandleHeight, roundWire, wire));
            float topZ = -kFloatMax;
            for (const auto v : handle.vertices) {
                topZ = std::max(topZ, shapeA(frameRoot[v], true).z);
            }
            Vector3 apexSum;
            int apexCount = 0;
            for (const auto v : handle.vertices) {
                const Vector3 p = shapeA(frameRoot[v], true);
                if (p.z >= topZ - 2.0f * wire) {
                    apexSum += p;
                    ++apexCount;
                }
            }
            const Vector3 apex(apexSum.x / static_cast<float>(apexCount), apexSum.y / static_cast<float>(apexCount), topZ - wire);
            const Vector3 cageA = shapeA(ToVector(a_result.cageCentre), false);
            log.push_back(fmt::format("worn: pelvis '{}' at {} (skeleton.nif); frame height {:.4f} -> {} (kLanternWornHeight), scale "
                                      "{:.5f}; yaw {} deg (kLanternWornYawDeg); handle wire radius {:.4f}, its top wire centre (scaled "
                                      "lantern space, {} vertices near the top) {}, {:.4f} off the cage axis in XY",
                kLanternBone, Text(a_result.pelvis), a_result.frameBounds.Size(2), kLanternWornHeight, scale, kLanternWornYawDeg, wire,
                apexCount, Text(ToArray(apex)), std::hypot(apex.x - cageA.x, apex.y - cageA.y)));

            // the fire, made in the ground frame's cage (its base where the ground model's torch fire node stands) and taken
            // through the lantern's own transform
            LanternFlame flame;
            LoadFlame(flame, a_inputs, a_walls, a_result, &log);

            // the lantern's points before the translation onto the ring (it hangs vertically: no tilt)
            std::vector<Vector3> framePoints;
            for (std::size_t i = 0; i < frameRoot.size(); ++i) {
                framePoints.push_back(shapeA(frameRoot[i], isHandle[i]));
            }
            std::vector<Vector3> flamePoints;
            for (const auto& layer : flame.layers) {
                for (const auto& v : layer.mesh.verts) {
                    flamePoints.push_back(shapeA(v, false));
                }
            }
            a_result.wornFlameClearance = LanternNearestTriangle(flamePoints, framePoints, frameTri->triangles);
            log.push_back(fmt::format("worn flame: its vertices' nearest frame triangle {:.4f} (kLanternFlameClearance {})",
                a_result.wornFlameClearance, kLanternFlameClearance));
            if (a_result.wornFlameClearance < kLanternFlameClearance) {
                throw std::runtime_error(fmt::format("the flame comes within {:.4f} of the worn frame (kLanternFlameClearance {})",
                    a_result.wornFlameClearance, kLanternFlameClearance));
            }
            // the lugs under the handle's feet (before the translation, like the points above)
            auto lugs = MakeLugs(framePoints, frameTri->triangles, isHandle, frameTri->UpdateRawUvs());
            log.insert(log.end(), lugs.log.begin(), lugs.log.end());

            // the ring beside the hip at the band's height and the lantern on it: walked outward until the lantern clears both
            // bodies. The ring's UVs: the box's U round the ring, V round the tube at the same texel density
            const nifly::Vector2 ringUvMin(kLanternIronUvMin[0], kLanternIronUvMin[1]);
            const nifly::Vector2 ringUvMax(kLanternIronUvMax[0],
                kLanternIronUvMin[1] + (kLanternIronUvMax[0] - kLanternIronUvMin[0]) * kLanternRingTube / kLanternRingRadius);
            const float ringZ = bandTop;
            log.push_back(fmt::format("ring: centre z {:.4f} (the highest belt band top), UVs (kLanternIronUvMin/Max u {}..{} v {}..{} of {}) u "
                                      "{:.4f}..{:.4f} v {:.4f}..{:.4f}",
                ringZ, kLanternIronUvMin[0], kLanternIronUvMax[0], kLanternIronUvMin[1], kLanternIronUvMax[1], kLanternDiffuseSource,
                ringUvMin.u, ringUvMax.u, ringUvMin.v, ringUvMax.v));
            // each body's gap to a_points, and the smallest
            const auto gaps = [&](const std::vector<Vector3>& a_points, LanternGap& a_worst) {
                std::string text;
                a_worst = LanternGap();
                a_worst.nearest = kFloatMax;
                for (const auto& body : bodies) {
                    const auto gap = SurfaceGap(body, a_points);
                    a_worst.points = gap.points;
                    a_worst.inside += gap.inside;
                    if (gap.nearest < a_worst.nearest) {
                        a_worst.nearest = gap.nearest;
                        a_worst.nearestPoint = gap.nearestPoint;
                    }
                    text += fmt::format(" {}: {};", body.name, GapText(gap));
                }
                return text;
            };
            LanternMesh ringMesh;
            Vector3 ring;
            Vector3 contact;
            Vector3 move;
            // the start: the ring's innermost point kLanternRingMargin off the bodies' leftmost surface within its reach; out by
            // the shortfall while its nearest gap reads under the margin (a point moves no further than the ring)
            const float y = pelvis.translation.y + kLanternRingY;
            const float reach = kLanternRingRadius + kLanternRingTube;
            const float halfY = kLanternRingTube + kLanternRingMargin;
            const float hip = HipLeft(bodies, pelvis.translation, y, halfY, ringZ - reach - kLanternRingMargin, ringZ + reach + kLanternRingMargin);
            float startX = hip - reach - kLanternRingMargin;
            for (int round = 0;; ++round) {
                LanternGap startGap;
                (void)gaps(MakeRing(Vector3(startX, y, ringZ), ringUvMin, ringUvMax).verts, startGap);
                if (startGap.inside == 0 && startGap.nearest >= kLanternRingMargin - kLanternRingFitTolerance * 0.5f) {
                    break;
                }
                if (round + 1 >= kLanternRingFitRounds) {
                    throw std::runtime_error(fmt::format("the ring at y {:.4f} still comes within {:.4f} of a body ({} points inside) after "
                                                         "kLanternRingFitRounds {} (kLanternRingMargin {})",
                        y, startGap.nearest, startGap.inside, kLanternRingFitRounds, kLanternRingMargin));
                }
                startX -= (startGap.inside > 0 ? startGap.nearest + reach : 0.0f) + kLanternRingMargin - startGap.nearest +
                          kLanternRingFitTolerance * 0.5f;
            }
            log.push_back(fmt::format("ring: y {:.4f} (pelvis + kLanternRingY {}); the hip's leftmost surface within its reach x {:.4f}, the "
                                      "ring's start x {:.4f}",
                y, kLanternRingY, hip, startX));
            bool placed = false;
            for (int step = 0; !placed; ++step) {
                const float out = static_cast<float>(step) * kLanternRingOutStep;
                if (out > kLanternRingOutMax + 1e-4f) {
                    break;
                }
                const float x = startX - out;
                LanternMesh tryRing = MakeRing(Vector3(x, y, ringZ), ringUvMin, ringUvMax);
                LanternGap ringGap;
                const std::string ringText = gaps(tryRing.verts, ringGap);
                const Vector3 tryCentre(x, y, ringZ);
                const Vector3 tryContact = tryCentre - Vector3(0.0f, 0.0f, kLanternRingRadius - kLanternRingTube - wire);
                const Vector3 tryMove = tryContact - apex;
                std::vector<Vector3> lantern;
                lantern.reserve(framePoints.size() + flamePoints.size() + lugs.mesh.verts.size());
                for (const auto* points : { &framePoints, &flamePoints, &lugs.mesh.verts }) {
                    for (const auto& p : *points) {
                        lantern.push_back(p + tryMove);
                    }
                }
                LanternGap lanternGap;
                const std::string lanternText = gaps(lantern, lanternGap);
                log.push_back(fmt::format("ring out {} (centre x {:.4f}): ring{} lantern{}", out, x, ringText, lanternText));
                a_result.ringSteps = step + 1;
                if (ringGap.inside == 0 && ringGap.nearest >= kLanternRingMargin - kLanternRingFitTolerance * 0.5f && lanternGap.inside == 0 &&
                    lanternGap.nearest >= kLanternBodyMargin) {
                    placed = true;
                    ringMesh = std::move(tryRing);
                    ring = tryCentre;
                    contact = tryContact;
                    move = tryMove;
                    a_result.ringOut = out;
                    a_result.ringGap = ringGap.nearest;
                    a_result.lanternGap = lanternGap.nearest;
                }
            }
            if (!placed) {
                throw std::runtime_error(fmt::format("at every outward step from 0 to {} (kLanternRingOutMax, step {}) the lantern comes "
                                                     "nearer to a body than kLanternBodyMargin {} (or the ring than kLanternRingMargin {}): "
                                                     "stopped (no tilt)",
                    kLanternRingOutMax, kLanternRingOutStep, kLanternBodyMargin, kLanternRingMargin));
            }
            a_result.ringCentre = ToArray(ring);
            a_result.contact = ToArray(contact);
            a_result.handleWire = wire;
            for (auto& v : lugs.mesh.verts) {
                v += move;
            }
            a_result.handleHeight = lugs.handleHeight;
            for (int side = 0; side < 2; ++side) {
                LanternFoot foot = lugs.feet[side];
                foot.centre = ToArray(ToVector(foot.centre) + move);
                foot.footZ += move.z;
                foot.lidZ += move.z;
                foot.lidMin += move.z;
                for (int a = 0; a < 3; ++a) {
                    foot.lug.min[a] += ToArray(move)[a];
                    foot.lug.max[a] += ToArray(move)[a];
                }
                a_result.feet[side] = foot;
            }
            log.push_back(fmt::format("ring out {} (kLanternRingOutStep {}, max {}): ring centre {}, {:.4f} off the bodies "
                                      "(kLanternRingMargin {}); the lantern {:.4f} off them (kLanternBodyMargin {})",
                a_result.ringOut, kLanternRingOutStep, kLanternRingOutMax, Text(a_result.ringCentre), a_result.ringGap,
                kLanternRingMargin, a_result.lanternGap, kLanternBodyMargin));

            const auto place = [&](const Vector3& a_root, bool a_handle) { return shapeA(a_root, a_handle) + move; };
            const auto placeNormal = [&](const Vector3& a_n, bool a_handle) { return normalA(a_n, a_handle); };
            a_result.wornCage = ToArray(cageA + move);
            log.push_back(fmt::format("lantern: hangs vertically (no tilt) from the ring, its handle's top wire on the ring's bottom tube "
                                      "at {}; cage centre {}; the flame moved with it",
                Text(a_result.contact), Text(a_result.wornCage)));

            // the worn file
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
                    throw std::runtime_error("nifly did not make or clone a shape");
                }
                const std::uint32_t id = hdr.GetBlockID(a_shape);
                if (ParentOf(worn, id) == nifly::NIF_NPOS) {
                    root->childRefs.AddBlockRef(id);
                }
                wornShapes.push_back(a_shape);
                return a_shape;
            };

            // the frame, from the ground model in memory (soot diffuse; the collision stays on the ground root)
            auto* wornFrame = adopt(worn.CloneShape(&a_frame, kLanternWornFrameName, &a_ground));
            BakeEach(*wornFrame, [&](std::size_t a_i, const Vector3& a_v) { return place(frameToRoot.ApplyTransform(a_v), isHandle[a_i]); },
                [&](std::size_t a_i, const Vector3& a_n) { return placeNormal(frameToRoot.rotation * a_n, isHandle[a_i]); });

            // the fire
            const auto layers = AddFlame(worn, flame, [&](const Vector3& a_v) { return place(a_v, false); },
                [&](const Vector3& a_n) { return normalA(a_n, false); }, log, "worn");
            for (auto* layer : layers) {
                (void)adopt(layer);
            }

            // the ring and the lugs (made in character space already)
            std::string frameNormal;
            {
                auto* shader = hdr.GetBlock<nifly::BSLightingShaderProperty>(wornFrame->ShaderPropertyRef());
                auto* set = shader ? hdr.GetBlock<nifly::BSShaderTextureSet>(shader->TextureSetRef()) : nullptr;
                frameNormal = set && set->textures.size() > kNormalSlot ? set->textures[kNormalSlot].get() : std::string();
            }
            (void)adopt(AddMeshShape(worn, kLanternRingName, ringMesh, *wornFrame, kLanternDiffuse, frameNormal));
            (void)adopt(AddMeshShape(worn, kLanternLugName, lugs.mesh, *wornFrame, kLanternDiffuse, frameNormal));
            log.push_back(fmt::format("ring: {} vertices, {} triangles, {}", ringMesh.verts.size(), ringMesh.tris.size(),
                BoxText(ringMesh.Bounds())));
            log.push_back(fmt::format("lugs: {} vertices, {} triangles, {}", lugs.mesh.verts.size(), lugs.mesh.tris.size(),
                BoxText(lugs.mesh.Bounds())));

            for (auto* shape : wornShapes) {
                SkinToBone(worn, *shape, boneId, pelvis);
            }
            // CloneShape brings nodes of the source file along as root children (the atronach's nodes): only the bone and the
            // shapes stay; the save drops what is then unreferenced
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
            {
                auto* tri = dynamic_cast<nifly::BSTriShape*>(wornFrame);
                a_result.wornFrame = EmptyBox();
                for (const auto& vertex : tri->vertData) {
                    Grow(a_result.wornFrame, vertex.vert);
                }
                log.push_back(fmt::format("worn frame (character space) {}", BoxText(a_result.wornFrame)));
            }
            BlackFlameLayers(worn, layers, log, "worn");
            SaveNew(worn, a_dataDir / kLanternWornNif);

            const Vector3 ringBottom = ring - Vector3(0.0f, 0.0f, kLanternRingRadius);
            a_result.summary.push_back(fmt::format("handle top wire centre (world) {}; ring centre {}; ring bottom tube centre {}; the wire "
                                                   "sits {:.4f} above the tube centre (tube {} + wire {:.4f}), {:.4f} below the ring "
                                                   "centre, {:.4f} off its vertical",
                Text(a_result.contact), Text(a_result.ringCentre), Text(ToArray(ringBottom)), contact.z - ringBottom.z, kLanternRingTube,
                wire, ring.z - contact.z, std::hypot(contact.x - ring.x, contact.y - ring.y)));
            a_result.summary.push_back(fmt::format("lantern hung vertically (no tilt); cage centre {}; scale {:.5f}", Text(a_result.wornCage),
                scale));
            a_result.summary.push_back(fmt::format("ring {} out from its start after {} step(s) (kLanternRingOutStep {}, max {}): ring "
                                                   "{:.4f} off the bodies (kLanternRingMargin {}), the lantern {:.4f} (kLanternBodyMargin {})",
                a_result.ringOut, a_result.ringSteps, kLanternRingOutStep, kLanternRingOutMax, a_result.ringGap, kLanternRingMargin,
                a_result.lanternGap, kLanternBodyMargin));
            for (const auto& band : a_result.bands) {
                a_result.summary.push_back(fmt::format("belt band {} '{}' at the left hip: top z {:.4f}, bottom z {:.4f} ({} vertices)", band.body,
                    band.shape, band.top, band.bottom, band.vertices));
            }
            a_result.summary.push_back(fmt::format("handle {:.4f} tall (x {:.5f} along Z about its feet, kLanternHandleHeight {})",
                a_result.handleHeight, handleScale, kLanternHandleHeight));
            for (int side = 0; side < 2; ++side) {
                const auto& foot = a_result.feet[side];
                a_result.summary.push_back(fmt::format("handle foot {} at {}: lowest z {:.4f}, {:.4f} above the lid (z {:.4f}); lug {}",
                    side, Text(foot.centre), foot.footZ, foot.footZ - foot.lidZ, foot.lidZ, BoxText(foot.lug)));
            }
            a_result.summary.push_back(fmt::format("ring and lugs {} / {}", kLanternDiffuse, frameNormal));
            for (const auto& layer : flame.layers) {
                a_result.summary.push_back(fmt::format("flame layer '{}': {} vertices, {} triangles, ground z {:.4f}..{:.4f}, widest radius "
                                                       "{:.4f}; x {:.5f} worn",
                    layer.layer->name, layer.mesh.verts.size(), layer.mesh.tris.size(), layer.base, layer.base + layer.height, layer.radius,
                    scale));
            }
            a_result.summary.push_back(fmt::format("flame base (ground space) {} (kLanternFlameBaseShare {:.4f}); the layers' nearest worn "
                                                   "frame triangle {:.4f} (kLanternFlameClearance {})",
                Text(a_result.flameBase), kLanternFlameBaseShare, a_result.wornFlameClearance, kLanternFlameClearance));
        }
    }

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
        LanternBox cageWalls = EmptyBox();
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
                std::vector<Vector3> rootVerts;
                for (const auto& v : verts) {
                    rootVerts.push_back(toRoot.ApplyTransform(v));
                }
                LanternBox walls = CageWallsOf(rootVerts, result.frameBounds);
                if (IsEmpty(walls)) {
                    log.push_back("cage walls: no frame vertex in the middle of its height, the whole bounds' centre used");
                    walls = result.frameBounds;
                }
                cageWalls = walls;
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
            log.push_back(fmt::format("cage centre (walls' X/Y, bounds' middle Z) {}", Text(result.cageCentre)));

            // the fire goes into the copy that is saved: the worn step clones the frame from the original, which stays without
            // it (SaveNif's string-table pass also writes through the file it saves). Round 6: no AddOnNode9100 - the grey copy
            // behind ADDN 0x804 (mps\mpsfiresmall01.nif) is three additive particle systems on GradBlackFlame.dds without the
            // dark layer the casting-art copies get; round 7: two flame layers made here; round 10 (user, 2026-10-22): the
            // vanilla torch fire's grey copy (ADDN 0x81D, normal blend on GradBlackFlame.dds) through AddOnNode9146 on the cage
            // axis, as Immersive Equipment Displays shows this model on the body with its addon nodes
            nifly::NifFile copy = ground;
            {
                auto* bsx = FindBsx(copy);
                if (!bsx) {
                    throw std::runtime_error(fmt::format("{} has no BSXFlags on its root", kLanternFrameSource));
                }
                const Vector3 at = TorchBaseOf(result.frameBounds, result.cageCentre);
                result.torchBase = ToArray(at);
                (void)AddValueNode(copy, *copy.GetRootNode(), kLanternTorchAddon, kLanternTorchValueFlags, at, kLanternTorchFireScale);
                const auto& reach = kLanternTorchFireReach;
                log.push_back(fmt::format("ground fire: AddOnNode{} (value flags {}) at {} = the cage axis, the frame's bottom z {:.4f} + "
                                          "{:.4f} (kLanternTorchBaseShare) x its height {:.4f} - {:.4f} (the emitter's foot kept); scale {} (kLanternTorchFireScale): "
                                          "MPSTorchFire01's particle quads reach about {} off the axis, {} above the node and {} below it "
                                          "unscaled (kLanternTorchFireReach), {:.2f}, z {:.2f} and z {:.2f} here; the cage walls {:.4f} "
                                          "off the axis",
                    kLanternTorchAddon, kLanternTorchValueFlags, Text(result.torchBase), result.frameBounds.min[2], kLanternTorchBaseShare,
                    result.frameBounds.Size(2), kLanternTorchEmitterRadius * (kLanternTorchRound11Scale - kLanternTorchFireScale), kLanternTorchFireScale, reach[0], reach[1], reach[2], reach[0] * kLanternTorchFireScale,
                    at.z + reach[1] * kLanternTorchFireScale, at.z - reach[2] * kLanternTorchFireScale,
                    std::min(cageWalls.Size(0), cageWalls.Size(1)) * 0.5f));
                const std::uint32_t before = bsx->integerData;
                bsx->integerData |= kBsxAnimated | kBsxAddon;
                log.push_back(fmt::format("ground BSXFlags {} -> {} (Animated 0x{:X} and Addon 0x{:X}, as the vanilla models with an addon "
                                          "node)", before, bsx->integerData, kBsxAnimated, kBsxAddon));
            }
            SaveNew(copy, a_dataDir / kLanternGroundNif);
            groundOk = true;
        } catch (const std::exception& e) {
            fail(kLanternGroundNif, e);
        }

        // 3. the worn model
        if (groundOk) {
            try {
                BuildWorn(a_inputs, a_dataDir, ground, *frame, cageWalls, result);
            } catch (const std::exception& e) {
                fail(kLanternWornNif, e);
            }
        }

        // 3b. the torch fire copy the ground model's AddOnNode9146 emits (ADDN 0x81D's model; round 11)
        try {
            BuildTorchFire(Need(a_inputs, kLanternTorchSource), a_dataDir / kLanternTorchNif, log);
        } catch (const std::exception& e) {
            fail(kLanternTorchNif, e);
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

        // 5. the empty model (LanternIed.h: the ARMA's biped model while IED shows the lantern): a BSFadeNode root, as the vanilla
        // biped NIFs and lantern_smoke.nif have, named, and nothing else
        try {
            nifly::NifFile empty;
            empty.Create(nifly::NiVersion::getSSE());
            (void)empty.GetHeader().ReplaceBlock(0, std::make_unique<nifly::BSFadeNode>());
            auto* root = empty.GetRootNode();
            if (!root || empty.GetHeader().GetBlockID(root) != 0 || std::string(root->GetBlockName()) != "BSFadeNode") {
                throw std::runtime_error("the root could not be made a BSFadeNode");
            }
            root->name.get() = kLanternEmptyRootName;
            log.push_back(fmt::format("empty: BSFadeNode '{}' only (no geometry, no BSXFlags)", kLanternEmptyRootName));
            SaveNew(empty, a_dataDir / kLanternEmptyNif);
        } catch (const std::exception& e) {
            fail(kLanternEmptyNif, e);
        }

        if (result.problems.empty()) {
            for (const auto& problem : VerifyLantern(a_inputs, a_dataDir)) {
                result.problems.push_back("read-back: " + problem);
            }
        }
        result.ok = result.problems.empty();
        return result;
    }

    namespace
    {
        /// The ground model's frame: its one shape that is not a flame layer; nullptr when there is not exactly one
        [[nodiscard]] nifly::NiShape* GroundFrame(nifly::NifFile& a_nif)
        {
            nifly::NiShape* found = nullptr;
            for (auto* shape : a_nif.GetShapes()) {
                if (!IsFlameLayer(shape->name.get())) {
                    if (found) {
                        return nullptr;
                    }
                    found = shape;
                }
            }
            return found;
        }
    }

    std::vector<LanternClearance> WornClearances(const std::vector<std::filesystem::path>& a_inputs, const std::filesystem::path& a_dataDir)
    {
        nifly::NifFile worn;
        LoadNif(worn, a_dataDir / kLanternWornNif);
        struct Part
        {
            const char* name;
            std::vector<std::string> shapes;
            float margin;
        };
        const std::array<Part, 2> parts{ { { "lantern", { kLanternWornFrameName, kLanternFlameLayers[0].name, kLanternFlameLayers[1].name,
                                                            kLanternLugName },
                                                 kLanternBodyMargin },
            { "ring", { kLanternRingName }, kLanternRingMargin } } };
        std::vector<LanternClearance> out;
        std::vector<LanternSurface> bodies;
        for (const char* body : kLanternBodySources) {
            if (const auto file = FindSource(a_inputs, body)) {
                bodies.push_back(LoadLanternSurface(*file));
            }
        }
        for (const auto& part : parts) {
            std::vector<Vector3> points;
            for (const auto& name : part.shapes) {
                std::vector<Vector3> verts;
                if (auto* shape = FindShape(worn, name); shape && worn.GetVertsForShape(shape, verts)) {
                    points.insert(points.end(), verts.begin(), verts.end());  // skin space = character space (global-to-skin identity)
                }
            }
            for (const auto& body : bodies) {
                LanternClearance c;
                c.part = part.name;
                c.body = body.name;
                c.margin = part.margin;
                c.gap = SurfaceGap(body, points);
                out.push_back(std::move(c));
            }
        }
        return out;
    }

    LanternHandleReadBack WornHandle(const std::filesystem::path& a_dataDir)
    {
        nifly::NifFile worn;
        LoadNif(worn, a_dataDir / kLanternWornNif);
        auto* frame = FindShape(worn, kLanternWornFrameName);
        auto* lugShape = FindShape(worn, kLanternLugName);
        std::vector<Vector3> verts;
        std::vector<nifly::Triangle> tris;
        std::vector<Vector3> lugVerts;
        if (!frame || !lugShape || !worn.GetVertsForShape(frame, verts) || !frame->GetTriangles(tris) || verts.empty() ||
            !worn.GetVertsForShape(lugShape, lugVerts) || lugVerts.empty()) {
            throw std::runtime_error(fmt::format("lantern_worn.nif has no frame '{}' or lugs '{}' with vertices", kLanternWornFrameName,
                kLanternLugName));
        }
        // skin space = character space (global-to-skin identity); the handle: the frame piece holding its highest vertex
        const auto top = static_cast<std::size_t>(
            std::max_element(verts.begin(), verts.end(), [](const Vector3& a_l, const Vector3& a_r) { return a_l.z < a_r.z; }) - verts.begin());
        std::vector<bool> isHandle(verts.size(), false);
        for (const auto& piece : LanternPieces(verts, tris)) {
            if (std::binary_search(piece.vertices.begin(), piece.vertices.end(), static_cast<std::uint16_t>(top))) {
                for (const auto v : piece.vertices) {
                    isHandle[v] = true;
                }
            }
        }
        const auto feet = FindHandleFeet(verts, isHandle);
        LanternHandleReadBack out;
        out.height = feet.handle.Size(2);
        for (int side = 0; side < 2; ++side) {
            auto& foot = out.feet[side];
            foot.centre = ToArray(feet.centre[side]);
            foot.footZ = feet.footZ[side];
            LidUnder(verts, tris, isHandle, foot, nullptr);
            foot.lug = EmptyBox();
        }
        // each lug vertex to the nearer foot (in XY)
        for (const auto& v : lugVerts) {
            const auto distance = [&](int a_side) {
                return std::hypot(v.x - out.feet[a_side].centre[0], v.y - out.feet[a_side].centre[1]);
            };
            Grow(out.feet[distance(0) <= distance(1) ? 0 : 1].lug, v);
        }
        for (const auto& foot : out.feet) {
            if (IsEmpty(foot.lug)) {
                throw std::runtime_error(fmt::format("no lug vertex is nearer the handle foot at {} than the other foot", Text(foot.centre)));
            }
        }
        return out;
    }

    float WornAxisTiltDeg(const std::filesystem::path& a_dataDir)
    {
        nifly::NifFile ground;
        LoadNif(ground, a_dataDir / kLanternGroundNif);
        nifly::NifFile worn;
        LoadNif(worn, a_dataDir / kLanternWornNif);
        auto* groundFrame = GroundFrame(ground);
        auto* wornFrame = FindShape(worn, kLanternWornFrameName);
        std::vector<Vector3> groundVerts;
        std::vector<Vector3> wornVerts;
        if (!groundFrame || !wornFrame || !ground.GetVertsForShape(groundFrame, groundVerts) || !worn.GetVertsForShape(wornFrame, wornVerts)) {
            throw std::runtime_error("the axis check finds no frame in lantern_gnd.nif or lantern_worn.nif");
        }
        const MatTransform toRoot = ToRootSpace(ground, ground.GetHeader().GetBlockID(groundFrame));
        for (auto& v : groundVerts) {
            v = toRoot.ApplyTransform(v);
        }
        return LanternAxisTiltDeg(groundVerts, wornVerts);  // worn: skin space = character space (global-to-skin identity)
    }

    LanternFlameReadBack FlameReadBack(const std::filesystem::path& a_dataDir)
    {
        LanternFlameReadBack out;
        nifly::NifFile nif;
        LoadNif(nif, a_dataDir / kLanternWornNif);
        auto* frame = FindShape(nif, kLanternWornFrameName);
        std::vector<Vector3> verts;
        std::vector<nifly::Triangle> tris;
        if (!frame || !nif.GetVertsForShape(frame, verts) || !frame->GetTriangles(tris)) {
            throw std::runtime_error(fmt::format("{} has no frame with triangles", kLanternWornNif));
        }
        // the worn shapes are skinned: skin space = character space
        std::vector<Vector3> points;
        for (std::size_t l = 0; l < kLanternFlameLayers.size(); ++l) {
            auto* layer = FindShape(nif, kLanternFlameLayers[l].name);
            std::vector<Vector3> layerVerts;
            if (!layer || !nif.GetVertsForShape(layer, layerVerts) || layerVerts.empty()) {
                throw std::runtime_error(fmt::format("{} has no flame layer '{}'", kLanternWornNif, kLanternFlameLayers[l].name));
            }
            out.worn[l] = EmptyBox();
            for (const auto& v : layerVerts) {
                Grow(out.worn[l], v);
                points.push_back(v);
            }
        }
        out.wornClearance = LanternNearestTriangle(points, verts, tris);
        return out;
    }

    std::vector<std::string> VerifyLantern(const std::vector<std::filesystem::path>& a_inputs, const std::filesystem::path& a_dataDir)
    {
        std::vector<std::string> problems;
        try {
            const auto shapeIn = [](const LanternNifInfo& a_info, const std::string& a_name) -> const LanternShapeInfo* {
                const auto it = std::find_if(a_info.shapes.begin(), a_info.shapes.end(), [&](const auto& a_s) { return a_s.name == a_name; });
                return it == a_info.shapes.end() ? nullptr : &*it;
            };
            // the fire in a model: both layers, each an effect shader with palette colour, vertex alpha, falloff, double-sided and
            // vertex colours on and palette alpha, the decal bits (and Skinned on the ground) off; its blend, base alpha, base
            // colour scale, falloff values, UV scale (kLanternFlameUvScale, on a wrapping clamp mode) and the black flame palette
            // for that blend; its counts; vertex alpha up to 255, the tip and the bottom ring at the profile's, the jag on
            // kLanternFlameJagRing (the dimmer alternate segments kLanternFlameJagFactor x the brighter); every controller on
            // its own shader, and no shader, alpha property, controller, interpolator or key block shared between the layers
            const auto checkFlame = [&](const LanternNifInfo& a_info, const char* a_model, bool a_worn) {
                std::set<std::uint32_t> blocks;
                for (const auto& layer : kLanternFlameLayers) {
                    const auto* s = shapeIn(a_info, layer.name);
                    Check(problems, s != nullptr, fmt::format("{}: no flame layer '{}'", a_model, layer.name));
                    if (!s) {
                        continue;
                    }
                    // the worn model's layers have Skinned back on (SkinToBone)
                    const std::uint32_t off = a_worn ? kLanternFlameFlags1Off & ~kSlsf1Skinned : kLanternFlameFlags1Off;
                    const std::uint32_t on = kLanternFlameFlags1On | (a_worn ? kSlsf1Skinned : 0u);
                    const std::uint32_t on2 = kLanternFlameFlags2On;
                    Check(problems, s->shader == "BSEffectShaderProperty" && (s->flags1 & off) == 0 && (s->flags1 & on) == on &&
                                        (s->flags2 & on2) == on2,
                        fmt::format("{} '{}': shader {} SLSF1 0x{:08X} SLSF2 0x{:08X}, expected SLSF1 0x{:08X} on and 0x{:08X} off, SLSF2 "
                                    "0x{:08X} on", a_model, s->name, s->shader, s->flags1, s->flags2, on, off, on2));
                    const std::string palette = std::string("greyscale ") + (s->additive ? kPaletteAddPath : kPalettePath);
                    Check(problems, s->hasAlpha && s->alphaFlags == layer.alphaFlags && s->textures.size() > 1 && s->textures[1] == palette,
                        fmt::format("{} '{}': alpha 0x{:04X} (expected 0x{:04X}), palette '{}' (expected '{}' for its blend)", a_model, s->name,
                            s->alphaFlags, layer.alphaFlags, s->textures.size() > 1 ? s->textures[1] : "", palette));
                    Check(problems, std::fabs(s->baseAlpha - layer.baseAlpha) <= 1e-6f,
                        fmt::format("{} '{}': base alpha {}, expected {}", a_model, s->name, s->baseAlpha, layer.baseAlpha));
                    bool falloff = true;
                    for (std::size_t i = 0; i < layer.falloff.size(); ++i) {
                        falloff = falloff && std::fabs(s->falloff[i] - layer.falloff[i]) <= 1e-6f;
                    }
                    Check(problems, falloff,
                        fmt::format("{} '{}': falloff start angle {} opacity {}, stop angle {} opacity {}, expected {} {}, {} {}", a_model, s->name,
                            s->falloff[0], s->falloff[2], s->falloff[1], s->falloff[3], layer.falloff[0], layer.falloff[2], layer.falloff[1],
                            layer.falloff[3]));
                    Check(problems, std::fabs(s->colourScale - layer.colourScale) <= 1e-6f,
                        fmt::format("{} '{}': base colour scale {}, expected {}", a_model, s->name, s->colourScale, layer.colourScale));
                    Check(problems, std::fabs(s->uvScale[0] - kLanternFlameUvScale[0]) <= 1e-6f &&
                                        std::fabs(s->uvScale[1] - kLanternFlameUvScale[1]) <= 1e-6f && (s->clampMode & 0xFFu) == kLanternClampWrap,
                        fmt::format("{} '{}': UV scale {} x {}, clamp mode 0x{:08X}, expected UV scale {} x {} and clamp mode {} (wrap S wrap T) "
                                    "in the low byte", a_model, s->name, s->uvScale[0], s->uvScale[1], s->clampMode, kLanternFlameUvScale[0],
                            kLanternFlameUvScale[1], kLanternClampWrap));
                    Check(problems, s->vertices == kLanternFlameVertices && s->triangles == kLanternFlameTriangles,
                        fmt::format("{} '{}': {} vertices, {} triangles, expected {} and {}", a_model, s->name, s->vertices, s->triangles,
                            kLanternFlameVertices, kLanternFlameTriangles));
                    const auto profileAlpha = [](std::size_t a_ring) {
                        return static_cast<int>(std::lround(kLanternFlameProfile[a_ring][2] * 255.0f));
                    };
                    Check(problems, s->vertexColors && s->alphaMax == 255 && s->alphaTop == profileAlpha(kLanternFlameProfile.size() - 1) &&
                                        s->alphaBottom == profileAlpha(0),
                        fmt::format("{} '{}': vertex colours {}, vertex alpha up to {}, {} at the tip and {} at the bottom ring (expected 255, {} "
                                    "and {})", a_model, s->name, s->vertexColors, s->alphaMax, s->alphaTop, s->alphaBottom,
                            profileAlpha(kLanternFlameProfile.size() - 1), profileAlpha(0)));
                    // the vertex alphas are whole steps of 1/255: on the jag ring's ~200 that is about 0.005 of the ratio
                    constexpr float kJagTolerance = 0.01f;
                    Check(problems, s->alphaJag >= 0.0f && std::fabs(s->alphaJag - kLanternFlameJagFactor) <= kJagTolerance,
                        fmt::format("{} '{}': on the {:.2f} ring the dimmer alternate segments' vertex alpha {} x the brighter's, expected {} "
                                    "(kLanternFlameJagFactor; -1: the ring not found)", a_model, s->name,
                            kLanternFlameProfile[kLanternFlameJagRing][0], s->alphaJag, kLanternFlameJagFactor));
                    Check(problems, !s->controllers.empty(), fmt::format("{} '{}': no shader controller (the UV scroll)", a_model, s->name));
                    for (const auto& controller : s->controllers) {
                        Check(problems, controller.find("WRONG") == std::string::npos,
                            fmt::format("{} '{}': controller {}", a_model, s->name, controller));
                    }
                    for (const auto id : s->blockIds) {
                        Check(problems, blocks.insert(id).second,
                            fmt::format("{} '{}': block {} shared with the other flame layer", a_model, s->name, id));
                    }
                }
            };

            // the ground model
            const auto gnd = InspectLanternNif(a_dataDir / kLanternGroundNif);
            Check(problems, gnd.shapes.size() == 1,
                fmt::format("gnd: {} shapes, expected the frame alone (the fire is the torch fire node)", gnd.shapes.size()));
            const LanternShapeInfo* frame = nullptr;
            for (const auto& s : gnd.shapes) {
                Check(problems, !IsFlameLayer(s.name), fmt::format("gnd: a flame layer '{}' (the fire is the torch fire node)", s.name));
                if (!IsFlameLayer(s.name)) {
                    frame = &s;
                }
                Check(problems, s.skinType.empty(), fmt::format("gnd: '{}' is skinned", s.name));
            }
            Check(problems, frame != nullptr, "gnd: no frame");
            if (frame) {
                Check(problems, frame->type == "BSTriShape" && frame->shader == "BSLightingShaderProperty",
                    fmt::format("gnd: frame '{}' is {} with {}", frame->name, frame->type, frame->shader));
                const std::string diffuse = fmt::format("[{}] {}", kSlotDiffuse, kLanternDiffuse);
                Check(problems, !frame->textures.empty() && frame->textures.front() == diffuse,
                    fmt::format("gnd: diffuse is '{}', expected '{}'", frame->textures.empty() ? "" : frame->textures.front(), diffuse));
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
                    Check(problems, frame->textures.size() > 1 && frame->textures[1] == normal,
                        fmt::format("gnd: normal map is '{}', the source's '{}'", frame->textures.size() > 1 ? frame->textures[1] : "", normal));
                }
            }
            Check(problems, gnd.hasCollision, "gnd: the collision is gone");
            // the torch fire node: one BSValueNode under the root, on the cage axis at the flame base, scaled
            {
                const std::string name = fmt::format("AddOnNode{}", kLanternTorchAddon);
                const auto* node = gnd.valueNodes.size() == 1 ? &gnd.valueNodes.front() : nullptr;
                Check(problems, node && node->name == name && node->value == kLanternTorchAddon && node->flags == kLanternTorchValueFlags &&
                                    node->parent == gnd.rootName,
                    fmt::format("gnd: {} addon nodes, expected one BSValueNode {} value {} flags {} under the root '{}'{}", gnd.valueNodes.size(),
                        name, kLanternTorchAddon, kLanternTorchValueFlags, gnd.rootName,
                        node ? fmt::format(" (read '{}' value {} flags {} under '{}')", node->name, node->value, node->flags, node->parent) :
                               std::string()));
                Check(problems, node && std::fabs(node->scale - kLanternTorchFireScale) <= 1e-6f,
                    fmt::format("gnd: {} scale {}, expected {} (kLanternTorchFireScale)", name, node ? node->scale : 0.0f, kLanternTorchFireScale));
                // its place: the cage axis from the frame's wall vertices, the base from its bounds (read back in root space)
                nifly::NifFile file;
                LoadNif(file, a_dataDir / kLanternGroundNif);
                auto* shape = GroundFrame(file);
                std::vector<Vector3> verts;
                if (node && shape && file.GetVertsForShape(shape, verts) && !verts.empty()) {
                    const MatTransform toRoot = ToRootSpace(file, file.GetHeader().GetBlockID(shape));
                    for (auto& v : verts) {
                        v = toRoot.ApplyTransform(v);
                    }
                    LanternBox bounds = EmptyBox();
                    for (const auto& v : verts) {
                        Grow(bounds, v);
                    }
                    LanternBox wallBox = CageWallsOf(verts, bounds);
                    if (IsEmpty(wallBox)) {
                        wallBox = bounds;  // as the build: no vertex in the band, the whole bounds' centre
                    }
                    const auto walls = wallBox.Centre();
                    const Vector3 expected = TorchBaseOf(bounds, { walls[0], walls[1], 0.0f });
                    constexpr float kPlaceTolerance = 1e-3f;
                    Check(problems, std::fabs(node->translation[0] - expected.x) <= kPlaceTolerance &&
                                        std::fabs(node->translation[1] - expected.y) <= kPlaceTolerance &&
                                        std::fabs(node->translation[2] - expected.z) <= kPlaceTolerance,
                        fmt::format("gnd: {} at {}, expected {} (the cage axis, TorchBaseOf: the frame's bottom + kLanternTorchBaseShare x its height, lowered for the scale)", name,
                            Text(node->translation), Text(ToArray(expected))));
                } else {
                    Check(problems, false, "gnd: the frame's vertices do not read for the torch fire node's place");
                }
            }
            for (const auto& problem : TorchFireWrong(a_dataDir / kLanternTorchNif)) {
                problems.push_back(problem);
            }
            Check(problems, gnd.nodeNames.size() == 2,
                fmt::format("gnd: {} nodes, expected the root and the torch fire node alone (no source bones)", gnd.nodeNames.size()));
            Check(problems, gnd.hasBsx && (gnd.bsx & kBsxAnimated) != 0 && (gnd.bsx & kBsxAddon) != 0,
                fmt::format("gnd: BSXFlags {}, expected the Animated and Addon bits", gnd.bsx));

            // the worn model
            const auto worn = InspectLanternNif(a_dataDir / kLanternWornNif);
            Check(problems, !worn.hasCollision, "worn: has a collision object");
            Check(problems, worn.valueNodes.empty(), "worn: has an addon node (the flame is the flame layers)");
            Check(problems, worn.sseCompatible, "worn: nifly says it is not SSE-compatible");
            std::vector<std::string> expected{ kLanternWornFrameName };
            for (const auto& layer : kLanternFlameLayers) {
                expected.emplace_back(layer.name);
            }
            expected.push_back(kLanternRingName);
            expected.push_back(kLanternLugName);
            const auto slot = [](const LanternShapeInfo* a_s, std::size_t a_i) {
                return a_s && a_s->textures.size() > a_i ? a_s->textures[a_i] : std::string();
            };
            {
                const auto* wornFrame = shapeIn(worn, kLanternWornFrameName);
                const auto* ring = shapeIn(worn, kLanternRingName);
                const auto* lugs = shapeIn(worn, kLanternLugName);
                const std::string soot = fmt::format("[{}] {}", kSlotDiffuse, kLanternDiffuse);
                for (const auto& [what, iron] : { std::pair<const char*, const LanternShapeInfo*>{ "ring", ring },
                         std::pair<const char*, const LanternShapeInfo*>{ "lugs", lugs } }) {
                    Check(problems, slot(iron, 0) == soot && !slot(wornFrame, 1).empty() && slot(iron, 1) == slot(wornFrame, 1),
                        fmt::format("worn {}: textures '{}' / '{}', expected '{}' / the frame's normal map '{}'", what, slot(iron, 0),
                            slot(iron, 1), soot, slot(wornFrame, 1)));
                }
                constexpr float kUvTolerance = 1e-3f;  // half-float UVs
                Check(problems, ring && ring->uvMin[0] >= kLanternIronUvMin[0] - kUvTolerance && ring->uvMin[1] >= kLanternIronUvMin[1] - kUvTolerance &&
                                    ring->uvMax[0] <= kLanternIronUvMax[0] + kUvTolerance && ring->uvMax[1] <= kLanternIronUvMax[1] + kUvTolerance &&
                                    ring->uvMin[0] <= ring->uvMax[0],
                    fmt::format("worn ring: UVs u {}..{} v {}..{} outside the iron box u {}..{} v {}..{}", ring ? ring->uvMin[0] : 0.0f,
                        ring ? ring->uvMax[0] : 0.0f, ring ? ring->uvMin[1] : 0.0f, ring ? ring->uvMax[1] : 0.0f, kLanternIronUvMin[0],
                        kLanternIronUvMax[0], kLanternIronUvMin[1], kLanternIronUvMax[1]));
            }
            checkFlame(worn, "worn", true);
            Check(problems, worn.shapes.size() == expected.size(),
                fmt::format("worn: {} shapes, expected {}", worn.shapes.size(), expected.size()));
            for (const auto& name : expected) {
                Check(problems, shapeIn(worn, name) != nullptr, fmt::format("worn: no shape '{}'", name));
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
                Check(problems, (s.flags2 & kSlsf2VertexColors) == 0 || s.vertexColors,
                    fmt::format("worn '{}': SLSF2 Vertex_Colors (0x{:08X}) on a shape without vertex colours", s.name, s.flags2));
                Check(problems, s.skinRoot == s.fileRoot,
                    fmt::format("worn '{}': the skin instance's skeleton root is block {}, not the file's root {}", s.name, s.skinRoot,
                        s.fileRoot));
                Check(problems, s.bindRead && s.bindError <= kLanternBindTolerance,
                    fmt::format("worn '{}': bind pose x skin-to-bone is {:.2e} off the identity (read {}; tolerance {})", s.name, s.bindError,
                        s.bindRead, kLanternBindTolerance));
                Check(problems, s.bindRead && s.sphereError <= kLanternBoundsTolerance && s.vertexError <= kLanternBoundsTolerance,
                    fmt::format("worn '{}': through the bind pose the bone-space bounds land {:.2e} and the vertices {:.2e} off the "
                                "shape's (tolerance {})", s.name, s.sphereError, s.vertexError, kLanternBoundsTolerance));
            }

            // the worn fire's clearance to the frame
            {
                const auto flame = FlameReadBack(a_dataDir);
                Check(problems, flame.wornClearance >= kLanternFlameClearance,
                    fmt::format("the flame comes within {:.4f} (worn) of the frame (kLanternFlameClearance {})", flame.wornClearance,
                        kLanternFlameClearance));
            }
            // the worn parts against the bodies (each body an --in folder has)
            for (const auto& c : WornClearances(a_inputs, a_dataDir)) {
                Check(problems, c.gap.points > 0 && c.gap.inside == 0 && c.gap.nearest >= c.margin,
                    fmt::format("worn {} / {}: {} (margin {})", c.part, c.body, GapText(c.gap), c.margin));
            }
            // the lantern hangs vertically
            const float tilt = WornAxisTiltDeg(a_dataDir);
            Check(problems, tilt <= kLanternVerticalToleranceDeg,
                fmt::format("worn: the lantern's cage axis leans {:.4f} deg off world Z (kLanternVerticalToleranceDeg {})", tilt,
                    kLanternVerticalToleranceDeg));
            // the handle's height, and its feet on the lid through the lugs
            {
                constexpr float kHeightTolerance = 1e-3f;
                const auto handle = WornHandle(a_dataDir);
                Check(problems, std::fabs(handle.height - kLanternHandleHeight) <= kHeightTolerance,
                    fmt::format("worn: the handle is {:.4f} tall, expected {} (kLanternHandleHeight)", handle.height, kLanternHandleHeight));
                for (const auto& foot : handle.feet) {
                    const float contact = std::max(foot.lug.min[2] - foot.lidZ, foot.footZ - foot.lug.max[2]);
                    const float height = foot.lug.Size(2);
                    Check(problems, contact <= kLanternLugContact,
                        fmt::format("worn: the lug under the handle foot at {} leaves a gap of {:.4f} (lug z {:.4f}..{:.4f}, lid z {:.4f}, foot z "
                                    "{:.4f}; kLanternLugContact {})", Text(foot.centre), contact, foot.lug.min[2], foot.lug.max[2], foot.lidZ,
                            foot.footZ, kLanternLugContact));
                    Check(problems, height >= kLanternLugMinHeight - kHeightTolerance && height <= kLanternLugMaxHeight + kHeightTolerance,
                        fmt::format("worn: the lug under the handle foot at {} is {:.4f} tall (kLanternLugMinHeight {} .. MaxHeight {})",
                            Text(foot.centre), height, kLanternLugMinHeight, kLanternLugMaxHeight));
                }
            }
            // the ring at the highest belt band top, kLanternRingY forward of the pelvis (the bodies and the
            // skeleton an --in folder has)
            {
                const auto skeletonFile = FindSource(a_inputs, kLanternSkeletonSource);
                std::vector<LanternBand> bands;
                nifly::NifFile skeleton;
                MatTransform pelvis;
                if (skeletonFile) {
                    LoadNif(skeleton, *skeletonFile);
                }
                const bool pelvisRead = skeletonFile && skeleton.GetNodeTransformToGlobal(kLanternBone, pelvis);
                if (pelvisRead) {
                    for (std::size_t b = 0; b < kLanternBodySources.size(); ++b) {
                        if (const auto file = FindSource(a_inputs, kLanternBodySources[b])) {
                            bands.push_back(MeasureLanternBand(*file, kLanternBandShapes[b], pelvis.translation));
                        }
                    }
                }
                const auto* ring = shapeIn(worn, kLanternRingName);
                if (!bands.empty() && ring) {
                    float bandTop = -kFloatMax;
                    for (const auto& band : bands) {
                        bandTop = std::max(bandTop, band.top);
                    }
                    constexpr float kRingTolerance = 1e-3f;
                    const auto centre = ring->bounds.Centre();
                    const float forward = centre[1] - pelvis.translation.y;
                    Check(problems, std::fabs(centre[2] - bandTop) <= kRingTolerance,
                        fmt::format("worn ring: centre z {:.4f}, expected the highest belt band top {:.4f}", centre[2], bandTop));
                    Check(problems, std::fabs(forward - kLanternRingY) <= kRingTolerance,
                        fmt::format("worn ring: {:.4f} forward of the pelvis, expected {} (kLanternRingY)", forward, kLanternRingY));
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

            // the empty model: one block, the root BSFadeNode, named
            const auto empty = InspectLanternNif(a_dataDir / kLanternEmptyNif);
            std::string emptyBlocks;
            for (const auto& type : empty.blockTypes) {
                emptyBlocks += emptyBlocks.empty() ? type : ", " + type;
            }
            Check(problems, empty.sseCompatible && empty.rootType == "BSFadeNode" && empty.rootName == kLanternEmptyRootName,
                fmt::format("empty: root {} '{}' (SSE {}), expected an SSE BSFadeNode '{}'", empty.rootType, empty.rootName, empty.sseCompatible,
                    kLanternEmptyRootName));
            Check(problems, empty.blockTypes.size() == 1 && empty.blockTypes.front() == "BSFadeNode x1" && empty.shapes.empty() && !empty.hasBsx &&
                                !empty.hasCollision && empty.unknownBlocks == 0,
                fmt::format("empty: blocks [{}], {} shapes, BSXFlags {}, expected the root BSFadeNode alone", emptyBlocks, empty.shapes.size(),
                    empty.hasBsx));

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
        for (const char* rel : { kLanternGroundNif, kLanternWornNif, kLanternSmokeNif, kLanternEmptyNif }) {
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
        std::cout << "\nworn (build):\n";
        for (const auto& line : result.summary) {
            std::cout << "  " << line << "\n";
        }
        {
            for (const auto& node : InspectLanternNif(a_dataDir / kLanternGroundNif).valueNodes) {
                const float lowered = kLanternTorchEmitterRadius * (kLanternTorchRound11Scale - kLanternTorchFireScale);
                std::cout << fmt::format("ground fire (read back): '{}' value {} flags {} at {} scale {} under '{}'; expected z {:.4f} = the "
                                         "frame's bottom {:.4f} + {:.4f} x {:.4f} - {:.4f}, scale {} (kLanternTorchFireScale)\n",
                    node.name, node.value, node.flags, Text(node.translation), node.scale, node.parent,
                    b.min[2] + kLanternTorchBaseShare * b.Size(2) - lowered, b.min[2], kLanternTorchBaseShare, b.Size(2), lowered,
                    kLanternTorchFireScale);
            }
            const auto flame = FlameReadBack(a_dataDir);
            for (std::size_t l = 0; l < kLanternFlameLayers.size(); ++l) {
                std::cout << fmt::format("worn flame '{}' (read back): {}\n", kLanternFlameLayers[l].name, BoxText(flame.worn[l]));
            }
            std::cout << fmt::format("worn flame (read back): the layers' nearest frame triangle {:.4f} (kLanternFlameClearance {})\n",
                flame.wornClearance, kLanternFlameClearance);
            for (const char* rel : { kLanternWornNif }) {
                for (const auto& s : InspectLanternNif(a_dataDir / rel).shapes) {
                    if (IsFlameLayer(s.name)) {
                        std::cout << fmt::format("{} {} (read back): {} SLSF1 0x{:08X} SLSF2 0x{:08X}, alpha 0x{:04X} ({}), base alpha {:.4f}, "
                                                 "base colour scale {:.4f}, falloff start angle {:.4f} opacity {:.4f} stop angle {:.4f} opacity "
                                                 "{:.4f}, UV scale {:.4f} x {:.4f} (clamp mode 0x{:08X}), {}, vertex alpha {}..{} (tip {}, bottom "
                                                 "{}), jag {:.4f}\n",
                            rel, s.name, s.shader, s.flags1, s.flags2, s.alphaFlags, s.additive ? "additive" : "normal blend", s.baseAlpha,
                            s.colourScale, s.falloff[0], s.falloff[2], s.falloff[1], s.falloff[3], s.uvScale[0], s.uvScale[1], s.clampMode,
                            s.textures.size() > 1 ? s.textures[1] : std::string("no palette"), s.alphaMin, s.alphaMax, s.alphaTop, s.alphaBottom,
                            s.alphaJag);
                    }
                }
            }
        }
        {
            const auto worn = InspectLanternNif(a_dataDir / kLanternWornNif);
            for (const auto& s : worn.shapes) {
                if (s.name == kLanternRingName) {
                    std::cout << fmt::format("worn ring (read back): centre {}, {:.4f} forward of the pelvis; UVs u {:.4f}..{:.4f} v {:.4f}..{:.4f}\n",
                        Text(s.bounds.Centre()), s.bounds.Centre()[1] - result.pelvis[1], s.uvMin[0], s.uvMax[0], s.uvMin[1], s.uvMax[1]);
                    for (const auto& band : result.bands) {
                        std::cout << fmt::format("belt band (measured) {} '{}': top z {:.4f}, bottom z {:.4f}; the ring centre {:.4f} above its top\n",
                            band.body, band.shape, band.top, band.bottom, s.bounds.Centre()[2] - band.top);
                    }
                }
                if (s.name == kLanternLugName) {
                    std::cout << fmt::format("worn lugs (read back): bounds {}; UVs u {:.4f}..{:.4f} v {:.4f}..{:.4f}\n", BoxText(s.bounds),
                        s.uvMin[0], s.uvMax[0], s.uvMin[1], s.uvMax[1]);
                }
            }
        }
        {
            const auto handle = WornHandle(a_dataDir);
            std::cout << fmt::format("worn handle (read back): {:.4f} tall (kLanternHandleHeight {})\n", handle.height, kLanternHandleHeight);
            for (const auto& foot : handle.feet) {
                std::cout << fmt::format("  foot at {}: lowest z {:.4f}, lid z {:.4f} below it, foot gap {:.4f}; lug z {:.4f}..{:.4f} ({:.4f} tall), "
                                         "lug to lid {:.4f}, lug to foot {:.4f} (<= {} is contact); lug {}\n",
                    Text(foot.centre), foot.footZ, foot.lidZ, foot.footZ - foot.lidZ, foot.lug.min[2], foot.lug.max[2], foot.lug.Size(2),
                    foot.lug.min[2] - foot.lidZ, foot.footZ - foot.lug.max[2], kLanternLugContact, BoxText(foot.lug));
            }
        }
        std::cout << fmt::format("worn lantern axis (read back, LanternAxisTiltDeg): {:.4f} deg off world Z (tolerance {})\n",
            WornAxisTiltDeg(a_dataDir), kLanternVerticalToleranceDeg);
        std::cout << "worn clearances (read back):\n";
        for (const auto& c : WornClearances(a_inputs, a_dataDir)) {
            std::cout << fmt::format("  {} / {}: {} (margin {})\n", c.part, c.body, GapText(c.gap), c.margin);
        }
        std::cout << "lantern: 4 NIFs and 1 texture written and read back\n";
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

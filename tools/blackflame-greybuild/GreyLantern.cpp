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
                if (auto* lit = dynamic_cast<nifly::BSLightingShaderProperty*>(shader)) {
                    s.refraction = lit->refractionStrength;
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
            if (s.refraction >= 0.0f) {
                std::cout << fmt::format("    refraction strength {:.4f}\n", s.refraction);
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

        /// A triangle list made in the builder (the strap and the ring)
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

        /// The smallest X of every body's surface in the hip window at a_z (strap Y band, z +- a_halfZ)
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

        struct LanternStrap
        {
            LanternMesh mesh;
            float outerBottomX = 0.0f;  // the outer layer's centreline X at kLanternStrapBottom
            float y = 0.0f;             // the strap's centre Y
            float length = 0.0f;        // the centreline's length
            std::vector<std::string> log;
        };

        /// The strap's centreline on the bodies, extruded (GreyLantern.h, the strap constants)
        [[nodiscard]] LanternStrap MakeStrap(const std::vector<LanternSurface>& a_bodies, const Vector3& a_pelvis)
        {
            LanternStrap strap;
            strap.y = a_pelvis.y + kLanternStrapY;
            const float halfY = kLanternStrapWidth * 0.5f + kLanternStrapMargin;
            const float t = kLanternStrapThickness;
            const float innerOff = kLanternStrapBodyGap + t * 0.5f;          // body to the inner layer's centreline
            const float between = t + kLanternStrapBeltGap;                   // centreline to centreline
            const float foldRadius = between * 0.5f;
            const float foldZ = kLanternStrapTop - foldRadius - t * 0.5f;      // the fold's centre: its outer face tops out there
            if (!(kLanternStrapInnerBottom < foldZ && kLanternStrapBottom < foldZ)) {
                throw std::runtime_error(fmt::format("the strap's fold centre z {:.3f} is not above kLanternStrapInnerBottom {} and "
                                                     "kLanternStrapBottom {}", foldZ, kLanternStrapInnerBottom, kLanternStrapBottom));
            }
            const auto left = [&](float a_z) {
                return HipLeft(a_bodies, a_pelvis, strap.y, halfY, a_z - kLanternStrapStep, a_z + kLanternStrapStep);
            };

            std::vector<Vector3> path;
            // the inner layer, upwards on the body
            std::vector<float> innerZ;
            for (float z = kLanternStrapInnerBottom; z < foldZ; z += kLanternStrapStep) {
                innerZ.push_back(z);
            }
            innerZ.push_back(foldZ);
            for (const float z : innerZ) {
                path.emplace_back(left(z) - innerOff, strap.y, z);
            }
            const float innerTopX = path.back().x;
            // the outer layer, downwards, draping: never back in towards the body below a wider part
            std::vector<Vector3> outer;
            float drape = innerTopX - between;
            for (float z = foldZ; z > kLanternStrapBottom; z -= kLanternStrapStep) {
                drape = std::min(drape, left(z) - innerOff - between);
                outer.emplace_back(drape, strap.y, z);
            }
            drape = std::min(drape, left(kLanternStrapBottom) - innerOff - between);
            outer.emplace_back(drape, strap.y, kLanternStrapBottom);
            strap.outerBottomX = drape;
            // the fold: a half circle from the inner layer's top over to the outer layer's top
            const float outerTopX = outer.front().x;
            const float cx = (innerTopX + outerTopX) * 0.5f;
            const float r = (innerTopX - outerTopX) * 0.5f;
            for (int i = 1; i < kLanternStrapFoldSegments; ++i) {
                const float a = kPi * static_cast<float>(i) / static_cast<float>(kLanternStrapFoldSegments);
                path.emplace_back(cx + r * std::cos(a), strap.y, foldZ + r * std::sin(a));
            }
            path.insert(path.end(), outer.begin(), outer.end());
            strap.log.push_back(fmt::format("strap: centre y {:.3f} (pelvis + kLanternStrapY {}), inner layer z {}..{:.3f} at {} off the "
                                            "body (kLanternStrapBodyGap), fold radius {:.3f} topping out at z {} (kLanternStrapTop), outer "
                                            "layer {} further out (kLanternStrapBeltGap) down to z {} (kLanternStrapBottom); inner top x "
                                            "{:.3f}, outer top x {:.3f}, outer bottom x {:.3f}",
                strap.y, kLanternStrapY, kLanternStrapInnerBottom, foldZ, kLanternStrapBodyGap, r, kLanternStrapTop, kLanternStrapBeltGap,
                kLanternStrapBottom, innerTopX, outerTopX, strap.outerBottomX));

            // tangents, the side normal in the XZ plane (pointing out of the fold's U), arc length
            const std::size_t count = path.size();
            std::vector<Vector3> side(count);
            std::vector<float> arc(count, 0.0f);
            for (std::size_t k = 0; k < count; ++k) {
                Vector3 tangent = path[std::min(k + 1, count - 1)] - path[k == 0 ? 0 : k - 1];
                tangent.Normalize();
                side[k] = Vector3(tangent.z, 0.0f, -tangent.x);
                if (k > 0) {
                    arc[k] = arc[k - 1] + path[k].DistanceTo(path[k - 1]);
                }
            }
            strap.length = arc.back();
            const Vector3 across(0.0f, 1.0f, 0.0f);
            const float halfW = kLanternStrapWidth * 0.5f;
            const float bandV = kLanternStrapBandV1 - kLanternStrapBandV0;
            auto& mesh = strap.mesh;
            // the two broad faces (out of the U and into it) and the two edges
            struct Face
            {
                float sideSign;    // which broad face (+1 out of the U, -1 into it), 0 for an edge
                float acrossSign;  // which edge, 0 for a broad face
            };
            for (const Face face : { Face{ 1.0f, 0.0f }, Face{ -1.0f, 0.0f }, Face{ 0.0f, 1.0f }, Face{ 0.0f, -1.0f } }) {
                std::vector<std::array<std::uint16_t, 2>> rows;
                for (std::size_t k = 0; k < count; ++k) {
                    const float u = arc[k] / kLanternStrapUvSpan;
                    std::array<std::uint16_t, 2> row{};
                    for (int e = 0; e < 2; ++e) {
                        const float sign = e == 0 ? -1.0f : 1.0f;
                        Vector3 at;
                        Vector3 normal;
                        float v = 0.0f;
                        if (face.sideSign != 0.0f) {
                            at = path[k] + side[k] * (face.sideSign * t * 0.5f) + across * (sign * halfW);
                            normal = side[k] * face.sideSign;
                            v = kLanternStrapBandV0 + bandV * (sign * 0.5f + 0.5f);
                        } else {
                            at = path[k] + side[k] * (sign * t * 0.5f) + across * (face.acrossSign * halfW);
                            normal = across * face.acrossSign;
                            v = kLanternStrapBandV0 + bandV * (t / kLanternStrapWidth) * (sign * 0.5f + 0.5f);
                        }
                        row[e] = mesh.Add(at, normal, u, v);
                    }
                    rows.push_back(row);
                }
                for (std::size_t k = 0; k + 1 < count; ++k) {
                    mesh.Quad(rows[k][0], rows[k + 1][0], rows[k + 1][1], rows[k][1]);
                }
            }
            // the two ends
            for (const std::size_t k : { std::size_t{ 0 }, count - 1 }) {
                Vector3 tangent = path[std::min(k + 1, count - 1)] - path[k == 0 ? 0 : k - 1];
                tangent.Normalize();
                const Vector3 normal = k == 0 ? tangent * -1.0f : tangent;
                const float u = arc[k] / kLanternStrapUvSpan;
                // round the cross-section: (-side, -across), (-side, +across), (+side, +across), (+side, -across)
                std::array<std::uint16_t, 4> corner{};
                constexpr std::array<std::array<float, 2>, 4> kCorners{ { { -1.0f, -1.0f }, { -1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, -1.0f } } };
                for (std::size_t c = 0; c < kCorners.size(); ++c) {
                    corner[c] = mesh.Add(path[k] + side[k] * (kCorners[c][0] * t * 0.5f) + across * (kCorners[c][1] * halfW), normal, u,
                        kLanternStrapBandV0 + bandV * (kCorners[c][1] * 0.5f + 0.5f));
                }
                mesh.Quad(corner[0], corner[1], corner[2], corner[3]);
            }
            return strap;
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

        /// a_shape cut down to a_piece's vertices and triangles (remapped); skinning must be gone already
        void KeepPiece(nifly::NiShape& a_shape, const LanternPiece& a_piece, const std::vector<nifly::Triangle>& a_tris)
        {
            auto* tri = dynamic_cast<nifly::BSTriShape*>(&a_shape);
            if (!tri) {
                throw std::runtime_error(fmt::format("shape '{}' is not a BSTriShape", a_shape.name.get()));
            }
            std::vector<int> remap(tri->vertData.size(), -1);
            std::vector<nifly::BSVertexData> kept;
            for (const auto v : a_piece.vertices) {
                remap[v] = static_cast<int>(kept.size());
                kept.push_back(tri->vertData[v]);
            }
            std::vector<nifly::Triangle> tris;
            for (const auto i : a_piece.triangles) {
                const auto& t = a_tris[i];
                tris.emplace_back(static_cast<std::uint16_t>(remap[t.p1]), static_cast<std::uint16_t>(remap[t.p2]),
                    static_cast<std::uint16_t>(remap[t.p3]));
            }
            tri->SetVertexData(kept);
            tri->SetTriangles(tris);
        }

        /// The volume both boxes share (0 when apart)
        [[nodiscard]] float Overlap(const LanternBox& a_a, const LanternBox& a_b) noexcept
        {
            float volume = 1.0f;
            for (int axis = 0; axis < 3; ++axis) {
                volume *= std::max(0.0f, std::min(a_a.max[axis], a_b.max[axis]) - std::max(a_a.min[axis], a_b.min[axis]));
            }
            return volume;
        }

        [[nodiscard]] std::string GapText(const LanternGap& a_gap)
        {
            return fmt::format("{} points, {} inside, nearest {:.3f} at {}", a_gap.points, a_gap.inside, a_gap.nearest,
                Text(a_gap.nearestPoint));
        }

        /// Scales every float key of a lighting shader controller that animates the refraction strength (nif.xml
        /// LightingShaderControlledFloat 0) by a_ratio; one log line per controller
        void ScaleRefractionKeys(nifly::NifFile& a_nif, nifly::NiShader& a_shader, float a_ratio, std::vector<std::string>& a_log)
        {
            constexpr std::uint32_t kRefractionStrength = 0;
            auto& hdr = a_nif.GetHeader();
            std::set<std::uint32_t> seen;
            for (std::uint32_t id = a_shader.controllerRef.index; id != nifly::NIF_NPOS;) {
                auto* controller = hdr.GetBlock<nifly::NiTimeController>(id);
                if (!controller || !seen.insert(id).second) {
                    break;
                }
                if (auto* lit = dynamic_cast<nifly::BSLightingShaderPropertyFloatController*>(controller)) {
                    std::string what = fmt::format("controls variable {}", lit->typeOfControlledVariable);
                    if (lit->typeOfControlledVariable == kRefractionStrength) {
                        auto* interp = hdr.GetBlock<nifly::NiFloatInterpolator>(lit->interpolatorRef);
                        auto* data = interp ? hdr.GetBlock<nifly::NiFloatData>(interp->dataRef) : nullptr;
                        if (interp) {
                            interp->floatValue *= a_ratio;
                        }
                        if (data) {
                            for (std::uint32_t k = 0; k < data->data.GetNumKeys(); ++k) {
                                auto key = data->data.GetKey(static_cast<int>(k));
                                key.value *= a_ratio;
                                key.forward *= a_ratio;
                                key.backward *= a_ratio;
                                data->data.SetKey(static_cast<int>(k), key);
                            }
                        }
                        what += fmt::format(" (refraction strength): its keys x {:.4f}", a_ratio);
                    } else {
                        what += " (not the refraction strength): left";
                    }
                    a_log.push_back(fmt::format("FireStripWarp controller {}", what));
                }
                id = controller->nextControllerRef.index;
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
            for (const char* body : kLanternBodySources) {
                bodies.push_back(LoadLanternSurface(Need(a_inputs, body)));
                log.push_back(fmt::format("body {}: {} triangles, winding {}", body, bodies.back().tris.size(),
                    bodies.back().outward > 0.0f ? "outward" : "inward"));
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
            const auto rawUvs = frameTri->UpdateRawUvs();
            nifly::Vector2 uvMin(kFloatMax, kFloatMax);
            nifly::Vector2 uvMax(-kFloatMax, -kFloatMax);
            for (const auto v : handle.vertices) {
                uvMin.u = std::min(uvMin.u, rawUvs[v].u);
                uvMin.v = std::min(uvMin.v, rawUvs[v].v);
                uvMax.u = std::max(uvMax.u, rawUvs[v].u);
                uvMax.v = std::max(uvMax.v, rawUvs[v].v);
            }
            log.push_back(fmt::format("handle: frame piece {} of {} ({} triangles, {} vertices) {}; hinge y {:.4f} z {:.4f}, turned "
                                      "{} deg about X (kLanternHandleTurnDeg); its UVs {:.4f},{:.4f}..{:.4f},{:.4f} (the ring takes them)",
                handles.front(), pieces.size(), handle.triangles.size(), handle.vertices.size(), BoxText(handle.bounds), hinge.y, hinge.z,
                kLanternHandleTurnDeg, uvMin.u, uvMin.v, uvMax.u, uvMax.v));

            // the lantern's own transform: handle turn, yaw round the cage axis, scale (contact and tilt come after)
            const float scale = kLanternWornHeight / a_result.frameBounds.Size(2);
            a_result.wornScale = scale;
            const Vector3 axis(a_result.cageCentre[0], a_result.cageCentre[1], 0.0f);
            const auto shapeA = [&](Vector3 a_p, bool a_handle) {
                if (a_handle) {
                    a_p = hinge + TurnX(a_p - hinge, kLanternHandleTurnDeg);
                }
                return (axis + TurnZ(a_p - axis, kLanternWornYawDeg)) * scale;
            };
            const auto normalA = [&](Vector3 a_n, bool a_handle) {
                if (a_handle) {
                    a_n = TurnX(a_n, kLanternHandleTurnDeg);
                }
                return TurnZ(a_n, kLanternWornYawDeg);
            };
            const float wire = handle.bounds.Size(2) * 0.5f * scale;
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
            log.push_back(fmt::format("worn: pelvis '{}' at {} (skeleton.nif); frame height {:.4f} -> {} (kLanternWornHeight), scale "
                                      "{:.5f}; yaw {} deg (kLanternWornYawDeg); handle wire radius {:.4f}, its top wire centre (scaled "
                                      "lantern space, {} vertices near the top) {}",
                kLanternBone, Text(a_result.pelvis), a_result.frameBounds.Size(2), kLanternWornHeight, scale, kLanternWornYawDeg, wire,
                apexCount, Text(ToArray(apex))));

            // the strap on the bodies, the ring in its lower end
            auto strap = MakeStrap(bodies, pelvis.translation);
            log.insert(log.end(), strap.log.begin(), strap.log.end());
            const float ringZ = kLanternStrapBottom + kLanternRingTube - kLanternRingRadius;
            const float reach = kLanternRingRadius + kLanternRingTube;
            const float ringLeft = HipLeft(bodies, pelvis.translation, strap.y, kLanternRingTube + kLanternStrapMargin, ringZ - reach,
                ringZ + reach);
            const float ringX = std::min(strap.outerBottomX, ringLeft - kLanternStrapBodyGap - reach);
            const Vector3 ring(ringX, strap.y, ringZ);
            a_result.ringCentre = ToArray(ring);
            log.push_back(fmt::format("ring: centre {} (radius {}, tube {}); its top tube centre at z {:.4f}, the strap's lower end z {}; "
                                      "ring x - the strap's outer bottom x = {:.4f}; the body's left at the ring {:.4f}",
                Text(a_result.ringCentre), kLanternRingRadius, kLanternRingTube, ringZ + kLanternRingRadius, kLanternStrapBottom,
                ringX - strap.outerBottomX, ringLeft));
            const auto ringMesh = MakeRing(ring, uvMin, uvMax);
            const Vector3 contact = ring - Vector3(0.0f, 0.0f, kLanternRingRadius - kLanternRingTube - wire);
            a_result.contact = ToArray(contact);
            a_result.handleWire = wire;
            const Vector3 move = contact - apex;

            // the strap and the ring against the bodies
            for (const auto& [part, points] : { std::pair<const char*, const std::vector<Vector3>*>{ "strap", &strap.mesh.verts },
                     std::pair<const char*, const std::vector<Vector3>*>{ "ring", &ringMesh.verts } }) {
                for (const auto& body : bodies) {
                    const auto gap = SurfaceGap(body, *points);
                    log.push_back(fmt::format("clearance {} / {}: {}", part, body.name, GapText(gap)));
                    if (gap.inside > 0 || gap.nearest < kLanternStrapMargin) {
                        throw std::runtime_error(fmt::format("the {} comes within {:.3f} of {} ({} points inside; kLanternStrapMargin {})",
                            part, gap.nearest, body.name, gap.inside, kLanternStrapMargin));
                    }
                }
            }

            // the flame: one connected piece of each strip at rest, both into the cage under one transform
            nifly::NifFile flame;
            LoadNif(flame, Need(a_inputs, kLanternFlameSource));
            struct Strip
            {
                nifly::NiShape* shape = nullptr;
                MatTransform toGlobal;
                std::vector<nifly::Triangle> tris;
                std::vector<LanternPiece> pieces;
                std::size_t chosen = 0;
            };
            std::array<Strip, 2> strips;
            for (std::size_t s = 0; s < kLanternFlameStrips.size(); ++s) {
                const char* name = kLanternFlameStrips[s];
                auto& strip = strips[s];
                strip.shape = FindShape(flame, name);
                auto* tri = strip.shape ? dynamic_cast<nifly::BSTriShape*>(strip.shape) : nullptr;
                if (!tri || !strip.shape->HasSkinInstance()) {
                    throw std::runtime_error(fmt::format("{} has no skinned BSTriShape '{}'", kLanternFlameSource, name));
                }
                MatTransform globalToSkin;
                if (!flame.CalcShapeTransformGlobalToSkin(strip.shape, globalToSkin)) {
                    throw std::runtime_error(fmt::format("'{}': no global-to-skin transform", name));
                }
                strip.toGlobal = globalToSkin.InverseTransform();
                strip.tris = tri->triangles;
                std::vector<Vector3> rest;
                for (const auto& vertex : tri->vertData) {
                    rest.push_back(strip.toGlobal.ApplyTransform(vertex.vert));
                }
                strip.pieces = LanternPieces(rest, strip.tris);
                if (strip.pieces.empty()) {
                    throw std::runtime_error(fmt::format("'{}' has no triangles", name));
                }
                if (s == 0) {
                    // the most triangles, then the tallest, then the first
                    for (std::size_t p = 1; p < strip.pieces.size(); ++p) {
                        const auto& a = strip.pieces[p];
                        const auto& b = strip.pieces[strip.chosen];
                        if (a.triangles.size() > b.triangles.size() ||
                            (a.triangles.size() == b.triangles.size() && a.bounds.Size(2) > b.bounds.Size(2) + kLanternWeld)) {
                            strip.chosen = p;
                        }
                    }
                } else {
                    // the warp piece over the chosen flame piece
                    const auto& fire = strips[0].pieces[strips[0].chosen].bounds;
                    for (std::size_t p = 1; p < strip.pieces.size(); ++p) {
                        if (Overlap(strip.pieces[p].bounds, fire) > Overlap(strip.pieces[strip.chosen].bounds, fire)) {
                            strip.chosen = p;
                        }
                    }
                    if (Overlap(strip.pieces[strip.chosen].bounds, fire) <= 0.0f) {
                        throw std::runtime_error("no FireStripWarp piece overlaps the chosen FireStrip08 piece");
                    }
                }
                const auto& piece = strip.pieces[strip.chosen];
                log.push_back(fmt::format("flame strip '{}': {} vertices, {} triangles in {} connected pieces; piece {} kept ({} triangles, "
                                          "{} vertices), rest-pose {}",
                    name, rest.size(), strip.tris.size(), strip.pieces.size(), strip.chosen, piece.triangles.size(), piece.vertices.size(),
                    BoxText(piece.bounds)));
            }
            LanternBox flameBox = strips[0].pieces[strips[0].chosen].bounds;
            Grow(flameBox, strips[1].pieces[strips[1].chosen].bounds);
            const std::array<float, 3> cageSize{ a_walls.Size(0), a_walls.Size(1), a_result.frameBounds.Size(2) };
            float fit = kFloatMax;
            for (int a = 0; a < 3; ++a) {
                if (flameBox.Size(a) > 0.0f) {
                    fit = std::min(fit, kLanternFlameFill * cageSize[a] / flameBox.Size(a));
                }
            }
            const Vector3 flameCentre = ToVector(flameBox.Centre());
            const Vector3 cage = ToVector(a_result.cageCentre);
            const auto flameRoot = [&](const Strip& a_strip, const Vector3& a_skin) {
                return cage + (a_strip.toGlobal.ApplyTransform(a_skin) - flameCentre) * fit;
            };
            log.push_back(fmt::format("flame pieces together {}, scaled x {:.5f} (kLanternFlameFill {} of the cage's {:.4f} x {:.4f} x "
                                      "{:.4f}) onto the cage centre",
                BoxText(flameBox), fit, kLanternFlameFill, cageSize[0], cageSize[1], cageSize[2]));

            // the lantern's points before the tilt, then the smallest tilt that clears both bodies. The frame turns about
            // the contact; the flame only moves with the cage centre and stays upright (a flame rises straight up)
            const Vector3 cageA = shapeA(cage, false) + move;
            const auto cageAt = [&](float a_deg) { return contact + TurnY(cageA - contact, a_deg); };
            std::vector<Vector3> framePoints;
            for (std::size_t i = 0; i < frameRoot.size(); ++i) {
                framePoints.push_back(shapeA(frameRoot[i], isHandle[i]) + move);
            }
            std::vector<Vector3> flamePoints;
            for (const auto& strip : strips) {
                auto* tri = dynamic_cast<nifly::BSTriShape*>(strip.shape);
                for (const auto v : strip.pieces[strip.chosen].vertices) {
                    flamePoints.push_back(shapeA(flameRoot(strip, tri->vertData[v].vert), false) + move);
                }
            }
            float tilt = -1.0f;
            for (float deg = 0.0f; deg <= kLanternTiltMaxDeg + 1e-3f; deg += kLanternTiltStepDeg) {
                std::vector<Vector3> tilted;
                tilted.reserve(framePoints.size() + flamePoints.size());
                for (const auto& p : framePoints) {
                    tilted.push_back(contact + TurnY(p - contact, deg));
                }
                const Vector3 shift = cageAt(deg) - cageA;
                for (const auto& p : flamePoints) {
                    tilted.push_back(p + shift);
                }
                bool clear = true;
                std::string line = fmt::format("lantern tilt {} deg:", deg);
                for (const auto& body : bodies) {
                    const auto gap = SurfaceGap(body, tilted);
                    clear = clear && gap.inside == 0 && gap.nearest >= kLanternBodyMargin;
                    line += fmt::format(" {}: {};", body.name, GapText(gap));
                }
                if (deg == 0.0f || clear || std::fmod(deg, 5.0f) < 1e-3f) {
                    log.push_back(line);
                }
                if (clear) {
                    tilt = deg;
                    break;
                }
            }
            if (tilt < 0.0f) {
                throw std::runtime_error(fmt::format("the lantern hanging from the ring comes nearer than {} (kLanternBodyMargin) to a "
                                                     "body at every tilt up to {} deg (kLanternTiltMaxDeg)", kLanternBodyMargin,
                    kLanternTiltMaxDeg));
            }
            a_result.tiltDeg = tilt;
            const Vector3 flameShift = cageAt(tilt) - cageA;
            const auto place = [&](const Vector3& a_root, bool a_handle) {
                return contact + TurnY(shapeA(a_root, a_handle) + move - contact, tilt);
            };
            const auto placeNormal = [&](const Vector3& a_n, bool a_handle) { return TurnY(normalA(a_n, a_handle), tilt); };
            const auto placeFlame = [&](const Vector3& a_root) { return shapeA(a_root, false) + move + flameShift; };
            {
                // the upright flame still inside the tilted cage: every flame point turned back into the cage's own frame lies
                // within the cage walls' X/Y and the frame's height
                LanternBox inside = EmptyBox();
                for (const float x : { a_walls.min[0], a_walls.max[0] }) {
                    for (const float y : { a_walls.min[1], a_walls.max[1] }) {
                        for (const float z : { a_result.frameBounds.min[2], a_result.frameBounds.max[2] }) {
                            Grow(inside, shapeA(Vector3(x, y, z), false) + move);
                        }
                    }
                }
                LanternBox flameInCage = EmptyBox();
                for (const auto& p : flamePoints) {
                    Grow(flameInCage, contact + TurnY(p + flameShift - contact, -tilt));
                }
                bool within = true;
                for (int a = 0; a < 3; ++a) {
                    within = within && flameInCage.min[a] >= inside.min[a] && flameInCage.max[a] <= inside.max[a];
                }
                log.push_back(fmt::format("flame in the cage's own frame {}; the cage {}", BoxText(flameInCage), BoxText(inside)));
                if (!within) {
                    throw std::runtime_error("the upright flame reaches out of the tilted cage: lower kLanternFlameFill");
                }
            }
            a_result.wornCage = ToArray(cageAt(tilt));
            log.push_back(fmt::format("lantern: tilted {} deg about Y round the contact {} (bottom outwards); cage centre {}; the "
                                      "flame moved with it, upright",
                tilt, Text(a_result.contact), Text(a_result.wornCage)));

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

            // the flame pieces
            for (auto& strip : strips) {
                const std::string name = strip.shape->name.get();
                flame.DeleteSkinning(strip.shape);
                auto* tri = dynamic_cast<nifly::BSTriShape*>(strip.shape);
                if (tri->vertData.size() <= strip.pieces[strip.chosen].vertices.back()) {
                    throw std::runtime_error(fmt::format("'{}' lost vertices with its skinning", name));
                }
                KeepPiece(*strip.shape, strip.pieces[strip.chosen], strip.tris);
                const MatTransform toGlobal = strip.toGlobal;
                BakeEach(*strip.shape, [&](std::size_t, const Vector3& a_v) { return placeFlame(flameRoot(strip, a_v)); },
                    [&](std::size_t, const Vector3& a_n) { return normalA(toGlobal.rotation * a_n, false); });
                auto* clone = adopt(worn.CloneShape(strip.shape, name, &flame));
                for (const auto& line : RetargetShaderControllers(worn, *clone)) {
                    log.push_back(fmt::format("flame strip '{}': controller {}", name, line));
                }
                if (name == kLanternFlameStrips[1]) {
                    auto* lit = hdr.GetBlock<nifly::BSLightingShaderProperty>(clone->ShaderPropertyRef());
                    if (!lit) {
                        throw std::runtime_error("FireStripWarp has no lighting shader");
                    }
                    const float before = lit->refractionStrength;
                    lit->refractionStrength = kLanternWarpRefraction;
                    log.push_back(fmt::format("FireStripWarp refraction strength {:.4f} -> {} (kLanternWarpRefraction)", before,
                        kLanternWarpRefraction));
                    ScaleRefractionKeys(worn, *lit, before > 0.0f ? kLanternWarpRefraction / before : 1.0f, log);
                }
            }

            // the strap and the ring (made in character space already)
            for (const char* texture : { kLanternStrapDiffuse, kLanternStrapNormal }) {
                const auto found = FindSource(a_inputs, texture);
                log.push_back(fmt::format("strap texture {} (vanilla, Skyrim - Textures3.bsa): {}", texture,
                    found ? fmt::format("in --in, {} bytes", std::filesystem::file_size(*found)) : std::string("not in an --in folder, not read")));
            }
            std::string frameNormal;
            {
                auto* shader = hdr.GetBlock<nifly::BSLightingShaderProperty>(wornFrame->ShaderPropertyRef());
                auto* set = shader ? hdr.GetBlock<nifly::BSShaderTextureSet>(shader->TextureSetRef()) : nullptr;
                frameNormal = set && set->textures.size() > kNormalSlot ? set->textures[kNormalSlot].get() : std::string();
            }
            (void)adopt(AddMeshShape(worn, kLanternStrapName, strap.mesh, *wornFrame, kLanternStrapDiffuse, kLanternStrapNormal));
            (void)adopt(AddMeshShape(worn, kLanternRingName, ringMesh, *wornFrame, kLanternDiffuse, frameNormal));
            log.push_back(fmt::format("strap: {} vertices, {} triangles, centreline {:.3f} long, {}", strap.mesh.verts.size(),
                strap.mesh.tris.size(), strap.length, BoxText(strap.mesh.Bounds())));
            log.push_back(fmt::format("ring: {} vertices, {} triangles, {}", ringMesh.verts.size(), ringMesh.tris.size(),
                BoxText(ringMesh.Bounds())));

            for (auto* shape : wornShapes) {
                SkinToBone(worn, *shape, boneId, pelvis);
            }
            // CloneShape brings nodes of the source file along as root children (the atronach's bones, the ground's addon
            // node): only the bone and the shapes stay; the save drops what is then unreferenced
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
            std::vector<std::string> notes;
            const GreyStats stats = GreyEffects(worn, kDefaultTint, kDefaultTintAdd, &notes);
            log.push_back(fmt::format("worn effect shaders {}: palette normal {} (GradBlackFlame.dds), additive {}, left {}; "
                                      "lighting shaders {} left alone; base colour x tint {}",
                stats.effectShaders, stats.paletted, stats.palettedAdd, stats.paletteLeft, stats.lightingShaders, kDefaultTint));
            for (const auto& note : notes) {
                log.push_back("worn: " + note);
            }
            SaveNew(worn, a_dataDir / kLanternWornNif);

            const Vector3 ringBottom = ring - Vector3(0.0f, 0.0f, kLanternRingRadius);
            a_result.summary.push_back(fmt::format("handle top wire centre (world) {}; ring centre {}; ring bottom tube centre {}; the wire "
                                                   "sits {:.4f} above the tube centre (tube {} + wire {:.4f}), {:.4f} below the ring "
                                                   "centre, {:.4f} off its vertical",
                Text(a_result.contact), Text(a_result.ringCentre), Text(ToArray(ringBottom)), contact.z - ringBottom.z, kLanternRingTube,
                wire, ring.z - contact.z, std::hypot(contact.x - ring.x, contact.y - ring.y)));
            a_result.summary.push_back(fmt::format("lantern tilt {} deg; cage centre {}; scale {:.5f}", tilt, Text(a_result.wornCage), scale));
            a_result.summary.push_back(fmt::format("strap textures {} / {} (vanilla leather); ring {} / {}", kLanternStrapDiffuse,
                kLanternStrapNormal, kLanternDiffuse, frameNormal));
            for (const auto& strip : strips) {
                const auto& piece = strip.pieces[strip.chosen];
                a_result.summary.push_back(fmt::format("flame piece '{}' #{}: {} triangles, {} vertices, rest-pose size ({:.4f}, {:.4f}, "
                                                       "{:.4f}), worn size x {:.5f} x {:.5f}",
                    strip.shape->name.get(), strip.chosen, piece.triangles.size(), piece.vertices.size(), piece.bounds.Size(0),
                    piece.bounds.Size(1), piece.bounds.Size(2), fit, scale));
            }
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
                BuildWorn(a_inputs, a_dataDir, ground, *frame, cageWalls, result);
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
        const std::array<Part, 3> parts{ { { "lantern", { kLanternWornFrameName, kLanternFlameStrips[0], kLanternFlameStrips[1] }, kLanternBodyMargin },
            { "strap", { kLanternStrapName }, kLanternStrapMargin }, { "ring", { kLanternRingName }, kLanternStrapMargin } } };
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
            expected.push_back(kLanternStrapName);
            expected.push_back(kLanternRingName);
            const auto shapeNamed = [&](const std::string& a_name) -> const LanternShapeInfo* {
                const auto it = std::find_if(worn.shapes.begin(), worn.shapes.end(), [&](const auto& a_s) { return a_s.name == a_name; });
                return it == worn.shapes.end() ? nullptr : &*it;
            };
            const auto slot = [](const LanternShapeInfo* a_s, std::size_t a_i) {
                return a_s && a_s->textures.size() > a_i ? a_s->textures[a_i] : std::string();
            };
            {
                const auto* frame = shapeNamed(kLanternWornFrameName);
                const auto* strap = shapeNamed(kLanternStrapName);
                const auto* ring = shapeNamed(kLanternRingName);
                const auto* warp = shapeNamed(kLanternFlameStrips[1]);
                const std::string strapDiffuse = fmt::format("[{}] {}", kSlotDiffuse, kLanternStrapDiffuse);
                const std::string strapNormal = fmt::format("[{}] {}", kNormalSlot, kLanternStrapNormal);
                const std::string soot = fmt::format("[{}] {}", kSlotDiffuse, kLanternDiffuse);
                Check(problems, slot(strap, 0) == strapDiffuse && slot(strap, 1) == strapNormal,
                    fmt::format("worn strap: textures '{}' / '{}', expected '{}' / '{}'", slot(strap, 0), slot(strap, 1), strapDiffuse,
                        strapNormal));
                Check(problems, slot(ring, 0) == soot && !slot(frame, 1).empty() && slot(ring, 1) == slot(frame, 1),
                    fmt::format("worn ring: textures '{}' / '{}', expected '{}' / the frame's normal map '{}'", slot(ring, 0), slot(ring, 1),
                        soot, slot(frame, 1)));
                Check(problems, warp && std::fabs(warp->refraction - kLanternWarpRefraction) <= 1e-6f,
                    fmt::format("worn FireStripWarp: refraction strength {}, expected {} (kLanternWarpRefraction)",
                        warp ? warp->refraction : -1.0f, kLanternWarpRefraction));
            }
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
                Check(problems, s.bindRead && s.bindError <= kLanternBindTolerance,
                    fmt::format("worn '{}': bind pose x skin-to-bone is {:.2e} off the identity (read {}; tolerance {})", s.name, s.bindError,
                        s.bindRead, kLanternBindTolerance));
                Check(problems, s.bindRead && s.sphereError <= kLanternBoundsTolerance && s.vertexError <= kLanternBoundsTolerance,
                    fmt::format("worn '{}': through the bind pose the bone-space bounds land {:.2e} and the vertices {:.2e} off the "
                                "shape's (tolerance {})", s.name, s.sphereError, s.vertexError, kLanternBoundsTolerance));
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

            // the worn parts against the bodies (each body an --in folder has)
            for (const auto& c : WornClearances(a_inputs, a_dataDir)) {
                Check(problems, c.gap.points > 0 && c.gap.inside == 0 && c.gap.nearest >= c.margin,
                    fmt::format("worn {} / {}: {} (margin {})", c.part, c.body, GapText(c.gap), c.margin));
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
        std::cout << "\nworn (build):\n";
        for (const auto& line : result.summary) {
            std::cout << "  " << line << "\n";
        }
        std::cout << "worn clearances (read back):\n";
        for (const auto& c : WornClearances(a_inputs, a_dataDir)) {
            std::cout << fmt::format("  {} / {}: {} (margin {})\n", c.part, c.body, GapText(c.gap), c.margin);
        }
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

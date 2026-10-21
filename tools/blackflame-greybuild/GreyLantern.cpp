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
#include "GreyParticleGraph.h"
#include "GreyShapes.h"
#include "GreyTexture.h"

#include <Animation.hpp>
#include <ExtraData.hpp>
#include <Geometry.hpp>
#include <NifFile.hpp>
#include <Nodes.hpp>
#include <Particles.hpp>
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

    namespace
    {
        constexpr std::size_t kKeysShown = 6;  // keys a printed controller line lists before "..."

        [[nodiscard]] const char* CycleName(std::uint16_t a_flags) noexcept
        {
            // NiTimeController flags bits 1-2 (nif.xml TimeControllerFlags): the cycle type
            switch ((a_flags >> 1) & 3u) {
            case 0:
                return "loop";
            case 1:
                return "reverse";
            case 2:
                return "clamp";
            default:
                return "?";
            }
        }

        [[nodiscard]] const char* KeyTypeName(std::uint32_t a_type) noexcept
        {
            constexpr std::array<const char*, 6> kNames{ "none", "linear", "quadratic", "tbc", "xyz", "const" };
            return a_type < kNames.size() ? kNames[a_type] : "?";
        }

        [[nodiscard]] std::string FloatValueText(float a_value)
        {
            return a_value <= kUnusedKeyValue ? std::string("unset") : fmt::format("{:.4f}", a_value);
        }

        [[nodiscard]] std::string NameOfBlock(const nifly::NiHeader& a_hdr, std::uint32_t a_id)
        {
            if (auto* net = a_hdr.GetBlock<nifly::NiObjectNET>(a_id)) {
                return net->name.get();
            }
            if (auto* modifier = a_hdr.GetBlock<nifly::NiPSysModifier>(a_id)) {
                return modifier->name.get();
            }
            return {};
        }

        /// An interpolator's values into a_c (float keys, transform keys) and its text
        [[nodiscard]] std::string ReadInterpolator(const nifly::NiHeader& a_hdr, std::uint32_t a_id, LanternController& a_c)
        {
            auto* object = a_hdr.GetBlock<nifly::NiObject>(a_id);
            if (!object) {
                return "no interpolator";
            }
            a_c.interpolator = object->GetBlockName();
            std::string text = a_c.interpolator;
            if (auto* f = a_hdr.GetBlock<nifly::NiFloatInterpolator>(a_id)) {
                a_c.value = f->floatValue;
                text += " value " + FloatValueText(f->floatValue);
                if (auto* data = a_hdr.GetBlock<nifly::NiFloatData>(f->dataRef)) {
                    const auto& keys = data->data;
                    text += fmt::format(", {} {} keys", keys.GetNumKeys(), KeyTypeName(keys.GetInterpolationType()));
                    for (std::uint32_t k = 0; k < keys.GetNumKeys(); ++k) {
                        const auto key = keys.GetKey(static_cast<int>(k));
                        a_c.keys.push_back({ key.time, key.value });
                        if (k < kKeysShown) {
                            text += fmt::format(" ({:.4f}, {:.4f})", key.time, key.value);
                        }
                    }
                    if (keys.GetNumKeys() > kKeysShown) {
                        text += " ...";
                    }
                }
            } else if (auto* b = a_hdr.GetBlock<nifly::NiBoolInterpolator>(a_id)) {
                text += fmt::format(" value {}", static_cast<int>(b->boolValue));
                if (auto* data = a_hdr.GetBlock<nifly::NiBoolData>(b->dataRef)) {
                    text += fmt::format(", {} keys", data->data.GetNumKeys());
                }
            } else if (auto* t = a_hdr.GetBlock<nifly::NiTransformInterpolator>(a_id)) {
                text += fmt::format(" translation {} rotation ({:.4f}, {:.4f}, {:.4f}, {:.4f}) scale {:.4f}", Text(ToArray(t->translation)),
                    t->rotation.w, t->rotation.x, t->rotation.y, t->rotation.z, t->scale);
                if (auto* data = a_hdr.GetBlock<nifly::NiTransformData>(t->dataRef)) {
                    a_c.rotationType = data->rotationType;
                    const auto& tr = data->translations;
                    text += fmt::format("; translation keys {} {}", tr.GetNumKeys(), KeyTypeName(tr.GetInterpolationType()));
                    for (std::uint32_t k = 0; k < tr.GetNumKeys(); ++k) {
                        const auto key = tr.GetKey(static_cast<int>(k));
                        a_c.translations.push_back({ key.time, key.value.x, key.value.y, key.value.z });
                        if (k < kKeysShown) {
                            text += fmt::format(" ({:.4f}: {:.4f}, {:.4f}, {:.4f})", key.time, key.value.x, key.value.y, key.value.z);
                        }
                    }
                    if (tr.GetNumKeys() > kKeysShown) {
                        text += " ...";
                    }
                    text += fmt::format("; rotation type {} ({} quaternion keys)", KeyTypeName(data->rotationType), data->quaternionKeys.size());
                    const std::array<std::pair<char, const nifly::NiAnimationKeyGroup<float>*>, 3> groups{ { { 'X', &data->xRotations },
                        { 'Y', &data->yRotations }, { 'Z', &data->zRotations } } };
                    for (const auto& [axis, group] : groups) {
                        if (group->GetNumKeys() == 0) {
                            continue;
                        }
                        text += fmt::format(", {} rotation keys {} {}", axis, group->GetNumKeys(), KeyTypeName(group->GetInterpolationType()));
                        for (std::uint32_t k = 0; k < group->GetNumKeys(); ++k) {
                            const auto key = group->GetKey(static_cast<int>(k));
                            if (axis == 'Z') {
                                a_c.zRotations.push_back({ key.time, key.value });
                            }
                            if (k < kKeysShown) {
                                text += fmt::format(" ({:.4f}: {:.4f})", key.time, key.value);
                            }
                        }
                        if (group->GetNumKeys() > kKeysShown) {
                            text += " ...";
                        }
                    }
                    text += fmt::format("; scale keys {}", data->scales.GetNumKeys());
                }
            }
            return text;
        }

        /// The controller a_id read out (its type's fields, its interpolator; a manager's sequences)
        [[nodiscard]] LanternController ReadController(const nifly::NiHeader& a_hdr, std::uint32_t a_id)
        {
            LanternController c;
            auto* ctrl = a_hdr.GetBlock<nifly::NiTimeController>(a_id);
            if (!ctrl) {
                c.type = "<missing>";
                c.text = fmt::format("block {} is no controller", a_id);
                return c;
            }
            c.type = ctrl->GetBlockName();
            c.flags = ctrl->flags;
            c.frequency = ctrl->frequency;
            c.phase = ctrl->phase;
            c.start = ctrl->startTime;
            c.stop = ctrl->stopTime;
            c.target = ctrl->targetRef.index;
            std::string extra;
            if (auto* effect = dynamic_cast<nifly::BSEffectShaderPropertyFloatController*>(ctrl)) {
                c.variable = effect->typeOfControlledVariable;
            } else if (auto* lit = dynamic_cast<nifly::BSLightingShaderPropertyFloatController*>(ctrl)) {
                c.variable = lit->typeOfControlledVariable;
            }
            if (c.variable != 0xFFFFFFFF) {
                extra += fmt::format(" variable {}", c.variable);
            }
            if (auto* modifier = dynamic_cast<nifly::NiPSysModifierCtlr*>(ctrl)) {
                extra += fmt::format(" modifier '{}'", modifier->modifierName.get());
            }
            if (auto* single = dynamic_cast<nifly::NiSingleInterpController*>(ctrl)) {
                extra += "; " + ReadInterpolator(a_hdr, single->interpolatorRef.index, c);
            }
            if (auto* multi = dynamic_cast<nifly::NiMultiTargetTransformController*>(ctrl)) {
                extra += fmt::format("; {} targets", multi->targetRefs.GetSize());
            }
            if (auto* manager = dynamic_cast<nifly::NiControllerManager*>(ctrl)) {
                for (const auto& ref : manager->controllerSequenceRefs) {
                    auto* sequence = a_hdr.GetBlock<nifly::NiControllerSequence>(ref);
                    if (!sequence) {
                        continue;
                    }
                    extra += fmt::format("; sequence '{}' cycle {} frequency {:.4f} {:.4f}..{:.4f}, {} controlled blocks", sequence->name.get(),
                        static_cast<std::uint32_t>(sequence->cycleType), sequence->frequency, sequence->startTime, sequence->stopTime,
                        sequence->controlledBlocks.size());
                    for (const auto& link : sequence->controlledBlocks) {
                        extra += fmt::format(" ['{}' {} {}: {}]", link.nodeName.get(), link.ctrlType.get(), link.propType.get(),
                            ReadInterpolator(a_hdr, link.interpolatorRef.index, c));
                    }
                }
            }
            c.text = fmt::format("{} flags 0x{:04X} ({}{}) frequency {:.4f} phase {:.4f} time {:.4f}..{:.4f} target {}", c.type, c.flags, CycleName(c.flags), (c.flags & 8u) != 0 ? ", active" : ", inactive", c.frequency, c.phase, c.start, c.stop, c.target);
            c.text += extra;
            return c;
        }

        /// Every particle system of a_nif read out
        [[nodiscard]] std::vector<LanternParticleInfo> ReadParticles(nifly::NifFile& a_nif)
        {
            std::vector<LanternParticleInfo> out;
            auto& hdr = a_nif.GetHeader();
            for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
                auto* system = hdr.GetBlock<nifly::NiParticleSystem>(id);
                if (!system) {
                    continue;
                }
                LanternParticleInfo p;
                p.name = system->name.get();
                p.type = system->GetBlockName();
                p.worldSpace = system->isWorldSpace;
                const std::uint32_t parent = ParentOf(a_nif, id);
                p.parent = parent == nifly::NIF_NPOS ? std::string("<none>") : NameOfBlock(hdr, parent);
                auto* data = hdr.GetBlock<nifly::NiPSysData>(system->psysDataRef);
                if (!data) {
                    data = hdr.GetBlock<nifly::NiPSysData>(system->dataRef);
                }
                p.pool = data ? data->GetNumVertices() : 0;
                if (auto* strip = dynamic_cast<nifly::BSStripPSysData*>(data)) {
                    p.lines.push_back(fmt::format("strip data: max points {}, start cap {}, end cap {}", strip->maxPointCount, strip->startCapSize,
                        strip->endCapSize));
                }
                if (auto* shader = hdr.GetBlock<nifly::BSEffectShaderProperty>(system->shaderPropertyRef)) {
                    p.flags1 = shader->shaderFlags1;
                    p.flags2 = shader->shaderFlags2;
                    p.source = shader->sourceTexture.get();
                    p.greyscale = shader->greyscaleTexture.get();
                    p.baseColor = { shader->baseColor.r, shader->baseColor.g, shader->baseColor.b, shader->baseColor.a };
                }
                if (auto* alpha = hdr.GetBlock<nifly::NiAlphaProperty>(system->alphaPropertyRef)) {
                    p.hasAlpha = true;
                    p.alphaFlags = alpha->flags;
                }
                bool firstEmitter = true;
                for (const auto& ref : system->modifierRefs) {
                    auto* modifier = hdr.GetBlock<nifly::NiPSysModifier>(ref);
                    if (!modifier) {
                        p.modifiers.push_back(fmt::format("<missing block {}>", ref.index));
                        continue;
                    }
                    p.modifiers.push_back(fmt::format("{} '{}'{}", modifier->GetBlockName(), modifier->name.get(), modifier->isActive ? "" : " (inactive)"));
                    std::string line = fmt::format("modifier {} '{}' order {}", modifier->GetBlockName(), modifier->name.get(), modifier->order);
                    if (auto* emitter = dynamic_cast<nifly::NiPSysEmitter*>(modifier)) {
                        if (firstEmitter) {
                            p.radius = emitter->radius;
                            p.radiusVariation = emitter->radiusVariation;
                            p.speed = emitter->speed;
                            p.lifeSpan = emitter->lifeSpan;
                            firstEmitter = false;
                        }
                        line += fmt::format(": radius {:.4f} +/- {:.4f}, speed {:.4f} +/- {:.4f}, declination {:.4f} +/- {:.4f}, life {:.4f} +/- "
                                            "{:.4f}",
                            emitter->radius, emitter->radiusVariation, emitter->speed, emitter->speedVariation, emitter->declination,
                            emitter->declinationVariation, emitter->lifeSpan, emitter->lifeSpanVariation);
                        if (auto* mesh = dynamic_cast<nifly::NiPSysMeshEmitter*>(emitter)) {
                            line += fmt::format(", velocity type {}, emit from {}, axis {}", static_cast<std::uint32_t>(mesh->velocityType),
                                static_cast<std::uint32_t>(mesh->emissionType), Text(ToArray(mesh->emissionAxis)));
                            for (const auto& meshRef : mesh->meshRefs) {
                                p.emitterMeshes.push_back(NameOfBlock(hdr, meshRef.index));
                                line += fmt::format(", mesh [{}] '{}'", meshRef.index, p.emitterMeshes.back());
                            }
                        }
                    } else if (auto* gravity = dynamic_cast<nifly::NiPSysGravityModifier*>(modifier)) {
                        line += fmt::format(": object [{}] '{}', axis {}, strength {:.4f}, decay {:.4f}, force {}, turbulence {:.4f} scale {:.4f}",
                            gravity->gravityObjRef.index, NameOfBlock(hdr, gravity->gravityObjRef.index), Text(ToArray(gravity->gravityAxis)),
                            gravity->strength, gravity->decay, static_cast<std::uint32_t>(gravity->forceType), gravity->turbulence,
                            gravity->turbulenceScale);
                    } else if (auto* bomb = dynamic_cast<nifly::NiPSysBombModifier*>(modifier)) {
                        line += fmt::format(": node [{}] '{}', delta V {:.4f}, decay {:.4f}", bomb->bombNodeRef.index,
                            NameOfBlock(hdr, bomb->bombNodeRef.index), bomb->deltaV, bomb->decay);
                    } else if (auto* scale = dynamic_cast<nifly::BSPSysScaleModifier*>(modifier)) {
                        line += ": scales";
                        for (const float f : scale->floats) {
                            line += fmt::format(" {:.4f}", f);
                        }
                    } else if (auto* colour = dynamic_cast<nifly::BSPSysSimpleColorModifier*>(modifier)) {
                        line += fmt::format(": fade in {:.4f} out {:.4f}, colours ({:.4f}, {:.4f}, {:.4f}, {:.4f}) ({:.4f}, {:.4f}, {:.4f}, {:.4f}) ({:.4f}, "
                                            "{:.4f}, {:.4f}, {:.4f})",
                            colour->fadeInPercent, colour->fadeOutPercent, colour->color1.r, colour->color1.g, colour->color1.b, colour->color1.a,
                            colour->color2.r, colour->color2.g, colour->color2.b, colour->color2.a, colour->color3.r, colour->color3.g,
                            colour->color3.b, colour->color3.a);
                    } else if (auto* rotation = dynamic_cast<nifly::NiPSysRotationModifier*>(modifier)) {
                        line += fmt::format(": speed {:.4f} +/- {:.4f}, angle {:.4f} +/- {:.4f}", rotation->initialSpeed,
                            rotation->initialSpeedVariation, rotation->initialAngle, rotation->initialAngleVariation);
                    } else if (auto* spawn = dynamic_cast<nifly::NiPSysSpawnModifier*>(modifier)) {
                        line += fmt::format(": generations {}, spawned {}..{} ({:.4f}), life {:.4f} +/- {:.4f}", spawn->numSpawnGenerations,
                            spawn->minSpawned, spawn->maxSpawned, spawn->percentSpawned, spawn->lifeSpan, spawn->lifeSpanVariation);
                    }
                    p.lines.push_back(std::move(line));
                }
                std::set<std::uint32_t> seen;
                for (std::uint32_t c = system->controllerRef.index; c != nifly::NIF_NPOS && seen.insert(c).second;) {
                    const auto controller = ReadController(hdr, c);
                    if (controller.type == "NiPSysEmitterCtlr" && p.birthRate < 0.0f) {
                        p.birthRate = controller.keys.empty() ? controller.value : controller.keys.front()[1];
                    }
                    p.lines.push_back("controller " + controller.text);
                    auto* ctrl = hdr.GetBlock<nifly::NiTimeController>(c);
                    c = ctrl ? ctrl->nextControllerRef.index : nifly::NIF_NPOS;
                }
                // the graph the system owns (its children that are no scene objects), and what it names outside that
                std::set<std::uint32_t> graph{ id };
                std::vector<std::uint32_t> todo{ id };
                while (!todo.empty()) {
                    const std::uint32_t at = todo.back();
                    todo.pop_back();
                    auto* block = hdr.GetBlock<nifly::NiObject>(at);
                    if (!block) {
                        continue;
                    }
                    std::set<nifly::NiRef*> refs;
                    HandGraph::ChildRefs(*block, refs);
                    for (auto* ref : refs) {
                        if (ref->index != nifly::NIF_NPOS && !hdr.GetBlock<nifly::NiAVObject>(ref->index) && graph.insert(ref->index).second) {
                            todo.push_back(ref->index);
                        }
                    }
                }
                for (const auto at : graph) {
                    auto* block = hdr.GetBlock<nifly::NiObject>(at);
                    if (!block) {
                        p.outside.push_back(fmt::format("block {} missing", at));
                        continue;
                    }
                    std::set<nifly::NiRef*> refs;
                    std::set<nifly::NiPtr*> ptrs;
                    HandGraph::ChildRefs(*block, refs);
                    block->GetPtrs(ptrs);
                    std::vector<std::uint32_t> named;
                    for (auto* ref : refs) {
                        named.push_back(ref->index);
                    }
                    for (auto* ptr : ptrs) {
                        named.push_back(ptr->index);
                    }
                    std::sort(named.begin(), named.end());
                    for (const auto target : named) {
                        if (target == nifly::NIF_NPOS || graph.count(target) != 0) {
                            continue;
                        }
                        auto* other = hdr.GetBlock<nifly::NiObject>(target);
                        p.outside.push_back(fmt::format("{} [{}] -> [{}] {} '{}'", block->GetBlockName(), at, target,
                            other ? other->GetBlockName() : "<past the block list>", NameOfBlock(hdr, target)));
                    }
                }
                out.push_back(std::move(p));
            }
            return out;
        }
    }

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
                std::set<std::uint32_t> seen;
                for (std::uint32_t c = node->controllerRef.index; c != nifly::NIF_NPOS && seen.insert(c).second;) {
                    info.nodeControllers.emplace_back(node->name.get(), ReadController(hdr, c));
                    auto* ctrl = hdr.GetBlock<nifly::NiTimeController>(c);
                    c = ctrl ? ctrl->nextControllerRef.index : nifly::NIF_NPOS;
                }
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
            s.avFlags = shape->flags;
            s.vertices = shape->GetNumVertices();
            const std::uint32_t shapeId = hdr.GetBlockID(shape);
            const std::uint32_t shaderId = shape->ShaderPropertyRef() ? shape->ShaderPropertyRef()->index : nifly::NIF_NPOS;
            if (auto* shader = hdr.GetBlock<nifly::NiShader>(shaderId)) {
                s.shader = shader->GetBlockName();
                if (auto* bs = dynamic_cast<nifly::BSShaderProperty*>(shader)) {
                    s.flags1 = bs->shaderFlags1;
                    s.flags2 = bs->shaderFlags2;
                }
                if (auto* lit = dynamic_cast<nifly::BSLightingShaderProperty*>(shader)) {
                    s.refraction = lit->refractionStrength;
                    s.glossiness = lit->glossiness;
                    s.specularStrength = lit->specularStrength;
                    s.specularColor = ToArray(lit->specularColor);
                }
                if (auto* effect = dynamic_cast<nifly::BSEffectShaderProperty*>(shader)) {
                    s.textures.push_back("source " + effect->sourceTexture.get());
                    s.textures.push_back("greyscale " + effect->greyscaleTexture.get());
                    s.effect = true;
                    s.baseColor = { effect->baseColor.r, effect->baseColor.g, effect->baseColor.b, effect->baseColor.a };
                    s.emissiveMultiple = effect->baseColorScale;
                    s.falloff = { effect->falloffStartAngle, effect->falloffStopAngle, effect->falloffStartOpacity, effect->falloffStopOpacity };
                    s.softFalloffDepth = effect->softFalloffDepth;
                    s.clampMode = effect->textureClampMode;
                    s.uvOffset = { effect->uvOffset.u, effect->uvOffset.v };
                    s.uvScale = { effect->uvScale.u, effect->uvScale.v };
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
                    s.controllerInfo.push_back(ReadController(hdr, id));
                    id = controller->nextControllerRef.index;
                }
            }
            if (auto* alpha = shape->AlphaPropertyRef() ? hdr.GetBlock<nifly::NiAlphaProperty>(*shape->AlphaPropertyRef()) : nullptr) {
                s.hasAlpha = true;
                s.alphaFlags = alpha->flags;
            }
            {
                const std::uint32_t parent = ParentOf(nif, shapeId);
                s.parent = parent == nifly::NIF_NPOS ? std::string("<none>") : NameOfBlock(hdr, parent);
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
        info.particles = ReadParticles(nif);
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
            std::cout << fmt::format("  shape '{}' {} vertices {} shader {} SLSF1 0x{:08X} SLSF2 0x{:08X}, vertex colours {}\n", s.name,
                s.type, s.vertices, s.shader.empty() ? "<none>" : s.shader, s.flags1, s.flags2, s.vertexColors ? "yes" : "no");
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
            if (s.effect) {
                std::cout << fmt::format("    effect: base colour ({:.4f}, {:.4f}, {:.4f}, {:.4f}) x emissive multiple {:.4f}; falloff angles {:.4f} -> "
                                         "{:.4f}, opacities {:.4f} -> {:.4f}; soft falloff depth {:.4f}; texture clamp mode {}; UV offset ({:.4f}, "
                                         "{:.4f}) scale ({:.4f}, {:.4f})\n",
                    s.baseColor[0], s.baseColor[1], s.baseColor[2], s.baseColor[3], s.emissiveMultiple, s.falloff[0], s.falloff[1], s.falloff[2],
                    s.falloff[3], s.softFalloffDepth, s.clampMode, s.uvOffset[0], s.uvOffset[1], s.uvScale[0], s.uvScale[1]);
            }
            if (s.hasAlpha) {
                std::cout << fmt::format("    alpha property flags 0x{:04X} (blend {}, source {}, destination {}, test {})\n", s.alphaFlags,
                    (s.alphaFlags & 1u) != 0 ? "on" : "off", (s.alphaFlags >> 1) & 15u, (s.alphaFlags >> 5) & 15u,
                    (s.alphaFlags & kAlphaTestOn) != 0 ? "on" : "off");
            }
            if (s.parent != a_info.rootName) {
                std::cout << "    under node '" << s.parent << "'\n";
            }
            if ((s.avFlags & 1u) != 0) {
                std::cout << fmt::format("    hidden (flags 0x{:X})\n", s.avFlags);
            }
            for (std::size_t c = 0; c < s.controllers.size(); ++c) {
                std::cout << "    controller " << s.controllers[c] << "\n";
                if (c < s.controllerInfo.size()) {
                    std::cout << "      " << s.controllerInfo[c].text << "\n";
                }
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
        for (const auto& [node, controller] : a_info.nodeControllers) {
            std::cout << fmt::format("  node '{}' controller {}\n", node, controller.text);
        }
        for (const auto& p : a_info.particles) {
            std::cout << fmt::format("  particles '{}' {} under '{}', world space {}, pool {}, shader SLSF1 0x{:08X} SLSF2 0x{:08X} source '{}' "
                                     "greyscale '{}' base ({:.4f}, {:.4f}, {:.4f}, {:.4f}), alpha {}\n",
                p.name, p.type, p.parent, p.worldSpace ? "yes" : "no", p.pool, p.flags1, p.flags2, p.source, p.greyscale, p.baseColor[0],
                p.baseColor[1], p.baseColor[2], p.baseColor[3], p.hasAlpha ? fmt::format("0x{:04X}", p.alphaFlags) : std::string("none"));
            std::cout << fmt::format("    first emitter radius {:.4f} +/- {:.4f}, speed {:.4f}, life {:.4f}, birth rate {:.4f}\n", p.radius,
                p.radiusVariation, p.speed, p.lifeSpan, p.birthRate);
            for (const auto& line : p.lines) {
                std::cout << "    " << line << "\n";
            }
            for (const auto& line : p.outside) {
                std::cout << "    names outside its graph: " << line << "\n";
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

        /// One point of a cross-section: s along the path's side normal, a along +Y, and its outward normal there
        struct SectionPoint
        {
            float s = 0.0f;
            float a = 0.0f;
            float ns = 0.0f;
            float na = 0.0f;
        };

        /// A cross-section: runs of points joined in order (closed: the last back to the first; a hard edge is where one
        /// run ends and the next begins) and the outline the end caps fan over
        struct Section
        {
            std::vector<std::vector<SectionPoint>> runs;
            bool closed = false;
            std::vector<SectionPoint> outline;
        };

        /// a_halfS x a_halfA with its four edges rounded at a_radius, a_segments segments each, one smooth closed run; each
        /// broad face (s = +-a_halfS) carries a_faceSegments - 1 points between its edges
        [[nodiscard]] Section RoundedSection(float a_halfS, float a_halfA, float a_radius, int a_segments, int a_faceSegments = 1)
        {
            if (!(a_radius > 0.0f && a_radius < a_halfS && a_radius < a_halfA) || a_segments < 1 || a_faceSegments < 1) {
                throw std::runtime_error(fmt::format("an edge radius {} with {} segments ({} across the faces) does not fit a {} x {} section",
                    a_radius, a_segments, a_faceSegments, a_halfS * 2.0f, a_halfA * 2.0f));
            }
            Section section;
            section.closed = true;
            section.runs.emplace_back();
            auto& run = section.runs.front();
            const float edge = a_halfA - a_radius;
            // the broad face at s = a_sign * a_halfS, from a = a_from to -a_from, between the corners
            const auto face = [&](float a_sign, float a_from) {
                for (int j = 1; j < a_faceSegments; ++j) {
                    const float a = a_from - 2.0f * a_from * static_cast<float>(j) / static_cast<float>(a_faceSegments);
                    run.push_back({ a_sign * a_halfS, a, a_sign, 0.0f });
                }
            };
            constexpr std::array<std::array<float, 2>, 4> kCorners{ { { 1.0f, 1.0f }, { -1.0f, 1.0f }, { -1.0f, -1.0f }, { 1.0f, -1.0f } } };
            for (std::size_t c = 0; c < kCorners.size(); ++c) {
                const float cs = kCorners[c][0] * (a_halfS - a_radius);
                const float ca = kCorners[c][1] * edge;
                for (int i = 0; i <= a_segments; ++i) {
                    const float angle = 0.5f * kPi * (static_cast<float>(c) + static_cast<float>(i) / static_cast<float>(a_segments));
                    const SectionPoint p{ cs + a_radius * std::cos(angle), ca + a_radius * std::sin(angle), std::cos(angle), std::sin(angle) };
                    run.push_back(p);
                }
                if (c == 1) {
                    face(-1.0f, edge);  // corner 1 ends at (-halfS, +edge), corner 2 starts at (-halfS, -edge)
                } else if (c == 3) {
                    face(1.0f, -edge);  // corner 3 ends at (+halfS, -edge), the run closes on corner 0 at (+halfS, +edge)
                }
            }
            section.outline = run;
            return section;
        }

        /// a_halfS x a_halfA with its four edges chamfered a_bevel at 45 degrees: eight flat runs of two points
        [[nodiscard]] Section BevelSection(float a_halfS, float a_halfA, float a_bevel)
        {
            if (!(a_bevel > 0.0f && a_bevel < a_halfS && a_bevel < a_halfA)) {
                throw std::runtime_error(fmt::format("a bevel {} does not fit a {} x {} section", a_bevel, a_halfS * 2.0f, a_halfA * 2.0f));
            }
            const float s = a_halfS;
            const float a = a_halfA;
            const float b = a_bevel;
            const float d = std::sqrt(0.5f);
            // the outline anticlockwise in (s, a) from the +s face's lower end; each face's outward normal
            const std::array<SectionPoint, 8> corners{ { { s, -(a - b) }, { s, a - b }, { s - b, a }, { -(s - b), a }, { -s, a - b },
                { -s, -(a - b) }, { -(s - b), -a }, { s - b, -a } } };
            const std::array<std::array<float, 2>, 8> normals{ { { 1.0f, 0.0f }, { d, d }, { 0.0f, 1.0f }, { -d, d }, { -1.0f, 0.0f },
                { -d, -d }, { 0.0f, -1.0f }, { d, -d } } };
            Section section;
            for (std::size_t f = 0; f < corners.size(); ++f) {
                SectionPoint p0 = corners[f];
                SectionPoint p1 = corners[(f + 1) % corners.size()];
                p0.ns = p1.ns = normals[f][0];
                p0.na = p1.na = normals[f][1];
                section.runs.push_back({ p0, p1 });
            }
            section.outline.assign(corners.begin(), corners.end());
            return section;
        }

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

        /// A section point moved at path point k (arc length along the path): its place and normal there
        using SectionMap = std::function<SectionPoint(std::size_t, float, const SectionPoint&)>;

        /// a_section swept along a_path (in an XZ plane: the section's s axis is the path's side normal there, its tangent
        /// turned a quarter about Y; its a axis +Y), both ends capped; a_map (when set) moves each section point at each path
        /// point. U from a_u(arc length), V from a_v(a of the unmoved point). The path's length.
        float Sweep(LanternMesh& a_mesh, const std::vector<Vector3>& a_path, const Section& a_section,
            const std::function<float(float)>& a_u, const std::function<float(float)>& a_v, const SectionMap& a_map = {})
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
            const auto moved = [&](std::size_t a_k, const SectionPoint& a_p) { return a_map ? a_map(a_k, arc[a_k], a_p) : a_p; };
            const auto at = [&](std::size_t a_k, const SectionPoint& a_p) {
                const SectionPoint q = moved(a_k, a_p);
                return a_path[a_k] + side[a_k] * q.s + across * q.a;
            };
            for (const auto& run : a_section.runs) {
                std::vector<std::vector<std::uint16_t>> rows;
                for (std::size_t k = 0; k < count; ++k) {
                    std::vector<std::uint16_t> row;
                    for (const auto& p : run) {
                        const SectionPoint q = moved(k, p);
                        row.push_back(a_mesh.Add(at(k, p), side[k] * q.ns + across * q.na, a_u(arc[k]), a_v(p.a)));
                    }
                    rows.push_back(std::move(row));
                }
                const std::size_t points = run.size();
                const std::size_t joins = a_section.closed ? points : points - 1;
                for (std::size_t k = 0; k + 1 < count; ++k) {
                    for (std::size_t j = 0; j < joins; ++j) {
                        const std::size_t next = (j + 1) % points;
                        a_mesh.Quad(rows[k][j], rows[k + 1][j], rows[k + 1][next], rows[k][next]);
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

        struct LanternStrap
        {
            LanternMesh mesh;
            float top = 0.0f;           // the fold's top (its outer face)
            float innerBottom = 0.0f;   // the inner layer's lower end
            float bottom = 0.0f;        // the outer layer's lower end
            float outerBottomX = 0.0f;  // the outer layer's centreline X at bottom
            float y = 0.0f;             // the strap's centre Y
            float length = 0.0f;        // the centreline's length
            std::vector<std::string> log;
        };

        /// One edge's move across the width at arc length a_arc (kLanternStrapEdgeWobble): two sines, deterministic
        [[nodiscard]] float StrapEdgeWobble(int a_edge, float a_arc)
        {
            // per edge, the two sines' phases (radians): any fixed values that differ between the edges
            constexpr std::array<std::array<float, 2>, 2> kPhases{ { { 0.0f, 1.3f }, { 2.1f, 4.4f } } };
            constexpr std::array<float, 2> kWeights{ 0.6f, 0.4f };
            float sum = 0.0f;
            for (int i = 0; i < 2; ++i) {
                sum += kWeights[i] * std::sin(2.0f * kPi * a_arc / kLanternStrapWobbleWaves[i] + kPhases[a_edge][i]);
            }
            return kLanternStrapEdgeWobble * sum;
        }

        /// The strap's centreline on the bodies, extruded (GreyLantern.h, the strap constants), on the belt band whose
        /// highest top is a_bandTop and lowest bottom a_bandBottom
        [[nodiscard]] LanternStrap MakeStrap(const std::vector<LanternSurface>& a_bodies, const Vector3& a_pelvis, float a_bandTop,
            float a_bandBottom)
        {
            LanternStrap strap;
            strap.y = a_pelvis.y + kLanternStrapY;
            strap.top = a_bandTop + kLanternStrapOverBand;
            strap.innerBottom = a_bandBottom - kLanternStrapUnderBand;
            strap.bottom = strap.top - kLanternStrapOuterLength;
            const float halfY = kLanternStrapWidth * 0.5f + kLanternStrapMargin;
            const float t = kLanternStrapThickness;
            const float innerOff = kLanternStrapBodyGap + t * 0.5f;          // body to the inner layer's centreline
            const float between = t + kLanternStrapBeltGap;                   // centreline to centreline
            const float foldRadius = between * 0.5f;
            const float foldZ = strap.top - foldRadius - t * 0.5f;            // the fold's centre: its outer face tops out there
            if (!(strap.innerBottom < foldZ && strap.bottom < foldZ)) {
                throw std::runtime_error(fmt::format("the strap's fold centre z {:.3f} is not above its inner end z {:.3f} and its outer "
                                                     "end z {:.3f}", foldZ, strap.innerBottom, strap.bottom));
            }
            const auto left = [&](float a_z) {
                return HipLeft(a_bodies, a_pelvis, strap.y, halfY, a_z - kLanternStrapStep, a_z + kLanternStrapStep);
            };

            std::vector<Vector3> path;
            // the inner layer, upwards on the body
            std::vector<float> innerZ;
            for (float z = strap.innerBottom; z < foldZ; z += kLanternStrapStep) {
                innerZ.push_back(z);
            }
            innerZ.push_back(foldZ);
            for (const float z : innerZ) {
                path.emplace_back(left(z) - innerOff, strap.y, z);
            }
            const std::size_t innerTop = path.size() - 1;  // the fold's first point (angle 0)
            const float innerTopX = path.back().x;
            // the outer layer, downwards, draping: never back in towards the body below a wider part
            std::vector<Vector3> outer;
            float drape = innerTopX - between;
            for (float z = foldZ; z > strap.bottom; z -= kLanternStrapStep) {
                drape = std::min(drape, left(z) - innerOff - between);
                outer.emplace_back(drape, strap.y, z);
            }
            drape = std::min(drape, left(strap.bottom) - innerOff - between);
            outer.emplace_back(drape, strap.y, strap.bottom);
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
            strap.log.push_back(fmt::format("strap: centre y {:.3f} (pelvis + kLanternStrapY {}), inner layer z {:.3f} (lowest band bottom "
                                            "{:.3f} - kLanternStrapUnderBand {})..{:.3f} at {} off the body (kLanternStrapBodyGap), fold "
                                            "radius {:.3f} topping out at z {:.3f} (highest band top {:.3f} + kLanternStrapOverBand {}), outer "
                                            "layer {} further out (kLanternStrapBeltGap) down to z {:.3f} (kLanternStrapOuterLength {} below "
                                            "the top); inner top x {:.3f}, outer top x {:.3f}, outer bottom x {:.3f}",
                strap.y, kLanternStrapY, strap.innerBottom, a_bandBottom, kLanternStrapUnderBand, foldZ, kLanternStrapBodyGap, r, strap.top,
                a_bandTop, kLanternStrapOverBand, kLanternStrapBeltGap, strap.bottom, kLanternStrapOuterLength, innerTopX, outerTopX,
                strap.outerBottomX));

            // the outer face's bulge weight per path point: 0 on the inner layer and the fold's first half, a smoothstep over
            // its second half, 1 on the outer layer
            std::vector<float> bulge(path.size(), 1.0f);
            for (std::size_t k = 0; k < path.size(); ++k) {
                const float share = (static_cast<float>(k) - static_cast<float>(innerTop)) / static_cast<float>(kLanternStrapFoldSegments);
                const float x = std::clamp((share - 0.5f) * 2.0f, 0.0f, 1.0f);
                bulge[k] = x * x * (3.0f - 2.0f * x);
            }
            // the section moved at each path point: the +s broad face (the loop's outside) bulged, the edges wobbled across
            const float halfW = kLanternStrapWidth * 0.5f;
            const float faceEdge = halfW - kLanternStrapEdgeRadius;
            const SectionMap shape = [&](std::size_t a_k, float a_arc, const SectionPoint& a_p) {
                SectionPoint q = a_p;
                if (a_p.ns > 0.99f && std::fabs(a_p.a) <= faceEdge + 1e-4f) {
                    const float w = bulge[a_k] * kLanternStrapBulge;
                    const float x = a_p.a / faceEdge;
                    q.s = a_p.s + w * (1.0f - x * x);
                    const float slope = -2.0f * w * a_p.a / (faceEdge * faceEdge);  // ds/da
                    const float length = std::sqrt(1.0f + slope * slope);
                    q.ns = 1.0f / length;
                    q.na = -slope / length;
                }
                q.a = a_p.a + StrapEdgeWobble(a_p.a >= 0.0f ? 0 : 1, a_arc) * (a_p.a / halfW);
                return q;
            };

            // the rounded section swept along it: V across the strap's width in the leather row (the edges' points land at
            // the row's borders), U along its length
            const float bandV = kLanternStrapBandV1 - kLanternStrapBandV0;
            strap.length = Sweep(strap.mesh, path,
                RoundedSection(t * 0.5f, halfW, kLanternStrapEdgeRadius, kLanternStrapEdgeSegments, kLanternStrapFaceSegments),
                [](float a_arc) { return a_arc / kLanternStrapUvSpan; },
                [bandV](float a_a) { return kLanternStrapBandV0 + bandV * (a_a / kLanternStrapWidth + 0.5f); }, shape);
            strap.log.push_back(fmt::format("strap: outer face bulge {} (kLanternStrapBulge, {} points across each broad face), edges "
                                            "wobbled up to {} (kLanternStrapEdgeWobble, wavelengths {} / {})",
                kLanternStrapBulge, kLanternStrapFaceSegments - 1, kLanternStrapEdgeWobble, kLanternStrapWobbleWaves[0],
                kLanternStrapWobbleWaves[1]));
            return strap;
        }

        /// The bracket's centreline (GreyLantern.h, the bracket constants): down the plate at a_plateX from a_topZ, a bend
        /// out to the bar at a_barZ, out to a_tipX, a bend down, the tip down to a_tipZ. Straight parts sampled every
        /// kLanternStrapStep (the clearance check tests vertices).
        [[nodiscard]] std::vector<Vector3> BracketPath(float a_y, float a_plateX, float a_topZ, float a_barZ, float a_tipX, float a_tipZ)
        {
            const float bend = kLanternBracketBend;
            if (!(a_topZ > a_barZ + bend && a_plateX - a_tipX >= 2.0f * bend && a_barZ - bend > a_tipZ)) {
                throw std::runtime_error(fmt::format("the bracket does not fit its bends (radius {}): plate z {:.3f} down to the bar at z "
                                                     "{:.3f}, x {:.3f} out to {:.3f}, tip down to z {:.3f}",
                    bend, a_topZ, a_barZ, a_plateX, a_tipX, a_tipZ));
            }
            std::vector<Vector3> path;
            const auto line = [&](const Vector3& a_from, const Vector3& a_to) {  // a_to left for the next part
                const int steps = std::max(1, static_cast<int>(std::ceil(a_from.DistanceTo(a_to) / kLanternStrapStep)));
                for (int i = 0; i < steps; ++i) {
                    path.push_back(a_from + (a_to - a_from) * (static_cast<float>(i) / static_cast<float>(steps)));
                }
            };
            const auto bendArc = [&](float a_cx, float a_cz, float a_fromDeg, float a_toDeg) {  // the end left for the next part
                for (int i = 0; i < kLanternBracketBendSegments; ++i) {
                    const float deg = a_fromDeg + (a_toDeg - a_fromDeg) * static_cast<float>(i) / static_cast<float>(kLanternBracketBendSegments);
                    path.emplace_back(a_cx + bend * std::cos(deg * kDegToRad), a_y, a_cz + bend * std::sin(deg * kDegToRad));
                }
            };
            line(Vector3(a_plateX, a_y, a_topZ), Vector3(a_plateX, a_y, a_barZ + bend));
            bendArc(a_plateX - bend, a_barZ + bend, 0.0f, -90.0f);
            line(Vector3(a_plateX - bend, a_y, a_barZ), Vector3(a_tipX + bend, a_y, a_barZ));
            bendArc(a_tipX + bend, a_barZ - bend, 90.0f, 180.0f);
            line(Vector3(a_tipX, a_y, a_barZ - bend), Vector3(a_tipX, a_y, a_tipZ));
            path.emplace_back(a_tipX, a_y, a_tipZ);
            return path;
        }

        /// The bracket swept from its path, UVs in the iron box at one texel density along and across
        [[nodiscard]] LanternMesh MakeBracket(const std::vector<Vector3>& a_path)
        {
            float length = 0.0f;
            for (std::size_t k = 1; k < a_path.size(); ++k) {
                length += a_path[k].DistanceTo(a_path[k - 1]);
            }
            const float perUnit = (kLanternIronUvMax[0] - kLanternIronUvMin[0]) / length;
            if (kLanternBracketWidth * perUnit > kLanternIronUvMax[1] - kLanternIronUvMin[1]) {
                throw std::runtime_error("the bracket's width does not fit the iron UV box at its length's texel density");
            }
            LanternMesh mesh;
            // the width tapers towards the tip: both edges move in by the same share of the half width (the bevel kept)
            const float halfW = kLanternBracketWidth * 0.5f;
            const SectionMap taper = [length, halfW](std::size_t, float a_arc, const SectionPoint& a_p) {
                SectionPoint q = a_p;
                const float in = halfW * kLanternBracketTaper * (a_arc / length);
                q.a = a_p.a >= 0.0f ? a_p.a - in : a_p.a + in;
                return q;
            };
            (void)Sweep(mesh, a_path, BevelSection(kLanternBracketThickness * 0.5f, halfW, kLanternBracketBevel),
                [perUnit](float a_arc) { return kLanternIronUvMin[0] + a_arc * perUnit; },
                [perUnit](float a_a) { return kLanternIronUvMin[1] + (a_a + kLanternBracketWidth * 0.5f) * perUnit; }, taper);
            return mesh;
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

        // ---- The wisp

        constexpr std::uint32_t kClampWrapST = 3;  // BSEffectShaderProperty texture clamp mode WRAP_S_WRAP_T (scrolled UVs)
        constexpr std::uint32_t kAvHidden = 0x8000F;  // NiAVObject flags of the vanilla wisp's hidden emitter shapes (524303)

        /// A UV sphere of a_radius round the origin: a_segments round Z, a_rings from the +Z pole down; U round, V down;
        /// outward normals; one triangle per quad at each pole
        [[nodiscard]] LanternMesh MakeSphere(float a_radius, int a_segments, int a_rings)
        {
            if (!(a_radius > 0.0f) || a_segments < 3 || a_rings < 2) {
                throw std::runtime_error(fmt::format("a sphere of radius {} with {} segments and {} rings", a_radius, a_segments, a_rings));
            }
            LanternMesh mesh;
            for (int j = 0; j <= a_rings; ++j) {
                const float theta = kPi * static_cast<float>(j) / static_cast<float>(a_rings);
                for (int i = 0; i <= a_segments; ++i) {
                    const float phi = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(a_segments);
                    const Vector3 n(std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta));
                    (void)mesh.Add(n * a_radius, n, static_cast<float>(i) / static_cast<float>(a_segments),
                        static_cast<float>(j) / static_cast<float>(a_rings));
                }
            }
            const auto at = [a_segments](int a_i, int a_j) { return static_cast<std::uint16_t>(a_j * (a_segments + 1) + a_i); };
            for (int j = 0; j < a_rings; ++j) {
                for (int i = 0; i < a_segments; ++i) {
                    if (j == 0) {
                        mesh.Face(at(i, j), at(i + 1, j + 1), at(i, j + 1));  // at(i, 0) and at(i + 1, 0) are the pole
                    } else if (j == a_rings - 1) {
                        mesh.Face(at(i, j), at(i + 1, j), at(i + 1, j + 1));  // the bottom row is the other pole
                    } else {
                        mesh.Quad(at(i, j), at(i + 1, j), at(i + 1, j + 1), at(i, j + 1));
                    }
                }
            }
            return mesh;
        }

        /// One wisp sphere's look (GreyLantern.h, the wisp constants)
        struct WispLook
        {
            const char* name = "";
            float radius = 0.0f;
            std::uint32_t flags1 = 0;
            const char* source = "";
            const char* greyscale = "";        // "" with the palette off
            std::array<float, 4> base{};
            float emissive = 1.0f;
            std::array<float, 4> falloff{};
            std::uint16_t alpha = 0;
            std::array<float, 2> scroll{};     // seconds per U / V repeat, the sign the direction; 0: no controller
            bool pulse = false;                // the emissive multiple's pulse
        };

        [[nodiscard]] std::array<WispLook, 3> WispLooks()
        {
            const float dark = kLanternWispShellColour;
            return { { { kLanternWispCoreName, kLanternWispCoreRadius, kLanternWispFlags1Palette, kLanternWispNoise, kPalettePath,
                           { 1.0f, 1.0f, 1.0f, 1.0f }, kLanternWispCoreEmissive, kLanternWispCoreFalloff, kLanternWispAdditive,
                           kLanternWispCoreScroll, true },
                { kLanternWispShellName, kLanternWispShellRadius, kLanternWispFlags1Dark, kLanternWispNoise, "",
                    { dark, dark, dark, kLanternWispShellAlpha }, 1.0f, kLanternWispShellFalloff, kLanternWispNormal,
                    { -kLanternWispShellScroll[0], -kLanternWispShellScroll[1] }, false },
                { kLanternWispHaloName, kLanternWispHaloRadius, kLanternWispFlags1Palette, kLanternWispHaloTexture, kPalettePath,
                    { 1.0f, 1.0f, 1.0f, kLanternWispHaloAlpha }, kLanternWispHaloEmissive, kLanternWispHaloFalloff, kLanternWispAdditive,
                    { 0.0f, 0.0f }, false } } };
        }

        /// a_id taken out of every node's children and made a child of a_parent
        void Reparent(nifly::NifFile& a_nif, std::uint32_t a_id, nifly::NiNode& a_parent)
        {
            auto& hdr = a_nif.GetHeader();
            for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
                if (auto* node = hdr.GetBlock<nifly::NiNode>(id)) {
                    std::vector<std::uint32_t> children;
                    node->childRefs.GetIndices(children);
                    const auto end = std::remove(children.begin(), children.end(), a_id);
                    if (end != children.end()) {
                        children.erase(end, children.end());
                        node->childRefs.SetIndices(children);
                    }
                }
            }
            a_parent.childRefs.AddBlockRef(a_id);
        }

        /// A looping float controller's keys (time, value), linear
        using FloatKeys = std::vector<std::array<float, 2>>;

        /// a_controllers (controlled variable, keys) as BSEffectShaderPropertyFloatControllers chained on the shader a_shaderId,
        /// each looping over its keys' span (kLanternWispControllerFlags), its interpolator's own value unset as the
        /// vanilla wisp's
        void AddShaderControllers(nifly::NifFile& a_nif, std::uint32_t a_shaderId, const std::vector<std::pair<std::uint32_t, FloatKeys>>& a_controllers)
        {
            auto& hdr = a_nif.GetHeader();
            auto* shader = hdr.GetBlock<nifly::NiObjectNET>(a_shaderId);
            if (!shader) {
                throw std::runtime_error(fmt::format("block {} is no shader", a_shaderId));
            }
            nifly::NiBlockRef<nifly::NiTimeController>* link = &shader->controllerRef;
            for (const auto& [variable, keys] : a_controllers) {
                if (keys.size() < 2) {
                    throw std::runtime_error("a looping controller needs two keys");
                }
                auto data = std::make_unique<nifly::NiFloatData>();
                data->data.SetInterpolationType(nifly::LINEAR_KEY);
                for (const auto& [time, value] : keys) {
                    nifly::NiAnimationKey<float> key;
                    key.type = nifly::LINEAR_KEY;
                    key.time = time;
                    key.value = value;
                    data->data.AddKey(key);
                }
                auto interp = std::make_unique<nifly::NiFloatInterpolator>();
                interp->floatValue = nifly::NiFloatMin;
                interp->dataRef.index = hdr.AddBlock(std::move(data));
                auto ctrl = std::make_unique<nifly::BSEffectShaderPropertyFloatController>();
                ctrl->flags = kLanternWispControllerFlags;
                ctrl->frequency = 1.0f;
                ctrl->phase = 0.0f;
                ctrl->startTime = keys.front()[0];
                ctrl->stopTime = keys.back()[0];
                ctrl->targetRef.index = a_shaderId;
                ctrl->typeOfControlledVariable = variable;
                ctrl->interpolatorRef.index = hdr.AddBlock(std::move(interp));
                const std::uint32_t id = hdr.AddBlock(std::move(ctrl));
                link->index = id;
                link = &hdr.GetBlock<nifly::BSEffectShaderPropertyFloatController>(id)->nextControllerRef;
            }
        }

        /// The emissive multiple's pulse (kLanternWispCoreEmissive, kLanternWispPulseAmplitude, kLanternWispPulseHz): one
        /// period of a sine in kLanternWispPulseKeys linear steps, its last key its first
        [[nodiscard]] FloatKeys PulseKeys()
        {
            FloatKeys keys;
            const float period = 1.0f / kLanternWispPulseHz;
            for (int k = 0; k <= kLanternWispPulseKeys; ++k) {
                const float share = static_cast<float>(k) / static_cast<float>(kLanternWispPulseKeys);
                const float wave = k == kLanternWispPulseKeys ? 0.0f : std::sin(2.0f * kPi * share);
                keys.push_back({ period * share, kLanternWispCoreEmissive + kLanternWispPulseAmplitude * wave });
            }
            return keys;
        }

        /// a_look's shader controllers in chain order: the U and V offset scroll (0 -> +-1 over its seconds), the core's pulse
        [[nodiscard]] std::vector<std::pair<std::uint32_t, FloatKeys>> WispControllers(const WispLook& a_look)
        {
            std::vector<std::pair<std::uint32_t, FloatKeys>> controllers;
            for (int axis = 0; axis < 2; ++axis) {
                const float seconds = a_look.scroll[axis];
                if (seconds != 0.0f) {
                    controllers.push_back({ axis == 0 ? kLanternWispVarUOffset : kLanternWispVarVOffset,
                        { { 0.0f, 0.0f }, { std::fabs(seconds), seconds > 0.0f ? 1.0f : -1.0f } } });
                }
            }
            if (a_look.pulse) {
                controllers.push_back({ kLanternWispVarEmissive, PulseKeys() });
            }
            return controllers;
        }

        /// a_mesh (moved by a_at) as a wisp shape of a_look under a_parent: its BSEffectShaderProperty, NiAlphaProperty and
        /// controllers (U and V offset scroll, the core's pulse)
        nifly::NiShape* AddWispShape(nifly::NifFile& a_nif, nifly::NiNode& a_parent, const WispLook& a_look, const Vector3& a_at)
        {
            auto& hdr = a_nif.GetHeader();
            LanternMesh mesh = MakeSphere(a_look.radius, kLanternWispSegments, kLanternWispRings);
            for (auto& v : mesh.verts) {
                v += a_at;
            }
            auto* shape = a_nif.CreateShapeFromData(a_look.name, &mesh.verts, &mesh.tris, &mesh.uvs, &mesh.normals);
            auto* tri = dynamic_cast<nifly::BSTriShape*>(shape);
            if (!tri) {
                throw std::runtime_error(fmt::format("nifly made no BSTriShape for '{}'", a_look.name));
            }
            tri->SetNormals(true);
            tri->SetNormals(mesh.normals);
            a_nif.DeleteShader(shape);
            auto prop = std::make_unique<nifly::BSEffectShaderProperty>();
            prop->shaderFlags1 = a_look.flags1;
            prop->shaderFlags2 = kLanternWispFlags2;
            prop->sourceTexture.get() = a_look.source;
            prop->greyscaleTexture.get() = a_look.greyscale;
            prop->textureClampMode = kClampWrapST;
            prop->baseColor = nifly::Color4(a_look.base[0], a_look.base[1], a_look.base[2], a_look.base[3]);
            prop->baseColorScale = a_look.emissive;
            prop->falloffStartAngle = a_look.falloff[0];
            prop->falloffStopAngle = a_look.falloff[1];
            prop->falloffStartOpacity = a_look.falloff[2];
            prop->falloffStopOpacity = a_look.falloff[3];
            prop->softFalloffDepth = kLanternWispSoftDepth;
            const std::uint32_t shaderId = hdr.AddBlock(std::move(prop));
            shape->ShaderPropertyRef()->index = shaderId;
            auto alpha = std::make_unique<nifly::NiAlphaProperty>();
            alpha->flags = a_look.alpha;
            alpha->threshold = 0;
            (void)a_nif.AssignAlphaProperty(shape, std::move(alpha));
            AddShaderControllers(a_nif, shaderId, WispControllers(a_look));
            Reparent(a_nif, hdr.GetBlockID(shape), a_parent);
            return shape;
        }

        /// The smallest distance from a_point to the triangles a_tris over a_verts
        [[nodiscard]] float NearestTriangle(const std::vector<Vector3>& a_verts, const std::vector<nifly::Triangle>& a_tris, const Vector3& a_point)
        {
            float nearest = kFloatMax;
            for (const auto& t : a_tris) {
                if (t.p1 >= a_verts.size() || t.p2 >= a_verts.size() || t.p3 >= a_verts.size()) {
                    throw std::runtime_error("a frame triangle names a vertex past the list");
                }
                nearest = std::min(nearest, ClosestOnTriangle(a_point, a_verts[t.p1], a_verts[t.p2], a_verts[t.p3]).DistanceTo(a_point));
            }
            return nearest;
        }

        /// The bob and turn keys (kLanternWispBob*, kLanternWispKeyRate): one cycle of kLanternWispBobsPerTurn bobs and a turn
        struct WispMotion
        {
            float cycle = 0.0f;
            std::vector<std::array<float, 4>> translations;  // time, x, y, z
        };

        [[nodiscard]] WispMotion MakeWispMotion(const Vector3& a_centre)
        {
            WispMotion motion;
            const float bobPeriod = 1.0f / kLanternWispBobHz;
            motion.cycle = bobPeriod * static_cast<float>(kLanternWispBobsPerTurn);
            const int keys = static_cast<int>(std::lround(motion.cycle * kLanternWispKeyRate));
            for (int k = 0; k <= keys; ++k) {
                const float share = static_cast<float>(k) / static_cast<float>(keys);
                const float wave = k == keys ? 0.0f : std::sin(2.0f * kPi * static_cast<float>(kLanternWispBobsPerTurn) * share);
                motion.translations.push_back({ motion.cycle * share, a_centre.x, a_centre.y, a_centre.z + kLanternWispBobAmplitude * wave });
            }
            return motion;
        }

        /// a_motion on a_node as one looping NiTransformController (apohoverlight01.nif's pattern: the interpolator's own
        /// values unset, linear translation keys, XYZ rotation keys; here Z turns once, 0 -> 2 pi, linear)
        void AddWispMotion(nifly::NifFile& a_nif, nifly::NiNode& a_node, const WispMotion& a_motion)
        {
            auto& hdr = a_nif.GetHeader();
            auto data = std::make_unique<nifly::NiTransformData>();
            data->translations.SetInterpolationType(nifly::LINEAR_KEY);
            for (const auto& t : a_motion.translations) {
                nifly::NiAnimationKey<Vector3> key;
                key.type = nifly::LINEAR_KEY;
                key.time = t[0];
                key.value = Vector3(t[1], t[2], t[3]);
                data->translations.AddKey(key);
            }
            data->rotationType = nifly::XYZ_ROTATION_KEY;
            const std::array<std::pair<nifly::NiAnimationKeyGroup<float>*, float>, 3> axes{ { { &data->xRotations, 0.0f },
                { &data->yRotations, 0.0f }, { &data->zRotations, 2.0f * kPi } } };
            for (const auto& [group, turn] : axes) {
                group->SetInterpolationType(nifly::LINEAR_KEY);
                for (const auto& [time, value] : { std::pair<float, float>{ 0.0f, 0.0f }, std::pair<float, float>{ a_motion.cycle, turn } }) {
                    nifly::NiAnimationKey<float> key;
                    key.type = nifly::LINEAR_KEY;
                    key.time = time;
                    key.value = value;
                    group->AddKey(key);
                }
            }
            auto interp = std::make_unique<nifly::NiTransformInterpolator>();
            interp->translation = nifly::NiVec3Min;
            interp->rotation.w = interp->rotation.x = interp->rotation.y = interp->rotation.z = nifly::NiFloatMin;
            interp->scale = nifly::NiFloatMin;
            interp->dataRef.index = hdr.AddBlock(std::move(data));
            auto ctrl = std::make_unique<nifly::NiTransformController>();
            ctrl->flags = kLanternWispControllerFlags;
            ctrl->frequency = 1.0f;
            ctrl->phase = 0.0f;
            ctrl->startTime = 0.0f;
            ctrl->stopTime = a_motion.cycle;
            ctrl->targetRef.index = hdr.GetBlockID(&a_node);
            ctrl->interpolatorRef.index = hdr.AddBlock(std::move(interp));
            a_node.controllerRef.index = hdr.AddBlock(std::move(ctrl));
        }

        /// fxwispparticleattach.nif's puffs and strips (kLanternWispParticles) cloned into a_nif under a_node, scaled
        /// (kLanternWispParticleScale), emitting from a_emitter, their gravity about a_node, their bombs left out, their
        /// palette GradBlackFlame.dds. Each system's reach (the emitter sphere + speed x life + gravity x life^2 / 2 + its
        /// radius) into a_reach. Throws when a system is missing or a clone names a block outside itself, a_node and a_emitter.
        void AddWispParticles(nifly::NifFile& a_nif, const std::filesystem::path& a_source, nifly::NiNode& a_node, std::uint32_t a_emitter,
            std::array<float, 2>& a_reach, std::vector<std::string>& a_log)
        {
            nifly::NifFile source;
            LoadNif(source, a_source);
            auto& sh = source.GetHeader();
            auto& dh = a_nif.GetHeader();
            const std::uint32_t nodeId = dh.GetBlockID(&a_node);
            const float s = kLanternWispParticleScale;
            for (std::size_t n = 0; n < kLanternWispParticles.size(); ++n) {
                std::uint32_t id = nifly::NIF_NPOS;
                for (std::uint32_t b = 0; b < sh.GetNumBlocks(); ++b) {
                    if (auto* system = sh.GetBlock<nifly::NiParticleSystem>(b); system && system->name.get() == kLanternWispParticles[n]) {
                        id = b;
                        break;
                    }
                }
                if (id == nifly::NIF_NPOS) {
                    throw std::runtime_error(fmt::format("{} has no particle system '{}'", kLanternWispSource, kLanternWispParticles[n]));
                }
                auto* system = sh.GetBlock<nifly::NiParticleSystem>(id);
                // the bombs out of the source copy before the clone: the modifiers and the controllers that switch them
                std::set<std::string> bombs;
                std::vector<std::uint32_t> kept;
                for (const auto& ref : system->modifierRefs) {
                    if (auto* bomb = sh.GetBlock<nifly::NiPSysBombModifier>(ref)) {
                        bombs.insert(bomb->name.get());
                    } else {
                        kept.push_back(ref.index);
                    }
                }
                system->modifierRefs.SetIndices(kept);
                int switches = 0;
                for (nifly::NiBlockRef<nifly::NiTimeController>* link = &system->controllerRef;;) {
                    auto* ctrl = sh.GetBlock<nifly::NiTimeController>(*link);
                    if (!ctrl) {
                        break;
                    }
                    auto* active = dynamic_cast<nifly::NiPSysModifierActiveCtlr*>(ctrl);
                    if (active && bombs.count(active->modifierName.get()) != 0) {
                        link->index = ctrl->nextControllerRef.index;
                        ++switches;
                    } else {
                        link = &ctrl->nextControllerRef;
                    }
                }
                HandGraph::Remap map;
                const std::uint32_t added = HandGraph::CloneGraph(dh, sh, id, id, map);
                HandGraph::RemapPointers(dh, map);
                std::set<std::uint32_t> clone;
                for (const auto& [oldId, newId] : map) {
                    clone.insert(newId);
                }
                float speed = 0.0f;
                float life = 0.0f;
                float radius = 0.0f;
                float gravity = 0.0f;
                std::string emitterLine;
                for (const auto newId : clone) {
                    if (auto* emitter = dh.GetBlock<nifly::NiPSysEmitter>(newId)) {
                        emitter->radius *= s;
                        emitter->radiusVariation *= s;
                        emitter->speed *= s;
                        emitter->speedVariation *= s;
                        speed = std::max(speed, emitter->speed);
                        life = std::max(life, emitter->lifeSpan);
                        radius = std::max(radius, emitter->radius);
                        emitterLine += fmt::format(" emitter '{}' radius {:.4f} +/- {:.4f}, speed {:.4f} +/- {:.4f}, life {:.4f};", emitter->name.get(),
                            emitter->radius, emitter->radiusVariation, emitter->speed, emitter->speedVariation, emitter->lifeSpan);
                        if (auto* mesh = dynamic_cast<nifly::NiPSysMeshEmitter*>(emitter)) {
                            mesh->meshRefs.SetIndices({ a_emitter });
                        }
                    }
                    if (auto* g = dh.GetBlock<nifly::NiPSysGravityModifier>(newId)) {
                        g->strength *= s;
                        g->gravityObjRef.index = nodeId;
                        gravity = std::max(gravity, std::fabs(g->strength));
                        emitterLine += fmt::format(" gravity '{}' strength {:.4f} about '{}';", g->name.get(), g->strength, kLanternWispNode);
                    }
                    if (auto* shader = dh.GetBlock<nifly::BSEffectShaderProperty>(newId)) {
                        emitterLine += fmt::format(" palette '{}' -> '{}';", shader->greyscaleTexture.get(), kPalettePath);
                        shader->greyscaleTexture.get() = kPalettePath;
                        shader->shaderFlags1 |= kSlsf1GreyscaleToPaletteColor;
                    }
                }
                // nothing else outside the clone
                for (const auto newId : clone) {
                    auto* block = dh.GetBlock<nifly::NiObject>(newId);
                    std::set<nifly::NiRef*> refs;
                    std::set<nifly::NiPtr*> ptrs;
                    HandGraph::ChildRefs(*block, refs);
                    block->GetPtrs(ptrs);
                    std::vector<std::uint32_t> named;
                    for (auto* ref : refs) {
                        named.push_back(ref->index);
                    }
                    for (auto* ptr : ptrs) {
                        named.push_back(ptr->index);
                    }
                    for (const auto target : named) {
                        if (target != nifly::NIF_NPOS && clone.count(target) == 0 && target != a_emitter && target != nodeId) {
                            throw std::runtime_error(fmt::format("the copy of '{}': its {} names block {} outside the copy", kLanternWispParticles[n],
                                block->GetBlockName(), target));
                        }
                    }
                }
                auto* copy = dh.GetBlock<nifly::NiParticleSystem>(added);
                copy->transform.Clear();
                a_node.childRefs.AddBlockRef(added);
                a_reach[n] = kLanternWispCoreRadius + speed * life + 0.5f * gravity * life * life + radius;
                a_log.push_back(fmt::format("wisp particles '{}' ({}): {} blocks copied, {} bomb modifiers and {} controllers switching them left out, "
                                            "x {} (kLanternWispParticleScale):{} reach about {:.3f} from the centre",
                    kLanternWispParticles[n], copy->GetBlockName(), clone.size(), bombs.size(), switches, s, emitterLine, a_reach[n]));
            }
        }

        /// The ground model's wisp (GreyLantern.h, the wisp constants) in a_nif: HoMBF_Wisp at the cage centre with the three
        /// spheres round its origin, the hidden emitter sphere, the bob and turn, the particles; BSXFlags Animated. a_frame and
        /// a_tris: the frame in root space, for the clearance (at every bob key) and the particles' reach. Throws on a failure.
        void AddGroundWisp(nifly::NifFile& a_nif, const std::vector<std::filesystem::path>& a_inputs, const std::vector<Vector3>& a_frame,
            const std::vector<nifly::Triangle>& a_tris, LanternBuildResult& a_result)
        {
            auto& log = a_result.log;
            auto& hdr = a_nif.GetHeader();
            auto* root = a_nif.GetRootNode();
            const Vector3 centre = ToVector(a_result.cageCentre);
            MatTransform at;
            at.translation = centre;
            auto* node = a_nif.AddNode(kLanternWispNode, at, root);
            if (!node || ParentOf(a_nif, hdr.GetBlockID(node)) != hdr.GetBlockID(root)) {
                throw std::runtime_error(fmt::format("nifly did not add the node '{}' under the root", kLanternWispNode));
            }
            for (const auto& look : WispLooks()) {
                (void)AddWispShape(a_nif, *node, look, Vector3());
            }
            // the hidden emitter: a plain sphere without a shader, as the vanilla wisp's EmitGeo shapes
            LanternMesh emit = MakeSphere(kLanternWispCoreRadius, kLanternWispEmitterSegments, kLanternWispEmitterRings);
            auto* emitter = a_nif.CreateShapeFromData(kLanternWispEmitterName, &emit.verts, &emit.tris, &emit.uvs, &emit.normals);
            auto* emitterTri = dynamic_cast<nifly::BSTriShape*>(emitter);
            if (!emitterTri) {
                throw std::runtime_error(fmt::format("nifly made no BSTriShape for '{}'", kLanternWispEmitterName));
            }
            emitterTri->SetNormals(true);
            emitterTri->SetNormals(emit.normals);
            a_nif.DeleteShader(emitter);
            emitter->flags = kAvHidden;
            const std::uint32_t emitterId = hdr.GetBlockID(emitter);
            Reparent(a_nif, emitterId, *node);

            // the bob and the turn, and the halo's clearance at every bob key (the keys 1 / kLanternWispKeyRate s apart sample
            // the bob's path to well under 0.1 unit)
            const WispMotion motion = MakeWispMotion(centre);
            AddWispMotion(a_nif, *node, motion);
            float nearest = kFloatMax;
            a_result.wispGroundClearance = kFloatMax;
            float low = kFloatMax;
            float high = -kFloatMax;
            for (const auto& t : motion.translations) {
                const float d = NearestTriangle(a_frame, a_tris, Vector3(t[1], t[2], t[3]));
                nearest = std::min(nearest, d);
                low = std::min(low, t[3]);
                high = std::max(high, t[3]);
            }
            a_result.wispGroundClearance = nearest - kLanternWispHaloRadius;
            const float restNearest = NearestTriangle(a_frame, a_tris, centre);
            log.push_back(fmt::format("wisp (ground): node '{}' at {}; {} translation keys over {:.4f} s (bob {} at {} Hz, z {:.4f}..{:.4f}; one turn "
                                      "about Z every {} bobs; flags 0x{:04X}, the pattern of vanilla meshes\\dlc02\\dungeons\\apocrypha\\clutter\\"
                                      "apohoverlight01.nif); the frame's nearest triangle {:.4f} from the halo's centre along the bob, {:.4f} off the halo "
                                      "(kLanternWispClearance {})",
                kLanternWispNode, Text(a_result.cageCentre), motion.translations.size(), motion.cycle, kLanternWispBobAmplitude, kLanternWispBobHz,
                low, high, kLanternWispBobsPerTurn, kLanternWispControllerFlags, nearest, a_result.wispGroundClearance, kLanternWispClearance));
            if (a_result.wispGroundClearance < kLanternWispClearance) {
                throw std::runtime_error(fmt::format("the ground wisp's halo comes within {:.4f} of the frame along its bob (kLanternWispClearance {})",
                    a_result.wispGroundClearance, kLanternWispClearance));
            }

            // the particles, which must stay inside the cage on their mean values
            AddWispParticles(a_nif, Need(a_inputs, kLanternWispSource), *node, emitterId, a_result.particleReach, log);
            for (std::size_t n = 0; n < kLanternWispParticles.size(); ++n) {
                log.push_back(fmt::format("wisp particles '{}': reach {:.4f} from the centre, the frame's nearest triangle {:.4f} from it",
                    kLanternWispParticles[n], a_result.particleReach[n], restNearest));
                if (a_result.particleReach[n] + kLanternWispBobAmplitude > restNearest) {
                    throw std::runtime_error(fmt::format("the wisp particles '{}' reach {:.4f} (+ the bob {}) from the centre, past the frame's nearest "
                                                         "triangle {:.4f}: lower kLanternWispParticleScale",
                        kLanternWispParticles[n], a_result.particleReach[n], kLanternWispBobAmplitude, restNearest));
                }
            }

            auto* bsx = FindBsx(a_nif);
            if (!bsx) {
                throw std::runtime_error(fmt::format("{} has no BSXFlags on its root", kLanternFrameSource));
            }
            const std::uint32_t before = bsx->integerData;
            bsx->integerData |= kBsxAnimated;
            log.push_back(fmt::format("ground BSXFlags {} -> {} (Animated bit 0x{:X}, as the animated vanilla statics carry it; no Addon bit: no "
                                      "addon node)",
                before, bsx->integerData, kBsxAnimated));
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
            float bandBottom = kFloatMax;
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
                bandBottom = std::min(bandBottom, band.bottom);
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

            // the strap on the bodies
            auto strap = MakeStrap(bodies, pelvis.translation, bandTop, bandBottom);
            log.insert(log.end(), strap.log.begin(), strap.log.end());
            a_result.strapOuterBottomX = strap.outerBottomX;
            a_result.strapTop = strap.top;
            a_result.strapInnerBottom = strap.innerBottom;
            a_result.strapBottom = strap.bottom;
            for (const auto& body : bodies) {
                const auto gap = SurfaceGap(body, strap.mesh.verts);
                log.push_back(fmt::format("clearance strap / {}: {}", body.name, GapText(gap)));
                if (gap.inside > 0 || gap.nearest < kLanternStrapMargin) {
                    throw std::runtime_error(fmt::format("the strap comes within {:.3f} of {} ({} points inside; kLanternStrapMargin {})",
                        gap.nearest, body.name, gap.inside, kLanternStrapMargin));
                }
            }

            // the lantern's points before the translation onto the ring (it hangs vertically: no tilt)
            std::vector<Vector3> framePoints;
            for (std::size_t i = 0; i < frameRoot.size(); ++i) {
                framePoints.push_back(shapeA(frameRoot[i], isHandle[i]));
            }
            // the wisp at the cage centre: its halo's vertices stand for it in the body clearance (the largest sphere)
            std::vector<Vector3> wispPoints;
            for (const auto& v : MakeSphere(kLanternWispHaloRadius, kLanternWispSegments, kLanternWispRings).verts) {
                wispPoints.push_back(cageA + v);
            }
            // the lugs under the handle's feet (before the translation, like the points above)
            auto lugs = MakeLugs(framePoints, frameTri->triangles, isHandle, frameTri->UpdateRawUvs());
            log.insert(log.end(), lugs.log.begin(), lugs.log.end());

            // the bracket, the ring welded to its tip and the lantern on the ring: the shortest bracket that clears both bodies
            const float barZ = strap.bottom - kLanternBracketBelow;
            const float tipZ = barZ - kLanternBracketDrop;  // the ring's top tube centre
            const float ringZ = tipZ - kLanternRingRadius;
            const float gripZ = strap.bottom + kLanternBracketGrip;
            // the ring's UVs: the box's U round the ring, V round the tube at the same texel density
            const nifly::Vector2 ringUvMin(kLanternIronUvMin[0], kLanternIronUvMin[1]);
            const nifly::Vector2 ringUvMax(kLanternIronUvMax[0],
                kLanternIronUvMin[1] + (kLanternIronUvMax[0] - kLanternIronUvMin[0]) * kLanternRingTube / kLanternRingRadius);
            log.push_back(fmt::format("iron UV box (kLanternIronUvMin/Max) u {}..{} v {}..{} of {}: the ring u {:.4f}..{:.4f} v "
                                      "{:.4f}..{:.4f}, the bracket from u {} v {} at its length's texel density",
                kLanternIronUvMin[0], kLanternIronUvMax[0], kLanternIronUvMin[1], kLanternIronUvMax[1], kLanternDiffuseSource,
                ringUvMin.u, ringUvMax.u, ringUvMin.v, ringUvMax.v, kLanternIronUvMin[0], kLanternIronUvMin[1]));
            log.push_back(fmt::format("bracket: plate at the strap's outer layer centreline x {:.4f} from z {:.4f} (the strap's lower end + "
                                      "kLanternBracketGrip {}) down, bar at z {:.4f} (kLanternBracketBelow {}), bends radius {}, {} x {} "
                                      "(kLanternBracketWidth x Thickness, edges bevelled {}, the width tapered {} to the tip), tip down {} "
                                      "(kLanternBracketDrop) to the ring's top tube centre z {:.4f}; ring centre z {:.4f}",
                strap.outerBottomX, gripZ, kLanternBracketGrip, barZ, kLanternBracketBelow, kLanternBracketBend, kLanternBracketWidth,
                kLanternBracketThickness, kLanternBracketBevel, kLanternBracketTaper, kLanternBracketDrop, tipZ, ringZ));
            LanternMesh bracketMesh;
            LanternMesh ringMesh;
            Vector3 ring;
            Vector3 contact;
            Vector3 move;
            float length = -1.0f;
            for (int step = 0;; ++step) {
                const float tryLength = kLanternBracketLength + static_cast<float>(step) * kLanternBracketLengthStep;
                if (tryLength > kLanternBracketLengthMax + 1e-4f) {
                    break;
                }
                const Vector3 tryRing(strap.outerBottomX - tryLength, strap.y, ringZ);
                const Vector3 tryContact = tryRing - Vector3(0.0f, 0.0f, kLanternRingRadius - kLanternRingTube - wire);
                const Vector3 tryMove = tryContact - apex;
                auto tryBracket = MakeBracket(BracketPath(strap.y, strap.outerBottomX, gripZ, barZ, tryRing.x, tipZ));
                auto tryRingMesh = MakeRing(tryRing, ringUvMin, ringUvMax);
                std::vector<Vector3> lantern;
                lantern.reserve(framePoints.size() + wispPoints.size() + lugs.mesh.verts.size());
                for (const auto* points : { &framePoints, &wispPoints, &lugs.mesh.verts }) {
                    for (const auto& p : *points) {
                        lantern.push_back(p + tryMove);
                    }
                }
                bool clear = true;
                std::string line = fmt::format("bracket length {} (ring centre x {:.4f}):", tryLength, tryRing.x);
                for (const auto& [part, points, margin] :
                    { std::tuple<const char*, const std::vector<Vector3>*, float>{ "lantern", &lantern, kLanternBodyMargin },
                        std::tuple<const char*, const std::vector<Vector3>*, float>{ "bracket", &tryBracket.verts, kLanternStrapMargin },
                        std::tuple<const char*, const std::vector<Vector3>*, float>{ "ring", &tryRingMesh.verts, kLanternStrapMargin } }) {
                    for (const auto& body : bodies) {
                        const auto gap = SurfaceGap(body, *points);
                        clear = clear && gap.inside == 0 && gap.nearest >= margin;
                        line += fmt::format(" {} / {}: {};", part, body.name, GapText(gap));
                    }
                }
                log.push_back(line);
                if (clear) {
                    length = tryLength;
                    bracketMesh = std::move(tryBracket);
                    ringMesh = std::move(tryRingMesh);
                    ring = tryRing;
                    contact = tryContact;
                    move = tryMove;
                    break;
                }
            }
            if (length < 0.0f) {
                throw std::runtime_error(fmt::format("at every bracket length from {} to {} (kLanternBracketLength..Max, step {}) a part "
                                                     "comes nearer to a body than its margin (lantern {}, bracket and ring {}): stopped",
                    kLanternBracketLength, kLanternBracketLengthMax, kLanternBracketLengthStep, kLanternBodyMargin, kLanternStrapMargin));
            }
            a_result.bracketLength = length;
            a_result.ringCentre = ToArray(ring);
            a_result.contact = ToArray(contact);
            a_result.handleWire = wire;
            a_result.bracket = bracketMesh.Bounds();
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
            log.push_back(fmt::format("bracket length {} (kLanternBracketLength {}): ring centre {}, {:.4f} out from the strap's outer "
                                      "layer at its lower end (x {:.4f}); bracket {}",
                length, kLanternBracketLength, Text(a_result.ringCentre), strap.outerBottomX - ring.x, strap.outerBottomX,
                BoxText(a_result.bracket)));

            const auto place = [&](const Vector3& a_root, bool a_handle) { return shapeA(a_root, a_handle) + move; };
            const auto placeNormal = [&](const Vector3& a_n, bool a_handle) { return normalA(a_n, a_handle); };
            a_result.wornCage = ToArray(cageA + move);
            a_result.wispWorn = a_result.wornCage;
            log.push_back(fmt::format("lantern: hangs vertically (no tilt) from the ring, its handle's top wire on the ring's bottom tube "
                                      "at {}; cage centre {}; the wisp moved with it",
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

            // the wisp: core, shell and halo at the cage centre, the frame's nearest triangle at least kLanternWispClearance off the
            // halo's sphere
            {
                auto* tri = dynamic_cast<nifly::BSTriShape*>(wornFrame);
                std::vector<Vector3> placed;
                for (const auto& vertex : tri->vertData) {
                    placed.push_back(vertex.vert);
                }
                const Vector3 centre = ToVector(a_result.wispWorn);
                const float nearest = NearestTriangle(placed, tri->triangles, centre);
                a_result.wispWornClearance = nearest - kLanternWispHaloRadius;
                log.push_back(fmt::format("wisp (worn): centre {}, radii core {} / shell {} / halo {} (kLanternWispCore/Shell/HaloRadius); the "
                                          "frame's nearest triangle {:.4f} from the centre, {:.4f} off the halo (kLanternWispClearance {})",
                    Text(a_result.wispWorn), kLanternWispCoreRadius, kLanternWispShellRadius, kLanternWispHaloRadius, nearest,
                    a_result.wispWornClearance, kLanternWispClearance));
                if (a_result.wispWornClearance < kLanternWispClearance) {
                    throw std::runtime_error(fmt::format("the worn wisp's halo comes within {:.4f} of the frame (kLanternWispClearance {})",
                        a_result.wispWornClearance, kLanternWispClearance));
                }
                for (const auto& look : WispLooks()) {
                    (void)adopt(AddWispShape(worn, *root, look, centre));
                }
            }

            // the strap, the bracket and the ring (made in character space already)
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
            auto* strapShape = adopt(AddMeshShape(worn, kLanternStrapName, strap.mesh, *wornFrame, kLanternStrapDiffuse, kLanternStrapNormal));
            (void)adopt(AddMeshShape(worn, kLanternBracketName, bracketMesh, *wornFrame, kLanternDiffuse, frameNormal));
            (void)adopt(AddMeshShape(worn, kLanternRingName, ringMesh, *wornFrame, kLanternDiffuse, frameNormal));
            (void)adopt(AddMeshShape(worn, kLanternLugName, lugs.mesh, *wornFrame, kLanternDiffuse, frameNormal));
            {
                auto* lit = hdr.GetBlock<nifly::BSLightingShaderProperty>(strapShape->ShaderPropertyRef());
                if (!lit) {
                    throw std::runtime_error("the strap has no lighting shader");
                }
                log.push_back(fmt::format("strap shader: glossiness {} -> {} (kLanternStrapGlossiness), specular strength {} -> {} "
                                          "(kLanternStrapSpecularStrength), specular colour {} -> {} (kLanternStrapSpecularColor; "
                                          "meshes\\clutter\\common\\leatherstrips01.nif)",
                    lit->glossiness, kLanternStrapGlossiness, lit->specularStrength, kLanternStrapSpecularStrength,
                    Text(ToArray(lit->specularColor)), Text(kLanternStrapSpecularColor)));
                lit->glossiness = kLanternStrapGlossiness;
                lit->specularStrength = kLanternStrapSpecularStrength;
                lit->specularColor = ToVector(kLanternStrapSpecularColor);
            }
            log.push_back(fmt::format("strap: {} vertices, {} triangles, centreline {:.3f} long, {}", strap.mesh.verts.size(),
                strap.mesh.tris.size(), strap.length, BoxText(strap.mesh.Bounds())));
            log.push_back(fmt::format("bracket: {} vertices, {} triangles, {}", bracketMesh.verts.size(), bracketMesh.tris.size(),
                BoxText(bracketMesh.Bounds())));
            log.push_back(fmt::format("ring: {} vertices, {} triangles, {}", ringMesh.verts.size(), ringMesh.tris.size(),
                BoxText(ringMesh.Bounds())));
            log.push_back(fmt::format("lugs: {} vertices, {} triangles, {}", lugs.mesh.verts.size(), lugs.mesh.tris.size(),
                BoxText(lugs.mesh.Bounds())));

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
            // no GreyEffects pass: it would put GradBlackFlameAdd.dds on the additive core; the wisp takes kPalettePath as made
            SaveNew(worn, a_dataDir / kLanternWornNif);

            const Vector3 ringBottom = ring - Vector3(0.0f, 0.0f, kLanternRingRadius);
            a_result.summary.push_back(fmt::format("handle top wire centre (world) {}; ring centre {}; ring bottom tube centre {}; the wire "
                                                   "sits {:.4f} above the tube centre (tube {} + wire {:.4f}), {:.4f} below the ring "
                                                   "centre, {:.4f} off its vertical",
                Text(a_result.contact), Text(a_result.ringCentre), Text(ToArray(ringBottom)), contact.z - ringBottom.z, kLanternRingTube,
                wire, ring.z - contact.z, std::hypot(contact.x - ring.x, contact.y - ring.y)));
            a_result.summary.push_back(fmt::format("lantern hung vertically (no tilt); cage centre {}; scale {:.5f}", Text(a_result.wornCage),
                scale));
            a_result.summary.push_back(fmt::format("bracket length {} (kLanternBracketLength {}, max {}): ring centre {:.4f} out from the "
                                                   "strap's outer layer at its lower end (x {:.4f}); bracket bounds {}",
                length, kLanternBracketLength, kLanternBracketLengthMax, strap.outerBottomX - ring.x, strap.outerBottomX,
                BoxText(a_result.bracket)));
            for (const auto& band : a_result.bands) {
                a_result.summary.push_back(fmt::format("belt band {} '{}' at the left hip: top z {:.4f}, bottom z {:.4f} ({} vertices)", band.body,
                    band.shape, band.top, band.bottom, band.vertices));
            }
            a_result.summary.push_back(fmt::format("strap: fold top z {:.4f} (highest band top + {}), inner end z {:.4f} (lowest band bottom - "
                                                   "{}), outer lower end z {:.4f} (top - {}), the bracket's plate in it",
                a_result.strapTop, kLanternStrapOverBand, a_result.strapInnerBottom, kLanternStrapUnderBand, a_result.strapBottom,
                kLanternStrapOuterLength));
            a_result.summary.push_back(fmt::format("handle {:.4f} tall (x {:.5f} along Z about its feet, kLanternHandleHeight {})",
                a_result.handleHeight, handleScale, kLanternHandleHeight));
            for (int side = 0; side < 2; ++side) {
                const auto& foot = a_result.feet[side];
                a_result.summary.push_back(fmt::format("handle foot {} at {}: lowest z {:.4f}, {:.4f} above the lid (z {:.4f}); lug {}",
                    side, Text(foot.centre), foot.footZ, foot.footZ - foot.lidZ, foot.lidZ, BoxText(foot.lug)));
            }
            a_result.summary.push_back(fmt::format("strap textures {} / {} (vanilla leather); bracket, ring and lugs {} / {}", kLanternStrapDiffuse,
                kLanternStrapNormal, kLanternDiffuse, frameNormal));
            a_result.summary.push_back(fmt::format("wisp (worn): centre {}, the frame {:.4f} off the halo (kLanternWispClearance {})",
                Text(a_result.wispWorn), a_result.wispWornClearance, kLanternWispClearance));
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
            log.push_back(fmt::format("cage centre (walls' X/Y, bounds' middle Z) {} -> the wisp node '{}'", Text(result.cageCentre),
                kLanternWispNode));

            // the wisp goes into the copy that is saved: the worn step clones the frame from the original, which stays without it
            // (SaveNif's string-table pass also writes through the file it saves)
            nifly::NifFile copy = ground;
            {
                std::vector<Vector3> frameRoot;
                for (const auto& v : verts) {
                    frameRoot.push_back(toRoot.ApplyTransform(v));
                }
                std::vector<nifly::Triangle> tris;
                if (!frame->GetTriangles(tris) || tris.empty()) {
                    throw std::runtime_error("the frame has no triangles");
                }
                AddGroundWisp(copy, a_inputs, frameRoot, tris, result);
            }
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

    namespace
    {
        /// The ground model's frame: its one shape with a lighting shader (the wisp's are effect shaders or none); nullptr
        /// when there is not exactly one
        [[nodiscard]] nifly::NiShape* GroundFrame(const nifly::NifFile& a_nif)
        {
            nifly::NiShape* found = nullptr;
            for (auto* shape : a_nif.GetShapes()) {
                if (a_nif.GetHeader().GetBlock<nifly::BSLightingShaderProperty>(shape->ShaderPropertyRef())) {
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
        const std::array<Part, 4> parts{ { { "lantern",
                                               { kLanternWornFrameName, kLanternWispCoreName, kLanternWispShellName, kLanternWispHaloName,
                                                   kLanternLugName },
                                               kLanternBodyMargin },
            { "strap", { kLanternStrapName }, kLanternStrapMargin }, { "bracket", { kLanternBracketName }, kLanternStrapMargin },
            { "ring", { kLanternRingName }, kLanternStrapMargin } } };
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

    LanternWispReadBack WispReadBack(const std::filesystem::path& a_dataDir)
    {
        LanternWispReadBack out;
        // the worn wisp: the common bounds centre of its three spheres, against the worn frame's triangles
        {
            nifly::NifFile worn;
            LoadNif(worn, a_dataDir / kLanternWornNif);
            auto* frame = FindShape(worn, kLanternWornFrameName);
            std::vector<Vector3> verts;
            std::vector<nifly::Triangle> tris;
            if (!frame || !worn.GetVertsForShape(frame, verts) || !frame->GetTriangles(tris)) {
                throw std::runtime_error(fmt::format("lantern_worn.nif has no frame '{}'", kLanternWornFrameName));
            }
            LanternBox box = EmptyBox();
            for (const char* name : { kLanternWispCoreName, kLanternWispShellName, kLanternWispHaloName }) {
                std::vector<Vector3> sphere;
                auto* shape = FindShape(worn, name);
                if (!shape || !worn.GetVertsForShape(shape, sphere) || sphere.empty()) {
                    throw std::runtime_error(fmt::format("lantern_worn.nif has no wisp shape '{}'", name));
                }
                for (const auto& v : sphere) {
                    Grow(box, v);  // skin space = character space
                }
            }
            out.wornCentre = box.Centre();
            out.wornClearance = NearestTriangle(verts, tris, ToVector(out.wornCentre)) - kLanternWispHaloRadius;
        }
        // the ground wisp: the node's translation keys (its centre at each), against the ground frame in root space
        {
            nifly::NifFile ground;
            LoadNif(ground, a_dataDir / kLanternGroundNif);
            auto& hdr = ground.GetHeader();
            auto* frame = GroundFrame(ground);
            std::vector<Vector3> verts;
            std::vector<nifly::Triangle> tris;
            if (!frame || !ground.GetVertsForShape(frame, verts) || !frame->GetTriangles(tris)) {
                throw std::runtime_error("lantern_gnd.nif has no frame");
            }
            const MatTransform toRoot = ToRootSpace(ground, hdr.GetBlockID(frame));
            for (auto& v : verts) {
                v = toRoot.ApplyTransform(v);
            }
            nifly::NiNode* node = nullptr;
            for (std::uint32_t id = 0; id < hdr.GetNumBlocks() && !node; ++id) {
                if (auto* n = hdr.GetBlock<nifly::NiNode>(id); n && n->name.get() == kLanternWispNode) {
                    node = n;
                }
            }
            if (!node) {
                throw std::runtime_error(fmt::format("lantern_gnd.nif has no node '{}'", kLanternWispNode));
            }
            const MatTransform nodeToRoot = ToRootSpace(ground, hdr.GetBlockID(node));
            out.groundCentre = ToArray(nodeToRoot.translation);
            const auto controller = ReadController(hdr, node->controllerRef.index);
            if (controller.translations.empty()) {
                throw std::runtime_error(fmt::format("the node '{}' has no translation keys", kLanternWispNode));
            }
            // the keys are the node's translation in its parent's space; the parent is the root (checked by VerifyLantern)
            out.groundClearance = kFloatMax;
            out.bobLow = kFloatMax;
            out.bobHigh = -kFloatMax;
            for (const auto& t : controller.translations) {
                out.groundClearance = std::min(out.groundClearance, NearestTriangle(verts, tris, Vector3(t[1], t[2], t[3])));
                out.bobLow = std::min(out.bobLow, t[3]);
                out.bobHigh = std::max(out.bobHigh, t[3]);
            }
            out.groundClearance -= kLanternWispHaloRadius;
        }
        return out;
    }

    std::array<float, 2> StrapLayerGap(const std::filesystem::path& a_dataDir)
    {
        nifly::NifFile worn;
        LoadNif(worn, a_dataDir / kLanternWornNif);
        auto* strap = FindShape(worn, kLanternStrapName);
        std::vector<Vector3> verts;
        if (!strap || !worn.GetVertsForShape(strap, verts) || verts.empty()) {
            throw std::runtime_error(fmt::format("lantern_worn.nif has no strap '{}'", kLanternStrapName));
        }
        LanternBox box = EmptyBox();
        for (const auto& v : verts) {
            Grow(box, v);
        }
        const float centreY = box.Centre()[1];
        const float band = kLanternStrapWidth * 0.25f;
        std::array<float, 2> gap{ kFloatMax, -kFloatMax };
        // windows kLanternStrapStep tall (each layer has a ring of vertices every kLanternStrapStep, on grids of their own), below
        // the fold. In a window the broad faces' vertices (within a quarter of the width of the centre) fall into clusters of X
        // (kFaceMerge apart at most): four clusters are the two layers' faces in -X order, the outer layer's outer and inner
        // face, the inner layer's outer and inner face, and the gap is between the second and the third. Other windows (one
        // layer, an end cap's centre vertex) are skipped. Limit: a layer sloping in X spreads its clusters over the window, which
        // narrows the gap read by up to its slope x the window (the made-up hip of the self-test is flat: exact there). Upgrade
        // path: the gap from one layer's vertices to the other's triangles.
        constexpr float kFaceMerge = 0.15f;  // half the narrowest separation of two faces (the 0.3 gap)
        const float foldBottom = box.max[2] - 2.0f * kLanternStrapThickness - kLanternStrapBeltGap;
        for (float z = box.min[2]; z <= foldBottom; z += kLanternStrapStep * 0.5f) {
            std::vector<float> xs;
            for (const auto& v : verts) {
                if (std::fabs(v.z - z) <= kLanternStrapStep * 0.5f && std::fabs(v.y - centreY) <= band) {
                    xs.push_back(v.x);
                }
            }
            std::sort(xs.begin(), xs.end());
            std::vector<std::array<float, 2>> clusters;  // min, max
            for (const float x : xs) {
                if (clusters.empty() || x - clusters.back()[1] > kFaceMerge) {
                    clusters.push_back({ x, x });
                } else {
                    clusters.back()[1] = x;
                }
            }
            if (clusters.size() != 4) {
                continue;
            }
            const float between = clusters[2][0] - clusters[1][1];
            gap[0] = std::min(gap[0], between);
            gap[1] = std::max(gap[1], between);
        }
        if (gap[0] > gap[1]) {
            throw std::runtime_error("no height of the strap has both layers");
        }
        return gap;
    }

    namespace
    {
        /// The wisp's three spheres in a_info (GreyLantern.h, the wisp constants): each an effect shader with its look and its
        /// controllers, of its radius, all round one centre, under the root (a_skinned: the worn copies, Skinned flag on) or
        /// under HoMBF_Wisp
        void CheckWisp(std::vector<std::string>& a_problems, const std::string& a_what, const LanternNifInfo& a_info, bool a_skinned)
        {
            constexpr float kValueTolerance = 1e-4f;
            constexpr float kRadiusTolerance = 1e-2f;  // the vertex positions as the file stores them
            const auto within = [](float a_l, float a_r, float a_t) { return std::fabs(a_l - a_r) <= a_t; };
            std::array<float, 3> centre{};
            bool first = true;
            for (const auto& look : WispLooks()) {
                const auto it = std::find_if(a_info.shapes.begin(), a_info.shapes.end(), [&](const auto& a_s) { return a_s.name == look.name; });
                if (it == a_info.shapes.end()) {
                    Check(a_problems, false, fmt::format("{}: no wisp shape '{}'", a_what, look.name));
                    continue;
                }
                const auto& s = *it;
                const std::uint32_t flags1 = look.flags1 | (a_skinned ? kSlsf1Skinned : 0u);
                Check(a_problems, s.shader == "BSEffectShaderProperty" && s.flags1 == flags1 && s.flags2 == kLanternWispFlags2,
                    fmt::format("{} '{}': {} SLSF1 0x{:08X} SLSF2 0x{:08X}, expected BSEffectShaderProperty 0x{:08X} 0x{:08X}", a_what, s.name, s.shader,
                        s.flags1, s.flags2, flags1, kLanternWispFlags2));
                const std::string source = std::string("source ") + look.source;
                const std::string greyscale = std::string("greyscale ") + look.greyscale;
                Check(a_problems, s.textures.size() == 2 && s.textures[0] == source && s.textures[1] == greyscale,
                    fmt::format("{} '{}': textures '{}' / '{}', expected '{}' / '{}'", a_what, s.name, s.textures.empty() ? "" : s.textures[0],
                        s.textures.size() > 1 ? s.textures[1] : "", source, greyscale));
                Check(a_problems, s.hasAlpha && s.alphaFlags == look.alpha,
                    fmt::format("{} '{}': alpha flags 0x{:04X}, expected 0x{:04X}", a_what, s.name, s.alphaFlags, look.alpha));
                bool values = within(s.emissiveMultiple, look.emissive, kValueTolerance) &&
                              within(s.softFalloffDepth, kLanternWispSoftDepth, kValueTolerance) && s.clampMode == kClampWrapST;
                for (int i = 0; i < 4; ++i) {
                    values = values && within(s.baseColor[i], look.base[i], kValueTolerance) && within(s.falloff[i], look.falloff[i], kValueTolerance);
                }
                Check(a_problems, values,
                    fmt::format("{} '{}': base colour ({:.4f}, {:.4f}, {:.4f}, {:.4f}) x {:.4f}, falloff {:.4f} {:.4f} {:.4f} {:.4f}, soft depth {:.4f}, "
                                "clamp {}: not the wisp constants",
                        a_what, s.name, s.baseColor[0], s.baseColor[1], s.baseColor[2], s.baseColor[3], s.emissiveMultiple, s.falloff[0],
                        s.falloff[1], s.falloff[2], s.falloff[3], s.softFalloffDepth, s.clampMode));
                const std::string parent = a_skinned ? a_info.rootName : std::string(kLanternWispNode);
                Check(a_problems, s.parent == parent && (s.skinType.empty() != a_skinned),
                    fmt::format("{} '{}': under '{}', skinned {}; expected under '{}', skinned {}", a_what, s.name, s.parent, !s.skinType.empty(),
                        parent, a_skinned));
                bool round = true;
                for (int a = 0; a < 3; ++a) {
                    round = round && within(s.bounds.Size(a) * 0.5f, look.radius, kRadiusTolerance);
                }
                const auto at = s.bounds.Centre();
                if (first) {
                    centre = at;
                    first = false;
                }
                Check(a_problems, round && within(at[0], centre[0], kRadiusTolerance) && within(at[1], centre[1], kRadiusTolerance) &&
                                      within(at[2], centre[2], kRadiusTolerance),
                    fmt::format("{} '{}': bounds {}, expected a sphere of radius {} round {}", a_what, s.name, BoxText(s.bounds), look.radius,
                        Text(centre)));
                const auto expected = WispControllers(look);
                bool controllers = s.controllerInfo.size() == expected.size() && s.controllers.size() == expected.size();
                for (std::size_t c = 0; controllers && c < expected.size(); ++c) {
                    const auto& got = s.controllerInfo[c];
                    const auto& [variable, keys] = expected[c];
                    controllers = got.type == "BSEffectShaderPropertyFloatController" && got.variable == variable &&
                                  got.flags == kLanternWispControllerFlags && within(got.frequency, 1.0f, kValueTolerance) &&
                                  within(got.start, keys.front()[0], kValueTolerance) && within(got.stop, keys.back()[0], kValueTolerance) &&
                                  got.keys.size() == keys.size() && s.controllers[c].find("its shader") != std::string::npos;
                    for (std::size_t k = 0; controllers && k < keys.size(); ++k) {
                        controllers = within(got.keys[k][0], keys[k][0], kValueTolerance) && within(got.keys[k][1], keys[k][1], kValueTolerance);
                    }
                }
                Check(a_problems, controllers,
                    fmt::format("{} '{}': {} controllers, expected {} (U / V offset scroll, the core's pulse) looping on its own shader", a_what,
                        s.name, s.controllerInfo.size(), expected.size()));
            }
        }
    }

    std::vector<std::string> VerifyLantern(const std::vector<std::filesystem::path>& a_inputs, const std::filesystem::path& a_dataDir)
    {
        std::vector<std::string> problems;
        try {
            // the ground model
            const auto gnd = InspectLanternNif(a_dataDir / kLanternGroundNif);
            const auto gndShape = [&](const std::string& a_name) -> const LanternShapeInfo* {
                const auto it = std::find_if(gnd.shapes.begin(), gnd.shapes.end(), [&](const auto& a_s) { return a_s.name == a_name; });
                return it == gnd.shapes.end() ? nullptr : &*it;
            };
            const LanternShapeInfo* gndFrame = nullptr;
            for (const auto& s : gnd.shapes) {
                if (s.shader == "BSLightingShaderProperty") {
                    Check(problems, gndFrame == nullptr, fmt::format("gnd: a second lit shape '{}' beside the frame", s.name));
                    gndFrame = &s;
                }
            }
            Check(problems, gnd.shapes.size() == 5 && gndFrame,
                fmt::format("gnd: {} shapes, expected the frame, the wisp's core, shell and halo and its emitter", gnd.shapes.size()));
            if (gndFrame) {
                const auto& frame = *gndFrame;
                Check(problems, frame.type == "BSTriShape" && frame.parent == gnd.rootName,
                    fmt::format("gnd: frame '{}' is {} under '{}'", frame.name, frame.type, frame.parent));
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
            Check(problems, gnd.valueNodes.empty(), fmt::format("gnd: {} addon nodes, expected none (the wisp replaced AddOnNode9100)",
                                                        gnd.valueNodes.size()));
            Check(problems, gnd.hasBsx && (gnd.bsx & kBsxAnimated) != 0 && (gnd.bsx & kBsxAddon) == 0,
                fmt::format("gnd: BSXFlags 0x{:X}, expected the Animated bit and no Addon bit", gnd.bsx));
            CheckWisp(problems, "gnd", gnd, false);
            {
                const auto* emitter = gndShape(kLanternWispEmitterName);
                Check(problems, emitter && emitter->shader.empty() && (emitter->avFlags & 1u) != 0 && emitter->parent == kLanternWispNode &&
                                    emitter->skinType.empty(),
                    fmt::format("gnd: the emitter '{}' is a hidden shape without a shader under '{}'", kLanternWispEmitterName, kLanternWispNode));
                // the node's motion: one looping NiTransformController, the bob's keys and one turn
                std::vector<const LanternController*> motions;
                for (const auto& [node, controller] : gnd.nodeControllers) {
                    if (node == kLanternWispNode) {
                        motions.push_back(&controller);
                    }
                }
                const float cycle = static_cast<float>(kLanternWispBobsPerTurn) / kLanternWispBobHz;
                const auto* motion = motions.size() == 1 ? motions.front() : nullptr;
                Check(problems, motion && motion->type == "NiTransformController" && motion->flags == kLanternWispControllerFlags &&
                                    std::fabs(motion->start) <= 1e-6f && std::fabs(motion->stop - cycle) <= 1e-4f && std::fabs(motion->frequency - 1.0f) <= 1e-6f,
                    fmt::format("gnd: '{}' has {} controllers; expected one NiTransformController, flags 0x{:04X} (loop, active), time 0..{:.4f}",
                        kLanternWispNode, motions.size(), kLanternWispControllerFlags, cycle));
                if (motion) {
                    const auto& t = motion->translations;
                    float low = kFloatMax;
                    float high = -kFloatMax;
                    for (const auto& key : t) {
                        low = std::min(low, key[3]);
                        high = std::max(high, key[3]);
                    }
                    constexpr float kKeyTolerance = 1e-3f;
                    const bool loops = t.size() > 2 && std::fabs(t.front()[0]) <= 1e-6f && std::fabs(t.back()[0] - cycle) <= 1e-4f &&
                                       std::fabs(t.front()[3] - t.back()[3]) <= 1e-5f && std::fabs(t.front()[1] - t.back()[1]) <= 1e-5f &&
                                       std::fabs(t.front()[2] - t.back()[2]) <= 1e-5f;
                    Check(problems, loops && std::fabs((high - low) - 2.0f * kLanternWispBobAmplitude) <= kKeyTolerance,
                        fmt::format("gnd: the bob's {} translation keys from {:.4f} to {:.4f} s, z {:.4f}..{:.4f}: expected a loop over 0..{:.4f} "
                                    "of {} up and down (kLanternWispBobAmplitude)",
                            t.size(), t.empty() ? 0.0f : t.front()[0], t.empty() ? 0.0f : t.back()[0], low, high, cycle, kLanternWispBobAmplitude));
                    const auto& z = motion->zRotations;
                    Check(problems, motion->rotationType == nifly::XYZ_ROTATION_KEY && z.size() == 2 && std::fabs(z.front()[1]) <= 1e-6f &&
                                        std::fabs(z.back()[1] - 2.0f * kPi) <= 1e-5f && std::fabs(z.back()[0] - cycle) <= 1e-4f,
                        "gnd: the wisp turns once about Z (XYZ rotation, 0 -> 2 pi) over the cycle");
                }
                // the particles: both systems under the node, their palette ours, emitting from the emitter, no bombs, scaled
                nifly::NifFile source;
                LoadNif(source, Need(a_inputs, kLanternWispSource));
                const auto sourceParticles = ReadParticles(source);
                Check(problems, gnd.particles.size() == kLanternWispParticles.size(),
                    fmt::format("gnd: {} particle systems, expected {}", gnd.particles.size(), kLanternWispParticles.size()));
                for (const char* name : kLanternWispParticles) {
                    const auto it = std::find_if(gnd.particles.begin(), gnd.particles.end(), [&](const auto& a_p) { return a_p.name == name; });
                    const auto from = std::find_if(sourceParticles.begin(), sourceParticles.end(), [&](const auto& a_p) { return a_p.name == name; });
                    if (it == gnd.particles.end() || from == sourceParticles.end()) {
                        Check(problems, false, fmt::format("gnd: no particle system '{}' (or none in the source)", name));
                        continue;
                    }
                    const auto& p = *it;
                    Check(problems, p.parent == kLanternWispNode && p.type == from->type && p.pool == from->pool,
                        fmt::format("gnd particles '{}': {} pool {} under '{}', expected the source's {} pool {} under '{}'", name, p.type, p.pool,
                            p.parent, from->type, from->pool, kLanternWispNode));
                    Check(problems, p.greyscale == kPalettePath && (p.flags1 & kSlsf1GreyscaleToPaletteColor) != 0,
                        fmt::format("gnd particles '{}': palette '{}' (palette colour {}), expected '{}'", name, p.greyscale,
                            (p.flags1 & kSlsf1GreyscaleToPaletteColor) != 0 ? "on" : "off", kPalettePath));
                    Check(problems, p.emitterMeshes.size() == 1 && p.emitterMeshes.front() == kLanternWispEmitterName,
                        fmt::format("gnd particles '{}': emits from {} meshes, expected '{}' alone", name, p.emitterMeshes.size(), kLanternWispEmitterName));
                    const bool bombs = std::any_of(p.modifiers.begin(), p.modifiers.end(),
                        [](const std::string& a_m) { return a_m.find("NiPSysBombModifier") != std::string::npos; });
                    Check(problems, !bombs, fmt::format("gnd particles '{}': a bomb modifier is left", name));
                    for (const auto& line : p.outside) {
                        Check(problems, line.find(std::string("'") + kLanternWispEmitterName + "'") != std::string::npos ||
                                            line.find(std::string("'") + kLanternWispNode + "'") != std::string::npos,
                            fmt::format("gnd particles '{}': names {}", name, line));
                    }
                    constexpr float kScaleTolerance = 1e-4f;
                    Check(problems, std::fabs(p.radius - from->radius * kLanternWispParticleScale) <= kScaleTolerance &&
                                        std::fabs(p.speed - from->speed * kLanternWispParticleScale) <= kScaleTolerance,
                        fmt::format("gnd particles '{}': radius {:.4f}, speed {:.4f}; expected the source's {:.4f}, {:.4f} x {}", name, p.radius,
                            p.speed, from->radius, from->speed, kLanternWispParticleScale));
                }
            }

            // the worn model
            const auto worn = InspectLanternNif(a_dataDir / kLanternWornNif);
            Check(problems, !worn.hasCollision, "worn: has a collision object");
            Check(problems, worn.valueNodes.empty(), "worn: has an addon node (the flame is the wisp)");
            Check(problems, worn.sseCompatible, "worn: nifly says it is not SSE-compatible");
            Check(problems, worn.nodeControllers.empty() && worn.particles.empty(),
                fmt::format("worn: {} node controllers and {} particle systems, expected none (skinned: the shaders' controllers alone)",
                    worn.nodeControllers.size(), worn.particles.size()));
            std::vector<std::string> expected{ kLanternWornFrameName, kLanternStrapName, kLanternBracketName, kLanternRingName, kLanternLugName,
                kLanternWispCoreName, kLanternWispShellName, kLanternWispHaloName };
            const auto shapeNamed = [&](const std::string& a_name) -> const LanternShapeInfo* {
                const auto it = std::find_if(worn.shapes.begin(), worn.shapes.end(), [&](const auto& a_s) { return a_s.name == a_name; });
                return it == worn.shapes.end() ? nullptr : &*it;
            };
            const auto slot = [](const LanternShapeInfo* a_s, std::size_t a_i) {
                return a_s && a_s->textures.size() > a_i ? a_s->textures[a_i] : std::string();
            };
            CheckWisp(problems, "worn", worn, true);
            {
                const auto* frame = shapeNamed(kLanternWornFrameName);
                const auto* strap = shapeNamed(kLanternStrapName);
                const auto* ring = shapeNamed(kLanternRingName);
                const std::string strapDiffuse = fmt::format("[{}] {}", kSlotDiffuse, kLanternStrapDiffuse);
                const std::string strapNormal = fmt::format("[{}] {}", kNormalSlot, kLanternStrapNormal);
                const std::string soot = fmt::format("[{}] {}", kSlotDiffuse, kLanternDiffuse);
                Check(problems, slot(strap, 0) == strapDiffuse && slot(strap, 1) == strapNormal,
                    fmt::format("worn strap: textures '{}' / '{}', expected '{}' / '{}'", slot(strap, 0), slot(strap, 1), strapDiffuse,
                        strapNormal));
                const auto* bracket = shapeNamed(kLanternBracketName);
                for (const auto& [what, iron] : { std::pair<const char*, const LanternShapeInfo*>{ "ring", ring },
                         std::pair<const char*, const LanternShapeInfo*>{ "bracket", bracket } }) {
                    Check(problems, slot(iron, 0) == soot && !slot(frame, 1).empty() && slot(iron, 1) == slot(frame, 1),
                        fmt::format("worn {}: textures '{}' / '{}', expected '{}' / the frame's normal map '{}'", what, slot(iron, 0),
                            slot(iron, 1), soot, slot(frame, 1)));
                    constexpr float kUvTolerance = 1e-3f;  // half-float UVs
                    Check(problems, iron && iron->uvMin[0] >= kLanternIronUvMin[0] - kUvTolerance &&
                                        iron->uvMin[1] >= kLanternIronUvMin[1] - kUvTolerance &&
                                        iron->uvMax[0] <= kLanternIronUvMax[0] + kUvTolerance &&
                                        iron->uvMax[1] <= kLanternIronUvMax[1] + kUvTolerance && iron->uvMin[0] <= iron->uvMax[0],
                        fmt::format("worn {}: UVs u {}..{} v {}..{} outside the iron box u {}..{} v {}..{}", what, iron ? iron->uvMin[0] : 0.0f,
                            iron ? iron->uvMax[0] : 0.0f, iron ? iron->uvMin[1] : 0.0f, iron ? iron->uvMax[1] : 0.0f, kLanternIronUvMin[0],
                            kLanternIronUvMax[0], kLanternIronUvMin[1], kLanternIronUvMax[1]));
                }
                const auto* lugs = shapeNamed(kLanternLugName);
                Check(problems, slot(lugs, 0) == soot && !slot(frame, 1).empty() && slot(lugs, 1) == slot(frame, 1),
                    fmt::format("worn lugs: textures '{}' / '{}', expected '{}' / the frame's normal map '{}'", slot(lugs, 0), slot(lugs, 1),
                        soot, slot(frame, 1)));
                constexpr float kShaderTolerance = 1e-4f;
                Check(problems, strap && std::fabs(strap->glossiness - kLanternStrapGlossiness) <= kShaderTolerance &&
                                    std::fabs(strap->specularStrength - kLanternStrapSpecularStrength) <= kShaderTolerance &&
                                    std::fabs(strap->specularColor[0] - kLanternStrapSpecularColor[0]) <= kShaderTolerance &&
                                    std::fabs(strap->specularColor[1] - kLanternStrapSpecularColor[1]) <= kShaderTolerance &&
                                    std::fabs(strap->specularColor[2] - kLanternStrapSpecularColor[2]) <= kShaderTolerance,
                    fmt::format("worn strap: glossiness {}, specular strength {}, colour {}; expected {}, {}, {} (vanilla leatherstrips01.nif)",
                        strap ? strap->glossiness : -1.0f, strap ? strap->specularStrength : -1.0f,
                        strap ? Text(strap->specularColor) : std::string("-"), kLanternStrapGlossiness, kLanternStrapSpecularStrength,
                        Text(kLanternStrapSpecularColor)));
            }
            Check(problems, worn.shapes.size() == expected.size(),
                fmt::format("worn: {} shapes, expected {}", worn.shapes.size(), expected.size()));
            for (const auto& name : expected) {
                Check(problems, shapeNamed(name) != nullptr, fmt::format("worn: no shape '{}'", name));
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
            // the wisp inside the cage on both models
            {
                const auto wisp = WispReadBack(a_dataDir);
                Check(problems, wisp.wornClearance >= kLanternWispClearance,
                    fmt::format("worn: the frame comes within {:.4f} of the wisp's halo (kLanternWispClearance {})", wisp.wornClearance,
                        kLanternWispClearance));
                Check(problems, wisp.groundClearance >= kLanternWispClearance,
                    fmt::format("gnd: the frame comes within {:.4f} of the wisp's halo along its bob (kLanternWispClearance {})",
                        wisp.groundClearance, kLanternWispClearance));
            }
            // the strap's layers lie on each other
            {
                const auto gap = StrapLayerGap(a_dataDir);
                Check(problems, gap[0] >= kLanternStrapLayerGapMin - 1e-3f && gap[0] <= kLanternStrapLayerGapMax + 1e-3f,
                    fmt::format("worn strap: the layers {:.4f}..{:.4f} apart, the narrowest expected {}..{} (kLanternStrapLayerGapMin/Max)", gap[0],
                        gap[1], kLanternStrapLayerGapMin, kLanternStrapLayerGapMax));
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
            // the strap's top over the belt band (the bodies and the skeleton an --in folder has)
            {
                const auto skeletonFile = FindSource(a_inputs, kLanternSkeletonSource);
                std::vector<LanternBand> bands;
                nifly::NifFile skeleton;
                MatTransform pelvis;
                if (skeletonFile) {
                    LoadNif(skeleton, *skeletonFile);
                }
                if (skeletonFile && skeleton.GetNodeTransformToGlobal(kLanternBone, pelvis)) {
                    for (std::size_t b = 0; b < kLanternBodySources.size(); ++b) {
                        if (const auto file = FindSource(a_inputs, kLanternBodySources[b])) {
                            bands.push_back(MeasureLanternBand(*file, kLanternBandShapes[b], pelvis.translation));
                        }
                    }
                }
                const auto* strap = shapeNamed(kLanternStrapName);
                if (!bands.empty() && strap) {
                    float bandTop = -kFloatMax;
                    for (const auto& band : bands) {
                        bandTop = std::max(bandTop, band.top);
                    }
                    constexpr float kTopTolerance = 1e-3f;
                    Check(problems, std::fabs(strap->bounds.max[2] - (bandTop + kLanternStrapOverBand)) <= kTopTolerance,
                        fmt::format("worn strap: top z {:.4f}, expected the highest belt band top {:.4f} + kLanternStrapOverBand {}",
                            strap->bounds.max[2], bandTop, kLanternStrapOverBand));
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
        std::cout << "\nworn (build):\n";
        for (const auto& line : result.summary) {
            std::cout << "  " << line << "\n";
        }
        {
            const auto worn = InspectLanternNif(a_dataDir / kLanternWornNif);
            for (const auto& s : worn.shapes) {
                if (s.name == kLanternStrapName) {
                    std::cout << fmt::format("worn strap shader (read back): glossiness {:.4f}, specular strength {:.4f}, specular colour {} "
                                             "(vanilla meshes\\clutter\\common\\leatherstrips01.nif: 80, 1, (0.4078, 0.3843, 0.3647))\n",
                        s.glossiness, s.specularStrength, Text(s.specularColor));
                }
                if (s.name == kLanternBracketName) {
                    std::cout << fmt::format("worn bracket (read back): bounds {}; UVs u {:.4f}..{:.4f} v {:.4f}..{:.4f}\n", BoxText(s.bounds),
                        s.uvMin[0], s.uvMax[0], s.uvMin[1], s.uvMax[1]);
                }
                if (s.name == kLanternRingName) {
                    std::cout << fmt::format("worn ring (read back): centre {}; UVs u {:.4f}..{:.4f} v {:.4f}..{:.4f}\n", Text(s.bounds.Centre()),
                        s.uvMin[0], s.uvMax[0], s.uvMin[1], s.uvMax[1]);
                }
                if (s.name == kLanternStrapName) {
                    float bandTop = -kFloatMax;
                    for (const auto& band : result.bands) {
                        std::cout << fmt::format("belt band (measured) {} '{}': top z {:.4f}, bottom z {:.4f}\n", band.body, band.shape, band.top,
                            band.bottom);
                        bandTop = std::max(bandTop, band.top);
                    }
                    std::cout << fmt::format("worn strap (read back): z {:.4f}..{:.4f}, top {:.4f} above the highest band top; inner end z "
                                             "{:.4f}, outer lower end z {:.4f}\n",
                        s.bounds.min[2], s.bounds.max[2], s.bounds.max[2] - bandTop, result.strapInnerBottom, result.strapBottom);
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
        {
            const auto wisp = WispReadBack(a_dataDir);
            std::cout << fmt::format("wisp (read back): worn centre {}, the worn frame {:.4f} off the halo (radius {}); ground node at {}, bob z "
                                     "{:.4f}..{:.4f} ({:.4f} up and down), the ground frame {:.4f} off the halo along the bob (kLanternWispClearance {})\n",
                Text(wisp.wornCentre), wisp.wornClearance, kLanternWispHaloRadius, Text(wisp.groundCentre), wisp.bobLow, wisp.bobHigh,
                (wisp.bobHigh - wisp.bobLow) * 0.5f, wisp.groundClearance, kLanternWispClearance);
            for (std::size_t n = 0; n < kLanternWispParticles.size(); ++n) {
                std::cout << fmt::format("wisp particles '{}' (build): reach about {:.4f} from the centre on their mean values\n",
                    kLanternWispParticles[n], result.particleReach[n]);
            }
            const auto gap = StrapLayerGap(a_dataDir);
            std::cout << fmt::format("worn strap layers (read back): {:.4f}..{:.4f} apart (kLanternStrapBeltGap {}; the narrowest expected "
                                     "{}..{}); the outer layer's inner face {:.4f} off the band where they are narrowest (the inner layer {} off "
                                     "it, {} thick)\n",
                gap[0], gap[1], kLanternStrapBeltGap, kLanternStrapLayerGapMin, kLanternStrapLayerGapMax,
                kLanternStrapBodyGap + kLanternStrapThickness + gap[0], kLanternStrapBodyGap, kLanternStrapThickness);
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

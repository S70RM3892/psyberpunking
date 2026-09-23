// CPUメッシュ → Filamentのバッファ、マテリアルの読込
#include <cstring>

#include <geometry/SurfaceOrientation.h>

#include "render.h"

namespace bench::render {

using namespace filament::math;

namespace {

template <typename T>
void setBuffer(Engine& engine, VertexBuffer* vb, int index, std::vector<T>&& data) {
    auto* heap = new std::vector<T>(std::move(data));
    vb->setBufferAt(engine, static_cast<uint8_t>(index),
                    VertexBuffer::BufferDescriptor(heap->data(), heap->size() * sizeof(T),
                                                   [](void*, size_t, void* user) { delete static_cast<std::vector<T>*>(user); },
                                                   heap));
}

}  // namespace

GpuMesh upload(Engine& engine, const Mesh& mesh, const std::vector<std::array<uint16_t, 4>>* joints,
               const std::vector<float4>* weights) {
    GpuMesh g;
    const size_t n = mesh.vertexCount();
    if (n == 0 || mesh.indices.empty()) return g;

    // 接空間を四元数（short4）にまとめる（Filament の TANGENTS 属性）
    std::vector<short4> quats(n);
    auto* so = geometry::SurfaceOrientation::Builder()
                   .vertexCount(n)
                   .normals(mesh.normals.data())
                   .tangents(mesh.tangents.data())
                   .build();
    so->getQuats(quats.data(), n);
    delete so;

    const bool skinned = joints && weights;
    VertexBuffer::Builder b;
    b.vertexCount(static_cast<uint32_t>(n))
        .bufferCount(skinned ? 6 : 4)
        .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT3)
        .attribute(VertexAttribute::TANGENTS, 1, VertexBuffer::AttributeType::SHORT4)
        .normalized(VertexAttribute::TANGENTS)
        .attribute(VertexAttribute::UV0, 2, VertexBuffer::AttributeType::FLOAT2)
        .attribute(VertexAttribute::CUSTOM0, 3, VertexBuffer::AttributeType::FLOAT4);
    if (skinned) {
        b.attribute(VertexAttribute::BONE_INDICES, 4, VertexBuffer::AttributeType::USHORT4)
            .attribute(VertexAttribute::BONE_WEIGHTS, 5, VertexBuffer::AttributeType::FLOAT4);
    }
    g.vb = b.build(engine);
    setBuffer(engine, g.vb, 0, std::vector<float3>(mesh.positions));
    setBuffer(engine, g.vb, 1, std::move(quats));
    setBuffer(engine, g.vb, 2, std::vector<float2>(mesh.uvs));
    setBuffer(engine, g.vb, 3, std::vector<float4>(mesh.custom));
    if (skinned) {
        std::vector<ushort4> j(n);
        for (size_t i = 0; i < n; ++i) j[i] = ushort4{(*joints)[i][0], (*joints)[i][1], (*joints)[i][2], (*joints)[i][3]};
        setBuffer(engine, g.vb, 4, std::move(j));
        setBuffer(engine, g.vb, 5, std::vector<float4>(*weights));
    }

    const bool small = n < 65536;
    g.ib = IndexBuffer::Builder()
               .indexCount(static_cast<uint32_t>(mesh.indices.size()))
               .bufferType(small ? IndexBuffer::IndexType::USHORT : IndexBuffer::IndexType::UINT)
               .build(engine);
    if (small) {
        auto* idx = new std::vector<uint16_t>(mesh.indices.begin(), mesh.indices.end());
        g.ib->setBuffer(engine, IndexBuffer::BufferDescriptor(idx->data(), idx->size() * 2,
                                                              [](void*, size_t, void* u) { delete static_cast<std::vector<uint16_t>*>(u); }, idx));
    } else {
        auto* idx = new std::vector<uint32_t>(mesh.indices);
        g.ib->setBuffer(engine, IndexBuffer::BufferDescriptor(idx->data(), idx->size() * 4,
                                                              [](void*, size_t, void* u) { delete static_cast<std::vector<uint32_t>*>(u); }, idx));
    }
    g.indexCount = static_cast<uint32_t>(mesh.indices.size());
    g.triangles = mesh.triangleCount();
    Aabb a = mesh.bounds();
    g.box = Box().set(a.min, a.max);
    return g;
}

void destroy(Engine& engine, GpuMesh& m) {
    if (m.vb) engine.destroy(m.vb);
    if (m.ib) engine.destroy(m.ib);
    m = {};
}

// ---- マテリアル ------------------------------------------------------------------------------------

bool Materials::load(Engine& engine, Platform& platform, std::string* error) {
    static const char* kNames[] = {"facade", "detail", "ground", "sign", "character", "vehicle",
                                   "sky",    "rain",   "steam",  "fog"};
    for (const char* name : kNames) {
        std::vector<uint8_t> data;
        std::string path = std::string("materials/") + name + ".filamat";
        if (!platform.readAsset(path, data)) {
            if (error) *error = "asset missing: " + path;
            return false;
        }
        Material* m = Material::Builder().package(data.data(), data.size()).build(engine);
        if (!m) {
            if (error) *error = "material failed to load (Filament version mismatch?): " + path;
            return false;
        }
        materials_[name] = m;
    }
    return true;
}

void Materials::destroy(Engine& engine) {
    for (auto& [name, m] : materials_) engine.destroy(m);
    materials_.clear();
}

Material* Materials::get(const char* name) const {
    auto it = materials_.find(name);
    return it == materials_.end() ? nullptr : it->second;
}

}  // namespace bench::render

// ボリューメトリックフォグ（自作）のジオメトリ。格子の頂点がフロクセル。
#include <algorithm>

#include <filament/RenderableManager.h>
#include <utils/EntityManager.h>

#include "render.h"

namespace bench::render {

using namespace filament::math;

void Fog::build(Engine& engine, Scene& scene, Material* material, const SceneConfig& cfg, const Preset& preset) {
    if (!preset.fog.enabled || !material) return;
    const int gx = std::max(2, preset.fog.grid[0]);
    const int gy = std::max(2, preset.fog.grid[1]);
    const int gz = std::max(1, preset.fog.grid[2]);
    params_ = cfg.fog;

    // 1スライス分の格子のインデックス（全スライス共通）。gx×gy ≤ 65535 を前提に USHORT
    std::vector<uint16_t>* idx = new std::vector<uint16_t>();
    idx->reserve(static_cast<size_t>(gx - 1) * (gy - 1) * 6);
    for (int y = 0; y + 1 < gy; ++y) {
        for (int x = 0; x + 1 < gx; ++x) {
            uint16_t a = static_cast<uint16_t>(y * gx + x), b = a + 1, c = static_cast<uint16_t>(a + gx), d = c + 1;
            idx->insert(idx->end(), {a, b, d, a, d, c});
        }
    }
    const uint32_t indexCount = static_cast<uint32_t>(idx->size());
    ib_ = IndexBuffer::Builder().indexCount(indexCount).bufferType(IndexBuffer::IndexType::USHORT).build(engine);
    ib_->setBuffer(engine, IndexBuffer::BufferDescriptor(idx->data(), idx->size() * 2,
                                                         [](void*, size_t, void* u) { delete static_cast<std::vector<uint16_t>*>(u); }, idx));

    mi_ = material->createInstance();
    mi_->setParameter("density", params_.density);
    mi_->setParameter("heightFalloff", params_.heightFalloff);
    mi_->setParameter("range", params_.range);
    mi_->setParameter("sliceCount", static_cast<float>(gz));
    mi_->setParameter("anisotropy", params_.anisotropyHint);
    mi_->setParameter("ambient", float3{0.004f, 0.003f, 0.006f});
    mi_->setParameter("moonColor", float3{0.02f, 0.025f, 0.04f});

    // スライスごとに頂点バッファ（x, y は画面端より少し外まで。TAAのジッタで端が欠けないように）
    const uint32_t vcount = static_cast<uint32_t>(gx * gy);
    for (int k = 0; k < gz; ++k) {
        auto* verts = new std::vector<float3>(vcount);
        for (int y = 0; y < gy; ++y) {
            for (int x = 0; x < gx; ++x) {
                float nx = -1.03f + 2.06f * static_cast<float>(x) / static_cast<float>(gx - 1);
                float ny = -1.03f + 2.06f * static_cast<float>(y) / static_cast<float>(gy - 1);
                (*verts)[static_cast<size_t>(y) * gx + x] = float3{nx, ny, static_cast<float>(k)};
            }
        }
        VertexBuffer* vb = VertexBuffer::Builder()
                               .vertexCount(vcount)
                               .bufferCount(1)
                               .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT3)
                               .build(engine);
        vb->setBufferAt(engine, 0, VertexBuffer::BufferDescriptor(verts->data(), verts->size() * sizeof(float3),
                                                                  [](void*, size_t, void* u) { delete static_cast<std::vector<float3>*>(u); }, verts));
        sliceVbs_.push_back(vb);
        utils::Entity e = utils::EntityManager::get().create();
        // 奥のスライスから描く（半透明の合成順）。priority は 0..7、7 が最後
        RenderableManager::Builder(1)
            .boundingBox({{0, 0, 0}, {1e5f, 1e5f, 1e5f}})
            .material(0, mi_)
            .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, vb, ib_, 0, indexCount)
            .culling(false)
            .castShadows(false)
            .receiveShadows(false)
            .priority(6)
            .blendOrder(0, static_cast<uint16_t>(gz - k))
            .build(engine, e);
        scene.addEntity(e);
        slices_.push_back(e);
        vertexCount_ += vcount;
    }
}

void Fog::update(double time, uint32_t frame, const std::vector<LightSample>& nearest, float3 moonDir) {
    if (!mi_) return;
    // スライスの奥行きを毎フレームずらす（Halton(2)）。TAAが縞を平均する
    float j = 0.f, f = 0.5f;
    for (uint32_t i = (frame % 16) + 1; i > 0; i /= 2, f *= 0.5f) j += f * static_cast<float>(i % 2);
    mi_->setParameter("jitter", j);
    mi_->setParameter("time", static_cast<float>(time));
    mi_->setParameter("moonDir", normalize(moonDir));

    float4 pos[16] = {}, col[16] = {};
    const int n = std::min<int>(16, static_cast<int>(nearest.size()));
    for (int i = 0; i < n; ++i) {
        pos[i] = float4{nearest[i].position, nearest[i].range};
        col[i] = float4{nearest[i].color, nearest[i].spot};
    }
    mi_->setParameter("lightPos", pos, 16);
    mi_->setParameter("lightColor", col, 16);
    mi_->setParameter("lightCount", static_cast<float>(n));
}

void Fog::destroy(Engine& engine) {
    for (auto e : slices_) engine.destroy(e);
    slices_.clear();
    for (auto* vb : sliceVbs_) engine.destroy(vb);
    sliceVbs_.clear();
    if (ib_) engine.destroy(ib_);
    if (mi_) engine.destroy(mi_);
    ib_ = nullptr;
    mi_ = nullptr;
    vertexCount_ = 0;
}

}  // namespace bench::render

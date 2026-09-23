// 雨と蒸気（頂点属性なし描画。位置は時刻とハッシュからシェーダで決まる）
#include <algorithm>

#include <filament/RenderableManager.h>
#include <utils/EntityManager.h>

#include "render.h"

namespace bench::render {

using namespace filament::math;

namespace {

utils::Entity makeProcedural(Engine& engine, Scene& scene, VertexBuffer*& vb, MaterialInstance* mi, uint32_t vertexCount,
                             uint8_t priority) {
    // 位置はシェーダが頂点番号から作る。POSITION は宣言上必須なので、使わない 0 を入れておく（half4 で最小）
    vb = VertexBuffer::Builder()
             .vertexCount(vertexCount)
             .bufferCount(1)
             .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::HALF4)
             .build(engine);
    auto* zeros = new std::vector<uint16_t>(static_cast<size_t>(vertexCount) * 4, 0);
    vb->setBufferAt(engine, 0, VertexBuffer::BufferDescriptor(zeros->data(), zeros->size() * 2,
                                                              [](void*, size_t, void* u) { delete static_cast<std::vector<uint16_t>*>(u); }, zeros));
    utils::Entity e = utils::EntityManager::get().create();
    RenderableManager::Builder(1)
        .boundingBox({{0, 0, 0}, {1e5f, 1e5f, 1e5f}})
        .material(0, mi)
        .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, vb)
        .culling(false)
        .castShadows(false)
        .receiveShadows(false)
        .priority(priority)
        .build(engine, e);
    scene.addEntity(e);
    return e;
}

}  // namespace

void Particles::build(Engine& engine, Scene& scene, Material* rain, Material* steam, const SceneConfig& cfg,
                      const CityData& city) {
    if (rain && cfg.particles.rain > 0) {
        rainMi_ = rain->createInstance();
        rainMi_->setParameter("boxSize", float3{cfg.particles.rainBox[0], cfg.particles.rainBox[1], cfg.particles.rainBox[2]});
        rainMi_->setParameter("wind", float3{0.8f, 0.0f, 1.4f});
        rainMi_->setParameter("fallSpeed", 9.0f);
        rainMi_->setParameter("nits", 2.2f);
        rainMi_->setParameter("tint", float3{0.55f, 0.65f, 0.9f});
        rain_ = makeProcedural(engine, scene, rainVb_, rainMi_, static_cast<uint32_t>(cfg.particles.rain) * 6, 7);
        count_ += cfg.particles.rain;
    }
    if (steam && cfg.particles.steam > 0 && !city.vents.empty()) {
        steamMi_ = steam->createInstance();
        float4 vents[16] = {};
        const int n = std::min<int>(16, static_cast<int>(city.vents.size()));
        for (int i = 0; i < n; ++i) vents[i] = float4{city.vents[i].position, city.vents[i].strength};
        steamMi_->setParameter("vents", vents, 16);
        steamMi_->setParameter("ventCount", static_cast<float>(n));
        steamMi_->setParameter("nits", 1.4f);
        steamMi_->setParameter("tint", float3{0.5f, 0.45f, 0.55f});
        steam_ = makeProcedural(engine, scene, steamVb_, steamMi_, static_cast<uint32_t>(cfg.particles.steam) * 6, 7);
        count_ += cfg.particles.steam;
    }
}

void Particles::update(double time, float3 steamTint) {
    if (rainMi_) rainMi_->setParameter("time", static_cast<float>(time));
    if (steamMi_) {
        steamMi_->setParameter("time", static_cast<float>(time));
        steamMi_->setParameter("tint", steamTint);
    }
}

void Particles::destroy(Engine& engine) {
    if (!rain_.isNull()) engine.destroy(rain_);
    if (!steam_.isNull()) engine.destroy(steam_);
    if (rainVb_) engine.destroy(rainVb_);
    if (steamVb_) engine.destroy(steamVb_);
    if (rainMi_) engine.destroy(rainMi_);
    if (steamMi_) engine.destroy(steamMi_);
    rainVb_ = steamVb_ = nullptr;
    rainMi_ = steamMi_ = nullptr;
    rain_ = steam_ = {};
    count_ = 0;
}

}  // namespace bench::render

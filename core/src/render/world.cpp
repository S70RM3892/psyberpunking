#include "world.h"

#include <algorithm>
#include <cmath>

#include <filament/Frustum.h>
#include <filament/RenderableManager.h>
#include <filament/TransformManager.h>
#include <utils/EntityManager.h>

#include "bench/rng.h"

namespace bench::render {

using namespace filament::math;

namespace {

constexpr uint8_t kLayerVisible = 0x1;
constexpr uint8_t kLayerHidden = 0x2;
constexpr float kPi = 3.14159265358979f;
constexpr size_t kMaxSceneLights = 250;

mat4f placement(const Placement& p, float scale = 1.0f) {
    return mat4f::translation(p.position) * mat4f::rotation(p.yaw, float3{0, 1, 0}) * mat4f::scaling(float3{scale});
}

Aabb toAabb(const Box& b) {
    Aabb a;
    a.add(b.getMin());
    a.add(b.getMax());
    return a;
}

float distanceToAabb(const Aabb& b, float3 p) {
    float3 d = max(max(b.min - p, p - b.max), float3{0});
    return length(d);
}

// 看板色の彩度を保ったまま明るさだけ揃える
float3 luminanceNormalized(float3 c) {
    float l = dot(c, float3{0.2126f, 0.7152f, 0.0722f});
    return l > 1e-4f ? c * (0.35f / l) : c;
}

}  // namespace

int World::addDrawable(utils::Entity e, const Aabb& b, size_t tris, int prims, bool shadow) {
    drawables_.push_back({e, b, tris, prims, shadow, true});
    return static_cast<int>(drawables_.size()) - 1;
}

utils::Entity World::makeRenderable(const std::vector<std::pair<const GpuMesh*, MaterialInstance*>>& parts,
                                    const mat4f& xf, bool castShadows, bool receiveShadows, bool culling) {
    utils::Entity e = utils::EntityManager::get().create();
    Aabb local;
    for (const auto& [m, mi] : parts) local.add(toAabb(m->box));
    RenderableManager::Builder b(parts.size());
    b.boundingBox(Box().set(local.min, local.max))
        .castShadows(castShadows)
        .receiveShadows(receiveShadows)
        .culling(culling);
    for (size_t i = 0; i < parts.size(); ++i) {
        b.geometry(i, RenderableManager::PrimitiveType::TRIANGLES, parts[i].first->vb, parts[i].first->ib, 0,
                   parts[i].first->indexCount);
        b.material(i, parts[i].second);
    }
    b.build(*engine_, e);
    auto& tcm = engine_->getTransformManager();
    tcm.create(e);
    tcm.setTransform(tcm.getInstance(e), xf);
    scene_->addEntity(e);
    return e;
}

bool World::build(Engine& engine, Scene& scene, View& view, Camera& camera, const BuildInput& in,
                  const std::function<void(float)>& progress, std::string* error) {
    engine_ = &engine;
    scene_ = &scene;
    view_ = &view;
    camera_ = &camera;
    cfg_ = in.config;
    preset_ = in.preset;
    city_ = in.city;
    path_ = in.path;
    const SceneConfig& cfg = *cfg_;
    const Preset& preset = *preset_;
    const CityData& city = *city_;
    auto report = [&](float p) {
        if (progress) progress(p);
    };

    if (!materials_.load(engine, *in.platform, error)) return false;
    report(0.1f);
    textures_ = loadTextures(engine, *in.platform, preset.textureMax);
    moonDir_ = normalize(float3{-0.32f, 0.42f, 0.85f});
    ibl_ = makeIbl(engine, *in.platform, moonDir_);
    scene.setIndirectLight(ibl_.light);
    report(0.25f);

    // ---- マテリアルインスタンス ----
    const auto& ts = textures_;
    facadeMi_ = materials_.get("facade")->createInstance();
    facadeMi_->setParameter("wallAlbedo", ts.get(Tex::WallAlbedo), ts.sampler);
    facadeMi_->setParameter("wallNormal", ts.get(Tex::WallNormal), ts.sampler);
    facadeMi_->setParameter("wallOrm", ts.get(Tex::WallOrm), ts.sampler);
    facadeMi_->setParameter("windowNits", 11.0f);
    facadeMi_->setParameter("litRatio", 0.38f);
    detailMi_ = materials_.get("detail")->createInstance();
    detailMi_->setParameter("metalAlbedo", ts.get(Tex::MetalAlbedo), ts.sampler);
    detailMi_->setParameter("concreteAlbedo", ts.get(Tex::ConcreteAlbedo), ts.sampler);
    detailMi_->setParameter("stripNits", 70.0f);
    groundMi_ = materials_.get("ground")->createInstance();
    groundMi_->setParameter("asphaltAlbedo", ts.get(Tex::AsphaltAlbedo), ts.sampler);
    groundMi_->setParameter("asphaltNormal", ts.get(Tex::AsphaltNormal), ts.sampler);
    groundMi_->setParameter("asphaltOrm", ts.get(Tex::AsphaltOrm), ts.sampler);
    groundMi_->setParameter("pavingAlbedo", ts.get(Tex::PavingAlbedo), ts.sampler);
    groundMi_->setParameter("pavingNormal", ts.get(Tex::PavingNormal), ts.sampler);
    groundMi_->setParameter("wetness", 0.9f);
    ownedMis_.insert(ownedMis_.end(), {facadeMi_, detailMi_, groundMi_});

    // ---- 空 ----
    {
        Mesh sphere;
        sphere.addEllipsoid(float3{0}, float3{1500.f}, 48, 24);
        skyMesh_ = upload(engine, sphere);
        skyMi_ = materials_.get("sky")->createInstance();
        skyMi_->setParameter("moonDir", moonDir_);
        skyMi_->setParameter("nits", 32.0f);
        ownedMis_.push_back(skyMi_);
        sky_ = makeRenderable({{&skyMesh_, skyMi_}}, mat4f{}, false, false, false);
        auto& rcm = engine.getRenderableManager();
        rcm.setPriority(rcm.getInstance(sky_), 0);
    }

    // ---- ビル（型を1回だけ転送し、200棟をインスタンスで置く） ----
    archetypeMeshes_.resize(city.archetypes.size());
    for (size_t a = 0; a < city.archetypes.size(); ++a) {
        for (int l = 0; l < 3; ++l) {
            archetypeMeshes_[a][l][0] = upload(engine, city.archetypes[a].lods[l].facade);
            archetypeMeshes_[a][l][1] = upload(engine, city.archetypes[a].lods[l].detail);
        }
        report(0.25f + 0.25f * static_cast<float>(a + 1) / static_cast<float>(city.archetypes.size()));
    }
    auto& rcm = engine.getRenderableManager();
    for (const auto& b : city.buildings) {
        BuildingEntities be;
        be.bounds = b.bounds;
        const mat4f xf = placement(b.place);
        for (int l = 0; l < 3; ++l) {
            const auto& m = archetypeMeshes_[b.archetype][l];
            be.lod[l] = makeRenderable({{&m[0], facadeMi_}, {&m[1], detailMi_}}, xf, true, true);
            be.drawable[l] = addDrawable(be.lod[l], b.bounds, m[0].triangles + m[1].triangles, 2, true);
            rcm.setLayerMask(rcm.getInstance(be.lod[l]), 0xff, kLayerHidden);
            drawables_[be.drawable[l]].visibleLayer = false;
        }
        buildings_.push_back(be);
    }
    report(0.55f);

    // ---- 看板 ----
    for (int s = 0; s < 4; ++s) {
        signMeshes_[s][0] = upload(engine, city.signShapes[s].face);
        signMeshes_[s][1] = upload(engine, city.signShapes[s].frame);
    }
    for (const auto& s : city.signs) {
        MaterialInstance* mi = materials_.get("sign")->createInstance();
        mi->setParameter("color", s.color);
        mi->setParameter("pattern", static_cast<float>(s.pattern));
        mi->setParameter("seed", s.seed);
        mi->setParameter("nits", s.shape == SignShape::Rooftop ? 60.0f : 45.0f);
        signMis_.push_back(mi);
        const auto& m = signMeshes_[static_cast<int>(s.shape)];
        utils::Entity e = makeRenderable({{&m[0], mi}, {&m[1], detailMi_}}, placement(s.place), true, true);
        addDrawable(e, s.bounds, m[0].triangles + m[1].triangles, 2, true);
        staticEntities_.push_back(e);
    }
    report(0.62f);

    // ---- 小物・地面・高架 ----
    for (int p = 0; p < 6; ++p) propMeshes_[p] = upload(engine, city.propShapes[p]);
    for (const auto& p : city.props) {
        const auto& m = propMeshes_[static_cast<int>(p.shape)];
        utils::Entity e = makeRenderable({{&m, detailMi_}}, placement(p.place), true, true);
        addDrawable(e, p.bounds, m.triangles, 1, true);
        staticEntities_.push_back(e);
    }
    auto addTiles = [&](const std::vector<Tile>& tiles, MaterialInstance* mi, bool shadow) {
        for (const auto& t : tiles) {
            tileMeshes_.push_back(upload(engine, t.mesh));
        }
        size_t first = tileMeshes_.size() - tiles.size();
        for (size_t i = 0; i < tiles.size(); ++i) {
            const GpuMesh& m = tileMeshes_[first + i];
            if (!m.valid()) continue;
            utils::Entity e = makeRenderable({{&m, mi}}, mat4f{}, shadow, true);
            addDrawable(e, tiles[i].bounds, m.triangles, 1, shadow);
            staticEntities_.push_back(e);
        }
    };
    tileMeshes_.reserve(city.groundTiles.size() + city.overpassTiles.size() + city.overpassDetailTiles.size());
    addTiles(city.groundTiles, groundMi_, false);
    addTiles(city.overpassTiles, groundMi_, true);
    addTiles(city.overpassDetailTiles, detailMi_, true);
    report(0.7f);

    // ---- 歩行者 ----
    rig_ = makeRig();
    for (int v = 0; v < 4; ++v) {
        CharacterMesh cm = generateCharacter(v);
        characterMeshes_[v] = upload(engine, cm.mesh, &cm.joints, &cm.weights);
    }
    {
        const int count = cfg.crowdCountFor(preset);
        walkers_ = makeWalkers(city, count, cfg.seed, cfg.crowd.variants);
        Rng r(cfg.seed, 555);
        static const float3 kJackets[] = {{0.02f, 0.02f, 0.025f}, {0.25f, 0.02f, 0.05f}, {0.03f, 0.08f, 0.18f},
                                          {0.35f, 0.3f, 0.25f},   {0.05f, 0.05f, 0.05f}, {0.4f, 0.12f, 0.02f},
                                          {0.12f, 0.12f, 0.14f},  {0.6f, 0.55f, 0.1f}};
        for (const auto& w : walkers_) {
            MaterialInstance* mi = materials_.get("character")->createInstance();
            mi->setParameter("jacket", kJackets[r.irange(0, 7)]);
            mi->setParameter("pants", float3{0.03f, 0.03f, 0.04f} * r.range(0.6f, 2.5f));
            float skin = r.range(0.15f, 0.65f);
            mi->setParameter("skin", float3{skin, skin * 0.72f, skin * 0.58f});
            mi->setParameter("hair", r.chance(0.2f) ? luminanceNormalized(float3{r.uniform(), 0.1f, r.uniform()}) * 0.4f
                                                    : float3{0.02f, 0.015f, 0.01f});
            mi->setParameter("glow", luminanceNormalized(r.chance(0.5f) ? float3{1, 0.05f, 0.6f} : float3{0, 0.8f, 1}) * 2.0f);
            mi->setParameter("glowNits", 70.0f);
            ownedMis_.push_back(mi);

            const GpuMesh& m = characterMeshes_[w.variant];
            utils::Entity e = utils::EntityManager::get().create();
            RenderableManager::Builder(1)
                .boundingBox(Box().set(float3{-0.6f, 0.f, -0.6f}, float3{0.6f, 2.0f, 0.6f}))
                .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, m.vb, m.ib, 0, m.indexCount)
                .material(0, mi)
                .skinning(CharacterRig::kBoneCount)
                .castShadows(true)
                .receiveShadows(true)
                .build(engine, e);
            engine.getTransformManager().create(e);
            scene.addEntity(e);
            WalkerEntity we;
            we.entity = e;
            we.variant = w.variant;
            we.mi = mi;
            we.drawable = addDrawable(e, {}, m.triangles, 1, true);
            walkerEntities_.push_back(we);
        }
    }
    report(0.8f);

    // ---- 車 ----
    for (int m = 0; m < 3; ++m) vehicleMeshes_[m] = upload(engine, generateVehicle(m));
    {
        vehicles_ = makeVehicles(city, cfg.vehicles.count, cfg.seed, 3);
        static const float3 kPaint[] = {{0.02f, 0.02f, 0.02f}, {0.6f, 0.6f, 0.62f}, {0.5f, 0.02f, 0.02f}, {0.02f, 0.05f, 0.2f},
                                        {0.6f, 0.4f, 0.02f},   {0.9f, 0.9f, 0.88f}, {0.04f, 0.25f, 0.28f}, {0.3f, 0.02f, 0.3f}};
        Rng r(cfg.seed, 777);
        for (const auto& v : vehicles_) {
            MaterialInstance* mi = materials_.get("vehicle")->createInstance();
            mi->setParameter("paint", kPaint[v.colorIndex]);
            mi->setParameter("underglow", r.chance(0.4f) ? luminanceNormalized(float3{r.uniform(), 0.1f, 1.0f}) * 2.0f : float3{0});
            mi->setParameter("lightNits", 900.0f);
            ownedMis_.push_back(mi);
            const GpuMesh& m = vehicleMeshes_[v.model];
            utils::Entity e = makeRenderable({{&m, mi}}, mat4f{}, true, true);
            VehicleEntity ve;
            ve.entity = e;
            ve.mi = mi;
            ve.drawable = addDrawable(e, {}, m.triangles, 1, true);
            vehicleEntities_.push_back(ve);
        }
    }
    report(0.86f);

    // ---- 光源 ----
    {
        // 月（カスケードシャドウ、コンタクトシャドウ）
        moon_ = utils::EntityManager::get().create();
        LightManager::ShadowOptions so;
        so.mapSize = static_cast<uint32_t>(preset.shadow.map);
        so.shadowCascades = static_cast<uint8_t>(preset.shadow.cascades);
        // 遠距離短め（Deck の Low/Low 設定に合わせる）。High ほど遠くまで
        so.shadowFar = preset.shadow.map >= 2048 ? 160.f : (preset.shadow.cascades >= 3 ? 110.f : 70.f);
        if (so.shadowCascades == 4) {
            so.cascadeSplitPositions[0] = 0.06f; so.cascadeSplitPositions[1] = 0.16f; so.cascadeSplitPositions[2] = 0.4f;
        } else if (so.shadowCascades == 3) {
            so.cascadeSplitPositions[0] = 0.1f; so.cascadeSplitPositions[1] = 0.32f;
        } else {
            so.cascadeSplitPositions[0] = 0.22f;
        }
        so.stable = false;
        so.lispsm = true;
        so.screenSpaceContactShadows = preset.shadow.contact;
        so.stepCount = 8;
        so.maxShadowDistance = 0.4f;
        LightManager::Builder(LightManager::Type::DIRECTIONAL)
            .color(float3{0.62f, 0.72f, 1.0f})
            .intensity(cfg.lights.moonLux)
            .direction(-moonDir_)
            .castShadows(true)
            .shadowOptions(so)
            .build(engine, moon_);
        scene.addEntity(moon_);

        for (const auto& l : city.lights) {
            LightEntity le;
            le.def = &l;
            le.entity = utils::EntityManager::get().create();
            if (l.kind == LightKind::NeonPoint) {
                LightManager::Builder(LightManager::Type::POINT)
                    .color(l.color)
                    .intensity(l.intensity)
                    .position(l.position)
                    .falloff(l.falloff)
                    .build(engine, le.entity);
            } else {
                LightManager::ShadowOptions lso;
                lso.mapSize = static_cast<uint32_t>(preset.shadow.localMap);
                lso.constantBias = 0.002f;
                lso.normalBias = 1.2f;
                LightManager::Builder(LightManager::Type::SPOT)
                    .color(l.color)
                    .intensity(l.intensity)
                    .position(l.position)
                    .direction(l.direction)
                    .spotLightCone(l.innerCone, l.outerCone)
                    .falloff(l.falloff)
                    .castShadows(false)
                    .shadowOptions(lso)
                    .build(engine, le.entity);
            }
            scene.addEntity(le.entity);
            lights_.push_back(le);
        }
    }

    // ---- 霧・粒子・View ----
    fog_.build(engine, scene, materials_.get("fog"), cfg, preset);
    particles_.build(engine, scene, materials_.get("rain"), materials_.get("steam"), cfg, city);
    applyPreset(engine, view, preset, cfg);
    view.setVisibleLayers(0xff, kLayerVisible);

    // 色づくり：コントラスト強めのトーンカーブ、暗部は少し青緑、ネオンの彩度は残す
    {
        GenericToneMapper tm(1.6f, 0.18f, 0.2f, 12.0f);
        colorGrading_ = ColorGrading::Builder()
                            .toneMapper(&tm)
                            .saturation(1.12f)
                            .vibrance(1.2f)
                            .contrast(1.04f)
                            .shadowsMidtonesHighlights(float4{0.95f, 1.0f, 1.07f, 0.0f}, float4{1.0f, 1.0f, 1.0f, 0.0f},
                                                       float4{1.04f, 1.0f, 0.99f, 0.0f}, float4{0.0f, 0.333f, 0.55f, 1.0f})
                            .build(engine);
        view.setColorGrading(colorGrading_);
    }

    // 夜のストリート撮影相当の露出（f/2.8, 1/30s, ISO 3200）
    camera.setExposure(2.8f, 1.0f / 30.0f, 3200.0f);
    report(1.0f);
    return true;
}

void World::update(double t, uint32_t frame) {
    Engine& engine = *engine_;
    auto& tcm = engine.getTransformManager();
    auto& rcm = engine.getRenderableManager();
    auto& lcm = engine.getLightManager();

    // ---- カメラ ----
    CameraKey key = path_->sample(t);
    const float3 eye = key.position;
    const float aspect = static_cast<float>(cfg_->width) / static_cast<float>(cfg_->height);
    camera_->setProjection(key.fovDeg, aspect, 0.1, 2500.0, Camera::Fov::VERTICAL);
    camera_->lookAt(eye, eye + key.forward, float3{0, 1, 0});
    camera_->setFocusDistance(28.0f);
    tcm.setTransform(tcm.getInstance(sky_), mat4f::translation(eye));

    // ---- ビルのLOD（XZ距離、仕様の [60, 180, 500] m） ----
    const auto& lod = cfg_->city.lodDistances;
    for (auto& b : buildings_) {
        float d = distanceToAabb(b.bounds, float3{eye.x, std::clamp(eye.y, b.bounds.min.y, b.bounds.max.y), eye.z});
        int want = d < lod[0] ? 0 : d < lod[1] ? 1 : d < lod[2] ? 2 : -1;
        if (want == b.current) continue;
        for (int l = 0; l < 3; ++l) {
            bool on = (l == want);
            rcm.setLayerMask(rcm.getInstance(b.lod[l]), 0xff, on ? kLayerVisible : kLayerHidden);
            drawables_[b.drawable[l]].visibleLayer = on;
        }
        b.current = want;
    }

    // ---- 歩行者 ----
    std::array<mat4f, CharacterRig::kBoneCount> bones;
    for (size_t i = 0; i < walkers_.size(); ++i) {
        const Walker& w = walkers_[i];
        ActorPose p = walkerPose(*city_, w, t);
        mat4f xf = mat4f::translation(p.position) * mat4f::rotation(p.yaw, float3{0, 1, 0}) * mat4f::scaling(float3{w.scale});
        tcm.setTransform(tcm.getInstance(walkerEntities_[i].entity), xf);
        Drawable& d = drawables_[walkerEntities_[i].drawable];
        d.bounds = {};
        d.bounds.add(p.position - float3{0.6f, 0, 0.6f});
        d.bounds.add(p.position + float3{0.6f, 2.0f, 0.6f});
        // 遠い歩行者はポーズ更新を省く（見えても静止で十分な距離）
        if (length(p.position - eye) < 90.f) {
            walkPose(rig_, p.phase, 1.0f, bones);
            rcm.setBones(rcm.getInstance(walkerEntities_[i].entity), bones.data(), bones.size(), 0);
        }
        walkerEntities_[i].mi->setParameter("time", static_cast<float>(t));
    }

    // ---- 車 ----
    for (size_t i = 0; i < vehicles_.size(); ++i) {
        ActorPose p = vehiclePose(*city_, vehicles_[i], t);
        float pitch = 0.f;
        if (vehicles_[i].overpass) {
            float dir = std::cos(p.yaw) >= 0 ? 1.f : -1.f;
            float h0 = city_->overpassHeightAt(p.position.z - 1.5f * dir), h1 = city_->overpassHeightAt(p.position.z + 1.5f * dir);
            pitch = -std::atan2(h1 - h0, 3.0f);
        }
        mat4f xf = mat4f::translation(p.position) * mat4f::rotation(p.yaw, float3{0, 1, 0}) * mat4f::rotation(pitch, float3{1, 0, 0});
        tcm.setTransform(tcm.getInstance(vehicleEntities_[i].entity), xf);
        Drawable& d = drawables_[vehicleEntities_[i].drawable];
        d.bounds = {};
        d.bounds.add(p.position - float3{2.8f, 0, 2.8f});
        d.bounds.add(p.position + float3{2.8f, 2.2f, 2.8f});
        vehicleEntities_[i].mi->setParameter("time", static_cast<float>(t));
    }

    // ---- 光源の間引き ----
    // Filament は1ビューで最大255灯（超えた分は黙って捨てられる）。カメラに近い順に kMaxSceneLights 灯だけを
    // シーンに入れ、どの端末でも同じ光源が効くようにする（月を足しても上限に収まる）
    {
        std::vector<std::pair<float, int>> byDist;
        byDist.reserve(lights_.size());
        for (size_t i = 0; i < lights_.size(); ++i) byDist.push_back({length(lights_[i].def->position - eye), static_cast<int>(i)});
        std::sort(byDist.begin(), byDist.end());
        std::vector<bool> keep(lights_.size(), false);
        for (size_t k = 0; k < byDist.size() && k < kMaxSceneLights; ++k) keep[byDist[k].second] = true;
        for (size_t i = 0; i < lights_.size(); ++i) {
            if (keep[i] == lights_[i].inScene) continue;
            if (keep[i]) scene_->addEntity(lights_[i].entity);
            else scene_->remove(lights_[i].entity);
            lights_[i].inScene = keep[i];
        }
    }

    // ---- 局所影：カメラに近く、前方にあるスポットから N 灯 ----
    {
        const int want = preset_->shadow.localLights;
        std::vector<std::pair<float, int>> cand;
        for (size_t i = 0; i < lights_.size(); ++i) {
            if (lights_[i].def->kind == LightKind::NeonPoint) continue;
            float3 to = lights_[i].def->position - eye;
            float d = length(to);
            float facing = dot(to / std::max(d, 1e-3f), key.forward);
            if (d > 70.f || facing < -0.3f) continue;
            cand.push_back({d - facing * 10.f, static_cast<int>(i)});
        }
        std::sort(cand.begin(), cand.end());
        std::vector<bool> on(lights_.size(), false);
        for (int k = 0; k < std::min<int>(want, static_cast<int>(cand.size())); ++k) on[cand[k].second] = true;
        int shadowed = 0;
        for (size_t i = 0; i < lights_.size(); ++i) {
            if (lights_[i].def->kind == LightKind::NeonPoint) continue;
            if (lights_[i].shadow != on[i]) {
                lcm.setShadowCaster(lcm.getInstance(lights_[i].entity), on[i]);
                lights_[i].shadow = on[i];
            }
            shadowed += on[i] ? 1 : 0;
        }
        stats_.shadowedSpots = shadowed;
    }

    // ---- 霧に渡す光源（近い16灯、強さ順） ----
    std::vector<LightSample> nearest;
    float3 steamTint{0.0f};
    {
        std::vector<std::pair<float, int>> byScore;
        for (size_t i = 0; i < lights_.size(); ++i) {
            const LightDef& l = *lights_[i].def;
            float d = length(l.position - eye);
            if (d > 60.f) continue;
            byScore.push_back({d * d / (l.intensity + 1.f), static_cast<int>(i)});
        }
        std::sort(byScore.begin(), byScore.end());
        float wsum = 0.f;
        for (size_t k = 0; k < byScore.size() && k < 16; ++k) {
            const LightDef& l = *lights_[byScore[k].second].def;
            LightSample s;
            s.position = l.position;
            // ルーメン → カンデラ（点光源は /4π、スポットは円錐に集中するので少し強め）
            float cd = l.intensity / (4.f * kPi) * (l.kind == LightKind::NeonPoint ? 1.f : 2.f);
            s.color = l.color * cd * 0.35f;
            s.range = l.falloff * 1.5f;
            s.spot = l.kind == LightKind::NeonPoint ? 0.f : 1.0f;
            nearest.push_back(s);
            float w = 1.f / (1.f + length(l.position - eye));
            steamTint += l.color * w;
            wsum += w;
        }
        steamTint = wsum > 0 ? steamTint / wsum : float3{0.5f};
        steamTint = steamTint * 0.45f + float3{0.45f, 0.45f, 0.5f};
    }
    fog_.update(t, frame, nearest, moonDir_);
    particles_.update(t, steamTint);

    // ---- 時間で動くマテリアル ----
    const float ft = static_cast<float>(t);
    facadeMi_->setParameter("time", ft);
    detailMi_->setParameter("time", ft);
    groundMi_->setParameter("time", ft);
    skyMi_->setParameter("time", ft);
    for (auto* mi : signMis_) mi->setParameter("time", ft);

    updateStats(eye);
}

void World::updateStats(const float3& eye) {
    Frustum frustum = camera_->getFrustum();
    size_t tris = 0, draws = 0;
    int renderables = 0;
    const int cascades = preset_->shadow.cascades;
    const float shadowFar = preset_->shadow.map >= 2048 ? 160.f : (cascades >= 3 ? 110.f : 70.f);
    for (const auto& d : drawables_) {
        if (!d.visibleLayer || d.bounds.empty()) continue;
        Box box = Box().set(d.bounds.min, d.bounds.max);
        if (frustum.intersects(box)) {
            tris += d.triangles;
            draws += static_cast<size_t>(d.primitives);
            ++renderables;
        }
        // 影パス：月のカスケード（範囲内の投影物は概ね2段に入る）と局所影
        if (d.castsShadow && distanceToAabb(d.bounds, eye) < shadowFar) {
            int passes = std::min(cascades, 2);
            tris += d.triangles * static_cast<size_t>(passes);
            draws += static_cast<size_t>(d.primitives * passes);
        }
    }
    for (const auto& l : lights_) {
        if (!l.shadow) continue;
        for (const auto& d : drawables_) {
            if (!d.visibleLayer || !d.castsShadow || d.bounds.empty()) continue;
            if (distanceToAabb(d.bounds, l.def->position) < l.def->falloff) {
                tris += d.triangles;
                draws += static_cast<size_t>(d.primitives);
            }
        }
    }
    int visibleLights = 0;
    for (const auto& l : lights_) {
        if (l.inScene && frustum.intersects(float4{l.def->position, l.def->falloff})) ++visibleLights;
    }
    stats_.lightsVisible = visibleLights + 1;  // + 月
    stats_.triangles = tris + (fog_.enabled() ? 0 : 0);
    stats_.drawCalls = draws + static_cast<size_t>(fog_.sliceCount()) + (particles_.count() > 0 ? 2 : 0);
    stats_.renderables = renderables;
}

void World::destroy(Engine& engine) {
    auto destroyEntity = [&](utils::Entity e) {
        if (e.isNull()) return;
        engine.destroy(e);
        utils::EntityManager::get().destroy(e);
    };
    fog_.destroy(engine);
    particles_.destroy(engine);
    for (auto& b : buildings_) {
        for (auto e : b.lod) destroyEntity(e);
    }
    buildings_.clear();
    for (auto e : staticEntities_) destroyEntity(e);
    staticEntities_.clear();
    for (auto& w : walkerEntities_) destroyEntity(w.entity);
    walkerEntities_.clear();
    for (auto& v : vehicleEntities_) destroyEntity(v.entity);
    vehicleEntities_.clear();
    for (auto& l : lights_) destroyEntity(l.entity);
    lights_.clear();
    destroyEntity(moon_);
    destroyEntity(sky_);
    moon_ = sky_ = {};
    for (auto* mi : signMis_) engine.destroy(mi);
    signMis_.clear();
    for (auto* mi : ownedMis_) engine.destroy(mi);
    ownedMis_.clear();
    for (auto& a : archetypeMeshes_) {
        for (auto& l : a) {
            for (auto& m : l) render::destroy(engine, m);
        }
    }
    archetypeMeshes_.clear();
    for (auto& s : signMeshes_) {
        for (auto& m : s) render::destroy(engine, m);
    }
    for (auto& m : propMeshes_) render::destroy(engine, m);
    for (auto& m : tileMeshes_) render::destroy(engine, m);
    tileMeshes_.clear();
    for (auto& m : characterMeshes_) render::destroy(engine, m);
    for (auto& m : vehicleMeshes_) render::destroy(engine, m);
    render::destroy(engine, skyMesh_);
    if (colorGrading_) engine.destroy(colorGrading_);
    colorGrading_ = nullptr;
    render::destroy(engine, textures_);
    render::destroy(engine, ibl_);
    materials_.destroy(engine);
    drawables_.clear();
}

}  // namespace bench::render

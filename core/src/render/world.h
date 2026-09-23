// シーン全体：街・人・車・光源・空・霧・粒子。生成（build）と毎フレームの更新（update）。
#pragma once

#include <filament/Camera.h>
#include <filament/ColorGrading.h>
#include <filament/LightManager.h>

#include "bench/animation.h"
#include "bench/bench_app.h"
#include "render.h"

namespace bench::render {

class World {
public:
    struct BuildInput {
        const SceneConfig* config = nullptr;
        const Preset* preset = nullptr;
        const CityData* city = nullptr;
        const CameraPath* path = nullptr;
        Platform* platform = nullptr;
    };

    bool build(Engine& engine, Scene& scene, View& view, Camera& camera, const BuildInput& in,
               const std::function<void(float)>& progress, std::string* error);
    // 時刻 t（周回内 0..60s）の状態にする。frame はTAA/霧のジッタ用
    void update(double t, uint32_t frame);
    void destroy(Engine& engine);

    RenderStats stats() const { return stats_; }

private:
    struct Drawable {
        utils::Entity entity;
        Aabb bounds;
        size_t triangles = 0;
        int primitives = 1;
        bool castsShadow = true;
        bool visibleLayer = true;
    };
    struct BuildingEntities {
        std::array<utils::Entity, 3> lod;
        std::array<int, 3> drawable{-1, -1, -1};
        int current = -1;
        Aabb bounds;
    };
    struct WalkerEntity {
        utils::Entity entity;
        int drawable = -1;
        int variant = 0;
        MaterialInstance* mi = nullptr;
    };
    struct VehicleEntity {
        utils::Entity entity;
        int drawable = -1;
        MaterialInstance* mi = nullptr;
    };
    struct LightEntity {
        utils::Entity entity;
        const LightDef* def = nullptr;
        bool shadow = false;
        bool inScene = true;
    };

    int addDrawable(utils::Entity e, const Aabb& b, size_t tris, int prims, bool shadow);
    utils::Entity makeRenderable(const std::vector<std::pair<const GpuMesh*, MaterialInstance*>>& parts, const mat4f& xf,
                                 bool castShadows, bool receiveShadows, bool culling = true);
    void updateStats(const math::float3& eye);

    Engine* engine_ = nullptr;
    Scene* scene_ = nullptr;
    View* view_ = nullptr;
    Camera* camera_ = nullptr;
    const SceneConfig* cfg_ = nullptr;
    const Preset* preset_ = nullptr;
    const CityData* city_ = nullptr;
    const CameraPath* path_ = nullptr;

    Materials materials_;
    TextureSet textures_;
    Ibl ibl_;
    ColorGrading* colorGrading_ = nullptr;
    Fog fog_;
    Particles particles_;

    // 共有メッシュ
    std::vector<std::array<std::array<GpuMesh, 2>, 3>> archetypeMeshes_;  // [型][LOD][facade/detail]
    std::array<std::array<GpuMesh, 2>, 4> signMeshes_;                     // [形][face/frame]
    std::array<GpuMesh, 6> propMeshes_;
    std::vector<GpuMesh> tileMeshes_;
    std::array<GpuMesh, 4> characterMeshes_;
    std::array<GpuMesh, 3> vehicleMeshes_;
    GpuMesh skyMesh_;
    CharacterRig rig_;

    // マテリアルインスタンス
    MaterialInstance* facadeMi_ = nullptr;
    MaterialInstance* detailMi_ = nullptr;
    MaterialInstance* groundMi_ = nullptr;
    MaterialInstance* skyMi_ = nullptr;
    std::vector<MaterialInstance*> signMis_;
    std::vector<MaterialInstance*> ownedMis_;

    std::vector<Drawable> drawables_;
    std::vector<BuildingEntities> buildings_;
    std::vector<utils::Entity> staticEntities_;
    std::vector<Walker> walkers_;
    std::vector<WalkerEntity> walkerEntities_;
    std::vector<Vehicle> vehicles_;
    std::vector<VehicleEntity> vehicleEntities_;
    std::vector<LightEntity> lights_;
    utils::Entity moon_;
    utils::Entity sky_;
    math::float3 moonDir_;

    RenderStats stats_;
};

}  // namespace bench::render

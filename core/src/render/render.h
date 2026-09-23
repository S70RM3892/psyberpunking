// 描画層の内部宣言（ホストからは見えない）。
#pragma once

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <filament/Box.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/IndirectLight.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/Scene.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>
#include <utils/Entity.h>

#include "bench/camera_path.h"
#include "bench/config.h"
#include "bench/platform.h"
#include "bench/procgen.h"
#include "bench/traffic.h"

namespace bench::render {

using namespace filament;
using filament::math::mat4f;

// ---- メッシュ -------------------------------------------------------------------------------------

struct GpuMesh {
    VertexBuffer* vb = nullptr;
    IndexBuffer* ib = nullptr;
    Box box;
    uint32_t indexCount = 0;
    size_t triangles = 0;
    bool valid() const { return vb && ib && indexCount > 0; }
};

// 頂点形式: POSITION float3, TANGENTS short4（四元数）, UV0 float2, CUSTOM0 float4（+ スキニングなら BONE_INDICES/WEIGHTS）
GpuMesh upload(Engine& engine, const Mesh& mesh, const std::vector<std::array<uint16_t, 4>>* joints = nullptr,
               const std::vector<math::float4>* weights = nullptr);
void destroy(Engine& engine, GpuMesh& m);

// ---- テクスチャ ------------------------------------------------------------------------------------

enum class Tex : int {
    AsphaltAlbedo = 0, AsphaltNormal, AsphaltOrm, PavingAlbedo, PavingNormal,
    WallAlbedo, WallNormal, WallOrm, MetalAlbedo, ConcreteAlbedo, Count
};

struct TextureSet {
    std::array<Texture*, static_cast<int>(Tex::Count)> tex{};
    TextureSampler sampler;
    int fromAssets = 0;  // KTX2 から読めた枚数（残りは実行時に手続き生成）
    Texture* get(Tex t) const { return tex[static_cast<int>(t)]; }
};

// textures/<name>_<1k|512>.ktx2 があれば使い、無ければ同じ役割のテクスチャを実行時に生成する
TextureSet loadTextures(Engine& engine, Platform& platform, int textureMax);
void destroy(Engine& engine, TextureSet& t);

struct Ibl {
    IndirectLight* light = nullptr;
    Texture* reflections = nullptr;
    Texture* environment = nullptr;
    bool fromAssets = false;
};
// ibl/night_ibl.ktx（cmgen の出力）があれば使い、無ければ夜の街の環境光を手続き生成してGPUでプリフィルタする
Ibl makeIbl(Engine& engine, Platform& platform, math::float3 moonDir);
void destroy(Engine& engine, Ibl& ibl);

// ---- マテリアル ------------------------------------------------------------------------------------

class Materials {
public:
    bool load(Engine& engine, Platform& platform, std::string* error);
    void destroy(Engine& engine);
    Material* get(const char* name) const;

private:
    std::map<std::string, Material*> materials_;
};

// ---- 霧・粒子 --------------------------------------------------------------------------------------

struct LightSample {
    math::float3 position;
    math::float3 color;   // リニア × 強度（フォグの散乱用に正規化済み）
    float range = 10.f;
    float spot = 0.f;     // 下向きスポットなら > 0
};

class Fog {
public:
    void build(Engine& engine, Scene& scene, Material* material, const SceneConfig& cfg, const Preset& preset);
    void update(double time, uint32_t frame, const std::vector<LightSample>& nearest, math::float3 moonDir);
    void destroy(Engine& engine);
    bool enabled() const { return !slices_.empty(); }
    size_t vertexCount() const { return vertexCount_; }
    int sliceCount() const { return static_cast<int>(slices_.size()); }

private:
    MaterialInstance* mi_ = nullptr;
    VertexBuffer* vb_ = nullptr;
    std::vector<VertexBuffer*> sliceVbs_;
    IndexBuffer* ib_ = nullptr;
    std::vector<utils::Entity> slices_;
    size_t vertexCount_ = 0;
    SceneConfig::Fog params_;
};

class Particles {
public:
    void build(Engine& engine, Scene& scene, Material* rain, Material* steam, const SceneConfig& cfg, const CityData& city);
    void update(double time, math::float3 steamTint);
    void destroy(Engine& engine);
    int count() const { return count_; }

private:
    MaterialInstance* rainMi_ = nullptr;
    MaterialInstance* steamMi_ = nullptr;
    VertexBuffer* rainVb_ = nullptr;
    VertexBuffer* steamVb_ = nullptr;
    utils::Entity rain_, steam_;
    int count_ = 0;
};

// ---- プリセット ------------------------------------------------------------------------------------

void applyPreset(Engine& engine, View& view, const Preset& preset, const SceneConfig& cfg);

}  // namespace bench::render

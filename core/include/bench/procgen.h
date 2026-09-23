// 夜の高密度都市を、シード固定で手続き生成する。手作業のモデリングはゼロ。
//
// 座標系: Y上、X = 街区の横方向、Z = 大通りの方向（カメラは +Z へ進む）。単位はメートル。
// ビルは「型（アーキタイプ）」を生成してインスタンス配置する（仕様の「ビル200棟をインスタンシング」）。
// 同じ型でも、窓の点灯パターンはシェーダがインスタンスのワールド位置から変える。
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "bench/config.h"
#include "bench/mesh.h"

namespace bench {

// custom0 の割り当て（materials/*.mat と一致させること）
//   facade:  x = style + seed*0.999, y = part, z = 柱間幅[m], w = 階高[m]
//   detail:  x = 材質種別（0 金属, 1 コンクリート, 2 塗装, 3 発光ストリップ, 4 航空障害灯）, y = 色相(0..1), z = 発光強度倍率
//   sign:    x = 形状の縦横比, y/z/w 未使用（色とパターンはマテリアルインスタンスで渡す）
//   ground:  x = 種別（0 車道, 1 歩道, 2 縁石, 3 高架床, 4 路地, 5 交差点）, y = 車線幅, z = 片側車線数, w = 濡れ具合
enum class FacadePart : int { Wall = 0, Reveal = 1, Glass = 2, Shopfront = 3, ProceduralWindows = 4 };
enum class DetailKind : int { Metal = 0, Concrete = 1, Painted = 2, LightStrip = 3, BeaconRed = 4 };
enum class GroundKind : int { Road = 0, Sidewalk = 1, Curb = 2, OverpassDeck = 3, Alley = 4, Intersection = 5 };

struct BuildingLod {
    Mesh facade;  // 外壁・窓（facade.mat）
    Mesh detail;  // 庇・フィン・屋上設備（detail.mat）
    size_t triangles() const { return facade.triangleCount() + detail.triangleCount(); }
};

struct BuildingArchetype {
    int style = 0;  // 0 ガラスのタワー, 1 コンクリートの集合住宅, 2 工業系
    float2 footprint;  // X, Z の外形[m]
    float height = 0.f;
    Aabb bounds;       // ローカル（原点 = 足元中央）
    std::vector<Aabb> tiers;  // 段（基壇・中層・塔）の外形。看板はこの壁面に付ける
    std::array<BuildingLod, 3> lods;  // LOD0 = 窓の凹凸まで実ジオメトリ, LOD1 = フィンと庇, LOD2 = 箱
};

struct Placement {
    float3 position;  // 足元中央
    float yaw = 0.f;  // Y軸回転[rad]
};

struct BuildingInstance {
    int archetype = 0;
    Placement place;
    Aabb bounds;  // ワールド
    float pathDistance = 0.f;  // カメラパスまでの水平距離（看板・光源の優先度付けに使う）
};

enum class SignShape : int { Blade = 0, Billboard = 1, Rooftop = 2, ShopBox = 3, Count = 4 };

struct SignShapeMesh {
    Mesh face;   // sign.mat（UV 0..1、面の外向き = +Z）
    Mesh frame;  // detail.mat
    float2 size; // 幅・高さ[m]
};

struct SignInstance {
    SignShape shape = SignShape::Blade;
    int building = -1;
    Placement place;
    float3 color;    // リニアRGB。対応する光源もこの色
    int pattern = 0; // sign.mat のパターン番号（0..7）
    float seed = 0.f;
    float3 facing;   // 看板の向き（外向き法線、ワールド）
    float3 lightPos; // 光源を置くなら、ここ
    Aabb bounds;
    float pathDistance = 0.f;
};

enum class PropShape : int { StreetLamp = 0, Dumpster = 1, AcUnit = 2, Pipe = 3, Barrier = 4, Vent = 5, Count = 6 };

struct PropInstance {
    PropShape shape = PropShape::StreetLamp;
    Placement place;
    Aabb bounds;
};

enum class LightKind : int { NeonPoint = 0, NeonSpot = 1, Street = 2 };

struct LightDef {
    LightKind kind = LightKind::NeonPoint;
    float3 position;
    float3 direction{0, -1, 0};
    float3 color{1};
    float intensity = 1000.f;  // ルーメン
    float falloff = 12.f;
    float innerCone = 0.4f;
    float outerCone = 0.8f;
    int sign = -1;             // 由来の看板（街灯は -1）
    float pathDistance = 0.f;
};

struct Lane {
    float3 a, b;  // 始点・終点（中心線）
    float width = 2.f;
};

struct SteamVent {
    float3 position;
    float strength = 1.f;
};

struct Tile {
    Mesh mesh;
    Aabb bounds;
};

struct CityData {
    std::vector<BuildingArchetype> archetypes;
    std::vector<BuildingInstance> buildings;
    std::array<SignShapeMesh, static_cast<int>(SignShape::Count)> signShapes;
    std::vector<SignInstance> signs;
    std::array<Mesh, static_cast<int>(PropShape::Count)> propShapes;
    std::vector<PropInstance> props;
    std::vector<LightDef> lights;
    std::vector<Tile> groundTiles;          // ground.mat
    std::vector<Tile> overpassTiles;        // ground.mat（高架床）
    std::vector<Tile> overpassDetailTiles;  // detail.mat（橋脚・欄干・発光ストリップ）
    std::vector<Lane> sidewalks;
    std::vector<Lane> roadLanes;            // 地上の車線
    std::vector<Lane> overpassLanes;        // 高架上の車線
    std::vector<SteamVent> vents;
    std::vector<float3> pathPoints;         // カメラパスの骨格（折れ線）。カメラパス生成と優先度付けで共用
    Aabb bounds;

    // 路線の主要座標
    float boulevardX = 0.f;
    float boulevardHalfWidth = 12.f;
    float overpassY = 11.f;
    float rampStartZ = 0.f;     // 上りランプ開始（地上）
    float rampLength = 60.f;
    float overpassEndZ = 0.f;   // 下りランプ開始
    float downRampEndZ = 0.f;   // 下りランプ終了（地上）
    float alleyZ = 0.f;         // 路地（X方向）の中心Z
    float alleyX0 = 0.f, alleyX1 = 0.f;
    float startZ = 0.f;         // カメラの出発点Z

    // 高架の床の高さ（ランプ含む）。範囲外は 0
    float overpassHeightAt(float z) const;

    size_t archetypeTriangles(int lod) const;
    // 決定性テスト用：全メッシュと全配置のハッシュ
    uint64_t hash() const;
};

CityData generateCity(const SceneConfig& config);

// 点から折れ線までの水平（XZ）距離
float distanceToPolylineXZ(const std::vector<float3>& poly, float3 p);

// ---- 歩行者（手続き生成の人体、13ボーン） -------------------------------------------------------

struct CharacterRig {
    static constexpr int kBoneCount = 13;
    enum Bone : int {
        Hips = 0, Spine, Chest, Neck, Head,
        UpperArmL, LowerArmL, UpperArmR, LowerArmR,
        UpperLegL, LowerLegL, UpperLegR, LowerLegR
    };
    std::array<int, kBoneCount> parent{};
    std::array<float3, kBoneCount> joint{};  // モデル空間での関節位置（バインドポーズ）
};

// custom0.x = 部位（0 肌, 1 上着, 2 ズボン, 3 靴, 4 髪, 5 発光ライン, 6 バイザー）
struct CharacterMesh {
    Mesh mesh;
    std::vector<std::array<uint16_t, 4>> joints;
    std::vector<float4> weights;
    CharacterRig rig;
};

CharacterRig makeRig();
CharacterMesh generateCharacter(int variant, int targetTriangles = 13000);

// ---- 車（3車種） ------------------------------------------------------------------------------
// custom0.x = 部位（0 車体, 1 ガラス, 2 タイヤ, 3 ヘッドライト, 4 テールライト, 5 下廻りネオン, 6 ホイール）
// 車体の窓はUVからシェーダで描く（uv.x = 前後方向 0..1, uv.y = 周方向 0..1）
Mesh generateVehicle(int model);

}  // namespace bench

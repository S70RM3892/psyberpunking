// 街の当たり判定（ドライブ）。手続き生成の CityData から作る。
//
// 走れるのは車道・交差点・路地・高架（ランプ含む）。街区の台（高さ 0.15m）は縁石が壁になるので歩道には
// 上がれない。高架は「今の高さが床の近くなら床の上」とみなし（上り口から来た車）、地上の車にとっては
// ランプの下で天井が低い区間が壁になる。
#pragma once

#include <vector>

#include "bench/procgen.h"

namespace drive {

using bench::Aabb;
using bench::float2;
using bench::float3;

// 車体を覆う円のひとつ
struct Circle {
    float2 center;
    float radius = 1.f;
};

// 動く障害物（交通の車・路上の歩行者）。カプセル（線分 ± 半径）で近似
struct DynamicBody {
    float2 center;
    float2 axis{0, 1};    // 長手方向（単位ベクトル）
    float halfLength = 0; // 線分の半長（歩行者は 0 = 円）
    float radius = 0.4f;
    float y = 0;          // 足元の高さ
    float height = 1.5f;
    float2 velocity{0};
    bool pedestrian = false;
};

struct Contact {
    float2 normal;        // 押し出す向き（障害物 → 車）
    float depth = 0;      // めり込み量[m]
    float2 point;         // 接触点（車体側）
    bool dynamic = false;
    bool pedestrian = false;
    float2 otherVelocity{0};
};

class CityCollision {
public:
    explicit CityCollision(const bench::CityData& city);

    const bench::CityData& city() const { return *city_; }

    // 走る面の高さ。高架の範囲で、今の高さ currentY が床の近くなら床（onDeck = true）、そうでなければ地面 0
    float surfaceHeight(float x, float z, float currentY, bool* onDeck = nullptr) const;

    // 円と障害物の重なりを out に足す。y は車体の下端、height は車体の高さ。withDynamic で交通・歩行者も含める
    void collide(const Circle& c, float y, float height, std::vector<Contact>& out, bool withDynamic = true) const;

    // 動く障害物（毎ステップ入れ替える）
    void setDynamicBodies(std::vector<DynamicBody> bodies) { dynamic_ = std::move(bodies); }
    const std::vector<DynamicBody>& dynamicBodies() const { return dynamic_; }

    // 線分 a→b が建物・高架・街の外周に入る最初の位置（0..1、当たらなければ 1）。カメラのめり込み防止用
    float raycast(float3 a, float3 b) const;

    // 点が車の走れる場所か（地上の車道・交差点・路地、または高架の床の上）。テスト・リスポーン用
    bool isDrivable(float3 p, float radius) const;

    // 近くの走れる位置（車線の中心）と向き。詰まった時の立て直し用
    void nearestRoadPose(float3 p, float3* outPos, float* outYaw) const;

private:
    struct Grid {
        float cell = 16.f;
        int nx = 0, nz = 0;
        float3 origin{0};
        std::vector<std::vector<int>> cells;
        void build(const std::vector<Aabb>& boxes, const Aabb& world, float cellSize);
        template <typename F> void query(float x0, float z0, float x1, float z1, F&& f) const;
    };

    bool deckLayer(float x, float z, float y) const;
    void pushOutOfBox(const Circle& c, const Aabb& b, bool dynamic, std::vector<Contact>& out) const;

    const bench::CityData* city_;
    std::vector<Aabb> solids_;      // 車がぶつかる箱（街区の台・橋脚・小物）
    Grid solidGrid_;
    std::vector<Aabb> occluders_;   // カメラが入り込まない箱（ビル・街区の台）
    Grid occluderGrid_;
    std::vector<DynamicBody> dynamic_;
    float rampBlockUpZ_ = 0, rampBlockDownZ_ = 0;  // 地上の車にとってランプ下が壁になる区間の境
};

}  // namespace drive

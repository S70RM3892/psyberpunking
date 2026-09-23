// 手続き生成が出力するCPU側メッシュ。描画層（render/）がFilamentのバッファへ変換する。
#pragma once

#include <cstdint>
#include <vector>

#include <math/vec2.h>
#include <math/vec3.h>
#include <math/vec4.h>

namespace bench {

using filament::math::float2;
using filament::math::float3;
using filament::math::float4;

struct Aabb {
    float3 min{1e30f};
    float3 max{-1e30f};
    void add(float3 p) {
        min = {p.x < min.x ? p.x : min.x, p.y < min.y ? p.y : min.y, p.z < min.z ? p.z : min.z};
        max = {p.x > max.x ? p.x : max.x, p.y > max.y ? p.y : max.y, p.z > max.z ? p.z : max.z};
    }
    void add(const Aabb& b) {
        if (b.empty()) return;
        add(b.min);
        add(b.max);
    }
    bool empty() const { return min.x > max.x; }
    float3 center() const { return (min + max) * 0.5f; }
    float3 halfExtent() const { return (max - min) * 0.5f; }
};

// custom0 の用途は材質ごとに決める（窓の行列・発光パターン番号・色など）。材質側の .mat の説明を参照
struct Mesh {
    std::vector<float3> positions;
    std::vector<float3> normals;
    std::vector<float4> tangents;  // xyz = 接線, w = 従法線の符号
    std::vector<float2> uvs;
    std::vector<float4> custom;
    std::vector<uint32_t> indices;

    size_t vertexCount() const { return positions.size(); }
    size_t triangleCount() const { return indices.size() / 3; }
    bool empty() const { return indices.empty(); }
    Aabb bounds() const;

    uint32_t addVertex(float3 p, float3 n, float4 t, float2 uv, float4 c);
    void addTriangle(uint32_t a, uint32_t b, uint32_t c) {
        indices.push_back(a);
        indices.push_back(b);
        indices.push_back(c);
    }

    // p0→p1→p2→p3 は反時計回り（表から見て）。uv は p0=(u0,v0) p2=(u1,v1) の矩形
    void addQuad(float3 p0, float3 p1, float3 p2, float3 p3, float2 uv0, float2 uv1, float4 c = float4{0});

    // 軸平行な箱。uvScale はワールド1mあたりのUV（壁の模様を実寸で揃えるため）
    void addBox(float3 center, float3 half, float uvScale, float4 c = float4{0}, bool bottom = false);

    // Y軸回りに回転した箱（車・人の部品用）
    void addOrientedBox(float3 center, float3 half, float yaw, float4 c = float4{0});

    // Y軸方向の円柱（柱・街灯・タイヤは rotate して使う）
    void addCylinder(float3 base, float radius, float height, int segments, float4 c = float4{0}, bool caps = true);

    // 楕円体（人体の部位）
    void addEllipsoid(float3 center, float3 radii, int slices, int stacks, float4 c = float4{0});

    void append(const Mesh& other);
};

// 決定性テスト用：全頂点・全インデックスのハッシュ
uint64_t hashMesh(const Mesh& m, uint64_t seed = 0xcbf29ce484222325ULL);

}  // namespace bench

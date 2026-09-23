#include "bench/mesh.h"

#include <cmath>

#include "bench/rng.h"

namespace bench {

using namespace filament::math;

namespace {
constexpr float kPi = 3.14159265358979f;

float4 tangentFor(float3 n) {
    // 法線にほぼ直交する軸から接線を作る（壁は水平方向、床はX方向がU）
    float3 up = std::abs(n.y) > 0.99f ? float3{0, 0, 1} : float3{0, 1, 0};
    float3 t = normalize(cross(up, n));
    return float4{t, 1.0f};
}
}  // namespace

Aabb Mesh::bounds() const {
    Aabb b;
    for (const auto& p : positions) b.add(p);
    return b;
}

uint32_t Mesh::addVertex(float3 p, float3 n, float4 t, float2 uv, float4 c) {
    positions.push_back(p);
    normals.push_back(n);
    tangents.push_back(t);
    uvs.push_back(uv);
    custom.push_back(c);
    return static_cast<uint32_t>(positions.size() - 1);
}

void Mesh::addQuad(float3 p0, float3 p1, float3 p2, float3 p3, float2 uv0, float2 uv1, float4 c) {
    float3 n = normalize(cross(p1 - p0, p3 - p0));
    float3 tdir = p1 - p0;
    float len = length(tdir);
    float4 t = len > 1e-6f ? float4{tdir / len, 1.0f} : tangentFor(n);
    uint32_t a = addVertex(p0, n, t, {uv0.x, uv0.y}, c);
    uint32_t b = addVertex(p1, n, t, {uv1.x, uv0.y}, c);
    uint32_t d = addVertex(p2, n, t, {uv1.x, uv1.y}, c);
    uint32_t e = addVertex(p3, n, t, {uv0.x, uv1.y}, c);
    addTriangle(a, b, d);
    addTriangle(a, d, e);
}

void Mesh::addBox(float3 c, float3 h, float s, float4 cu, bool bottom) {
    float3 lo = c - h, hi = c + h;
    float sx = 2 * h.x * s, sy = 2 * h.y * s, sz = 2 * h.z * s;
    // +Z
    addQuad({lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}, {0, 0}, {sx, sy}, cu);
    // -Z
    addQuad({hi.x, lo.y, lo.z}, {lo.x, lo.y, lo.z}, {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z}, {0, 0}, {sx, sy}, cu);
    // +X
    addQuad({hi.x, lo.y, hi.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}, {0, 0}, {sz, sy}, cu);
    // -X
    addQuad({lo.x, lo.y, lo.z}, {lo.x, lo.y, hi.z}, {lo.x, hi.y, hi.z}, {lo.x, hi.y, lo.z}, {0, 0}, {sz, sy}, cu);
    // +Y
    addQuad({lo.x, hi.y, hi.z}, {hi.x, hi.y, hi.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z}, {0, 0}, {sx, sz}, cu);
    if (bottom) {
        addQuad({lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z}, {lo.x, lo.y, hi.z}, {0, 0}, {sx, sz}, cu);
    }
}

void Mesh::addOrientedBox(float3 c, float3 h, float yaw, float4 cu) {
    size_t first = positions.size();
    addBox(float3{0}, h, 1.0f, cu, true);
    float cs = std::cos(yaw), sn = std::sin(yaw);
    auto rot = [&](float3 v) { return float3{cs * v.x + sn * v.z, v.y, -sn * v.x + cs * v.z}; };
    for (size_t i = first; i < positions.size(); ++i) {
        positions[i] = rot(positions[i]) + c;
        normals[i] = rot(normals[i]);
        tangents[i] = float4{rot(tangents[i].xyz), tangents[i].w};
    }
}

void Mesh::addCylinder(float3 base, float r, float height, int seg, float4 cu, bool caps) {
    uint32_t start = static_cast<uint32_t>(positions.size());
    for (int i = 0; i <= seg; ++i) {
        float a = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(seg);
        float3 n{std::cos(a), 0, std::sin(a)};
        float4 t{-std::sin(a), 0, std::cos(a), 1};
        float u = static_cast<float>(i) / static_cast<float>(seg);
        addVertex(base + n * r, n, t, {u, 0}, cu);
        addVertex(base + n * r + float3{0, height, 0}, n, t, {u, height}, cu);
    }
    for (int i = 0; i < seg; ++i) {
        uint32_t a = start + 2 * i, b = a + 1, c = a + 2, d = a + 3;
        addTriangle(a, b, c);
        addTriangle(c, b, d);
    }
    if (caps) {
        uint32_t center = addVertex(base + float3{0, height, 0}, {0, 1, 0}, {1, 0, 0, 1}, {0.5f, 0.5f}, cu);
        uint32_t ring = static_cast<uint32_t>(positions.size());
        for (int i = 0; i <= seg; ++i) {
            float a = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(seg);
            addVertex(base + float3{std::cos(a) * r, height, std::sin(a) * r}, {0, 1, 0}, {1, 0, 0, 1},
                      {0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a)}, cu);
        }
        for (int i = 0; i < seg; ++i) addTriangle(center, ring + i + 1, ring + i);
    }
}

void Mesh::addEllipsoid(float3 c, float3 r, int slices, int stacks, float4 cu) {
    uint32_t start = static_cast<uint32_t>(positions.size());
    for (int j = 0; j <= stacks; ++j) {
        float v = static_cast<float>(j) / static_cast<float>(stacks);
        float phi = kPi * v;
        for (int i = 0; i <= slices; ++i) {
            float u = static_cast<float>(i) / static_cast<float>(slices);
            float th = 2.0f * kPi * u;
            float3 unit{std::sin(phi) * std::cos(th), std::cos(phi), std::sin(phi) * std::sin(th)};
            float3 n = normalize(unit / r);
            float4 t{-std::sin(th), 0, std::cos(th), 1};
            addVertex(c + unit * r, n, t, {u, v}, cu);
        }
    }
    uint32_t row = static_cast<uint32_t>(slices + 1);
    for (int j = 0; j < stacks; ++j) {
        for (int i = 0; i < slices; ++i) {
            uint32_t a = start + j * row + i, b = a + row;
            addTriangle(a, a + 1, b);
            addTriangle(a + 1, b + 1, b);
        }
    }
}

void Mesh::append(const Mesh& o) {
    uint32_t base = static_cast<uint32_t>(positions.size());
    positions.insert(positions.end(), o.positions.begin(), o.positions.end());
    normals.insert(normals.end(), o.normals.begin(), o.normals.end());
    tangents.insert(tangents.end(), o.tangents.begin(), o.tangents.end());
    uvs.insert(uvs.end(), o.uvs.begin(), o.uvs.end());
    custom.insert(custom.end(), o.custom.begin(), o.custom.end());
    for (uint32_t i : o.indices) indices.push_back(base + i);
}

uint64_t hashMesh(const Mesh& m, uint64_t h) {
    h = fnv1a(m.positions.data(), m.positions.size() * sizeof(float3), h);
    h = fnv1a(m.normals.data(), m.normals.size() * sizeof(float3), h);
    h = fnv1a(m.uvs.data(), m.uvs.size() * sizeof(float2), h);
    h = fnv1a(m.custom.data(), m.custom.size() * sizeof(float4), h);
    h = fnv1a(m.indices.data(), m.indices.size() * sizeof(uint32_t), h);
    return h;
}

}  // namespace bench

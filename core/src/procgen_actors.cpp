// 歩行者と車の手続き生成、歩行アニメーション
#include <algorithm>
#include <cmath>

#include "bench/animation.h"
#include "bench/procgen.h"

namespace bench {

using namespace filament::math;

namespace {

constexpr float kPi = 3.14159265358979f;

// 面法線を頂点に足し込んで滑らかな法線を作る（first 以降の頂点が対象）
void smoothNormals(Mesh& m, size_t firstVertex, size_t firstIndex) {
    for (size_t i = firstVertex; i < m.normals.size(); ++i) m.normals[i] = float3{0};
    for (size_t i = firstIndex; i + 2 < m.indices.size(); i += 3) {
        uint32_t a = m.indices[i], b = m.indices[i + 1], c = m.indices[i + 2];
        float3 n = cross(m.positions[b] - m.positions[a], m.positions[c] - m.positions[a]);
        m.normals[a] += n;
        m.normals[b] += n;
        m.normals[c] += n;
    }
    for (size_t i = firstVertex; i < m.normals.size(); ++i) {
        float len = length(m.normals[i]);
        float3 n = len > 1e-8f ? m.normals[i] / len : float3{0, 1, 0};
        m.normals[i] = n;
        float3 up = std::abs(n.y) > 0.99f ? float3{0, 0, 1} : float3{0, 1, 0};
        m.tangents[i] = float4{normalize(cross(up, n)), 1.0f};
    }
}

void transformRange(Mesh& m, size_t first, const mat4f& t) {
    mat3f n = transpose(inverse(t.upperLeft()));
    for (size_t i = first; i < m.positions.size(); ++i) {
        m.positions[i] = (t * float4{m.positions[i], 1}).xyz;
        m.normals[i] = normalize(n * m.normals[i]);
        m.tangents[i] = float4{normalize(t.upperLeft() * m.tangents[i].xyz), m.tangents[i].w};
    }
}

}  // namespace

// ---- 人体 --------------------------------------------------------------------------------------

CharacterRig makeRig() {
    CharacterRig r;
    using B = CharacterRig;
    r.parent = {-1, B::Hips, B::Spine, B::Chest, B::Neck, B::Chest, B::UpperArmL, B::Chest, B::UpperArmR,
                B::Hips, B::UpperLegL, B::Hips, B::UpperLegR};
    r.joint = {float3{0, 0.95f, 0},  float3{0, 1.08f, 0},  float3{0, 1.28f, 0},  float3{0, 1.50f, 0},
               float3{0, 1.56f, 0},  float3{0.21f, 1.44f, 0}, float3{0.21f, 1.16f, 0}, float3{-0.21f, 1.44f, 0},
               float3{-0.21f, 1.16f, 0}, float3{0.1f, 0.92f, 0}, float3{0.1f, 0.5f, 0}, float3{-0.1f, 0.92f, 0},
               float3{-0.1f, 0.5f, 0}};
    return r;
}

CharacterMesh generateCharacter(int variant, int targetTriangles) {
    CharacterMesh out;
    out.rig = makeRig();
    using B = CharacterRig;
    enum Kind { Skin = 0, Jacket = 1, Pants = 2, Shoes = 3, Hair = 4, Glow = 5, Visor = 6 };

    struct Part {
        float3 c, r;
        int bone;
        int kind;
        float detail;  // 分割数の相対倍率
        bool blend;    // 親ボーンと関節付近をブレンドする
    };
    std::vector<Part> parts = {
        {{0, 0.95f, 0}, {0.17f, 0.12f, 0.11f}, B::Hips, Pants, 1.0f, false},
        {{0, 1.11f, 0}, {0.155f, 0.13f, 0.1f}, B::Spine, Jacket, 1.0f, true},
        {{0, 1.32f, 0}, {0.2f, 0.17f, 0.12f}, B::Chest, Jacket, 1.2f, true},
        {{0, 1.5f, 0}, {0.05f, 0.06f, 0.05f}, B::Neck, Skin, 0.5f, false},
        {{0, 1.64f, 0.01f}, {0.095f, 0.115f, 0.105f}, B::Head, Skin, 1.3f, false},
        {{0, 1.69f, -0.012f}, {0.101f, 0.086f, 0.11f}, B::Head, Hair, 0.9f, false},
        {{0, 1.345f, 0}, {0.205f, 0.012f, 0.125f}, B::Chest, Glow, 0.8f, false},
    };
    for (int s = -1; s <= 1; s += 2) {
        float fs = static_cast<float>(s);
        int ua = s > 0 ? B::UpperArmL : B::UpperArmR, la = s > 0 ? B::LowerArmL : B::LowerArmR;
        int ul = s > 0 ? B::UpperLegL : B::UpperLegR, ll = s > 0 ? B::LowerLegL : B::LowerLegR;
        parts.push_back({{0.21f * fs, 1.3f, 0}, {0.058f, 0.155f, 0.058f}, ua, Jacket, 0.9f, true});
        parts.push_back({{0.21f * fs, 1.02f, 0}, {0.047f, 0.15f, 0.047f}, la, variant % 2 ? Skin : Jacket, 0.8f, true});
        parts.push_back({{0.21f * fs, 0.84f, 0.012f}, {0.038f, 0.06f, 0.026f}, la, Skin, 0.5f, false});
        parts.push_back({{0.1f * fs, 0.71f, 0}, {0.076f, 0.23f, 0.082f}, ul, Pants, 1.0f, true});
        parts.push_back({{0.1f * fs, 0.29f, 0}, {0.058f, 0.22f, 0.06f}, ll, Pants, 0.9f, true});
        parts.push_back({{0.1f * fs, 0.045f, 0.05f}, {0.052f, 0.045f, 0.12f}, ll, Shoes, 0.7f, false});
    }
    if (variant == 1 || variant == 3) {
        parts.push_back({{0, 1.655f, 0.078f}, {0.086f, 0.026f, 0.04f}, B::Head, Visor, 0.6f, false});
    }
    if (variant == 2) {
        parts.push_back({{0, 0.82f, -0.01f}, {0.2f, 0.26f, 0.15f}, B::Hips, Jacket, 1.0f, false});  // コートの裾
    }
    if (variant == 0) {
        parts.push_back({{0, 1.3f, -0.17f}, {0.14f, 0.17f, 0.07f}, B::Chest, Pants, 0.7f, false});  // リュック
    }

    float detailSum = 0;
    for (const auto& p : parts) detailSum += p.detail;
    // 楕円体1個の三角形数 ≈ slices × stacks × 2（stacks = slices/2）= slices²
    const float base = std::sqrt(static_cast<float>(targetTriangles) / detailSum);

    for (const auto& p : parts) {
        int slices = std::max(8, static_cast<int>(std::lround(base * std::sqrt(p.detail))));
        int stacks = std::max(4, slices / 2);
        size_t first = out.mesh.vertexCount();
        out.mesh.addEllipsoid(p.c, p.r, slices, stacks, float4{static_cast<float>(p.kind), 0, 0, 0});
        const int parent = out.rig.parent[p.bone];
        const float3 j = out.rig.joint[p.bone];
        for (size_t i = first; i < out.mesh.vertexCount(); ++i) {
            float wParent = 0.f;
            if (p.blend && parent >= 0) {
                float d = length(out.mesh.positions[i] - j);
                wParent = 0.5f * std::clamp(1.0f - d / 0.09f, 0.0f, 1.0f);
            }
            out.joints.push_back({static_cast<uint16_t>(p.bone), static_cast<uint16_t>(std::max(parent, 0)), 0, 0});
            out.weights.push_back({1.0f - wParent, wParent, 0, 0});
        }
    }
    return out;
}

void walkPose(const CharacterRig& rig, float phase, float stride, std::array<mat4f, CharacterRig::kBoneCount>& out) {
    using B = CharacterRig;
    const float w = 2.0f * kPi * phase;
    const float s = std::sin(w), c = std::cos(w);
    const float amp = std::clamp(stride, 0.3f, 1.5f);

    std::array<mat4f, B::kBoneCount> local;
    for (auto& m : local) m = mat4f{};
    auto pivot = [&](int bone, const mat4f& r) {
        const float3 j = rig.joint[bone];
        return mat4f::translation(j) * r * mat4f::translation(-j);
    };
    local[B::Hips] = mat4f::translation(float3{0, 0.028f * std::cos(2 * w) - 0.01f, 0}) *
                     pivot(B::Hips, mat4f::rotation(0.07f * s * amp, float3{0, 1, 0}));
    local[B::Spine] = pivot(B::Spine, mat4f::rotation(-0.05f * s * amp, float3{0, 1, 0}) * mat4f::rotation(0.04f, float3{1, 0, 0}));
    local[B::Chest] = pivot(B::Chest, mat4f::rotation(-0.05f * s * amp, float3{0, 1, 0}));
    local[B::Head] = pivot(B::Head, mat4f::rotation(0.06f * s * amp, float3{0, 1, 0}));
    local[B::UpperLegL] = pivot(B::UpperLegL, mat4f::rotation(-0.42f * s * amp, float3{1, 0, 0}));
    local[B::UpperLegR] = pivot(B::UpperLegR, mat4f::rotation(0.42f * s * amp, float3{1, 0, 0}));
    local[B::LowerLegL] = pivot(B::LowerLegL, mat4f::rotation((0.1f + 0.55f * std::max(0.f, std::sin(w + 1.3f))) * amp, float3{1, 0, 0}));
    local[B::LowerLegR] = pivot(B::LowerLegR, mat4f::rotation((0.1f + 0.55f * std::max(0.f, std::sin(w + 1.3f + kPi))) * amp, float3{1, 0, 0}));
    local[B::UpperArmL] = pivot(B::UpperArmL, mat4f::rotation(0.34f * s * amp, float3{1, 0, 0}) * mat4f::rotation(-0.08f, float3{0, 0, 1}));
    local[B::UpperArmR] = pivot(B::UpperArmR, mat4f::rotation(-0.34f * s * amp, float3{1, 0, 0}) * mat4f::rotation(0.08f, float3{0, 0, 1}));
    local[B::LowerArmL] = pivot(B::LowerArmL, mat4f::rotation(-0.3f - 0.12f * std::max(0.f, c), float3{1, 0, 0}));
    local[B::LowerArmR] = pivot(B::LowerArmR, mat4f::rotation(-0.3f - 0.12f * std::max(0.f, -c), float3{1, 0, 0}));

    for (int b = 0; b < B::kBoneCount; ++b) {
        int p = rig.parent[b];
        out[b] = (p >= 0) ? out[p] * local[b] : local[b];  // 親は必ず先に来る順に並べてある
    }
}

// ---- 車 ----------------------------------------------------------------------------------------

Mesh generateVehicle(int model) {
    struct Spec {
        float length, width, beltH, hoodH, roofH, cabin0, cabin1, glassSlope, groundClear;
    };
    static const Spec kSpecs[] = {
        {4.6f, 1.85f, 0.95f, 0.98f, 1.45f, 0.28f, 0.72f, 0.12f, 0.18f},  // セダン
        {5.0f, 2.00f, 1.05f, 1.15f, 2.05f, 0.18f, 0.97f, 0.05f, 0.22f},  // バン
        {4.4f, 1.95f, 0.85f, 0.82f, 1.18f, 0.35f, 0.70f, 0.16f, 0.12f},  // スポーツ
    };
    const Spec& sp = kSpecs[std::clamp(model, 0, 2)];
    Mesh m;
    const int N = 48, R = 32;
    const float L = sp.length, W = sp.width * 0.5f;

    auto smoothstep = [](float e0, float e1, float x) {
        float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
        return t * t * (3 - 2 * t);
    };
    // 前後方向 u (0 = 後, 1 = 前) ごとの断面：上端の高さ、幅
    auto topAt = [&](float u) {
        float cab = smoothstep(sp.cabin0 - sp.glassSlope, sp.cabin0 + sp.glassSlope * 0.3f, u) *
                    (1.0f - smoothstep(sp.cabin1 - sp.glassSlope * 0.3f, sp.cabin1 + sp.glassSlope, u));
        float nose = 1.0f - 0.25f * smoothstep(0.85f, 1.0f, u) - 0.12f * (1.0f - smoothstep(0.0f, 0.1f, u));
        return sp.hoodH * nose + (sp.roofH - sp.hoodH) * cab;
    };
    auto widthAt = [&](float u) {
        return W * (0.86f + 0.14f * std::sin(std::clamp(u, 0.02f, 0.98f) * kPi));
    };

    const float4 body{0, 0, sp.cabin0, sp.cabin1};
    const uint32_t start = static_cast<uint32_t>(m.vertexCount());
    for (int k = 0; k <= N; ++k) {
        float u = static_cast<float>(k) / static_cast<float>(N);
        float z = (u - 0.5f) * L;
        float top = topAt(u), bottom = sp.groundClear + 0.1f;
        float mid = (top + bottom) * 0.5f, hh = (top - bottom) * 0.5f;
        float w = widthAt(u);
        for (int i = 0; i <= R; ++i) {
            float v = static_cast<float>(i) / static_cast<float>(R);
            float th = 2.0f * kPi * v;
            float cs = std::cos(th), sn = std::sin(th);
            // 超楕円（n = 5）で角の丸い箱形にする。上半分は屋根ほど絞る
            float px = std::copysign(std::pow(std::abs(cs), 2.0f / 5.0f), cs);
            float py = std::copysign(std::pow(std::abs(sn), 2.0f / 5.0f), sn);
            float taper = py > 0 ? 1.0f - 0.2f * py * smoothstep(sp.beltH, sp.roofH, top) : 1.0f;
            m.addVertex({px * w * taper, mid + py * hh, z}, {0, 1, 0}, {1, 0, 0, 1}, {u, v}, body);
        }
    }
    const uint32_t row = R + 1;
    for (int k = 0; k < N; ++k) {
        for (int i = 0; i < R; ++i) {
            uint32_t a = start + k * row + i, b = a + row;
            m.addTriangle(a, a + 1, b);
            m.addTriangle(a + 1, b + 1, b);
        }
    }
    // 前後の蓋
    for (int end = 0; end < 2; ++end) {
        int k = end == 0 ? 0 : N;
        float z = (end == 0 ? -0.5f : 0.5f) * L;
        float u = end == 0 ? 0.f : 1.f;
        float top = topAt(u), bottom = sp.groundClear + 0.1f;
        uint32_t c = m.addVertex({0, (top + bottom) * 0.5f, z}, {0, 0, end == 0 ? -1.f : 1.f}, {1, 0, 0, 1}, {u, 0.5f}, body);
        for (int i = 0; i < R; ++i) {
            uint32_t a = start + k * row + i;
            if (end == 0) m.addTriangle(c, a + 1, a);  // 後端（−Z向き）
            else m.addTriangle(c, a, a + 1);           // 前端（+Z向き）
        }
    }
    smoothNormals(m, start, 0);

    // タイヤとホイール（Y軸の円柱を X 軸向きに寝かせる）
    const float wr = 0.34f, wheelbase = L * 0.62f;
    const mat4f toX = mat4f::rotation(-kPi * 0.5f, float3{0, 0, 1});
    for (int i = 0; i < 4; ++i) {
        float sx = (i & 1) ? 1.f : -1.f, sz = (i & 2) ? 1.f : -1.f;
        size_t f = m.vertexCount();
        m.addCylinder({0, -0.13f, 0}, wr, 0.26f, 32, {2, 0, 0, 0});
        m.addCylinder({0, 0.12f, 0}, wr * 0.62f, 0.03f, 24, {6, 0, 0, 0});
        mat4f t = mat4f::translation(float3{sx * (W - 0.1f), wr, sz * wheelbase * 0.5f}) *
                  (sx > 0 ? toX : mat4f::rotation(kPi * 0.5f, float3{0, 0, 1}));
        transformRange(m, f, t);
    }
    // 灯火類
    const float front = L * 0.5f, rear = -L * 0.5f;
    for (float sx : {-1.f, 1.f}) {
        m.addBox({sx * (W * 0.62f), sp.hoodH * 0.72f, front - 0.1f}, {0.2f, 0.045f, 0.12f}, 1, {3, 0, 0, 0});
    }
    m.addBox({0, sp.hoodH * 0.78f, rear + 0.08f}, {W * 0.85f, 0.035f, 0.1f}, 1, {4, 0, 0, 0});
    m.addBox({0, sp.groundClear + 0.02f, 0}, {W * 0.8f, 0.015f, L * 0.36f}, 1, {5, 0, 0, 0}, true);
    return m;
}

}  // namespace bench

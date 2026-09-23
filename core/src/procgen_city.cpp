// 街の手続き生成。区画 → 敷地 → ビルの型 → 配置 → 看板 → 光源 → 道路・高架 → 小物 の順に作る。
#include <algorithm>
#include <cmath>
#include <numeric>

#include "bench/procgen.h"
#include "bench/rng.h"

namespace bench {

using namespace filament::math;

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kSlab = 0.15f;       // 歩道・街区の嵩上げ[m]
constexpr int kRecessFloors = 12;    // LOD0で窓の凹凸を実ジオメトリにする階数（それより上は窓をシェーダで描く）

// カメラパスの区間ごとの移動距離。カメラパス生成（camera_path.cpp）と同じ値を使う
constexpr float kSection1Distance = 160.f;  // 大通り 8 m/s × 20 s
constexpr float kSection2Distance = 280.f;  // 高架 14 m/s × 20 s
constexpr float kTurnDistance = 14.f;       // 下りランプ終端から路地の中心まで

struct StyleParams {
    float floorH;
    float bay;
    float winW;   // 柱間に対する窓幅の比
    float winH;   // 階高に対する窓高の比
    float sill;   // 窓下端[m]
    float depth;  // 窓の奥行[m]
};

StyleParams styleParams(int style) {
    switch (style) {
        case 0: return {3.8f, 2.4f, 0.86f, 0.78f, 0.35f, 0.18f};  // ガラスのタワー
        case 1: return {3.1f, 3.2f, 0.56f, 0.52f, 0.85f, 0.28f};  // 集合住宅
        default: return {4.2f, 4.0f, 0.62f, 0.50f, 1.20f, 0.35f}; // 工業系
    }
}

float4 facadeCustom(int style, float seed, FacadePart part, float bay, float floorH) {
    return {static_cast<float>(style) + seed * 0.999f, static_cast<float>(part), bay, floorH};
}

float4 detailCustom(DetailKind kind, float hue = 0.f, float emissive = 1.f) {
    return {static_cast<float>(kind), hue, emissive, 0.f};
}

float3 absv(float3 v) { return {std::abs(v.x), std::abs(v.y), std::abs(v.z)}; }

// 向きを指定して四角形を足す。頂点順が逆なら裏返して、法線が want 側を向くようにする
void quadFacing(Mesh& m, float3 p0, float3 p1, float3 p2, float3 p3, float2 uv0, float2 uv1, float4 c, float3 want) {
    if (dot(cross(p1 - p0, p3 - p0), want) >= 0.f) {
        m.addQuad(p0, p1, p2, p3, uv0, uv1, c);
    } else {
        m.addQuad(p1, p0, p3, p2, {uv1.x, uv0.y}, {uv0.x, uv1.y}, c);
    }
}

// 回転（Y軸）と平行移動を点・方向に適用
float3 rotY(float3 v, float yaw) {
    float c = std::cos(yaw), s = std::sin(yaw);
    return {c * v.x + s * v.z, v.y, -s * v.x + c * v.z};
}

Aabb transformAabb(const Aabb& b, const Placement& p) {
    Aabb r;
    for (int i = 0; i < 8; ++i) {
        float3 c{(i & 1) ? b.max.x : b.min.x, (i & 2) ? b.max.y : b.min.y, (i & 4) ? b.max.z : b.min.z};
        r.add(rotY(c, p.yaw) + p.position);
    }
    return r;
}

// ---- 外壁 ---------------------------------------------------------------------------------------

struct Wall {
    float3 origin;  // 壁の左下（外から見て）。y は 0（ビルの足元）
    float3 right;   // 水平右方向（単位）
    float3 normal;  // 外向き（単位）
    float width;
    float y0, y1;   // この壁が覆う高さ
};

float3 wallPoint(const Wall& w, float u, float v, float d) {
    return w.origin + w.right * u + float3{0, v, 0} - w.normal * d;
}

void addWallQuad(Mesh& m, const Wall& w, float u0, float u1, float v0, float v1, float4 c) {
    if (u1 - u0 < 1e-4f || v1 - v0 < 1e-4f) return;
    m.addQuad(wallPoint(w, u0, v0, 0), wallPoint(w, u1, v0, 0), wallPoint(w, u1, v1, 0), wallPoint(w, u0, v1, 0),
              {u0, v0}, {u1, v1}, c);
}

// 開口部つきの1区画（壁4片＋見込み4面＋ガラス）
void addCellWithOpening(Mesh& m, const Wall& w, float u0, float u1, float v0, float v1, float ou0, float ou1,
                        float ov0, float ov1, float d, float4 wall, float4 reveal, float4 glass) {
    addWallQuad(m, w, u0, ou0, v0, v1, wall);
    addWallQuad(m, w, ou1, u1, v0, v1, wall);
    addWallQuad(m, w, ou0, ou1, v0, ov0, wall);
    addWallQuad(m, w, ou0, ou1, ov1, v1, wall);
    // 見込み（左・右・下・上）
    m.addQuad(wallPoint(w, ou0, ov0, 0), wallPoint(w, ou0, ov0, d), wallPoint(w, ou0, ov1, d), wallPoint(w, ou0, ov1, 0),
              {ou0, ov0}, {ou0 + d, ov1}, reveal);
    m.addQuad(wallPoint(w, ou1, ov0, d), wallPoint(w, ou1, ov0, 0), wallPoint(w, ou1, ov1, 0), wallPoint(w, ou1, ov1, d),
              {ou1, ov0}, {ou1 + d, ov1}, reveal);
    m.addQuad(wallPoint(w, ou0, ov0, 0), wallPoint(w, ou1, ov0, 0), wallPoint(w, ou1, ov0, d), wallPoint(w, ou0, ov0, d),
              {ou0, ov0}, {ou1, ov0 + d}, reveal);
    m.addQuad(wallPoint(w, ou0, ov1, d), wallPoint(w, ou1, ov1, d), wallPoint(w, ou1, ov1, 0), wallPoint(w, ou0, ov1, 0),
              {ou0, ov1}, {ou1, ov1 + d}, reveal);
    m.addQuad(wallPoint(w, ou0, ov0, d), wallPoint(w, ou1, ov0, d), wallPoint(w, ou1, ov1, d), wallPoint(w, ou0, ov1, d),
              {ou0, ov0}, {ou1, ov1}, glass);
}

// 壁面上の箱（フィン・庇・バルコニー）。u,v は壁座標、out は壁から外への張り出し
void addWallBox(Mesh& m, const Wall& w, float uc, float vc, float out, float halfU, float halfV, float halfOut,
                float4 c) {
    float3 center = w.origin + w.right * uc + float3{0, vc, 0} + w.normal * out;
    float3 half = absv(w.right) * halfU + float3{0, halfV, 0} + absv(w.normal) * halfOut;
    m.addBox(center, half, 1.0f, c, true);
}

void buildWall(std::array<BuildingLod, 3>& lods, const Wall& w, int style, float seed, float stripHue, Rng& rng) {
    const StyleParams sp = styleParams(style);
    const int nb = std::max(1, static_cast<int>(std::lround(w.width / sp.bay)));
    const float bw = w.width / static_cast<float>(nb);
    const float fh = sp.floorH;
    const int f0 = static_cast<int>(std::lround(w.y0 / fh));
    const int f1 = static_cast<int>(std::lround(w.y1 / fh));

    const float4 cWall = facadeCustom(style, seed, FacadePart::Wall, bw, fh);
    const float4 cReveal = facadeCustom(style, seed, FacadePart::Reveal, bw, fh);
    const float4 cGlass = facadeCustom(style, seed, FacadePart::Glass, bw, fh);
    const float4 cShop = facadeCustom(style, seed, FacadePart::Shopfront, bw, fh);
    const float4 cProc = facadeCustom(style, seed, FacadePart::ProceduralWindows, bw, fh);

    // LOD2: 壁1枚
    addWallQuad(lods[2].facade, w, 0, w.width, w.y0, w.y1, cProc);

    // LOD1: 壁1枚（窓はシェーダ）＋ 1階の店舗はジオメトリ
    {
        Mesh& m = lods[1].facade;
        float vStart = w.y0;
        if (f0 == 0) {
            for (int b = 0; b < nb; ++b) {
                float u0 = b * bw, u1 = u0 + bw;
                addCellWithOpening(m, w, u0, u1, 0, fh, u0 + bw * 0.06f, u1 - bw * 0.06f, 0.1f, fh * 0.82f, 0.4f,
                                   cWall, cReveal, cShop);
            }
            vStart = fh;
        }
        addWallQuad(m, w, 0, w.width, vStart, w.y1, cProc);
    }

    // LOD0: 下層は窓の凹凸まで実ジオメトリ
    {
        Mesh& m = lods[0].facade;
        const int recessTop = std::min(f1, kRecessFloors);
        for (int f = f0; f < recessTop; ++f) {
            float v0 = f * fh, v1 = v0 + fh;
            for (int b = 0; b < nb; ++b) {
                float u0 = b * bw, u1 = u0 + bw;
                if (f == 0) {
                    addCellWithOpening(m, w, u0, u1, v0, v1, u0 + bw * 0.06f, u1 - bw * 0.06f, 0.1f, fh * 0.82f, 0.4f,
                                       cWall, cReveal, cShop);
                } else {
                    float ow = bw * sp.winW, oh = fh * sp.winH;
                    float ou0 = u0 + (bw - ow) * 0.5f;
                    float ov0 = v0 + std::min(sp.sill, fh - oh - 0.1f);
                    addCellWithOpening(m, w, u0, u1, v0, v1, ou0, ou0 + ow, ov0, ov0 + oh, sp.depth, cWall, cReveal,
                                       cGlass);
                }
            }
        }
        float procStart = std::max(w.y0, static_cast<float>(recessTop) * fh);
        if (procStart < w.y1 - 1e-3f) addWallQuad(m, w, 0, w.width, procStart, w.y1, cProc);
    }

    // 付帯物（フィン・庇・バルコニー・発光ストリップ）
    const float h = w.y1 - w.y0;
    const float4 metal = detailCustom(DetailKind::Metal);
    const float4 concrete = detailCustom(DetailKind::Concrete);
    const float4 strip = detailCustom(DetailKind::LightStrip, stripHue, 1.0f);
    if (style == 0) {
        for (int b = 0; b <= nb; ++b) {
            float u = b * bw;
            addWallBox(lods[0].detail, w, u, w.y0 + h * 0.5f, 0.16f, 0.07f, h * 0.5f, 0.16f, metal);
            if (b % 2 == 0) addWallBox(lods[1].detail, w, u, w.y0 + h * 0.5f, 0.16f, 0.09f, h * 0.5f, 0.16f, metal);
        }
        // 角の縦の発光ストリップ（遠景のスカイラインにも効くので全LOD）
        for (auto& lod : lods) {
            addWallBox(lod.detail, w, 0.25f, w.y0 + h * 0.5f, 0.05f, 0.06f, h * 0.5f, 0.05f, strip);
        }
    } else if (style == 1) {
        for (int f = std::max(f0, 1); f < f1; ++f) {
            float v = f * fh;
            addWallBox(lods[1].detail, w, w.width * 0.5f, v, 0.12f, w.width * 0.5f + 0.12f, 0.08f, 0.12f, concrete);
            if (f >= kRecessFloors) {
                addWallBox(lods[0].detail, w, w.width * 0.5f, v, 0.12f, w.width * 0.5f + 0.12f, 0.08f, 0.12f, concrete);
                continue;
            }
            for (int b = 0; b < nb; ++b) {
                if ((b + f) % 2 != 0) continue;
                float uc = (b + 0.5f) * bw;
                // 床スラブ＋手すり（3面）
                addWallBox(lods[0].detail, w, uc, v + 0.08f, 0.55f, bw * 0.42f, 0.08f, 0.55f, concrete);
                addWallBox(lods[0].detail, w, uc, v + 0.6f, 1.08f, bw * 0.42f, 0.45f, 0.03f, metal);
                addWallBox(lods[0].detail, w, uc - bw * 0.42f, v + 0.6f, 0.55f, 0.03f, 0.45f, 0.55f, metal);
                addWallBox(lods[0].detail, w, uc + bw * 0.42f, v + 0.6f, 0.55f, 0.03f, 0.45f, 0.55f, metal);
            }
        }
    } else {
        for (int b = 0; b <= nb; b += 2) {
            float u = b * bw;
            addWallBox(lods[0].detail, w, u, w.y0 + h * 0.5f, 0.2f, 0.3f, h * 0.5f, 0.2f, concrete);
            addWallBox(lods[1].detail, w, u, w.y0 + h * 0.5f, 0.2f, 0.3f, h * 0.5f, 0.2f, concrete);
        }
        // 室外機・配管（LOD0のみ）
        for (int f = std::max(f0, 1); f < std::min(f1, kRecessFloors); ++f) {
            for (int b = 0; b < nb; ++b) {
                if (!rng.chance(0.18f)) continue;
                float uc = (b + 0.5f) * bw;
                addWallBox(lods[0].detail, w, uc, f * fh + 0.45f, 0.35f, 0.45f, 0.35f, 0.35f, metal);
            }
        }
    }
    // 1階の庇（全スタイル）
    if (f0 == 0) {
        for (int l = 0; l < 2; ++l) {
            addWallBox(lods[l].detail, w, w.width * 0.5f, fh * 0.9f, 0.9f, w.width * 0.5f, 0.1f, 0.9f, metal);
        }
    }
}

// 段（箱）の4面に外壁を張る
void buildTier(BuildingArchetype& a, const Aabb& box, float seedBase, float stripHue, Rng& rng) {
    const float3 lo = box.min, hi = box.max;
    const float y0 = lo.y, y1 = hi.y;
    Wall walls[4] = {
        {{lo.x, 0, hi.z}, {1, 0, 0}, {0, 0, 1}, hi.x - lo.x, y0, y1},    // +Z
        {{hi.x, 0, lo.z}, {-1, 0, 0}, {0, 0, -1}, hi.x - lo.x, y0, y1},  // -Z
        {{hi.x, 0, hi.z}, {0, 0, -1}, {1, 0, 0}, hi.z - lo.z, y0, y1},   // +X
        {{lo.x, 0, lo.z}, {0, 0, 1}, {-1, 0, 0}, hi.z - lo.z, y0, y1},   // -X
    };
    for (int i = 0; i < 4; ++i) {
        buildWall(a.lods, walls[i], a.style, std::fmod(seedBase + 0.137f * static_cast<float>(i), 1.0f), stripHue, rng);
    }
    // 屋上面とパラペット
    const float4 concrete = detailCustom(DetailKind::Concrete);
    for (int l = 0; l < 3; ++l) {
        Mesh& m = a.lods[l].detail;
        m.addQuad({lo.x, y1, hi.z}, {hi.x, y1, hi.z}, {hi.x, y1, lo.z}, {lo.x, y1, lo.z}, {lo.x, hi.z}, {hi.x, lo.z},
                  concrete);
        if (l < 2) {
            const float t = 0.3f, ph = 1.1f;
            m.addBox({(lo.x + hi.x) * 0.5f, y1 + ph * 0.5f, hi.z - t * 0.5f}, {(hi.x - lo.x) * 0.5f, ph * 0.5f, t * 0.5f}, 1, concrete);
            m.addBox({(lo.x + hi.x) * 0.5f, y1 + ph * 0.5f, lo.z + t * 0.5f}, {(hi.x - lo.x) * 0.5f, ph * 0.5f, t * 0.5f}, 1, concrete);
            m.addBox({hi.x - t * 0.5f, y1 + ph * 0.5f, (lo.z + hi.z) * 0.5f}, {t * 0.5f, ph * 0.5f, (hi.z - lo.z) * 0.5f - t}, 1, concrete);
            m.addBox({lo.x + t * 0.5f, y1 + ph * 0.5f, (lo.z + hi.z) * 0.5f}, {t * 0.5f, ph * 0.5f, (hi.z - lo.z) * 0.5f - t}, 1, concrete);
        }
    }
}

void buildRoof(BuildingArchetype& a, const Aabb& top, float stripHue, Rng& rng) {
    const float y = top.max.y;
    const float3 c = top.center();
    const float3 he = top.halfExtent();
    const float4 metal = detailCustom(DetailKind::Metal);
    const float4 painted = detailCustom(DetailKind::Painted, rng.uniform());
    const float4 beacon = detailCustom(DetailKind::BeaconRed, 0.f, 1.f);
    const float4 strip = detailCustom(DetailKind::LightStrip, stripHue, 1.2f);

    // 室外機群
    int units = rng.irange(3, 8);
    for (int i = 0; i < units; ++i) {
        float3 p{c.x + rng.range(-he.x, he.x) * 0.7f, y + 0.8f, c.z + rng.range(-he.z, he.z) * 0.7f};
        float3 s{rng.range(0.8f, 1.6f), 0.8f, rng.range(0.8f, 1.4f)};
        a.lods[0].detail.addBox(p, s, 1, metal, false);
        if (i < 2) a.lods[1].detail.addBox(p, s, 1, metal, false);
        // ファン（円柱）
        a.lods[0].detail.addCylinder(p + float3{0, s.y, 0}, std::min(s.x, s.z) * 0.7f, 0.12f, 20, metal);
    }
    if (a.style != 0) {
        // 給水塔
        float3 p{c.x + he.x * 0.4f, y, c.z - he.z * 0.35f};
        for (int l = 0; l < 2; ++l) {
            a.lods[l].detail.addCylinder(p + float3{0, 2.5f, 0}, 1.8f, 3.2f, l == 0 ? 28 : 12, painted);
            for (int k = 0; k < 4; ++k) {
                float ang = kPi * 0.5f * k + 0.785f;
                a.lods[l].detail.addBox(p + float3{std::cos(ang) * 1.3f, 1.25f, std::sin(ang) * 1.3f}, {0.1f, 1.25f, 0.1f}, 1, metal);
            }
        }
    }
    if (a.height > 60.f) {
        // アンテナと航空障害灯
        float mast = rng.range(8.f, 22.f);
        for (int l = 0; l < 3; ++l) {
            a.lods[l].detail.addCylinder({c.x, y, c.z}, 0.25f, mast, l == 0 ? 16 : 6, metal);
            a.lods[l].detail.addBox({c.x, y + mast + 0.3f, c.z}, float3{0.35f}, 1, beacon);
        }
    }
    if (a.style == 0) {
        // 屋上の発光縁取り
        for (int l = 0; l < 3; ++l) {
            Mesh& m = a.lods[l].detail;
            m.addBox({c.x, y + 1.15f, top.max.z}, {he.x, 0.06f, 0.06f}, 1, strip);
            m.addBox({c.x, y + 1.15f, top.min.z}, {he.x, 0.06f, 0.06f}, 1, strip);
            m.addBox({top.max.x, y + 1.15f, c.z}, {0.06f, 0.06f, he.z}, 1, strip);
            m.addBox({top.min.x, y + 1.15f, c.z}, {0.06f, 0.06f, he.z}, 1, strip);
        }
    }
}

BuildingArchetype makeArchetype(int style, float2 footprint, float height, Rng rng) {
    BuildingArchetype a;
    a.style = style;
    a.footprint = footprint;
    const float fh = styleParams(style).floorH;
    const int floors = std::max(3, static_cast<int>(std::lround(height / fh)));
    a.height = floors * fh;
    const float stripHue = rng.uniform();

    const float hx = footprint.x * 0.5f, hz = footprint.y * 0.5f;
    std::vector<Aabb> tiers;
    auto tier = [&](float inset, float y0, float y1) {
        Aabb b;
        b.min = {-hx + inset, y0, -hz + inset};
        b.max = {hx - inset, y1, hz - inset};
        tiers.push_back(b);
    };
    if (a.height < 30.f || std::min(footprint.x, footprint.y) < 14.f) {
        tier(0, 0, a.height);
    } else {
        int podium = rng.irange(3, 4);
        if (a.height > 80.f && std::min(footprint.x, footprint.y) > 24.f) {
            int mid = static_cast<int>(std::lround(floors * rng.range(0.55f, 0.75f)));
            tier(0, 0, podium * fh);
            float in1 = rng.range(1.5f, 3.5f);
            tier(in1, podium * fh, mid * fh);
            tier(in1 + rng.range(2.0f, 4.0f), mid * fh, a.height);
        } else {
            tier(0, 0, podium * fh);
            tier(rng.range(1.5f, 3.0f), podium * fh, a.height);
        }
    }
    float seed = rng.uniform();
    for (const auto& t : tiers) buildTier(a, t, seed, stripHue, rng);
    buildRoof(a, tiers.back(), stripHue, rng);
    a.tiers = tiers;
    for (const auto& lod : a.lods) {
        a.bounds.add(lod.facade.bounds());
        a.bounds.add(lod.detail.bounds());
    }
    return a;
}

// ---- 看板 ---------------------------------------------------------------------------------------

std::array<SignShapeMesh, 4> makeSignShapes() {
    std::array<SignShapeMesh, 4> s;
    const float4 metal = detailCustom(DetailKind::Metal);
    // 突き出し看板：壁から +Z へ張り出す縦長の板。表裏2面（±X）が発光面
    {
        auto& m = s[0];
        m.size = {1.2f, 5.0f};
        float4 c{m.size.x / m.size.y, 0, 0, 0};
        float z0 = 0.35f, z1 = 1.55f, y0 = -2.5f, y1 = 2.5f, x = 0.15f;
        m.face.addQuad({x, y0, z1}, {x, y0, z0}, {x, y1, z0}, {x, y1, z1}, {0, 0}, {1, 1}, c);
        m.face.addQuad({-x, y0, z0}, {-x, y0, z1}, {-x, y1, z1}, {-x, y1, z0}, {0, 0}, {1, 1}, c);
        m.frame.addBox({0, 0, (z0 + z1) * 0.5f}, {x * 0.9f, 2.55f, 0.62f}, 1, metal, true);
        m.frame.addBox({0, 1.8f, z0 * 0.5f}, {0.05f, 0.05f, z0 * 0.5f}, 1, metal);
        m.frame.addBox({0, -1.8f, z0 * 0.5f}, {0.05f, 0.05f, z0 * 0.5f}, 1, metal);
    }
    // 壁面ビルボード
    {
        auto& m = s[1];
        m.size = {6.0f, 3.0f};
        float4 c{m.size.x / m.size.y, 0, 0, 0};
        m.face.addQuad({-3, -1.5f, 0.3f}, {3, -1.5f, 0.3f}, {3, 1.5f, 0.3f}, {-3, 1.5f, 0.3f}, {0, 0}, {1, 1}, c);
        m.frame.addBox({0, 0, 0.14f}, {3.15f, 1.65f, 0.14f}, 1, metal);
    }
    // 屋上看板（両面、トラス脚付き）
    {
        auto& m = s[2];
        m.size = {10.0f, 4.0f};
        float4 c{m.size.x / m.size.y, 0, 0, 0};
        float y0 = 1.2f, y1 = 5.2f;
        m.face.addQuad({-5, y0, 0.12f}, {5, y0, 0.12f}, {5, y1, 0.12f}, {-5, y1, 0.12f}, {0, 0}, {1, 1}, c);
        m.face.addQuad({5, y0, -0.12f}, {-5, y0, -0.12f}, {-5, y1, -0.12f}, {5, y1, -0.12f}, {0, 0}, {1, 1}, c);
        m.frame.addBox({0, (y0 + y1) * 0.5f, 0}, {5.1f, 2.1f, 0.1f}, 1, metal);
        for (int i = -2; i <= 2; ++i) {
            m.frame.addBox({i * 2.3f, y0 * 0.5f, 0}, {0.08f, y0 * 0.5f, 0.08f}, 1, metal);
            m.frame.addBox({i * 2.3f, y0 * 0.5f, -0.9f}, {0.08f, y0 * 0.5f, 0.08f}, 1, metal);
        }
    }
    // 店舗の袖看板（小さな箱、正面が発光）
    {
        auto& m = s[3];
        m.size = {3.0f, 1.0f};
        float4 c{m.size.x / m.size.y, 0, 0, 0};
        m.face.addQuad({-1.5f, -0.5f, 0.42f}, {1.5f, -0.5f, 0.42f}, {1.5f, 0.5f, 0.42f}, {-1.5f, 0.5f, 0.42f}, {0, 0}, {1, 1}, c);
        m.frame.addBox({0, 0, 0.2f}, {1.55f, 0.55f, 0.2f}, 1, metal);
    }
    return s;
}

// 看板の色（リニア）。マゼンタとシアンを多めにする
float3 neonColor(Rng& rng) {
    static const float3 kPalette[] = {
        {1.00f, 0.05f, 0.55f},  // マゼンタ
        {0.00f, 0.75f, 1.00f},  // シアン
        {1.00f, 0.08f, 0.25f},  // ホットピンク
        {0.00f, 0.85f, 0.95f},  // シアン2
        {0.95f, 0.70f, 0.05f},  // イエロー
        {0.55f, 0.12f, 1.00f},  // バイオレット
        {0.35f, 1.00f, 0.12f},  // ライム
        {1.00f, 0.30f, 0.02f},  // オレンジ
        {0.65f, 0.80f, 1.00f},  // 白青
        {1.00f, 0.03f, 0.03f},  // 赤
    };
    static const float kWeight[] = {3, 3, 2, 1.5f, 1.2f, 1.5f, 0.8f, 1, 0.8f, 1};
    float total = 0;
    for (float w : kWeight) total += w;
    float r = rng.uniform() * total;
    for (int i = 0; i < 10; ++i) {
        if ((r -= kWeight[i]) <= 0) return kPalette[i];
    }
    return kPalette[0];
}

}  // namespace

// ---- 公開関数 -----------------------------------------------------------------------------------

float distanceToPolylineXZ(const std::vector<float3>& poly, float3 p) {
    if (poly.empty()) return 0.f;
    float best = 1e30f;
    float2 q{p.x, p.z};
    for (size_t i = 0; i + 1 < poly.size(); ++i) {
        float2 a{poly[i].x, poly[i].z}, b{poly[i + 1].x, poly[i + 1].z};
        float2 ab = b - a;
        float len2 = dot(ab, ab);
        float t = len2 > 0 ? std::clamp(dot(q - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
        best = std::min(best, length(q - (a + ab * t)));
    }
    if (poly.size() == 1) best = length(q - float2{poly[0].x, poly[0].z});
    return best;
}

float CityData::overpassHeightAt(float z) const {
    auto smooth = [](float s) {
        s = std::clamp(s, 0.0f, 1.0f);
        return s * s * (3.0f - 2.0f * s);
    };
    if (z < rampStartZ || z > downRampEndZ) return 0.f;
    if (z < rampStartZ + rampLength) return overpassY * smooth((z - rampStartZ) / rampLength);
    if (z <= overpassEndZ) return overpassY;
    return overpassY * (1.0f - smooth((z - overpassEndZ) / rampLength));
}

size_t CityData::archetypeTriangles(int lod) const {
    size_t n = 0;
    for (const auto& a : archetypes) n += a.lods[lod].triangles();
    return n;
}

uint64_t CityData::hash() const {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (const auto& a : archetypes) {
        for (const auto& l : a.lods) {
            h = hashMesh(l.facade, h);
            h = hashMesh(l.detail, h);
        }
    }
    for (const auto& b : buildings) h = fnv1a(&b.place, sizeof(b.place), fnv1a(&b.archetype, sizeof(int), h));
    for (const auto& s : signs) {
        h = fnv1a(&s.place, sizeof(s.place), h);
        h = fnv1a(&s.color, sizeof(s.color), h);
        h = fnv1a(&s.pattern, sizeof(int), h);
    }
    for (const auto& l : lights) h = fnv1a(&l.position, sizeof(float3), h);
    for (const auto& t : groundTiles) h = hashMesh(t.mesh, h);
    for (const auto& t : overpassTiles) h = hashMesh(t.mesh, h);
    for (const auto& t : overpassDetailTiles) h = hashMesh(t.mesh, h);
    for (const auto& p : props) h = fnv1a(&p.place, sizeof(p.place), h);
    return h;
}

CityData generateCity(const SceneConfig& cfg) {
    CityData city;
    Rng root(cfg.seed);
    const int nx = std::max(2, cfg.city.blocks[0]);
    const int nz = std::max(4, cfg.city.blocks[1]);
    const float bsx = cfg.city.blockSize[0], bsz = cfg.city.blockSize[1];

    // ---- 通りの幅と位置 --------------------------------------------------------------------------
    const int ib = nx / 2;  // 大通りの位置（縦の通りの番号）
    std::vector<float> wv(nx + 1), sv(nx + 1), cx(nx + 1);
    for (int i = 0; i <= nx; ++i) {
        wv[i] = (i == ib) ? cfg.city.streetWidth * 1.35f : cfg.city.streetWidth * 0.8f;
        sv[i] = (i == ib) ? 4.0f : 3.0f;  // 歩道幅
    }
    float x = 0;
    std::vector<float> bx0(nx);
    for (int i = 0; i <= nx; ++i) {
        cx[i] = x + wv[i] * 0.5f;
        x += wv[i];
        if (i < nx) {
            bx0[i] = x;
            x += bsx;
        }
    }
    const float totalX = x;

    // 横の通り。路地の行は、出発点から 大通り160m + 高架(ランプ含む) + 曲がり の先に来るように選ぶ
    std::vector<float> wh(nz + 1, cfg.city.streetWidth * 0.75f), sh(nz + 1, 3.0f);
    const float pitch = bsz + cfg.city.streetWidth * 0.75f;
    const float rampLength = 60.f;
    const float pathToAlley = kSection1Distance + kSection2Distance + rampLength + kTurnDistance;
    int ja = std::clamp(static_cast<int>(std::ceil((pathToAlley + 1.5f * pitch) / pitch)), 2, nz - 1);
    wh[ja] = cfg.city.alleyWidth;
    sh[ja] = 0.0f;
    std::vector<float> cz(nz + 1), bz0(nz);
    float z = 0;
    for (int j = 0; j <= nz; ++j) {
        cz[j] = z + wh[j] * 0.5f;
        z += wh[j];
        if (j < nz) {
            bz0[j] = z;
            z += bsz;
        }
    }
    const float totalZ = z;

    city.boulevardX = cx[ib];
    city.boulevardHalfWidth = wv[ib] * 0.5f;
    city.overpassY = cfg.city.overpassHeight;
    city.rampLength = rampLength;
    city.alleyZ = cz[ja];
    city.startZ = city.alleyZ - pathToAlley;
    city.rampStartZ = city.startZ + kSection1Distance;
    city.overpassEndZ = city.rampStartZ + kSection2Distance;
    city.downRampEndZ = city.overpassEndZ + rampLength;
    city.alleyX0 = city.boulevardX + city.boulevardHalfWidth;
    city.alleyX1 = cx[std::min(ib + 1, nx)];
    const float bxv = city.boulevardX;

    city.pathPoints = {
        {bxv - 2.5f, 2.2f, city.startZ},
        {bxv - 2.5f, 2.2f, city.rampStartZ - 25.f},
        {bxv, city.overpassY * 0.5f + 2.2f, city.rampStartZ + rampLength * 0.5f},
        {bxv, city.overpassY + 2.2f, city.rampStartZ + rampLength},
        {bxv, city.overpassY + 2.2f, city.overpassEndZ},
        {bxv, 2.2f, city.downRampEndZ},
        {bxv + 3.f, 1.9f, city.alleyZ - 2.f},
        {city.alleyX0 + 6.f, 1.8f, city.alleyZ},
        {city.alleyX1 - 4.f, 1.8f, city.alleyZ},
    };

    // ---- 敷地 ----------------------------------------------------------------------------------
    struct Lot {
        float x0, x1, z0, z1;
        int cls;  // 0 = 街区まるごと, 1 = 半分
        float dist;
        bool facesAlley;
    };
    std::vector<Lot> lots;
    for (int c = 0; c < nx; ++c) {
        for (int r = 0; r < nz; ++r) {
            float x0 = bx0[c], x1 = x0 + bsx, z0 = bz0[r], z1 = z0 + bsz;
            bool nearBoulevard = (c == ib - 1 || c == ib);
            bool alley = (r == ja - 1 || r == ja);
            if (nearBoulevard) {
                float zm = (z0 + z1) * 0.5f;
                lots.push_back({x0, x1, z0, zm - 0.5f, 1, 0, alley && r == ja});
                lots.push_back({x0, x1, zm + 0.5f, z1, 1, 0, alley && r == ja - 1});
            } else {
                lots.push_back({x0, x1, z0, z1, 0, 0, alley});
            }
        }
    }
    for (auto& l : lots) {
        l.dist = distanceToPolylineXZ(city.pathPoints, {(l.x0 + l.x1) * 0.5f, 0, (l.z0 + l.z1) * 0.5f});
    }
    // 遠い敷地から空き地（駐車場）にしてビル数を合わせる
    std::vector<int> order(lots.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return lots[a].dist < lots[b].dist; });
    const int nBuildings = std::min<int>(cfg.city.buildings, static_cast<int>(lots.size()));
    std::vector<int> used(order.begin(), order.begin() + nBuildings);
    std::sort(used.begin(), used.end());

    // ---- ビルの型 ------------------------------------------------------------------------------
    const float margin = 1.0f;
    const float2 fpFull{bsx - 2 * margin, bsz - 2 * margin};
    const float2 fpHalf{bsx - 2 * margin, bsz * 0.5f - 0.5f - 2 * margin};
    const int kPerClass = 10;
    {
        Rng arng = root.fork(1);
        for (int cls = 0; cls < 2; ++cls) {
            for (int k = 0; k < kPerClass; ++k) {
                int style = k % 3;
                float u = arng.uniform();
                float hlo = cfg.city.heightRange[0], hhi = cfg.city.heightRange[1];
                float h = hlo + (hhi - hlo) * std::pow(u, 1.6f);
                if (style == 2) h = std::min(h, hlo + (hhi - hlo) * 0.35f);  // 工業系は低め
                city.archetypes.push_back(makeArchetype(style, cls == 0 ? fpFull : fpHalf, h, arng.fork(100 + k + cls * 50)));
            }
        }
    }

    // ---- 配置 ----------------------------------------------------------------------------------
    {
        Rng prng = root.fork(2);
        for (int li : used) {
            const Lot& l = lots[li];
            BuildingInstance b;
            int k;
            if (l.facesAlley) {
                // 路地沿いは中低層（集合住宅・工業系）で囲む
                do { k = prng.irange(0, kPerClass - 1); } while (k % 3 == 0 || city.archetypes[l.cls * kPerClass + k].height > 70.f);
            } else {
                k = prng.irange(0, kPerClass - 1);
            }
            b.archetype = l.cls * kPerClass + k;
            b.place.position = {(l.x0 + l.x1) * 0.5f, kSlab, (l.z0 + l.z1) * 0.5f};
            b.place.yaw = prng.chance(0.5f) ? 0.0f : kPi;
            b.bounds = transformAabb(city.archetypes[b.archetype].bounds, b.place);
            b.pathDistance = l.dist;
            city.buildings.push_back(b);
        }
    }

    // ---- 看板 ----------------------------------------------------------------------------------
    city.signShapes = makeSignShapes();
    {
        Rng srng = root.fork(3);
        for (size_t bi = 0; bi < city.buildings.size(); ++bi) {
            const auto& b = city.buildings[bi];
            const auto& a = city.archetypes[b.archetype];
            Rng r = srng.fork(static_cast<uint32_t>(bi));
            int want = b.pathDistance < 40.f ? r.irange(7, 11) : b.pathDistance < 110.f ? r.irange(3, 5) : r.irange(0, 1);
            struct Rect { int wall; float u0, u1, v0, v1; };
            std::vector<Rect> taken;
            auto overlaps = [&](const Rect& q) {
                for (const auto& t : taken) {
                    if (t.wall == q.wall && q.u0 < t.u1 + 0.6f && t.u0 < q.u1 + 0.6f && q.v0 < t.v1 + 0.6f && t.v0 < q.v1 + 0.6f) return true;
                }
                return false;
            };
            for (int n = 0, tries = 0; n < want && tries < want * 12; ++tries) {
                // 形状：近いビルは突き出しと袖看板、遠いビルは屋上看板とビルボード
                float roll = r.uniform();
                SignShape shape = roll < 0.38f ? SignShape::Blade : roll < 0.62f ? SignShape::ShopBox
                                : roll < 0.88f ? SignShape::Billboard : SignShape::Rooftop;
                if (b.pathDistance > 110.f) shape = r.chance(0.6f) ? SignShape::Rooftop : SignShape::Billboard;
                const auto& shapeMesh = city.signShapes[static_cast<int>(shape)];

                int tierIdx = (shape == SignShape::Rooftop) ? static_cast<int>(a.tiers.size()) - 1
                            : (shape == SignShape::Billboard) ? r.irange(0, static_cast<int>(a.tiers.size()) - 1) : 0;
                const Aabb& t = a.tiers[tierIdx];
                int wall = r.irange(0, 3);
                // 壁（ローカル）: 0 +Z, 1 -Z, 2 +X, 3 -X
                float3 nLocal = wall == 0 ? float3{0, 0, 1} : wall == 1 ? float3{0, 0, -1} : wall == 2 ? float3{1, 0, 0} : float3{-1, 0, 0};
                float3 rLocal{nLocal.z, 0, -nLocal.x};  // 壁の右方向（外から見て）
                float wallW = (wall < 2) ? t.max.x - t.min.x : t.max.z - t.min.z;
                float3 wallCenter{(wall == 2) ? t.max.x : (wall == 3) ? t.min.x : 0.f, 0,
                                  (wall == 0) ? t.max.z : (wall == 1) ? t.min.z : 0.f};
                float halfW = shapeMesh.size.x * 0.5f + (shape == SignShape::Blade ? 0.2f : 0.f);
                if (shape == SignShape::Blade) halfW = 0.4f;
                if (wallW < 2 * halfW + 1.f) continue;
                float u = r.range(-wallW * 0.5f + halfW + 0.5f, wallW * 0.5f - halfW - 0.5f);
                float v;
                const float fh = styleParams(a.style).floorH;
                if (shape == SignShape::ShopBox) {
                    v = fh + 0.75f;  // 1階の庇の上
                } else if (shape == SignShape::Rooftop) {
                    v = t.max.y;
                    u = 0;
                } else {
                    float half = shapeMesh.size.y * 0.5f;
                    float lo = std::max(t.min.y + fh + half + 0.8f, half + fh * 1.2f);
                    float hi = t.max.y - half - 1.0f;
                    if (shape == SignShape::Blade) hi = std::min(hi, 26.f);
                    if (hi <= lo) continue;
                    v = r.range(lo, hi);
                }
                Rect rect{wall, u - halfW, u + halfW, v - shapeMesh.size.y * 0.5f, v + shapeMesh.size.y * 0.5f};
                if (shape != SignShape::Rooftop && overlaps(rect)) continue;
                if (shape == SignShape::Rooftop) {
                    bool dup = false;
                    for (const auto& q : taken) dup |= q.wall == -1;
                    if (dup) continue;
                    rect.wall = -1;
                }
                taken.push_back(rect);

                // ローカル配置 → ワールド
                float wallOffset = (shape == SignShape::Rooftop) ? -1.5f : 0.45f;
                float3 pLocal = wallCenter + rLocal * u + float3{0, v, 0} + nLocal * wallOffset;
                float yawLocal = std::atan2(nLocal.x, nLocal.z);  // 看板の +Z を壁の外向きへ
                SignInstance s;
                s.shape = shape;
                s.building = static_cast<int>(bi);
                s.place.position = rotY(pLocal, b.place.yaw) + b.place.position;
                s.place.yaw = yawLocal + b.place.yaw;
                s.facing = rotY(nLocal, b.place.yaw);
                s.color = neonColor(r);
                s.pattern = r.irange(0, 7);
                s.seed = r.uniform();
                Aabb lb = shapeMesh.face.bounds();
                lb.add(shapeMesh.frame.bounds());
                s.bounds = transformAabb(lb, s.place);
                float3 center = s.bounds.center();
                s.lightPos = (shape == SignShape::Blade) ? center + s.facing * 0.2f + float3{0, -1.0f, 0}
                           : (shape == SignShape::Rooftop) ? center + s.facing * 2.0f
                           : center + s.facing * 1.1f;
                s.pathDistance = distanceToPolylineXZ(city.pathPoints, center);
                city.signs.push_back(s);
                ++n;
            }
        }
    }

    // ---- 光源 ----------------------------------------------------------------------------------
    {
        std::vector<int> shops, others;
        for (size_t i = 0; i < city.signs.size(); ++i) {
            (city.signs[i].shape == SignShape::ShopBox ? shops : others).push_back(static_cast<int>(i));
        }
        auto byDist = [&](int a, int b) {
            if (city.signs[a].pathDistance != city.signs[b].pathDistance) return city.signs[a].pathDistance < city.signs[b].pathDistance;
            return a < b;
        };
        std::sort(shops.begin(), shops.end(), byDist);
        std::sort(others.begin(), others.end(), byDist);
        std::vector<bool> lit(city.signs.size(), false);
        auto take = [&](std::vector<int>& primary, std::vector<int>& secondary) -> int {
            for (auto* pool : {&primary, &secondary}) {
                for (int i : *pool) {
                    if (!lit[i]) {
                        lit[i] = true;
                        return i;
                    }
                }
            }
            return -1;
        };
        for (int k = 0; k < cfg.lights.neonSpot; ++k) {
            int i = take(shops, others);
            if (i < 0) break;
            const auto& s = city.signs[i];
            LightDef l;
            l.kind = LightKind::NeonSpot;
            l.position = s.bounds.center() + s.facing * 0.6f + float3{0, -0.4f, 0};
            l.direction = normalize(s.facing * 0.55f + float3{0, -1, 0});
            l.color = s.color;
            l.intensity = cfg.lights.neonIntensity * 1.4f;
            l.falloff = cfg.lights.falloff * 0.8f;
            l.innerCone = 0.35f;
            l.outerCone = 0.85f;
            l.sign = i;
            l.pathDistance = s.pathDistance;
            city.lights.push_back(l);
        }
        for (int k = 0; k < cfg.lights.neonPoint; ++k) {
            int i = take(others, shops);
            if (i < 0) break;
            const auto& s = city.signs[i];
            LightDef l;
            l.kind = LightKind::NeonPoint;
            l.position = s.lightPos;
            l.color = s.color;
            l.intensity = cfg.lights.neonIntensity * (s.shape == SignShape::Rooftop ? 3.0f : 1.0f);
            l.falloff = cfg.lights.falloff * (s.shape == SignShape::Rooftop ? 1.8f : 1.0f);
            l.sign = i;
            l.pathDistance = s.pathDistance;
            city.lights.push_back(l);
        }
    }

    // ---- 小物の形 -------------------------------------------------------------------------------
    {
        const float4 metal = detailCustom(DetailKind::Metal);
        const float4 painted = detailCustom(DetailKind::Painted, 0.36f);
        const float4 concrete = detailCustom(DetailKind::Concrete);
        const float4 lampGlow = detailCustom(DetailKind::LightStrip, 0.58f, 2.5f);
        auto& lamp = city.propShapes[static_cast<int>(PropShape::StreetLamp)];
        lamp.addCylinder({0, 0, 0}, 0.13f, 8.0f, 16, metal);
        lamp.addBox({0, 7.95f, 1.2f}, {0.08f, 0.08f, 1.25f}, 1, metal);
        lamp.addBox({0, 7.85f, 2.35f}, {0.25f, 0.12f, 0.45f}, 1, metal, true);
        lamp.addBox({0, 7.72f, 2.35f}, {0.2f, 0.02f, 0.4f}, 1, lampGlow, true);
        auto& dump = city.propShapes[static_cast<int>(PropShape::Dumpster)];
        dump.addBox({0, 0.65f, 0}, {0.9f, 0.6f, 0.55f}, 1, painted, true);
        dump.addBox({0, 1.3f, -0.05f}, {0.95f, 0.05f, 0.62f}, 1, metal);
        for (int i = -1; i <= 1; i += 2) dump.addCylinder({i * 0.7f, 0, 0.4f}, 0.06f, 0.08f, 10, metal);
        auto& ac = city.propShapes[static_cast<int>(PropShape::AcUnit)];
        ac.addBox({0, 0, 0.35f}, {0.45f, 0.35f, 0.35f}, 1, metal, true);
        ac.addCylinder({0, 0.35f, 0.35f}, 0.25f, 0.05f, 20, metal);
        auto& pipe = city.propShapes[static_cast<int>(PropShape::Pipe)];
        pipe.addCylinder({0, 0, 0.2f}, 0.12f, 12.0f, 14, metal, false);
        for (int i = 0; i < 6; ++i) pipe.addBox({0, 0.5f + i * 2.2f, 0.1f}, {0.18f, 0.06f, 0.12f}, 1, metal);
        auto& barrier = city.propShapes[static_cast<int>(PropShape::Barrier)];
        barrier.addBox({0, 0.3f, 0}, {0.3f, 0.3f, 2.0f}, 1, concrete);
        barrier.addBox({0, 0.75f, 0}, {0.15f, 0.15f, 2.0f}, 1, concrete);
        barrier.addBox({0, 0.95f, 0}, {0.03f, 0.05f, 1.95f}, 1, detailCustom(DetailKind::LightStrip, 0.92f, 0.8f));
        auto& vent = city.propShapes[static_cast<int>(PropShape::Vent)];
        vent.addCylinder({0, 0, 0}, 0.45f, 0.04f, 24, metal);
    }

    auto addProp = [&](PropShape shape, float3 pos, float yaw) {
        PropInstance p;
        p.shape = shape;
        p.place = {pos, yaw};
        p.bounds = transformAabb(city.propShapes[static_cast<int>(shape)].bounds(), p.place);
        city.props.push_back(p);
    };

    // ---- 街灯 ----------------------------------------------------------------------------------
    {
        const int total = cfg.lights.street;
        const int nOver = total / 4, nAlley = std::max(0, total * 15 / 100);
        const int nBoul = std::max(0, total - nOver - nAlley);
        const float edge = city.boulevardHalfWidth - 0.8f;
        const float3 cool{0.72f, 0.84f, 1.0f}, sodium{1.0f, 0.52f, 0.18f};
        const float span0 = city.startZ - 15.f, span1 = city.rampStartZ + 95.f;
        for (int k = 0; k < nBoul; ++k) {
            float zz = span0 + (span1 - span0) * (k + 0.5f) / static_cast<float>(nBoul);
            float side = (k % 2 == 0) ? -1.f : 1.f;
            float3 base{bxv + side * edge, kSlab, zz};
            float yaw = side < 0 ? kPi * 0.5f : -kPi * 0.5f;  // 腕を車道側へ
            addProp(PropShape::StreetLamp, base, yaw);
            LightDef l;
            l.kind = LightKind::Street;
            l.position = base + rotY(float3{0, 7.6f, 2.35f}, yaw);
            l.direction = {0, -1, 0};
            l.color = cool;
            l.intensity = cfg.lights.streetIntensity;
            l.falloff = 20.f;
            l.innerCone = 0.55f;
            l.outerCone = 1.05f;
            l.pathDistance = distanceToPolylineXZ(city.pathPoints, l.position);
            city.lights.push_back(l);
        }
        for (int k = 0; k < nOver; ++k) {
            float zz = city.rampStartZ + rampLength + (city.overpassEndZ - city.rampStartZ - rampLength) * (k + 0.5f) / static_cast<float>(nOver);
            float side = (k % 2 == 0) ? -1.f : 1.f;
            float3 base{bxv + side * (cfg.city.overpassWidth * 0.5f - 0.4f), city.overpassHeightAt(zz) + 1.1f, zz};
            float yaw = side < 0 ? kPi * 0.5f : -kPi * 0.5f;
            addProp(PropShape::StreetLamp, base, yaw);
            LightDef l;
            l.kind = LightKind::Street;
            l.position = base + rotY(float3{0, 7.6f, 2.35f}, yaw);
            l.color = cool;
            l.intensity = cfg.lights.streetIntensity * 0.9f;
            l.falloff = 20.f;
            l.innerCone = 0.55f;
            l.outerCone = 1.05f;
            l.pathDistance = distanceToPolylineXZ(city.pathPoints, l.position);
            city.lights.push_back(l);
        }
        for (int k = 0; k < nAlley; ++k) {
            float xx = city.alleyX0 + 3.f + (city.alleyX1 - city.alleyX0 - 6.f) * (k + 0.5f) / static_cast<float>(std::max(1, nAlley));
            float side = (k % 2 == 0) ? -1.f : 1.f;
            LightDef l;
            l.kind = LightKind::Street;
            l.position = {xx, 4.2f, city.alleyZ + side * (cfg.city.alleyWidth * 0.5f - 0.6f)};
            l.direction = normalize(float3{0, -1, -side * 0.35f});
            l.color = sodium;
            l.intensity = cfg.lights.streetIntensity * 0.35f;
            l.falloff = 11.f;
            l.innerCone = 0.6f;
            l.outerCone = 1.15f;
            l.pathDistance = distanceToPolylineXZ(city.pathPoints, l.position);
            city.lights.push_back(l);
            addProp(PropShape::AcUnit, float3{xx + 1.2f, 4.6f, city.alleyZ + side * cfg.city.alleyWidth * 0.5f}, side < 0 ? 0.f : kPi);
        }
    }

    // ---- 路地の小物・中央分離帯・蒸気口 ------------------------------------------------------------
    {
        Rng r = root.fork(4);
        const float half = cfg.city.alleyWidth * 0.5f;
        for (float xx = city.alleyX0 + 4.f; xx < city.alleyX1 - 3.f; xx += r.range(5.f, 9.f)) {
            float side = r.chance(0.5f) ? -1.f : 1.f;
            float zz = city.alleyZ + side * (half - 0.7f);
            int kind = r.irange(0, 2);
            if (kind == 0) addProp(PropShape::Dumpster, {xx, 0, zz}, side < 0 ? 0.f : kPi);
            if (kind == 1) addProp(PropShape::Pipe, {xx, 0, city.alleyZ + side * half}, side < 0 ? 0.f : kPi);
            if (kind == 2) addProp(PropShape::AcUnit, {xx, r.range(2.8f, 7.f), city.alleyZ + side * half}, side < 0 ? 0.f : kPi);
            if (r.chance(0.6f)) {
                float3 vp{xx + r.range(-1.f, 1.f), 0.01f, city.alleyZ + r.range(-1.2f, 1.2f)};
                addProp(PropShape::Vent, vp, 0);
                city.vents.push_back({vp, r.range(0.8f, 1.4f)});
            }
        }
        for (float zz = city.startZ - 40.f; zz < city.rampStartZ - 4.f; zz += 4.2f) {
            addProp(PropShape::Barrier, {bxv, 0, zz}, 0);
        }
        for (int k = 0; k < 6; ++k) {
            float zz = city.startZ + 12.f + k * 26.f;
            float3 vp{bxv + ((k % 2) ? 7.0f : -7.0f), 0.01f, zz};
            addProp(PropShape::Vent, vp, 0);
            city.vents.push_back({vp, 1.0f});
        }
    }

    // ---- 地面（行ごとのタイル） --------------------------------------------------------------------
    {
        auto groundC = [](GroundKind k, float laneW, float lanes, float wet) {
            return float4{static_cast<float>(k), laneW, lanes, wet};
        };
        for (int j = 0; j <= nz; ++j) {
            Tile tile;
            Mesh& m = tile.mesh;
            // 横の通り（交差点で区切る）
            const float hz0 = cz[j] - wh[j] * 0.5f + sh[j], hz1 = cz[j] + wh[j] * 0.5f - sh[j];
            const bool alley = (j == ja);
            for (int i = 0; i <= nx; ++i) {
                float ix0 = cx[i] - wv[i] * 0.5f + sv[i], ix1 = cx[i] + wv[i] * 0.5f - sv[i];
                // 交差点
                m.addQuad({ix0, 0, hz1}, {ix1, 0, hz1}, {ix1, 0, hz0}, {ix0, 0, hz0}, {ix0 - cx[i], hz1 - cz[j]},
                          {ix1 - cx[i], hz0 - cz[j]},
                          groundC(alley ? GroundKind::Alley : GroundKind::Intersection, (ix1 - ix0) * 0.5f, (hz1 - hz0) * 0.5f, alley ? 1.0f : 0.85f));
                if (i < nx) {
                    float sx0 = ix1, sx1 = cx[i + 1] - wv[i + 1] * 0.5f + sv[i + 1];
                    // 横の車道（uv = (通りの中心からの距離, 通りに沿った距離)）
                    m.addQuad({sx0, 0, hz0}, {sx0, 0, hz1}, {sx1, 0, hz1}, {sx1, 0, hz0}, {hz0 - cz[j], sx0}, {hz1 - cz[j], sx1},
                              groundC(alley ? GroundKind::Alley : GroundKind::Road, 3.4f, 1, alley ? 1.0f : 0.8f));
                }
            }
            if (j < nz) {
                // 街区（歩道の高さの台）と縦の車道
                const float z0 = bz0[j] - sh[j], z1 = bz0[j] + bsz + sh[j + 1];
                for (int i = 0; i <= nx; ++i) {
                    float rx0 = cx[i] - wv[i] * 0.5f + sv[i], rx1 = cx[i] + wv[i] * 0.5f - sv[i];
                    bool boul = (i == ib);
                    m.addQuad({rx0, 0, z1}, {rx1, 0, z1}, {rx1, 0, z0}, {rx0, 0, z0}, {rx0 - cx[i], z1}, {rx1 - cx[i], z0},
                              groundC(GroundKind::Road, 3.4f, boul ? 2.f : 1.f, 0.8f));
                    if (i < nx) {
                        float x0s = bx0[i] - sv[i], x1s = bx0[i] + bsx + sv[i + 1];
                        m.addQuad({x0s, kSlab, z1}, {x1s, kSlab, z1}, {x1s, kSlab, z0}, {x0s, kSlab, z0}, {x0s, z1}, {x1s, z0},
                                  groundC(GroundKind::Sidewalk, 0, 0, 0.5f));
                        // 縁石（4面）
                        float4 curb = groundC(GroundKind::Curb, 0, 0, 0.4f);
                        m.addQuad({x0s, 0, z1}, {x1s, 0, z1}, {x1s, kSlab, z1}, {x0s, kSlab, z1}, {x0s, 0}, {x1s, kSlab}, curb);
                        m.addQuad({x1s, 0, z0}, {x0s, 0, z0}, {x0s, kSlab, z0}, {x1s, kSlab, z0}, {x1s, 0}, {x0s, kSlab}, curb);
                        m.addQuad({x1s, 0, z1}, {x1s, 0, z0}, {x1s, kSlab, z0}, {x1s, kSlab, z1}, {z1, 0}, {z0, kSlab}, curb);
                        m.addQuad({x0s, 0, z0}, {x0s, 0, z1}, {x0s, kSlab, z1}, {x0s, kSlab, z0}, {z0, 0}, {z1, kSlab}, curb);
                    }
                }
            }
            tile.bounds = m.bounds();
            city.groundTiles.push_back(std::move(tile));
        }
        city.bounds.min = {0, 0, 0};
        city.bounds.max = {totalX, cfg.city.heightRange[1] + 30.f, totalZ};
    }

    // ---- 高架 ----------------------------------------------------------------------------------
    {
        const float4 deck{static_cast<float>(GroundKind::OverpassDeck), 3.5f, 2.f, 0.6f};
        const float4 concrete = detailCustom(DetailKind::Concrete);
        const float4 strip = detailCustom(DetailKind::LightStrip, 0.52f, 1.6f);
        const float4 strip2 = detailCustom(DetailKind::LightStrip, 0.88f, 1.2f);
        const float z0 = city.rampStartZ, z1 = city.downRampEndZ;
        const float tileLen = 40.f;
        for (float tz = z0; tz < z1 - 0.01f; tz += tileLen) {
            Tile t, td;
            float tz1 = std::min(z1, tz + tileLen);
            for (float s = tz; s < tz1 - 0.01f; s += 2.0f) {
                float s1 = std::min(tz1, s + 2.0f);
                float h0 = city.overpassHeightAt(s), h1 = city.overpassHeightAt(s1);
                bool top0 = s >= city.rampStartZ + rampLength && s1 <= city.overpassEndZ;
                float hw = top0 ? cfg.city.overpassWidth * 0.5f : 5.5f;
                float y0 = h0 + 0.02f, y1 = h1 + 0.02f;
                quadFacing(t.mesh, {bxv - hw, y0, s}, {bxv + hw, y0, s}, {bxv + hw, y1, s1}, {bxv - hw, y1, s1},
                           {-hw, s}, {hw, s1}, deck, {0, 1, 0});
                // 床版の裏面と側面（下の大通りから見える）
                if (h0 > 0.5f || h1 > 0.5f) {
                    float b0 = std::max(0.f, h0 - 1.2f), b1 = std::max(0.f, h1 - 1.2f);
                    quadFacing(td.mesh, {bxv - hw, b0, s}, {bxv + hw, b0, s}, {bxv + hw, b1, s1}, {bxv - hw, b1, s1},
                               {-hw, s}, {hw, s1}, concrete, {0, -1, 0});
                    quadFacing(td.mesh, {bxv + hw, b0, s}, {bxv + hw, b1, s1}, {bxv + hw, y1, s1}, {bxv + hw, y0, s},
                               {s, b0}, {s1, y1}, concrete, {1, 0, 0});
                    quadFacing(td.mesh, {bxv - hw, b0, s}, {bxv - hw, b1, s1}, {bxv - hw, y1, s1}, {bxv - hw, y0, s},
                               {s, b0}, {s1, y1}, concrete, {-1, 0, 0});
                }
                // 欄干（内側面・上面・外側面）と発光ストリップ
                for (int side = -1; side <= 1; side += 2) {
                    const float fs = static_cast<float>(side);
                    const float xo = bxv + fs * hw, xi = bxv + fs * (hw - 0.35f);
                    const float bh = 1.1f;
                    const float3 inward{-fs, 0, 0}, outward{fs, 0, 0};
                    quadFacing(td.mesh, {xi, y0, s}, {xi, y1, s1}, {xi, y1 + bh, s1}, {xi, y0 + bh, s}, {s, 0}, {s1, bh},
                               concrete, inward);
                    quadFacing(td.mesh, {xo, y0 + bh, s}, {xi, y0 + bh, s}, {xi, y1 + bh, s1}, {xo, y1 + bh, s1}, {s, 0},
                               {s1, 0.35f}, concrete, {0, 1, 0});
                    quadFacing(td.mesh, {xo, y0, s}, {xo, y1, s1}, {xo, y1 + bh, s1}, {xo, y0 + bh, s}, {s, 0}, {s1, bh},
                               concrete, outward);
                    const float xs = xi - fs * 0.01f;
                    quadFacing(td.mesh, {xs, y0 + 0.55f, s}, {xs, y1 + 0.55f, s1}, {xs, y1 + 0.65f, s1}, {xs, y0 + 0.65f, s},
                               {s, 0}, {s1, 0.1f}, side < 0 ? strip : strip2, inward);
                }
            }
            // 橋脚
            for (float pz = std::ceil(tz / 30.f) * 30.f; pz < tz1; pz += 30.f) {
                float h = city.overpassHeightAt(pz);
                if (h < 4.f) continue;
                bool top0 = pz >= city.rampStartZ + rampLength && pz <= city.overpassEndZ;
                float hw = top0 ? cfg.city.overpassWidth * 0.5f : 5.5f;
                td.mesh.addBox({bxv, (h - 1.2f) * 0.5f, pz}, {0.8f, (h - 1.2f) * 0.5f, 1.2f}, 1, concrete, false);
                td.mesh.addBox({bxv, h - 1.7f, pz}, {hw - 0.5f, 0.5f, 1.0f}, 1, concrete, true);
            }
            t.bounds = t.mesh.bounds();
            td.bounds = td.mesh.bounds();
            city.overpassTiles.push_back(std::move(t));
            city.overpassDetailTiles.push_back(std::move(td));
        }
    }

    // ---- 歩行者・車の通り道 ------------------------------------------------------------------------
    {
        const float hw = city.boulevardHalfWidth;
        const float za = city.startZ - 30.f, zb = city.downRampEndZ + 40.f;
        for (float side : {-1.f, 1.f}) {
            city.sidewalks.push_back({{bxv + side * (hw - 1.3f), kSlab, za}, {bxv + side * (hw - 1.3f), kSlab, zb}, 1.2f});
            city.sidewalks.push_back({{bxv + side * (hw - 2.9f), kSlab, zb}, {bxv + side * (hw - 2.9f), kSlab, za}, 1.2f});
        }
        city.sidewalks.push_back({{city.alleyX0 + 1.f, 0, city.alleyZ - 1.2f}, {city.alleyX1 - 1.f, 0, city.alleyZ - 1.2f}, 1.0f});
        city.sidewalks.push_back({{city.alleyX1 - 1.f, 0, city.alleyZ + 1.2f}, {city.alleyX0 + 1.f, 0, city.alleyZ + 1.2f}, 1.0f});

        const float zr0 = city.startZ - 80.f, zr1 = std::min(totalZ, city.alleyZ + 120.f);
        // 地上は外側の車線（ランプの脇を通る）、高架は外側2車線。カメラは中央を走るので車と重ならない
        city.roadLanes.push_back({{bxv - 7.0f, 0, zr0}, {bxv - 7.0f, 0, zr1}, 3.4f});  // +Z 向き
        city.roadLanes.push_back({{bxv + 7.0f, 0, zr1}, {bxv + 7.0f, 0, zr0}, 3.4f});  // −Z 向き
        city.overpassLanes.push_back({{bxv - 3.8f, 0, city.rampStartZ}, {bxv - 3.8f, 0, city.downRampEndZ}, 3.5f});
        city.overpassLanes.push_back({{bxv + 3.8f, 0, city.downRampEndZ}, {bxv + 3.8f, 0, city.rampStartZ}, 3.5f});
    }
    return city;
}

}  // namespace bench

#include "drive/collision.h"

#include <algorithm>
#include <cmath>

namespace drive {

namespace {

constexpr float kCarClearance = 1.6f;   // 地上の車がランプの下を通れる天井の高さ[m]
constexpr float kDeckThickness = 1.2f;  // 高架の床版の厚み（procgen と同じ）
constexpr float kBarrierInset = 0.35f;  // 欄干の厚み

bool overlapY(const Aabb& b, float y0, float y1) { return b.max.y > y0 && b.min.y < y1; }

}  // namespace

// ---- 格子 --------------------------------------------------------------------------------------

void CityCollision::Grid::build(const std::vector<Aabb>& boxes, const Aabb& world, float cellSize) {
    cell = cellSize;
    origin = world.min;
    nx = std::max(1, static_cast<int>(std::ceil((world.max.x - world.min.x) / cell)) + 1);
    nz = std::max(1, static_cast<int>(std::ceil((world.max.z - world.min.z) / cell)) + 1);
    cells.assign(static_cast<size_t>(nx) * nz, {});
    for (size_t i = 0; i < boxes.size(); ++i) {
        const Aabb& b = boxes[i];
        int ix0 = std::clamp(static_cast<int>((b.min.x - origin.x) / cell), 0, nx - 1);
        int ix1 = std::clamp(static_cast<int>((b.max.x - origin.x) / cell), 0, nx - 1);
        int iz0 = std::clamp(static_cast<int>((b.min.z - origin.z) / cell), 0, nz - 1);
        int iz1 = std::clamp(static_cast<int>((b.max.z - origin.z) / cell), 0, nz - 1);
        for (int z = iz0; z <= iz1; ++z)
            for (int x = ix0; x <= ix1; ++x) cells[static_cast<size_t>(z) * nx + x].push_back(static_cast<int>(i));
    }
}

template <typename F>
void CityCollision::Grid::query(float x0, float z0, float x1, float z1, F&& f) const {
    int ix0 = std::clamp(static_cast<int>((x0 - origin.x) / cell), 0, nx - 1);
    int ix1 = std::clamp(static_cast<int>((x1 - origin.x) / cell), 0, nx - 1);
    int iz0 = std::clamp(static_cast<int>((z0 - origin.z) / cell), 0, nz - 1);
    int iz1 = std::clamp(static_cast<int>((z1 - origin.z) / cell), 0, nz - 1);
    // 同じ箱が複数セルに入るので、呼び出し側は重複を気にしない処理にする（押し出しは最大値で合成）
    for (int z = iz0; z <= iz1; ++z)
        for (int x = ix0; x <= ix1; ++x)
            for (int i : cells[static_cast<size_t>(z) * nx + x]) f(i);
}

// ---- 構築 --------------------------------------------------------------------------------------

CityCollision::CityCollision(const bench::CityData& city) : city_(&city) {
    // 車がぶつかる箱
    for (const Aabb& b : city.blocks) solids_.push_back(b);
    for (const Aabb& b : city.overpassPillars) solids_.push_back(b);
    for (const auto& p : city.props) {
        // 地面に張り付いた薄い物（蒸気口など）と、壁の高い所の物（室外機）は除く。高さの判定は collide で行う
        if (p.bounds.max.y - p.bounds.min.y < 0.2f) continue;
        solids_.push_back(p.bounds);
    }
    Aabb world = city.bounds;
    world.add(float3{-20.f, -1.f, -20.f});
    world.add(city.bounds.max + float3{20.f, 0.f, 20.f});
    solidGrid_.build(solids_, world, 16.f);

    // カメラが入り込まない箱：ビル（全体）
    for (const auto& b : city.buildings) occluders_.push_back(b.bounds);
    occluderGrid_.build(occluders_, world, 24.f);

    // ランプの下で、地上の車にとって天井が低い（通れない）区間
    const float needH = kCarClearance + kDeckThickness;
    auto solve = [&](float z0, float z1) {
        // overpassHeightAt が needH を越える z を二分法で求める（z0 側が低い）
        float lo = z0, hi = z1;
        for (int i = 0; i < 40; ++i) {
            float mid = 0.5f * (lo + hi);
            if (city.overpassHeightAt(mid) < needH) lo = mid;
            else hi = mid;
        }
        return 0.5f * (lo + hi);
    };
    rampBlockUpZ_ = solve(city.rampStartZ, city.rampStartZ + city.rampLength);
    {
        // 下りランプ：downRampEndZ 側が低い
        float lo = city.overpassEndZ, hi = city.downRampEndZ;
        for (int i = 0; i < 40; ++i) {
            float mid = 0.5f * (lo + hi);
            if (city.overpassHeightAt(mid) >= needH) lo = mid;
            else hi = mid;
        }
        rampBlockDownZ_ = 0.5f * (lo + hi);
    }
}

// ---- 高さ --------------------------------------------------------------------------------------

bool CityCollision::deckLayer(float x, float z, float y) const {
    const float hw = city_->overpassHalfWidthAt(z);
    if (hw <= 0.f || std::abs(x - city_->boulevardX) > hw) return false;
    const float h = city_->overpassHeightAt(z);
    return y >= h - 0.6f;
}

float CityCollision::surfaceHeight(float x, float z, float currentY, bool* onDeck) const {
    const bool deck = deckLayer(x, z, currentY);
    if (onDeck) *onDeck = deck;
    return deck ? city_->overpassHeightAt(z) : 0.f;
}

// ---- 押し出し ----------------------------------------------------------------------------------

void CityCollision::pushOutOfBox(const Circle& c, const Aabb& b, bool dynamic, std::vector<Contact>& out) const {
    const float2 p = c.center;
    const float2 q{std::clamp(p.x, b.min.x, b.max.x), std::clamp(p.y, b.min.z, b.max.z)};
    const float2 d = p - q;
    const float dist2 = d.x * d.x + d.y * d.y;
    Contact k;
    k.dynamic = dynamic;
    if (dist2 > 1e-8f) {
        const float dist = std::sqrt(dist2);
        if (dist >= c.radius) return;
        k.normal = d / dist;
        k.depth = c.radius - dist;
        k.point = q;
    } else {
        // 中心が箱の中：いちばん浅い面から出す
        const float dl = p.x - b.min.x, dr = b.max.x - p.x, db = p.y - b.min.z, df = b.max.z - p.y;
        const float m = std::min({dl, dr, db, df});
        if (m == dl) k.normal = {-1, 0};
        else if (m == dr) k.normal = {1, 0};
        else if (m == db) k.normal = {0, -1};
        else k.normal = {0, 1};
        k.depth = m + c.radius;
        k.point = p;
    }
    out.push_back(k);
}

void CityCollision::collide(const Circle& c, float y, float height, std::vector<Contact>& out, bool withDynamic) const {
    const float y0 = y + 0.02f, y1 = y + height;
    const float2 p = c.center;
    const float r = c.radius;

    // 静的な箱
    solidGrid_.query(p.x - r, p.y - r, p.x + r, p.y + r, [&](int i) {
        const Aabb& b = solids_[static_cast<size_t>(i)];
        if (!overlapY(b, y0, y1)) return;
        pushOutOfBox(c, b, false, out);
    });

    // 高架
    const float bx = city_->boulevardX;
    const float z = p.y;
    const float hw = city_->overpassHalfWidthAt(z);
    const bool onDeck = deckLayer(p.x, z, y);
    if (onDeck) {
        // 欄干の内側に閉じ込める
        const float lim = hw - kBarrierInset;
        const float dx = p.x - bx;
        if (dx + r > lim) out.push_back({{-1, 0}, dx + r - lim, {bx + lim, z}});
        if (-dx + r > lim) out.push_back({{1, 0}, -dx + r - lim, {bx - lim, z}});
    } else {
        // 地上：ランプの下で天井が低い区間は壁（上りと下りの2か所）
        const float rhw = city_->rampHalfWidth;
        Aabb up, down;
        up.add(float3{bx - rhw, 0.f, city_->rampStartZ});
        up.add(float3{bx + rhw, 5.f, rampBlockUpZ_});
        down.add(float3{bx - rhw, 0.f, rampBlockDownZ_});
        down.add(float3{bx + rhw, 5.f, city_->downRampEndZ});
        // 上り口の真正面（ランプの床の高さが 0.6m 未満）は床に乗れるので壁にしない
        auto rampFace = [&](Aabb box, float footZ, float dir) {
            // footZ から dir 向きに、床が 0.6m に達するまでは乗り上げられる
            float lo = footZ, hi = footZ + dir * city_->rampLength;
            for (int i = 0; i < 30; ++i) {
                float mid = 0.5f * (lo + hi);
                if (city_->overpassHeightAt(mid) < 0.6f) lo = mid;
                else hi = mid;
            }
            if (dir > 0) box.min.z = std::max(box.min.z, lo);
            else box.max.z = std::min(box.max.z, lo);
            return box;
        };
        up = rampFace(up, city_->rampStartZ, 1.f);
        down = rampFace(down, city_->downRampEndZ, -1.f);
        if (y0 < 5.f) {
            pushOutOfBox(c, up, false, out);
            pushOutOfBox(c, down, false, out);
        }
    }

    // 街の外周
    const Aabb& wb = city_->bounds;
    if (p.x - r < wb.min.x) out.push_back({{1, 0}, wb.min.x - (p.x - r), {wb.min.x, p.y}});
    if (p.x + r > wb.max.x) out.push_back({{-1, 0}, p.x + r - wb.max.x, {wb.max.x, p.y}});
    if (p.y - r < wb.min.z) out.push_back({{0, 1}, wb.min.z - (p.y - r), {p.x, wb.min.z}});
    if (p.y + r > wb.max.z) out.push_back({{0, -1}, p.y + r - wb.max.z, {p.x, wb.max.z}});

    // 動く物
    if (!withDynamic) return;
    for (const DynamicBody& d : dynamic_) {
        if (d.y + d.height < y0 || d.y > y1) continue;
        const float2 a = d.center - d.axis * d.halfLength, bb = d.center + d.axis * d.halfLength;
        const float2 ab = bb - a;
        const float t = d.halfLength > 0 ? std::clamp(dot(p - a, ab) / dot(ab, ab), 0.f, 1.f) : 0.f;
        const float2 q = a + ab * t;
        const float2 dd = p - q;
        const float rr = r + d.radius;
        const float dist2 = dot(dd, dd);
        if (dist2 >= rr * rr) continue;
        const float dist = std::sqrt(std::max(dist2, 1e-8f));
        Contact k;
        k.normal = dist > 1e-4f ? dd / dist : float2{0, 1};
        k.depth = rr - dist;
        k.point = q + k.normal * d.radius;
        k.dynamic = true;
        k.pedestrian = d.pedestrian;
        k.otherVelocity = d.velocity;
        out.push_back(k);
    }
}

// ---- カメラ用 ------------------------------------------------------------------------------------

float CityCollision::raycast(float3 a, float3 b) const {
    const float3 d = b - a;
    const float len = length(d);
    if (len < 1e-3f) return 1.f;
    const int steps = std::max(2, static_cast<int>(std::ceil(len / 0.4f)));
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float3 p = a + d * t;
        bool hit = false;
        occluderGrid_.query(p.x, p.z, p.x, p.z, [&](int k) {
            if (hit) return;
            const Aabb& o = occluders_[static_cast<size_t>(k)];
            const float m = 0.35f;  // 壁から少し離す
            if (p.x > o.min.x - m && p.x < o.max.x + m && p.z > o.min.z - m && p.z < o.max.z + m && p.y < o.max.y + m) hit = true;
        });
        // 高架の床版（下から見上げる・上から見下ろす時に床を突き抜けない）
        const float hw = city_->overpassHalfWidthAt(p.z);
        if (!hit && hw > 0.f && std::abs(p.x - city_->boulevardX) < hw) {
            const float h = city_->overpassHeightAt(p.z);
            if (h > 0.5f && p.y > h - kDeckThickness - 0.3f && p.y < h + 0.2f) hit = true;
        }
        const Aabb& wb = city_->bounds;
        if (p.x < wb.min.x || p.x > wb.max.x || p.z < wb.min.z || p.z > wb.max.z) hit = true;
        if (hit) return std::max(0.f, static_cast<float>(i - 1) / static_cast<float>(steps));
    }
    return 1.f;
}

bool CityCollision::isDrivable(float3 p, float radius) const {
    std::vector<Contact> cs;
    const float y = surfaceHeight(p.x, p.z, p.y);
    collide({{p.x, p.z}, radius}, y, 1.4f, cs, false);
    return cs.empty();
}

void CityCollision::nearestRoadPose(float3 p, float3* outPos, float* outYaw) const {
    // 近い順（1m刻みの渦巻き）に、半径 1.6m の円が何にも当たらない地面の点を探す
    const float r = 1.6f;
    for (int ring = 0; ring < 80; ++ring) {
        const int n = std::max(1, ring * 8);
        for (int k = 0; k < n; ++k) {
            const float ang = 6.2831853f * static_cast<float>(k) / static_cast<float>(n);
            const float3 q{p.x + std::cos(ang) * ring, 0.f, p.z + std::sin(ang) * ring};
            if (isDrivable(q, r)) {
                *outPos = q;
                // 通りの向き：南北か東西の近い方（格子状の街）
                const float yaw = *outYaw;
                const float snapped = std::round(yaw / 1.5707963f) * 1.5707963f;
                *outYaw = snapped;
                return;
            }
        }
    }
    *outPos = p;
}

}  // namespace drive

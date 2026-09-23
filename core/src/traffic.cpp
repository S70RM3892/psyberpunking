#include "bench/traffic.h"

#include <algorithm>
#include <cmath>

#include "bench/rng.h"

namespace bench {

using namespace filament::math;

namespace {

float laneLength(const Lane& l) { return length(float2{l.b.x - l.a.x, l.b.z - l.a.z}); }

float wrap(float x, float len) {
    float r = std::fmod(x, len);
    return r < 0 ? r + len : r;
}

}  // namespace

std::vector<Walker> makeWalkers(const CityData& city, int count, uint32_t seed, int variants) {
    std::vector<Walker> out;
    if (city.sidewalks.empty()) return out;
    Rng r(seed, 77);
    // 大通りの歩道4本に8割、路地の2本に2割
    const int nLanes = static_cast<int>(city.sidewalks.size());
    for (int i = 0; i < count; ++i) {
        Walker w;
        bool alley = nLanes > 4 && r.chance(0.2f);
        w.lane = alley ? r.irange(4, nLanes - 1) : r.irange(0, std::min(3, nLanes - 1));
        w.variant = r.irange(0, std::max(1, variants) - 1);
        const Lane& l = city.sidewalks[w.lane];
        float len = laneLength(l);
        // 大通りでは、カメラが通る区間（出発点〜ランプの先）に多く置く
        if (!alley) {
            float z0 = city.startZ - 10.f, z1 = city.rampStartZ + 60.f;
            float zPick = r.chance(0.8f) ? r.range(z0, z1) : r.range(std::min(l.a.z, l.b.z), std::max(l.a.z, l.b.z));
            w.offset = std::abs(zPick - l.a.z);
        } else {
            w.offset = r.range(0, len);
        }
        w.speed = r.range(1.1f, 1.6f);
        w.lateral = r.range(-0.45f, 0.45f);
        w.phase0 = r.uniform();
        w.scale = r.range(0.92f, 1.07f);
        out.push_back(w);
    }
    return out;
}

std::vector<Vehicle> makeVehicles(const CityData& city, int count, uint32_t seed, int models) {
    std::vector<Vehicle> out;
    Rng r(seed, 99);
    const int ground = static_cast<int>(std::lround(count * 0.6f));
    for (int i = 0; i < count; ++i) {
        Vehicle v;
        v.overpass = i >= ground;
        const auto& lanes = v.overpass ? city.overpassLanes : city.roadLanes;
        if (lanes.empty()) continue;
        v.lane = i % static_cast<int>(lanes.size());
        v.model = r.irange(0, std::max(1, models) - 1);
        v.colorIndex = r.irange(0, 7);
        v.speed = v.overpass ? r.range(11.f, 16.f) : r.range(7.f, 12.f);
        out.push_back(v);
    }
    // 同じ車線の車を等間隔＋ゆらぎで並べる（重なり防止）
    for (int pass = 0; pass < 2; ++pass) {
        const auto& lanes = pass ? city.overpassLanes : city.roadLanes;
        for (int li = 0; li < static_cast<int>(lanes.size()); ++li) {
            std::vector<Vehicle*> onLane;
            for (auto& v : out) {
                if (v.overpass == (pass == 1) && v.lane == li) onLane.push_back(&v);
            }
            float len = laneLength(lanes[li]);
            float speed = 0;
            for (auto* v : onLane) speed += v->speed;
            speed = onLane.empty() ? 0 : speed / static_cast<float>(onLane.size());
            for (size_t k = 0; k < onLane.size(); ++k) {
                onLane[k]->offset = len * (static_cast<float>(k) + r.range(0.1f, 0.5f)) / static_cast<float>(onLane.size());
                onLane[k]->speed = speed;  // 同じ車線は同じ速度（追突しない）
            }
        }
    }
    return out;
}

ActorPose walkerPose(const CityData& city, const Walker& w, double t) {
    const Lane& l = city.sidewalks[w.lane];
    float len = laneLength(l);
    float s = wrap(w.offset + w.speed * static_cast<float>(t), len);
    float2 dir = float2{l.b.x - l.a.x, l.b.z - l.a.z} / std::max(len, 1e-3f);
    float2 side{-dir.y, dir.x};
    ActorPose p;
    p.position = float3{l.a.x + dir.x * s + side.x * w.lateral, l.a.y, l.a.z + dir.y * s + side.y * w.lateral};
    p.yaw = std::atan2(dir.x, dir.y);
    // 歩幅 ~0.75m × 2歩 = 1周期
    p.phase = std::fmod(w.phase0 + w.speed * static_cast<float>(t) / 1.5f, 1.0f);
    return p;
}

ActorPose vehiclePose(const CityData& city, const Vehicle& v, double t) {
    const Lane& l = (v.overpass ? city.overpassLanes : city.roadLanes)[v.lane];
    float len = laneLength(l);
    float s = wrap(v.offset + v.speed * static_cast<float>(t), len);
    float2 dir = float2{l.b.x - l.a.x, l.b.z - l.a.z} / std::max(len, 1e-3f);
    ActorPose p;
    p.position = float3{l.a.x + dir.x * s, l.a.y, l.a.z + dir.y * s};
    if (v.overpass) p.position.y = city.overpassHeightAt(p.position.z) + 0.02f;
    p.yaw = std::atan2(dir.x, dir.y);
    return p;
}

}  // namespace bench

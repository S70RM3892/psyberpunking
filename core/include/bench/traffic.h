// 歩行者と車の動き。時刻だけから位置が決まる（状態を持たない）ので、どの端末でも同じ絵になる。
#pragma once

#include <vector>

#include "bench/procgen.h"

namespace bench {

struct Walker {
    int lane = 0;
    int variant = 0;
    float offset = 0;  // 通り道の上での初期位置[m]
    float speed = 1.3f;
    float lateral = 0; // 通り道の中心からの横ずれ[m]
    float phase0 = 0;
    float scale = 1.0f;  // 身長のばらつき
};

struct Vehicle {
    int lane = 0;
    bool overpass = false;
    int model = 0;
    int colorIndex = 0;
    float offset = 0;
    float speed = 10.f;
};

struct ActorPose {
    float3 position;
    float yaw = 0;    // +Z 向きが 0
    float phase = 0;  // 歩行位相 [0,1)
};

std::vector<Walker> makeWalkers(const CityData& city, int count, uint32_t seed, int variants);
std::vector<Vehicle> makeVehicles(const CityData& city, int count, uint32_t seed, int models);

ActorPose walkerPose(const CityData& city, const Walker& w, double t);
ActorPose vehiclePose(const CityData& city, const Vehicle& v, double t);

}  // namespace bench

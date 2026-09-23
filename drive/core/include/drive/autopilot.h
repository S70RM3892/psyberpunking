// 自動運転（デモ・撮影・CIの煙テスト用）。折れ線の経路を先読み追従（pure pursuit）で走る。
#pragma once

#include <vector>

#include "drive/vehicle.h"

namespace drive {

class Autopilot {
public:
    // waypoints は XZ の経路（y は無視）。targetSpeed [m/s]
    Autopilot(std::vector<float3> waypoints, float targetSpeed) : wp_(std::move(waypoints)), speed_(targetSpeed) {}
    VehicleInput drive(const VehicleState& s, float wheelbase);
    bool finished() const { return done_; }
    size_t index() const { return i_; }

private:
    std::vector<float3> wp_;
    float speed_;
    size_t i_ = 0;
    bool done_ = false;
};

}  // namespace drive

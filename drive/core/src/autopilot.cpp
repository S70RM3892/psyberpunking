#include "drive/autopilot.h"

#include <algorithm>
#include <cmath>

namespace drive {

VehicleInput Autopilot::drive(const VehicleState& s, float wheelbase) {
    VehicleInput in;
    if (wp_.empty()) return in;
    const float2 p{s.position.x, s.position.z};
    // 近づいた点は通過扱い
    while (i_ + 1 < wp_.size()) {
        const float2 w{wp_[i_].x, wp_[i_].z};
        if (length(w - p) < 6.f) ++i_;
        else break;
    }
    const float2 last{wp_.back().x, wp_.back().z};
    if (i_ + 1 >= wp_.size() && length(last - p) < 6.f) done_ = true;

    // 先読み点：今の目標点から lookahead だけ経路に沿って進んだ所
    const float speed = length(s.velocity);
    const float lookahead = std::clamp(4.f + speed * 0.6f, 5.f, 18.f);
    float2 target{wp_[i_].x, wp_[i_].z};
    {
        float remain = lookahead - length(target - p);
        size_t k = i_;
        while (remain > 0.f && k + 1 < wp_.size()) {
            const float2 a{wp_[k].x, wp_[k].z}, b{wp_[k + 1].x, wp_[k + 1].z};
            const float seg = length(b - a);
            if (seg >= remain) {
                target = a + (b - a) * (remain / seg);
                break;
            }
            remain -= seg;
            target = b;
            ++k;
        }
    }
    // pure pursuit：目標点への方位差から舵角を決める（舵は右が正、yaw は左が正）
    const float2 f{std::sin(s.yaw), std::cos(s.yaw)};
    const float2 r{-std::cos(s.yaw), std::sin(s.yaw)};
    const float2 d = target - p;
    const float ld = std::max(1.f, length(d));
    const float sinA = dot(d, r) / ld;
    const float curvature = 2.f * sinA / ld;
    const float steerAngle = std::atan(curvature * wheelbase);
    const float maxSteer = 0.6f / (1.f + 0.085f * std::abs(s.forwardSpeed));
    in.steer = std::clamp(steerAngle / maxSteer, -1.f, 1.f);
    (void)f;

    // 曲がる手前では速度を落とす
    float want = done_ ? 0.f : speed_;
    if (i_ + 1 < wp_.size()) {
        const float2 a{wp_[i_].x, wp_[i_].z}, b{wp_[i_ + 1].x, wp_[i_ + 1].z};
        const float2 u = normalize(a - p), v = normalize(b - a);
        const float turn = 1.f - std::clamp(dot(u, v), -1.f, 1.f);  // 0 = まっすぐ
        if (length(a - p) < 30.f) want = std::min(want, speed_ * (1.f - std::min(0.7f, turn * 1.2f)));
    }
    if (speed < want - 0.5f) in.throttle = std::clamp((want - speed) * 0.25f, 0.2f, 1.f);
    else if (speed > want + 1.5f) in.brake = std::clamp((speed - want) * 0.15f, 0.1f, 1.f);
    return in;
}

}  // namespace drive

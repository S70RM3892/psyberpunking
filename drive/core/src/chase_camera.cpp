#include "drive/chase_camera.h"

#include <algorithm>
#include <cmath>

namespace drive {

namespace {

constexpr float kPi = 3.14159265f;

float wrapAngle(float a) {
    while (a > kPi) a -= 2.f * kPi;
    while (a < -kPi) a += 2.f * kPi;
    return a;
}

float3 dirFromYawPitch(float yaw, float pitch) {
    return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
}

}  // namespace

void ChaseCamera::reset(const VehicleState& s) {
    yaw_ = s.yaw;
    lookYaw_ = lookPitch_ = 0.f;
    const float3 f = dirFromYawPitch(yaw_, 0.f);
    eye_ = s.position + float3{0, 2.2f, 0} - f * 6.3f;
    eyeVel_ = float3{0};
    fov_ = 62.f;
    pull_ = 1.f;
    valid_ = true;
}

CameraOutput ChaseCamera::update(float dt, const VehicleState& s, const CityCollision& world, float2 look) {
    if (!valid_) reset(s);
    dt = std::clamp(dt, 0.f, 0.1f);
    const float speed = length(s.velocity);
    CameraOutput out;

    // 見回し：スティックの位置へ寄せ、離せば戻る
    lookYaw_ += (look.x * 2.4f - lookYaw_) * std::min(1.f, dt * 6.f);
    lookPitch_ += (-look.y * 0.45f - lookPitch_) * std::min(1.f, dt * 6.f);

    // 速さで画角を広げる（体感速度）
    const float fovTarget = (mode_ == Mode::Chase ? 62.f : 70.f) + std::clamp(speed / 45.f, 0.f, 1.f) * 12.f;
    fov_ += (fovTarget - fov_) * std::min(1.f, dt * 3.f);
    out.fovDeg = fov_;

    if (mode_ == Mode::Bonnet) {
        // ボンネット：車体に固定（ピッチ・ロールは半分だけ伝える）
        const float3 up{0, 1, 0};
        const float yaw = s.yaw + lookYaw_;
        const float3 f = dirFromYawPitch(yaw, -s.pitch * 0.5f + lookPitch_);
        const float3 carF = dirFromYawPitch(s.yaw, 0.f);
        out.eye = s.position + up * 1.08f + carF * 0.35f;
        out.forward = f;
        out.focusDistance = 30.f;
        eye_ = out.eye;
        eyeVel_ = float3{0};
        yaw_ = s.yaw;
        return out;
    }

    // 向き：車の向きに遅れて追う。バック中は後ろを振り向かない（前向きのまま）
    float headingYaw = s.yaw;
    const float moveYaw = std::atan2(s.velocity.x, s.velocity.y);
    if (speed > 6.f && std::abs(wrapAngle(moveYaw - s.yaw)) < 1.2f) {
        // ドリフト中は進行方向と車体の間を見る（流れる車体が画面に入る）
        headingYaw = s.yaw + wrapAngle(moveYaw - s.yaw) * 0.45f;
    }
    yaw_ += wrapAngle(headingYaw - yaw_) * std::min(1.f, dt * 4.0f);

    const float dist = 6.1f + std::clamp(speed / 40.f, 0.f, 1.f) * 1.4f;
    const float height = 2.05f;
    const float yaw = yaw_ + lookYaw_;
    const float3 dir = dirFromYawPitch(yaw, 0.f);
    const float3 pivot = s.position + float3{0, 1.25f, 0};
    float3 desired = s.position + float3{0, height, 0} - dir * dist;
    // 見上げ・見下ろし（ピボットのまわりに回す）
    desired.y += std::sin(lookPitch_) * dist;

    // めり込み防止：ピボットから目標へ線を引き、建物・高架の手前で止める
    const float hit = world.raycast(pivot, desired);
    const float pullTarget = std::max(0.25f, hit);
    pull_ = pullTarget < pull_ ? pullTarget : pull_ + (pullTarget - pull_) * std::min(1.f, dt * 2.f);
    desired = pivot + (desired - pivot) * pull_;

    // ばね・ダンパ（臨界減衰）で追う。速い時は少し硬く
    const float w = 9.f + std::clamp(speed / 30.f, 0.f, 1.f) * 4.f;
    const float3 acc = (desired - eye_) * (w * w) - eyeVel_ * (2.f * w);
    eyeVel_ += acc * dt;
    eye_ += eyeVel_ * dt;
    // 追従が遅れすぎたら（リスポーン直後など）詰める
    if (length(eye_ - desired) > 12.f) {
        eye_ = desired;
        eyeVel_ = float3{0};
    }

    const float3 target = s.position + float3{0, 1.1f, 0} + dirFromYawPitch(s.yaw, 0.f) * 3.5f;
    out.eye = eye_;
    out.forward = normalize(target - eye_);
    out.focusDistance = length(s.position - eye_);
    return out;
}

}  // namespace drive

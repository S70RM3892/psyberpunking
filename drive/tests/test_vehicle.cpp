// 車の運動（何も無い平地で）：加速・制動・最高速・旋回半径・アンダーステア・ドリフト・後退・決定性
#include <gtest/gtest.h>

#include <cmath>

#include "drive/vehicle.h"
#include "drive_test_util.h"

using namespace drive;

namespace {

constexpr float kDt = 1.f / 240.f;

struct Rig {
    bench::CityData city = openField();
    CityCollision world{city};
    Vehicle car;
    Rig() { car.reset({0, 0, 0}, 0.f); }
    void run(float seconds, const VehicleInput& in) {
        for (int i = 0; i < static_cast<int>(seconds / kDt); ++i) car.step(kDt, in, world);
    }
    float speed() const { return length(car.state().velocity); }
};

VehicleInput throttle(float t, float steer = 0.f) {
    VehicleInput in;
    in.throttle = t;
    in.steer = steer;
    return in;
}

}  // namespace

TEST(Vehicle, AcceleratesToHundredInFewSeconds) {
    Rig r;
    float t = 0;
    while (r.speed() < 100.f / 3.6f && t < 20.f) {
        r.run(0.05f, throttle(1.f));
        t += 0.05f;
    }
    // スポーツカーらしく、でも速すぎない（0-100 km/h が 4〜8 秒）
    EXPECT_GT(t, 4.f);
    EXPECT_LT(t, 8.f);
    EXPECT_GE(r.car.state().gear, 2);  // 100 km/h は2速の上の方（約6400rpm）
}

TEST(Vehicle, TopSpeedIsBounded) {
    Rig r;
    r.run(60.f, throttle(1.f));
    const float kmh = r.speed() * 3.6f;
    EXPECT_GT(kmh, 190.f);
    EXPECT_LT(kmh, 270.f);
}

TEST(Vehicle, BrakesFromHundredInReasonableDistance) {
    Rig r;
    while (r.speed() < 100.f / 3.6f) r.run(0.02f, throttle(1.f));
    const float z0 = r.car.state().position.z;
    VehicleInput b;
    b.brake = 1.f;
    for (int i = 0; i < 240 * 10 && r.speed() > 0.01f; ++i) r.car.step(kDt, b, r.world);
    const float dist = r.car.state().position.z - z0;
    EXPECT_GT(dist, 25.f);
    EXPECT_LT(dist, 60.f);
    EXPECT_LT(r.speed(), 0.05f);
}

TEST(Vehicle, TightTurnAtLowSpeed) {
    Rig r;
    while (r.speed() < 8.f) r.run(0.02f, throttle(0.5f));
    // 速度を保って右いっぱい
    for (int i = 0; i < 240 * 4; ++i) {
        VehicleInput in = throttle(r.speed() < 8.f ? 0.4f : 0.f, 1.f);
        r.car.step(kDt, in, r.world);
    }
    const auto& s = r.car.state();
    const float radius = r.speed() / std::max(1e-3f, std::abs(s.yawRate));
    EXPECT_GT(radius, 4.f);
    EXPECT_LT(radius, 12.f);
    EXPECT_LT(s.yawRate, 0.f);  // 右に切ると右回り（ヨーは左が正）
}

TEST(Vehicle, UndersteersInsteadOfExceedingGrip) {
    Rig r;
    while (r.speed() < 30.f) r.run(0.02f, throttle(1.f));
    float maxLat = 0;
    for (int i = 0; i < 240 * 3; ++i) {
        r.car.step(kDt, throttle(0.3f, 1.f), r.world);
        maxLat = std::max(maxLat, std::abs(r.car.state().yawRate * r.car.state().forwardSpeed));
    }
    EXPECT_LT(maxLat, r.car.params().grip * 9.81f * 1.1f);
    EXPECT_GT(maxLat, 5.f);
}

TEST(Vehicle, HandbrakeDriftsAndRecovers) {
    Rig r;
    while (r.speed() < 20.f) r.run(0.02f, throttle(1.f));
    auto slipAngle = [&] {
        const auto& s = r.car.state();
        const float2 f{std::sin(s.yaw), std::cos(s.yaw)};
        const float v = length(s.velocity);
        if (v < 2.f) return 0.f;
        return std::acos(std::clamp(dot(s.velocity / v, f), -1.f, 1.f));
    };
    float maxSlip = 0;
    VehicleInput drift = throttle(0.5f, 0.8f);
    drift.handbrake = true;
    for (int i = 0; i < 240 * 1; ++i) {
        r.car.step(kDt, drift, r.world);
        maxSlip = std::max(maxSlip, slipAngle());
    }
    EXPECT_GT(maxSlip, 15.f * 3.14159f / 180.f) << "サイドブレーキで車体が流れる";
    // 離してまっすぐにすると戻る
    r.run(2.5f, throttle(0.4f, 0.f));
    EXPECT_LT(slipAngle(), 5.f * 3.14159f / 180.f);
}

TEST(Vehicle, ReversesWhenBrakeHeldAtStandstill) {
    Rig r;
    VehicleInput b;
    b.brake = 1.f;
    r.run(8.f, b);
    const auto& s = r.car.state();
    EXPECT_LT(s.position.z, -10.f);
    EXPECT_LE(std::abs(s.forwardSpeed), r.car.params().reverseMaxSpeed + 0.2f);
    EXPECT_EQ(s.gear, -1);
    EXPECT_EQ(s.brake, 0.f) << "後退中はブレーキランプを点けない";
}

TEST(Vehicle, Deterministic) {
    auto runOnce = [] {
        Rig r;
        for (int i = 0; i < 240 * 20; ++i) {
            VehicleInput in;
            in.throttle = 0.5f + 0.5f * std::sin(i * 0.003f);
            in.steer = std::sin(i * 0.0021f);
            in.handbrake = (i / 600) % 5 == 3;
            r.car.step(kDt, in, r.world);
        }
        return r.car.state();
    };
    VehicleState a = runOnce(), b = runOnce();
    EXPECT_EQ(a.position.x, b.position.x);
    EXPECT_EQ(a.position.z, b.position.z);
    EXPECT_EQ(a.yaw, b.yaw);
}

// 本物の街での当たり判定：壁を抜けない、高架を上り下りできる、ランプの下の低い所には入れない、カメラ
#include <gtest/gtest.h>

#include <cmath>

#include "bench/rng.h"
#include "drive/vehicle.h"
#include "drive_test_util.h"

using namespace drive;

namespace {

constexpr float kDt = 1.f / 240.f;

float worstPenetration(const Vehicle& car, const CityCollision& world) {
    Circle cs[3];
    car.circles(cs);
    std::vector<Contact> out;
    for (const Circle& c : cs) world.collide(c, car.state().position.y, car.params().bodyHeight, out, false);
    float worst = 0;
    for (const auto& k : out) worst = std::max(worst, k.depth);
    return worst;
}

float3 spawnOf(const bench::CityData& c) { return {c.boulevardX - 3.2f, 0.f, c.startZ}; }

}  // namespace

TEST(Collision, SpawnIsClear) {
    const auto& city = realCity();
    CityCollision world(city);
    EXPECT_TRUE(world.isDrivable(spawnOf(city), 1.0f));
}

TEST(Collision, CannotDriveThroughBlocks) {
    const auto& city = realCity();
    CityCollision world(city);
    Vehicle car;
    // 西向き（-X、yaw = -90°）に全開で街区へ突っ込む
    car.reset(spawnOf(city), -1.5707963f);
    VehicleInput in;
    in.throttle = 1.f;
    float worst = 0;
    for (int i = 0; i < 240 * 6; ++i) {
        car.step(kDt, in, world);
        worst = std::max(worst, worstPenetration(car, world));
    }
    EXPECT_LT(worst, 0.12f);
    // 大通りの歩道の縁より西へは出ていない
    EXPECT_GT(car.state().position.x, city.boulevardX - city.boulevardHalfWidth - 0.2f);
}

TEST(Collision, RandomDrivingNeverTunnels) {
    const auto& city = realCity();
    CityCollision world(city);
    Vehicle car;
    car.reset(spawnOf(city), 0.f);
    bench::Rng rng(7);
    VehicleInput in;
    float worst = 0;
    for (int i = 0; i < 240 * 180; ++i) {
        if (i % 180 == 0) {
            in.throttle = rng.chance(0.8f) ? rng.range(0.4f, 1.f) : 0.f;
            in.brake = in.throttle == 0.f ? rng.range(0.f, 1.f) : 0.f;
            in.steer = rng.range(-1.f, 1.f);
            in.handbrake = rng.chance(0.1f);
        }
        car.step(kDt, in, world);
        worst = std::max(worst, worstPenetration(car, world));
        const auto& p = car.state().position;
        ASSERT_GE(p.x, city.bounds.min.x);
        ASSERT_LE(p.x, city.bounds.max.x);
        ASSERT_GE(p.z, city.bounds.min.z);
        ASSERT_LE(p.z, city.bounds.max.z);
        ASSERT_GE(p.y, -0.01f);
    }
    EXPECT_LT(worst, 0.2f);
}

TEST(Collision, DrivesUpAndDownTheOverpass) {
    const auto& city = realCity();
    CityCollision world(city);
    Vehicle car;
    car.reset(spawnOf(city), 0.f);
    VehicleInput in;
    float maxY = 0;
    bool wasOnDeck = false;
    for (int i = 0; i < 240 * 60 && car.state().position.z < city.downRampEndZ + 30.f; ++i) {
        const auto& s = car.state();
        // 速度を 15 m/s 前後に保ち、ランプの中心線（大通りの中心）へ寄せる
        in.throttle = length(s.velocity) < 15.f ? 0.7f : 0.f;
        in.steer = std::clamp((s.position.x - city.boulevardX) * 0.25f + s.yaw * 2.f, -1.f, 1.f);
        car.step(kDt, in, world);
        maxY = std::max(maxY, car.state().position.y);
        wasOnDeck |= car.state().onDeck && car.state().position.y > city.overpassY - 0.1f;
    }
    EXPECT_TRUE(wasOnDeck);
    EXPECT_NEAR(maxY, city.overpassY, 0.05f);
    EXPECT_GT(car.state().position.z, city.downRampEndZ);
    EXPECT_NEAR(car.state().position.y, 0.f, 0.01f) << "下りランプを降りて地上に戻る";
}

TEST(Collision, LowPartUnderRampIsAWall) {
    const auto& city = realCity();
    CityCollision world(city);
    // 地上、ランプの横から、ランプへ向かって -X へ突っ込む
    auto driveIn = [&](float rampFraction) {
        Vehicle car;
        car.reset({city.boulevardX + 7.0f, 0.f, city.rampStartZ + city.rampLength * rampFraction}, -1.5707963f);
        VehicleInput in;
        in.throttle = 0.6f;
        for (int i = 0; i < 240 * 3; ++i) car.step(kDt, in, world);
        return car.state();
    };
    // 上り始め（床 1.1m ほど）：床版の下は 1.6m 未満なので壁
    VehicleState low = driveIn(0.2f);
    EXPECT_NEAR(low.position.y, 0.f, 0.01f);
    EXPECT_GT(low.position.x, city.boulevardX + city.rampHalfWidth + 0.5f);
    // 高い所（床 4.7m ほど）：床版の下に入れる（真ん中の橋脚で止まることはある）
    VehicleState high = driveIn(0.45f);
    EXPECT_NEAR(high.position.y, 0.f, 0.01f);
    EXPECT_LT(high.position.x, city.boulevardX + city.rampHalfWidth - 1.0f);
}

TEST(Collision, CameraRaycastStopsAtBuildings) {
    const auto& city = realCity();
    CityCollision world(city);
    // 出発点（大通りの上、開けている）から、いちばん近いビルの中心へ線を引く
    const float3 s = spawnOf(city) + float3{0, 2.f, 0};
    const bench::BuildingInstance* nearest = nullptr;
    float best = 1e9f;
    for (const auto& b : city.buildings) {
        const float d = length(b.bounds.center() - s);
        if (d < best) best = d, nearest = &b;
    }
    ASSERT_NE(nearest, nullptr);
    const float3 c = nearest->bounds.center();
    const float t = world.raycast(s, float3{c.x, 2.f, c.z});
    EXPECT_LT(t, 1.f);
    EXPECT_GT(t, 0.f);
    // 開けた車道の上は当たらない
    EXPECT_EQ(world.raycast(s, s + float3{0, 0.5f, -5.f}), 1.f);
}

TEST(Collision, TrafficBumpsButDoesNotTunnel) {
    const auto& city = realCity();
    CityCollision world(city);
    // 止まっている車（カプセル）の真後ろから突っ込む
    const float3 s = spawnOf(city);
    DynamicBody other;
    other.center = {s.x, s.z + 25.f};
    other.axis = {0, 1};
    other.halfLength = 1.35f;
    other.radius = 1.0f;
    world.setDynamicBodies({other});
    Vehicle car;
    car.reset(s, 0.f);
    VehicleInput in;
    in.throttle = 1.f;
    for (int i = 0; i < 240 * 4; ++i) car.step(kDt, in, world);
    // 相手の後端（中心 - 半長 - 半径）より前へは入り込まない
    EXPECT_LT(car.state().position.z + 2.2f, other.center.y - other.halfLength - other.radius + 0.25f);
    EXPECT_GT(car.state().impact + 1.f, 0.f);
}

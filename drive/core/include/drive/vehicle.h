// 自車の動き（アーケード寄り）。固定ステップで回す（step の dt は 1/240 s を想定）。
//
// 向き yaw は +Z が 0、増えると +X（車から見て左）へ回る。前方 = (sin yaw, cos yaw)。
//  - 旋回：速度に応じて最大舵角を絞り、自転車モデルのヨーレートをグリップ（μg）で頭打ちにする（アンダーステア）
//  - ドリフト：サイドブレーキで進行方向を車体の向きに追従させない割合を上げる → 横滑り、カウンターで戻る
//  - 前後：出力と最大駆動力で頭打ちの加速、空気抵抗・転がり抵抗・坂、止まってブレーキを踏み続けると後退
//  - 衝突：車体を3つの円で覆い、押し出し＋撃力（反発・摩擦・回転）
#pragma once

#include <math/mat4.h>

#include "drive/collision.h"

namespace drive {

using filament::math::mat4f;

struct VehicleInput {
    float steer = 0;        // -1 左 .. +1 右
    float throttle = 0;     // 0..1
    float brake = 0;        // 0..1（止まっていれば後退）
    bool handbrake = false;
};

struct VehicleParams {
    float mass = 1350.f;            // kg
    float wheelbase = 2.6f;         // m
    float maxSteerLow = 0.60f;      // 止まっている時の最大舵角 [rad]
    float steerFalloff = 0.085f;    // 速度で舵角を絞る係数（maxSteerLow / (1 + k v)）
    float steerRate = 4.0f;         // 舵角が目標に追いつく速さ [1/s]
    float enginePower = 150e3f;     // W
    float maxDriveForce = 7200.f;   // N（発進の加速の上限）
    float brakeForce = 12500.f;     // N
    float handbrakeForce = 4000.f;  // N
    float reverseMaxSpeed = 11.f;   // m/s
    float drag = 0.38f;             // 空気抵抗 0.5 ρ Cd A
    float rollResistance = 160.f;   // N
    float grip = 1.05f;             // 横方向の摩擦係数 μ
    float halfLength = 2.2f;
    float halfWidth = 0.97f;
    float bodyHeight = 1.3f;
};

struct VehicleState {
    float3 position{0};    // 接地点（車体中央の真下）
    float yaw = 0;
    float2 velocity{0};    // XZ [m/s]
    float yawRate = 0;     // [rad/s]、正 = 左回り
    float verticalSpeed = 0;
    float steerAngle = 0;  // 前輪の切れ角 [rad]、正 = 右
    bool onDeck = false;

    // 見た目・音・HUD 用
    float pitch = 0, roll = 0;          // 車体の傾き（坂の角度込み）[rad]
    float pitchVel = 0, rollVel = 0;
    float slope = 0;                    // 路面の縦の傾き [rad]
    float forwardSpeed = 0;             // 前向きの速さ [m/s]（後退は負）
    float slip = 0;                     // タイヤの滑り 0..1
    float rpm = 900.f;
    int gear = 1;                       // -1 後退, 1..6
    float impact = 0;                   // 直近の衝突の強さ [m/s]（音用、時間で減衰）
    float throttle = 0, brake = 0;      // 実際に効いている入力（ブレーキランプ・音用）
    bool reversing = false;
};

// 描画用の姿勢（接地点が原点、+Z が前、ピッチ・ロール込み）
mat4f vehicleTransform(const VehicleState& s);

class Vehicle {
public:
    explicit Vehicle(const VehicleParams& p = {}) : p_(p) {}

    void reset(float3 position, float yaw);
    void step(float dt, const VehicleInput& in, const CityCollision& world);

    const VehicleState& state() const { return s_; }
    const VehicleParams& params() const { return p_; }

    float2 forward() const;
    float2 right() const;
    mat4f transform() const { return vehicleTransform(s_); }
    // 車体を覆う円（当たり判定・テスト用）
    void circles(Circle out[3]) const;

private:
    void resolveCollisions(const CityCollision& world);
    void updateGearbox(float dt);

    VehicleParams p_;
    VehicleState s_;
    float throttleAvg_ = 0.f;   // 変速点を決めるための、ならしたアクセル開度
    float shiftTimer_ = 0.f;    // 変速直後は続けて変えない
};

}  // namespace drive

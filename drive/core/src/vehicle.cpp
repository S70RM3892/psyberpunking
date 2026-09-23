#include "drive/vehicle.h"

#include <algorithm>
#include <cmath>

namespace drive {

namespace {

constexpr float kG = 9.81f;
constexpr float kCircleOffset = 1.3f;   // 3つの円の前後位置
constexpr float kCircleRadius = 0.97f;

float approach(float v, float target, float rate, float dt) { return v + (target - v) * std::min(1.f, rate * dt); }

}  // namespace

float2 Vehicle::forward() const { return {std::sin(s_.yaw), std::cos(s_.yaw)}; }
float2 Vehicle::right() const { return {-std::cos(s_.yaw), std::sin(s_.yaw)}; }

void Vehicle::reset(float3 position, float yaw) {
    s_ = {};
    throttleAvg_ = 0.f;
    shiftTimer_ = 0.f;
    s_.position = position;
    s_.yaw = yaw;
}

void Vehicle::circles(Circle out[3]) const {
    const float2 c{s_.position.x, s_.position.z};
    const float2 f = forward();
    for (int i = 0; i < 3; ++i) out[i] = {c + f * (kCircleOffset * static_cast<float>(i - 1)), kCircleRadius};
}

mat4f vehicleTransform(const VehicleState& s) {
    return mat4f::translation(s.position) * mat4f::rotation(s.yaw, float3{0, 1, 0}) *
           mat4f::rotation(s.pitch, float3{1, 0, 0}) * mat4f::rotation(s.roll, float3{0, 0, 1});
}

void Vehicle::step(float dt, const VehicleInput& inRaw, const CityCollision& world) {
    VehicleInput in = inRaw;
    in.steer = std::clamp(in.steer, -1.f, 1.f);
    in.throttle = std::clamp(in.throttle, 0.f, 1.f);
    in.brake = std::clamp(in.brake, 0.f, 1.f);

    float2 fwd = forward(), rgt = right();
    float vF = dot(s_.velocity, fwd);
    float vR = dot(s_.velocity, rgt);
    const float speed = length(s_.velocity);

    // ---- 舵：速いほど切れ角を絞る（高速で神経質にならないように） ----
    const float maxSteer = p_.maxSteerLow / (1.f + p_.steerFalloff * std::abs(vF));
    s_.steerAngle = approach(s_.steerAngle, in.steer * maxSteer, p_.steerRate * 2.5f, dt);

    // ---- 前後の力 ----
    float force = 0.f;
    s_.reversing = false;
    const bool rollingBack = vF < -0.3f;
    if (in.brake > 0.05f && in.throttle < 0.05f && vF < 0.5f) {
        // 止まっている（か後ろへ動いている）時のブレーキ＝後退
        s_.reversing = true;
        if (vF > -p_.reverseMaxSpeed) force -= in.brake * p_.maxDriveForce * 0.55f;
    } else {
        if (in.throttle > 0.f) {
            if (rollingBack) force += in.throttle * p_.brakeForce;  // 後ろへ動いている時のアクセルはブレーキ
            else force += in.throttle * std::min(p_.maxDriveForce, p_.enginePower / std::max(std::abs(vF), 1.f));
        }
        if (in.brake > 0.f && vF > 0.f) force -= in.brake * p_.brakeForce;
    }
    if (in.handbrake && std::abs(vF) > 0.1f) force -= (vF > 0 ? 1.f : -1.f) * p_.handbrakeForce;
    force -= p_.drag * vF * std::abs(vF);
    if (std::abs(vF) > 0.05f) force -= (vF > 0 ? 1.f : -1.f) * p_.rollResistance;
    // 坂：重力の斜面方向成分
    force -= p_.mass * kG * std::sin(s_.slope);
    const float accel = force / p_.mass;
    float vFn = vF + accel * dt;
    // ブレーキ・抵抗で速度の向きが反転しない（止まる）
    const bool driving = in.throttle > 0.05f || s_.reversing;
    if (!driving && ((vF > 0 && vFn < 0) || (vF < 0 && vFn > 0))) vFn = 0.f;
    if (in.handbrake && std::abs(vFn) < 0.3f && !driving) vFn = 0.f;
    vF = vFn;

    // ---- 旋回 ----
    // 自転車モデルのヨーレート（舵角は右が正、ヨーは左が正なので符号を反転）
    float yawTarget = -vF * std::tan(s_.steerAngle) / p_.wheelbase;
    const float aMax = p_.grip * kG;
    const float demand = std::abs(vF * yawTarget);
    float slide = 0.f;  // 進行方向を車体の向きに追従させない割合
    const bool drifting = in.handbrake && std::abs(vF) > 6.f;
    if (drifting) {
        // 後輪が流れる：向きは舵より大きく回り、進行方向は残る
        yawTarget *= 1.7f;
        slide = 0.82f;
    } else if (demand > aMax) {
        // グリップの限界：曲がり切れない（アンダーステア）。超えた分だけ少し滑る
        yawTarget *= aMax / demand;
        slide = std::clamp((demand - aMax) / (demand + 1e-3f), 0.f, 0.5f);
    }
    s_.yawRate = approach(s_.yawRate, yawTarget, drifting ? 3.0f : 9.0f, dt);
    s_.yaw += s_.yawRate * dt;

    // 速度：新しい向きに沿わせた速度と、元の速度（慣性）を slide で混ぜる
    const float2 oldVel = fwd * vF + rgt * vR;
    fwd = forward();
    rgt = right();
    const float2 turned = fwd * vF + rgt * vR;
    float2 vel = turned * (1.f - slide) + oldVel * slide;
    // 横滑りは路面の摩擦で減る（ドリフト中はゆっくり）
    float vRn = dot(vel, rgt);
    const float latDamp = drifting ? 1.1f : (slide > 0.f ? 4.0f : 10.f);
    vRn *= std::exp(-latDamp * dt);
    vel = fwd * dot(vel, fwd) + rgt * vRn;
    s_.velocity = vel;

    // 滑り（音・見た目）：横滑り、サイドブレーキ、急ブレーキ、発進時の空転
    const float latSlip = std::abs(vRn) / std::max(4.f, speed);
    float slipTarget = std::clamp(latSlip * 2.2f, 0.f, 1.f);
    if (in.handbrake && speed > 3.f) slipTarget = std::max(slipTarget, 0.7f);
    if (in.brake > 0.8f && vF > 12.f) slipTarget = std::max(slipTarget, 0.45f);
    if (in.throttle > 0.9f && std::abs(vF) < 6.f && !s_.reversing) slipTarget = std::max(slipTarget, 0.35f);
    s_.slip = approach(s_.slip, slipTarget, 6.f, dt);

    // ---- 移動・高さ ----
    s_.position.x += s_.velocity.x * dt;
    s_.position.z += s_.velocity.y * dt;
    bool onDeck = false;
    const float ground = world.surfaceHeight(s_.position.x, s_.position.z, s_.position.y, &onDeck);
    if (s_.position.y > ground + 0.05f) {
        // 宙に浮いた（段差から落ちた）：重力で落とす
        s_.verticalSpeed -= kG * dt;
        s_.position.y = std::max(ground, s_.position.y + s_.verticalSpeed * dt);
        if (s_.position.y <= ground) s_.verticalSpeed = 0.f;
    } else {
        s_.position.y = ground;
        s_.verticalSpeed = 0.f;
    }
    s_.onDeck = onDeck;
    // 路面の縦の傾き（前輪と後輪の位置の高さの差）
    {
        const float2 c{s_.position.x, s_.position.z};
        const float2 fp = c + fwd * (p_.wheelbase * 0.5f), rp = c - fwd * (p_.wheelbase * 0.5f);
        const float hf = world.surfaceHeight(fp.x, fp.y, s_.position.y + 0.3f);
        const float hr = world.surfaceHeight(rp.x, rp.y, s_.position.y + 0.3f);
        s_.slope = std::atan2(hf - hr, p_.wheelbase);
    }

    resolveCollisions(world);

    // ---- 見た目の傾き（ばね・ダンパ）：加速で尻が沈み、減速で頭が下がる。旋回で外へ傾く ----
    {
        const float latAccel = s_.yawRate * vF;
        const float pitchTarget = -s_.slope + std::clamp(accel, -12.f, 8.f) * -0.006f;
        const float rollTarget = std::clamp(latAccel, -12.f, 12.f) * 0.0065f;
        const float k = 90.f, c = 14.f;
        s_.pitchVel += ((pitchTarget - s_.pitch) * k - s_.pitchVel * c) * dt;
        s_.rollVel += ((rollTarget - s_.roll) * k - s_.rollVel * c) * dt;
        s_.pitch += s_.pitchVel * dt;
        s_.roll += s_.rollVel * dt;
    }

    s_.forwardSpeed = dot(s_.velocity, forward());
    s_.throttle = in.throttle;
    // ブレーキランプ：ブレーキ（後退中は除く）かサイドブレーキ
    s_.brake = (in.brake > 0.05f && !s_.reversing) ? in.brake : 0.f;
    if (in.handbrake) s_.brake = std::max(s_.brake, 0.6f);
    s_.impact = std::max(0.f, s_.impact - dt * 20.f);
    updateGearbox(dt);
}

void Vehicle::resolveCollisions(const CityCollision& world) {
    const float invMass = 1.f / p_.mass;
    const float inertia = p_.mass * ((2.f * p_.halfLength) * (2.f * p_.halfLength) + (2.f * p_.halfWidth) * (2.f * p_.halfWidth)) / 12.f;
    std::vector<Contact> contacts;
    for (int iter = 0; iter < 3; ++iter) {
        Circle cs[3];
        circles(cs);
        contacts.clear();
        for (const Circle& c : cs) world.collide(c, s_.position.y, p_.bodyHeight, contacts);
        if (contacts.empty()) return;

        // 押し出し：向きごとに最も深いものだけ使う（同じ壁を複数の円が拾っても押し過ぎない）
        float2 push{0};
        for (const Contact& k : contacts) {
            const float along = dot(push, k.normal);
            if (along < k.depth) push += k.normal * (k.depth - std::max(0.f, along));
        }
        s_.position.x += push.x;
        s_.position.z += push.y;

        // 速度の撃力
        const float2 center{s_.position.x, s_.position.z};
        for (const Contact& k : contacts) {
            const float2 r = k.point - center;
            const float2 vp = s_.velocity + float2{s_.yawRate * r.y, -s_.yawRate * r.x};
            const float2 rel = vp - k.otherVelocity;
            const float vn = dot(rel, k.normal);
            if (vn >= 0.f) continue;
            const float e = k.pedestrian ? 0.0f : (k.dynamic ? 0.3f : 0.12f);
            // 回転の効き：r×n（Y軸まわり）。τ = rz Jx − rx Jz
            const float rn = r.y * k.normal.x - r.x * k.normal.y;
            float j = -(1.f + e) * vn / (invMass + rn * rn / inertia);
            if (k.dynamic && !k.pedestrian) j *= 0.6f;  // 相手（交通）にも質量がある分、跳ね返りは弱い
            const float2 J = k.normal * j;
            s_.velocity += J * invMass;
            s_.yawRate += (r.y * J.x - r.x * J.y) / inertia * 0.6f;
            // 摩擦：壁をこすると減速
            const float2 tangent{-k.normal.y, k.normal.x};
            const float vt = dot(rel, tangent);
            const float jt = std::clamp(-vt / (invMass + 1e-6f), -0.35f * j, 0.35f * j);
            s_.velocity += tangent * (jt * invMass);
            s_.impact = std::max(s_.impact, -vn);
        }
    }
}

void Vehicle::updateGearbox(float dt) {
    static const float kRatio[] = {0.f, 3.3f, 2.15f, 1.55f, 1.18f, 0.94f, 0.78f};
    constexpr float kFinal = 3.7f, kWheelR = 0.33f, kIdle = 900.f, kRedline = 7200.f;
    const float v = std::abs(s_.forwardSpeed);
    if (s_.reversing || s_.forwardSpeed < -0.5f) {
        s_.gear = -1;
    } else {
        if (s_.gear < 1) s_.gear = 1;
        auto rpmAt = [&](int g) { return v / kWheelR * kRatio[g] * kFinal * 60.f / 6.2831853f; };
        // 変速点はアクセルの踏み込みで変える（流す時は早めに上げ、踏めば引っ張る）
        throttleAvg_ = throttleAvg_ + (s_.throttle - throttleAvg_) * std::min(1.f, dt * 2.f);
        const float up = 2600.f + throttleAvg_ * 4000.f;
        const float down = 1300.f + throttleAvg_ * 1900.f;
        shiftTimer_ = std::max(0.f, shiftTimer_ - dt);
        if (shiftTimer_ == 0.f) {
            if (s_.gear < 6 && rpmAt(s_.gear) > up) ++s_.gear, shiftTimer_ = 0.4f;
            else if (s_.gear > 1 && rpmAt(s_.gear) < down && rpmAt(s_.gear - 1) < up * 0.92f) --s_.gear, shiftTimer_ = 0.4f;
        }
    }
    const int g = s_.gear < 0 ? 1 : s_.gear;
    float rpm = v / kWheelR * kRatio[g] * kFinal * 60.f / 6.2831853f;
    // 低速ではクラッチが滑っている扱い：アクセルで吹け上がる
    const float slipRpm = kIdle + s_.throttle * 2600.f * (1.f - std::clamp(v / 8.f, 0.f, 1.f));
    rpm = std::max(rpm, slipRpm);
    rpm = std::min(rpm, kRedline);
    s_.rpm = approach(s_.rpm, rpm, 12.f, dt);
}

}  // namespace drive

// 追従カメラ。車の後ろ上から、ばね・ダンパで遅れて付いていく。速いほど画角を広げる。
// 右スティックで見回し（離すと戻る）、切り替えでボンネット視点。建物・高架にはめり込まない。
#pragma once

#include "drive/vehicle.h"

namespace drive {

struct CameraOutput {
    float3 eye{0};
    float3 forward{0, 0, 1};
    float fovDeg = 62.f;
    float focusDistance = 7.f;
};

class ChaseCamera {
public:
    enum class Mode { Chase, Bonnet };

    void reset(const VehicleState& car);
    // look は右スティック（-1..1）。dt は描画フレームの時間
    CameraOutput update(float dt, const VehicleState& car, const CityCollision& world, float2 look);
    void toggleMode() { mode_ = mode_ == Mode::Chase ? Mode::Bonnet : Mode::Chase; }
    Mode mode() const { return mode_; }

private:
    Mode mode_ = Mode::Chase;
    float3 eye_{0};
    float3 eyeVel_{0};
    float yaw_ = 0;         // カメラが見ている向き（車の向きに遅れて追う）
    float lookYaw_ = 0, lookPitch_ = 0;
    float fov_ = 62.f;
    float pull_ = 1.f;      // めり込み防止で縮めた距離の割合（戻る時はゆっくり）
    bool valid_ = false;
};

}  // namespace drive

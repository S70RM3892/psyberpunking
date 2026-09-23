// ドライブの入口。ホスト（Linux / Android）はこれだけを使う。
//
//   DriveApp app(platform);
//   app.init(nativeWindow, headless, &err);
//   app.load("deck", progress, &err);
//   while (...) app.frame(realDt, input);   // 物理は 240Hz 固定ステップ、描画は補間
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "bench/config.h"
#include "bench/platform.h"
#include "drive/chase_camera.h"
#include "drive/vehicle.h"

namespace drive {

struct DriveInput {
    VehicleInput car;
    float2 look{0};             // 右スティック
    bool toggleCamera = false;  // 押された瞬間だけ true
    bool reset = false;         // 押された瞬間だけ true（近くの道路に戻す）
    bool headlights = true;
};

// HUD と音に渡す値
struct DriveTelemetry {
    float speedKmh = 0;
    int gear = 1;
    float rpm = 900;
    float throttle = 0;
    float slip = 0;
    float impact = 0;          // 衝突の強さ（音用）
    bool onDeck = false;
    bool bonnetCamera = false;
    float3 position{0};
};

class DriveApp {
public:
    explicit DriveApp(bench::Platform& platform);
    ~DriveApp();

    DriveApp(const DriveApp&) = delete;
    DriveApp& operator=(const DriveApp&) = delete;

    bool init(void* nativeWindow, bool headless, std::string* error);
    void setNativeWindow(void* nativeWindow);
    bool load(const std::string& preset, const std::function<void(float)>& progress, std::string* error);

    // 1フレーム進めて描く。realDt は前フレームからの実時間[s]
    void frame(double realDt, const DriveInput& input);
    // 物理だけ進める（テスト・ヘッドレスのデモ用）
    void simulate(double realDt, const DriveInput& input);
    // 今の状態を描く（simulate の後の撮影用）
    bool render();
    bool readPixels(std::vector<uint8_t>& rgba);

    DriveTelemetry telemetry() const;
    const Vehicle& vehicle() const;
    const CityCollision& collision() const;
    const bench::CityData& city() const;
    const bench::SceneConfig& config() const { return config_; }
    // 出発点に戻す
    void respawn();

private:
    struct Impl;
    bench::Platform& platform_;
    bench::SceneConfig config_;
    std::unique_ptr<Impl> impl_;
};

}  // namespace drive

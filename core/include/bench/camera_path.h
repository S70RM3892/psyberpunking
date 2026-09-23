// 固定カメラパス（約60秒、地上 → 高架 → 路地）。キーフレームは30Hzでバイナリに焼き、実行時は補間するだけ。
//
// バイナリ形式（リトルエンディアン）:
//   char[4] "BDCP", uint32 version(=1), uint32 rateHz, uint32 count,
//   count × { float px, py, pz, fx, fy, fz, fovDeg }
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "bench/procgen.h"

namespace bench {

struct CameraKey {
    float3 position;
    float3 forward;  // 単位ベクトル
    float fovDeg = 60.f;  // 縦画角
};

struct CameraPath {
    int rateHz = 30;
    std::vector<CameraKey> keys;

    double duration() const { return keys.size() > 1 ? static_cast<double>(keys.size() - 1) / rateHz : 0.0; }
    CameraKey sample(double t) const;

    std::vector<uint8_t> serialize() const;
    // 失敗時は false（形式違い・切れたファイル）
    bool deserialize(const uint8_t* data, size_t size);
};

// 街の配置からカメラパスを作る（決定的）
CameraPath buildCameraPath(const SceneConfig& config, const CityData& city);

}  // namespace bench

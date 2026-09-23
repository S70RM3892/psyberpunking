// ホスト（Android / Linux）がコアに渡す窓口。ファイル読込とログだけ。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bench {

class Platform {
public:
    virtual ~Platform() = default;
    // データルートからの相対パス（"scenes/scene_v1.json", "materials/facade.filamat", "textures/asphalt_albedo_1k.ktx2" …）
    // 無ければ false（任意アセットは無くても動く）
    virtual bool readAsset(const std::string& path, std::vector<uint8_t>& out) = 0;
    virtual void log(const char* message) = 0;
};

}  // namespace bench

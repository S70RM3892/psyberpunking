// シーン定義JSON（scenes/scene_v1.json）の型。負荷のノブはすべてここに集め、コードに数値を直書きしない。
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bench {

struct Baseline {
    double avgFps = 0.0;   // Deck実機・Linux版・Deckプリセットの平均fps
    double low1Fps = 0.0;  // 同 1% low
};

struct ShadowPreset {
    int cascades = 3;
    int map = 1024;
    int localLights = 8;
    int localMap = 512;
    bool contact = true;
};

enum class Quality { Low, Medium, High };

struct SsaoPreset {
    bool enabled = true;
    bool halfRes = true;
    Quality quality = Quality::Low;
};

struct SsrPreset {
    bool enabled = true;
    bool halfRes = true;
    int steps = 24;
};

struct FogPreset {
    bool enabled = true;
    std::array<int, 3> grid{120, 75, 64};  // x, y = 密度格子（スライスのテッセレーション）, z = スライス数
};

struct Preset {
    std::string name;
    float renderScale = 0.67f;
    std::string upscaler = "fsr";
    ShadowPreset shadow;
    SsaoPreset ssao;
    SsrPreset ssr;
    FogPreset fog;
    float crowdScale = 1.0f;
    int textureMax = 1024;
    bool taa = true;
    bool bloom = true;
    bool dof = true;
    bool motionBlur = false;
};

struct Section {
    std::string name;
    double t0 = 0.0;
    double t1 = 0.0;
};

struct SceneConfig {
    std::string version;
    uint32_t seed = 2077;
    std::string targetDevice;
    double targetAvgFps = 40.0;
    std::optional<Baseline> baseline;

    int width = 1280;
    int height = 800;
    std::string backend = "vulkan";

    struct City {
        std::array<int, 2> blocks{10, 20};
        std::array<float, 2> blockSize{48.f, 36.f};
        float streetWidth = 18.f;
        float alleyWidth = 6.f;
        int buildings = 200;
        std::array<float, 3> lodDistances{60.f, 180.f, 500.f};
        std::array<float, 2> heightRange{18.f, 140.f};
        float overpassHeight = 11.f;
        float overpassWidth = 14.f;
    } city;

    struct Lights {
        int neonPoint = 200;
        int neonSpot = 56;
        int street = 40;
        float neonIntensity = 1600.f;
        float streetIntensity = 5200.f;
        float falloff = 14.f;
        float moonLux = 0.9f;
    } lights;

    struct Crowd {
        std::string model;
        int count = 60;
        int variants = 4;
        std::array<float, 2> walkSpeed{1.1f, 1.6f};
    } crowd;

    struct Vehicles {
        std::vector<std::string> models;
        int count = 30;
        std::array<float, 2> speed{7.f, 13.f};
    } vehicles;

    struct Particles {
        int rain = 16000;
        int steam = 4000;
        std::array<float, 3> rainBox{40.f, 24.f, 40.f};
    } particles;

    struct Fog {
        float density = 0.045f;
        float heightFalloff = 0.08f;
        float range = 70.f;
        float anisotropyHint = 0.35f;
    } fog;

    struct CameraPath {
        double duration = 60.0;
        int rateHz = 30;
        std::vector<Section> sections;
        std::string keys;
    } cameraPath;

    std::vector<Preset> presets;

    const Preset* findPreset(std::string_view name) const;
    int crowdCountFor(const Preset& p) const;
    // 時刻tが属する区間の添字（範囲外は最後の区間）
    int sectionAt(double t) const;
};

// JSON文字列から読む。例外は使わない（Filamentに合わせて -fno-exceptions）。
// 失敗時は std::nullopt を返し、error に「どのキーがなぜ不正か」を入れる。
std::optional<SceneConfig> parseSceneConfig(std::string_view json, std::string* error);

// ベースライン値だけを書き換えたJSONを返す（キャリブレーション確定時に使う）。失敗時は空文字列
std::string writeBaseline(std::string_view json, const Baseline& b);

}  // namespace bench

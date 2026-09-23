// コアの入口。ホストはこれだけを使う（シーン生成・描画・計測を一つにまとめる）。
//
//   BenchApp app(platform);
//   app.init(nativeWindow, w, h)            // 画面が無い計測・CIでは init(nullptr, w, h, headless=true)
//   app.load("deck", progress)              // 街の生成・GPUへの転送
//   app.start(options)                       // 計測開始（ウォームアップ → 3周）
//   while (app.frame(dt, thermal)) {}       // 毎フレーム
//   RunResult r = app.result(device)
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "bench/config.h"
#include "bench/measure.h"
#include "bench/platform.h"

namespace bench {

struct RenderStats {
    int lightsVisible = 0;
    int shadowedSpots = 0;
    size_t triangles = 0;   // 推定（CPUの可視判定。影パスを含む）
    size_t drawCalls = 0;   // 推定（同上）
    int renderables = 0;
    int texturesFromAssets = 0;  // CC0素材（KTX2）から読めたテクスチャの数（残りは実行時生成）
    bool iblFromAssets = false;  // 環境光が cmgen の KTX か（false なら手続き生成）
};

class BenchApp {
public:
    explicit BenchApp(Platform& platform);
    ~BenchApp();

    BenchApp(const BenchApp&) = delete;
    BenchApp& operator=(const BenchApp&) = delete;

    // 描画先。nativeWindow は ANativeWindow* / X11 Window / Wayland など Filament が受け付けるもの。
    // 描画は常に config の width×height（1280×800）。画面への拡大縮小はホスト側（Surfaceの固定サイズ）で行う
    bool init(void* nativeWindow, bool headless, std::string* error);
    // Android の Surface 作り直し。null で破棄
    void setNativeWindow(void* nativeWindow);

    const SceneConfig& config() const { return config_; }
    bool load(const std::string& preset, const std::function<void(float)>& progress, std::string* error);

    // 計測の開始。以後 frame() が計測を進める
    void start(const BenchSession::Options& options);
    bool running() const;
    BenchSession* session() { return session_.get(); }

    // 1フレーム描く。realDt は前フレームからの実時間[s]、hostCpuNs はホストが測ったCPU時間、
    // displayNs は表示間隔（Choreographer等、無ければ -1）。計測が終われば false
    bool frame(double realDt, int64_t hostCpuNs, int64_t displayNs, Thermal thermal, float headroom = -1.f);
    // 計測なしで時刻 t の絵を描く（スクリーンショット・画像差分用）
    bool renderAt(double sceneTime);
    // 最後に描いたフレームを RGBA8 で読み出す（headless の時だけ）
    bool readPixels(std::vector<uint8_t>& rgba);

    void abort(const std::string& reason);
    RunResult result(const DeviceInfo& device, const std::string& appVersion);

    RenderStats stats() const;

private:
    struct Impl;
    Platform& platform_;
    SceneConfig config_;
    std::unique_ptr<Impl> impl_;
    std::unique_ptr<BenchSession> session_;
};

}  // namespace bench

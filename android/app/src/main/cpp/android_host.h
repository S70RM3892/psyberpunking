// Android 側のホスト。描画ループは C++ の専用スレッドで回し、UIスレッドは止めない（仕様）。
// Kotlin からは状態を 100ms ごとに poll() するだけで、ネイティブ → Java の呼び出しはしない。
#pragma once

#include <android/asset_manager.h>
#include <android/native_window.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "bench/bench_app.h"

namespace benchandroid {

enum class HostPhase : int { Idle = 0, Loading = 1, Warmup = 2, Measure = 3, Done = 4, Aborted = 5, Error = 6 };

struct HostState {
    HostPhase phase = HostPhase::Idle;
    float progress = 0.f;  // 読込中は読込の進捗、計測中は全体の進捗
    int lap = 0;
    int section = 0;
    float fps = 0.f;       // 直近1秒の平均（表示用。計測値ではない）
    std::string message;   // エラーや中断の理由
};

class AssetPlatform : public bench::Platform {
public:
    explicit AssetPlatform(AAssetManager* am) : am_(am) {}
    bool readAsset(const std::string& path, std::vector<uint8_t>& out) override;
    void log(const char* message) override;

private:
    AAssetManager* am_;
};

class AndroidHost {
public:
    AndroidHost(AAssetManager* assets, std::string appVersion);
    ~AndroidHost();

    // UIスレッドから
    void setWindow(ANativeWindow* window);  // null で破棄（surfaceDestroyed）
    // lapSeconds <= 0 ならシーン定義の長さ（60秒）
    void start(const std::string& preset, int laps, bool warmup, double lapSeconds, const bench::DeviceInfo& device);
    void abort(const std::string& reason);
    void setThermal(int status);
    void setHeadroom(float headroom);
    void onVsync(int64_t frameTimeNanos);
    HostState poll();
    std::string resultJson();
    std::string resultCsv();

    // 画像差分用：オフスクリーンで固定カメラの絵を描いて RGBA8 を返す（計測とは排他。呼び出し元のスレッドで動く）
    bool renderShots(const std::vector<double>& times, std::vector<std::vector<uint8_t>>& out, std::string* error);

private:
    void loop();
    void setState(HostPhase phase, const std::string& message = {});

    AssetPlatform platform_;
    std::string appVersion_;

    std::mutex mutex_;
    std::condition_variable cv_;
    ANativeWindow* window_ = nullptr;
    bool windowChanged_ = false;
    bool quit_ = false;
    bool startRequested_ = false;
    std::string preset_;
    std::string loadedPreset_;
    bench::BenchSession::Options options_;
    double lapSeconds_ = 0;
    bench::DeviceInfo device_;
    std::string abortReason_;

    std::atomic<int> thermal_{0};
    std::atomic<float> headroom_{-1.f};
    std::atomic<int64_t> lastVsync_{0};
    std::atomic<int64_t> vsyncInterval_{-1};

    std::mutex stateMutex_;
    HostState state_;
    std::string resultJson_, resultCsv_;

    std::unique_ptr<bench::BenchApp> app_;
    std::thread thread_;
};

}  // namespace benchandroid

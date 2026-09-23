// ドライブの Android ホスト。描画と物理は C++ の専用スレッドで回し、UIスレッドは入力を渡して状態を読むだけ。
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

#include "bench/platform.h"
#include "drive/audio_synth.h"
#include "drive/drive_app.h"

struct AAudioStreamStruct;

namespace citydrive {

enum class Phase : int { Idle = 0, Loading = 1, Running = 2, Paused = 3, Error = 4 };

struct HostState {
    Phase phase = Phase::Idle;
    float progress = 0.f;
    drive::DriveTelemetry telemetry;
    float fps = 0.f;
    std::string message;
};

// UIスレッドから渡す入力。軸は最新の値、ボタンは押された回数（取りこぼさない）
struct PadState {
    float steer = 0, throttle = 0, brake = 0;
    float lookX = 0, lookY = 0;
    bool handbrake = false;
    bool headlights = true;
    int cameraPresses = 0;
    int resetPresses = 0;
};

class AssetPlatform : public bench::Platform {
public:
    explicit AssetPlatform(AAssetManager* am) : am_(am) {}
    bool readAsset(const std::string& path, std::vector<uint8_t>& out) override;
    void log(const char* message) override;

private:
    AAssetManager* am_;
};

class DriveHost {
public:
    explicit DriveHost(AAssetManager* assets);
    ~DriveHost();

    void setWindow(ANativeWindow* window);  // null で破棄
    void start(const std::string& preset);  // 読込んで走り出す（同じプリセットなら続きから）
    void setPaused(bool paused);
    void setPad(const PadState& pad);
    void setFrameInterval(double seconds);  // 目標のフレーム間隔（0 で制限なし）
    HostState poll();

private:
    void loop();
    void setPhase(Phase p, const std::string& message = {});
    // 音（AAudio）。描画スレッドから開く・止める。出力先が変わったら（イヤホン等）開き直す
    void openAudio();
    void closeAudio();
    void setAudioRunning(bool on);
    static int audioCallback(AAudioStreamStruct* stream, void* user, void* data, int32_t frames);
    static void audioError(AAudioStreamStruct* stream, void* user, int32_t error);

    AssetPlatform platform_;
    std::mutex mutex_;
    std::condition_variable cv_;
    ANativeWindow* window_ = nullptr;
    bool windowChanged_ = false;
    bool quit_ = false;
    bool startRequested_ = false;
    bool paused_ = false;
    std::string preset_, loadedPreset_;
    PadState pad_;
    int consumedCamera_ = 0, consumedReset_ = 0;
    std::atomic<double> frameInterval_{1.0 / 60.0};

    std::mutex stateMutex_;
    HostState state_;

    std::unique_ptr<drive::DriveApp> app_;
    std::thread thread_;

    std::unique_ptr<drive::AudioSynth> synth_;
    AAudioStreamStruct* audio_ = nullptr;
    bool audioRunning_ = false;
    std::atomic<bool> audioLost_{false};
};

}  // namespace citydrive

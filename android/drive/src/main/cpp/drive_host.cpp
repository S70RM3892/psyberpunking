#include "drive_host.h"

#include <aaudio/AAudio.h>
#include <android/log.h>

#include <chrono>

namespace citydrive {

using Clock = std::chrono::steady_clock;

bool AssetPlatform::readAsset(const std::string& path, std::vector<uint8_t>& out) {
    AAsset* a = AAssetManager_open(am_, path.c_str(), AASSET_MODE_BUFFER);
    if (!a) return false;
    const size_t n = static_cast<size_t>(AAsset_getLength(a));
    out.resize(n);
    const int read = AAsset_read(a, out.data(), n);
    AAsset_close(a);
    return read == static_cast<int>(n);
}

void AssetPlatform::log(const char* message) { __android_log_print(ANDROID_LOG_INFO, "CityDrive", "%s", message); }

DriveHost::DriveHost(AAssetManager* assets) : platform_(assets) {
    thread_ = std::thread([this] { loop(); });
}

DriveHost::~DriveHost() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        quit_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    if (window_) ANativeWindow_release(window_);
    window_ = nullptr;
}

void DriveHost::setWindow(ANativeWindow* window) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (window_) ANativeWindow_release(window_);
        window_ = window;
        if (window_) ANativeWindow_acquire(window_);
        windowChanged_ = true;
    }
    cv_.notify_all();
}

void DriveHost::start(const std::string& preset) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        preset_ = preset;
        startRequested_ = true;
        paused_ = false;
    }
    cv_.notify_all();
}

void DriveHost::setPaused(bool paused) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        paused_ = paused;
    }
    cv_.notify_all();
}

void DriveHost::setPad(const PadState& pad) {
    std::lock_guard<std::mutex> lock(mutex_);
    pad_ = pad;
}

void DriveHost::setFrameInterval(double seconds) { frameInterval_ = seconds; }

HostState DriveHost::poll() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return state_;
}

void DriveHost::setPhase(Phase p, const std::string& message) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    state_.phase = p;
    state_.message = message;
}

// ---- 音 ----------------------------------------------------------------------------------------

int DriveHost::audioCallback(AAudioStreamStruct*, void* user, void* data, int32_t frames) {
    auto* self = static_cast<DriveHost*>(user);
    if (self->synth_) self->synth_->render(static_cast<float*>(data), frames);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void DriveHost::audioError(AAudioStreamStruct*, void* user, int32_t error) {
    // コールバックの中では開き直さない（描画スレッドが拾って開き直す）
    if (error == AAUDIO_ERROR_DISCONNECTED) static_cast<DriveHost*>(user)->audioLost_ = true;
}

void DriveHost::openAudio() {
    closeAudio();
    AAudioStreamBuilder* b = nullptr;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return;
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setSampleRate(b, 48000);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setUsage(b, AAUDIO_USAGE_GAME);
    AAudioStreamBuilder_setContentType(b, AAUDIO_CONTENT_TYPE_SONIFICATION);
    AAudioStreamBuilder_setDataCallback(b, audioCallback, this);
    AAudioStreamBuilder_setErrorCallback(b, audioError, this);
    AAudioStream* stream = nullptr;
    const aaudio_result_t r = AAudioStreamBuilder_openStream(b, &stream);
    AAudioStreamBuilder_delete(b);
    if (r != AAUDIO_OK || !stream) {
        platform_.log("audio: could not open an AAudio stream (continuing without sound)");
        return;
    }
    synth_ = std::make_unique<drive::AudioSynth>(AAudioStream_getSampleRate(stream));
    audio_ = stream;
    audioRunning_ = false;
    audioLost_ = false;
}

void DriveHost::closeAudio() {
    if (!audio_) return;
    AAudioStream_requestStop(audio_);
    AAudioStream_close(audio_);
    audio_ = nullptr;
    audioRunning_ = false;
}

void DriveHost::setAudioRunning(bool on) {
    if (!audio_ || on == audioRunning_) return;
    if (on) AAudioStream_requestStart(audio_);
    else AAudioStream_requestPause(audio_);
    audioRunning_ = on;
}

void DriveHost::loop() {
    bool running = false;  // 読込済みで走っている（一時停止・Surface なしの間も状態は保つ）
    auto last = Clock::now();
    double fpsTime = 0;
    int fpsFrames = 0;
    for (;;) {
        std::string preset;
        ANativeWindow* window = nullptr;
        bool paused = false;
        PadState pad;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            // 走っていない・一時停止・Surface なし の間は眠る
            cv_.wait(lock, [&] {
                return quit_ || windowChanged_ || (startRequested_ && window_) || (running && !paused_ && window_);
            });
            if (quit_) break;
            if (windowChanged_) {
                windowChanged_ = false;
                if (app_) app_->setNativeWindow(window_);
            }
            window = window_;
            paused = paused_;
            pad = pad_;
            if (startRequested_ && window_) {
                startRequested_ = false;
                preset = preset_;
            }
        }

        // ---- 読込（初回・プリセット変更時） ----
        if (!preset.empty() && (!app_ || preset != loadedPreset_)) {
            setPhase(Phase::Loading);
            running = false;
            app_.reset();
            auto app = std::make_unique<drive::DriveApp>(platform_);
            std::string err;
            if (!app->init(window, false, &err) ||
                !app->load(preset, [this](float p) {
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    state_.progress = p;
                }, &err)) {
                setPhase(Phase::Error, err);
                continue;
            }
            app_ = std::move(app);
            loadedPreset_ = preset;
            last = Clock::now();
        }
        if (!preset.empty()) running = true;
        if (!running || !window) {
            setAudioRunning(false);
            continue;
        }
        if (paused) {
            setPhase(Phase::Paused);
            setAudioRunning(false);
            last = Clock::now();
            continue;
        }
        if (!audio_ || audioLost_) openAudio();
        setAudioRunning(true);

        // ---- 1フレーム ----
        const auto frameStart = Clock::now();
        const double dt = std::chrono::duration<double>(frameStart - last).count();
        last = frameStart;
        drive::DriveInput in;
        in.car.steer = pad.steer;
        in.car.throttle = pad.throttle;
        in.car.brake = pad.brake;
        in.car.handbrake = pad.handbrake;
        in.look = {pad.lookX, pad.lookY};
        in.headlights = pad.headlights;
        in.toggleCamera = pad.cameraPresses != consumedCamera_;
        in.reset = pad.resetPresses != consumedReset_;
        consumedCamera_ = pad.cameraPresses;
        consumedReset_ = pad.resetPresses;
        app_->frame(dt, in);
        if (synth_) {
            const drive::DriveTelemetry t = app_->telemetry();
            drive::AudioState a;
            a.rpm = t.rpm;
            a.throttle = t.throttle;
            a.speed = t.speedKmh / 3.6f;
            a.slip = t.slip;
            a.impact = t.impact;
            synth_->setState(a);
        }

        ++fpsFrames;
        fpsTime += dt;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            state_.phase = Phase::Running;
            state_.telemetry = app_->telemetry();
            if (fpsTime >= 0.5) {
                state_.fps = static_cast<float>(fpsFrames / fpsTime);
                fpsTime = 0;
                fpsFrames = 0;
            }
        }

        // 目標のフレーム間隔に揃える（120Hz の画面で 60fps を保つ。早く終わった分だけ待つ）
        const double interval = frameInterval_.load();
        if (interval > 0) {
            const auto target = frameStart + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(interval * 0.97));
            if (Clock::now() < target) std::this_thread::sleep_until(target);
        }
    }
    closeAudio();
    app_.reset();
}

}  // namespace citydrive

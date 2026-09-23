#include "android_host.h"

#include <android/log.h>

#include <chrono>

namespace benchandroid {

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

void AssetPlatform::log(const char* message) {
    __android_log_print(ANDROID_LOG_INFO, "BenchDeck", "%s", message);
}

AndroidHost::AndroidHost(AAssetManager* assets, std::string appVersion)
    : platform_(assets), appVersion_(std::move(appVersion)) {
    thread_ = std::thread([this] { loop(); });
}

AndroidHost::~AndroidHost() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        quit_ = true;
        abortReason_ = "interrupted";
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    if (window_) ANativeWindow_release(window_);
    window_ = nullptr;
}

void AndroidHost::setWindow(ANativeWindow* window) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (window_) ANativeWindow_release(window_);
        window_ = window;
        if (window_) ANativeWindow_acquire(window_);
        windowChanged_ = true;
    }
    cv_.notify_all();
}

void AndroidHost::start(const std::string& preset, int laps, bool warmup, double lapSeconds, const bench::DeviceInfo& device) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        preset_ = preset;
        options_.laps = laps;
        options_.warmup = warmup;
        lapSeconds_ = lapSeconds;
        device_ = device;
        abortReason_.clear();
        startRequested_ = true;
    }
    setState(HostPhase::Loading);
    cv_.notify_all();
}

void AndroidHost::abort(const std::string& reason) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (abortReason_.empty()) abortReason_ = reason;
        startRequested_ = false;
    }
    cv_.notify_all();
}

void AndroidHost::setThermal(int status) { thermal_ = status; }
void AndroidHost::setHeadroom(float headroom) { headroom_ = headroom; }

void AndroidHost::onVsync(int64_t frameTimeNanos) {
    int64_t last = lastVsync_.exchange(frameTimeNanos);
    if (last > 0 && frameTimeNanos > last) vsyncInterval_ = frameTimeNanos - last;
}

HostState AndroidHost::poll() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return state_;
}

std::string AndroidHost::resultJson() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return resultJson_;
}

std::string AndroidHost::resultCsv() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return resultCsv_;
}

void AndroidHost::setState(HostPhase phase, const std::string& message) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    state_.phase = phase;
    if (!message.empty() || phase == HostPhase::Loading) state_.message = message;
    if (phase == HostPhase::Loading) state_.progress = 0.f;
}

void AndroidHost::loop() {
    for (;;) {
        // ---- 開始の指示とウィンドウを待つ ----
        std::string preset;
        bench::BenchSession::Options opt;
        bench::DeviceInfo device;
        ANativeWindow* window = nullptr;
        double lapSeconds = 0;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [&] { return quit_ || (startRequested_ && window_) || windowChanged_; });
            if (quit_) break;
            if (windowChanged_) {
                windowChanged_ = false;
                // Surface の作り直し：Engine とシーンは保持して、スワップチェーンだけ作り直す
                if (app_) app_->setNativeWindow(window_);
            }
            if (!(startRequested_ && window_)) continue;
            startRequested_ = false;
            preset = preset_;
            opt = options_;
            device = device_;
            window = window_;
            lapSeconds = lapSeconds_;
        }

        // ---- 読込（プリセットが変わった時だけ作り直す） ----
        std::string err;
        if (!app_ || loadedPreset_ != preset) {
            app_.reset();
            loadedPreset_.clear();
            auto app = std::make_unique<bench::BenchApp>(platform_);
            if (!app->init(window, false, &err)) {
                resultJson_ = "{\"error\":\"engine_error\"}\n";
                setState(HostPhase::Error, "engine_error: " + err);
                continue;
            }
            bool ok = app->load(preset, [&](float p) {
                std::lock_guard<std::mutex> lock(stateMutex_);
                state_.progress = p;
            }, &err);
            if (!ok) {
                setState(HostPhase::Error, "asset_error: " + err);
                continue;
            }
            app_ = std::move(app);
            loadedPreset_ = preset;
        }

        // ---- 計測 ----
        opt.lapDuration = lapSeconds > 0 ? lapSeconds : app_->config().cameraPath.duration;
        app_->start(opt);
        lastVsync_ = 0;
        vsyncInterval_ = -1;
        auto last = Clock::now();
        auto fpsWindow = last;
        int fpsFrames = 0;
        int64_t lastCpuNs = -1;
        std::string abortReason;
        for (;;) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (quit_ || !abortReason_.empty() || !window_) {
                    abortReason = abortReason_.empty() ? "interrupted" : abortReason_;
                    break;
                }
                if (windowChanged_) {
                    // 計測中の Surface 変化は中断扱い（数値が信用できなくなるため）
                    abortReason = "interrupted";
                    break;
                }
            }
            auto now = Clock::now();
            const double dt = std::chrono::duration<double>(now - last).count();
            last = now;
            const auto c0 = Clock::now();
            bool more = app_->frame(dt, lastCpuNs, vsyncInterval_.load(), static_cast<bench::Thermal>(thermal_.load()),
                                    headroom_.load());
            lastCpuNs = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - c0).count();
            ++fpsFrames;

            auto* s = app_->session();
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                state_.phase = s->phase() == bench::BenchSession::Phase::Warmup ? HostPhase::Warmup : HostPhase::Measure;
                state_.progress = s->progress();
                state_.lap = s->lap();
                state_.section = s->section();
                double span = std::chrono::duration<double>(now - fpsWindow).count();
                if (span >= 1.0) {
                    state_.fps = static_cast<float>(fpsFrames / span);
                    fpsFrames = 0;
                    fpsWindow = now;
                }
            }
            if (!more) break;
        }
        if (!abortReason.empty()) app_->abort(abortReason);

        bench::RunResult r = app_->result(device, appVersion_);
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            resultJson_ = bench::resultToJson(r);
            resultCsv_ = bench::resultToCsv(r);
        }
        if (r.aborted) {
            setState(HostPhase::Aborted, r.abortReason);
        } else {
            setState(HostPhase::Done);
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            abortReason_.clear();
        }
    }
    app_.reset();
}

bool AndroidHost::renderShots(const std::vector<double>& times, std::vector<std::vector<uint8_t>>& out, std::string* error) {
    bench::BenchApp app(platform_);
    if (!app.init(nullptr, true, error)) return false;
    if (!app.load("deck", nullptr, error)) return false;
    for (double t : times) {
        for (int k = 0; k < 12; ++k) app.renderAt(t);
        std::vector<uint8_t> rgba;
        if (!app.readPixels(rgba)) {
            if (error) *error = "readPixels failed";
            return false;
        }
        for (size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;  // 描画結果のαは不定なので不透明にする
        out.push_back(std::move(rgba));
    }
    return true;
}

}  // namespace benchandroid

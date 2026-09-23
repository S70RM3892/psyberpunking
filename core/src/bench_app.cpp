#include "bench/bench_app.h"

#include <chrono>
#include <deque>

#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/Fence.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/SwapChain.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <backend/PixelBufferDescriptor.h>
#include <utils/EntityManager.h>

#include "bench/camera_path.h"
#include "bench/procgen.h"
#include "render/world.h"

namespace bench {

using namespace filament;

struct BenchApp::Impl {
    Engine* engine = nullptr;
    Renderer* renderer = nullptr;
    SwapChain* swapChain = nullptr;
    Scene* scene = nullptr;
    View* view = nullptr;
    Camera* camera = nullptr;
    utils::Entity cameraEntity;
    bool headless = false;
    uint32_t frameCounter = 0;  // Filament の frameId と同じ数え方（beginFrame ごとに +1）
    uint32_t renderedFrames = 0;

    CityData city;
    CameraPath path;
    Preset preset;
    render::World world;
    bool loaded = false;

    std::vector<uint8_t> pixels;

    // Filament は「1つ前のフレームのGPU処理が終わっていなければ beginFrame が false（フレームを捨てる）」。
    // 計測ループは表示同期で待たないので、そのままだと beginFrame を空回りして CPU を燃やす。
    // そこで beginFrame の直前に、前フレームのフェンスを待つ（次フレームのシーン更新は前フレームのGPU処理と並行）
    std::deque<Fence*> inFlight;
    void markSubmitted() { inFlight.push_back(engine->createFence()); }
    void waitPrevious() { drainFences(); }
    void drainFences() {
        for (Fence* f : inFlight) Fence::waitAndDestroy(f);
        inFlight.clear();
    }

    // 描画統計の平均（結果JSON用）
    double triSum = 0, drawSum = 0;
    int statFrames = 0;
    int lightsMax = 0;
};

BenchApp::BenchApp(Platform& platform) : platform_(platform), impl_(std::make_unique<Impl>()) {}

BenchApp::~BenchApp() {
    Impl& s = *impl_;
    if (!s.engine) return;
    s.drainFences();
    s.world.destroy(*s.engine);
    if (s.view) s.engine->destroy(s.view);
    if (s.scene) s.engine->destroy(s.scene);
    if (!s.cameraEntity.isNull()) {
        s.engine->destroyCameraComponent(s.cameraEntity);
        utils::EntityManager::get().destroy(s.cameraEntity);
    }
    if (s.swapChain) s.engine->destroy(s.swapChain);
    if (s.renderer) s.engine->destroy(s.renderer);
    Engine::destroy(&s.engine);
}

bool BenchApp::init(void* nativeWindow, bool headless, std::string* error) {
    // シーン定義
    std::vector<uint8_t> json;
    if (!platform_.readAsset("scenes/scene_v1.json", json)) {
        if (error) *error = "asset missing: scenes/scene_v1.json";
        return false;
    }
    auto cfg = parseSceneConfig(std::string_view(reinterpret_cast<const char*>(json.data()), json.size()), error);
    if (!cfg) return false;
    config_ = *cfg;

    Impl& s = *impl_;
    s.headless = headless;
    Engine::Config ec;
    // 数千ドローコール＋影パスを1フレームに積むので、コマンドバッファを広めにとる
    ec.commandBufferSizeMB = 24;
    ec.perRenderPassArenaSizeMB = 24;
    ec.minCommandBufferSizeMB = 8;
    ec.perFrameCommandsSizeMB = 8;
    ec.driverHandleArenaSizeMB = 16;
    s.engine = Engine::Builder().backend(Engine::Backend::VULKAN).config(&ec).build();
    if (!s.engine) {
        if (error) *error = "Filament Engine の作成に失敗（Vulkan 1.1 ドライバが必要）";
        return false;
    }
    s.renderer = s.engine->createRenderer();
    const uint32_t w = static_cast<uint32_t>(config_.width), h = static_cast<uint32_t>(config_.height);
    if (headless || !nativeWindow) {
        s.swapChain = s.engine->createSwapChain(w, h, SwapChain::CONFIG_READABLE);
    } else {
        s.swapChain = s.engine->createSwapChain(nativeWindow, 0);
    }
    s.scene = s.engine->createScene();
    s.view = s.engine->createView();
    s.cameraEntity = utils::EntityManager::get().create();
    s.camera = s.engine->createCamera(s.cameraEntity);
    s.view->setScene(s.scene);
    s.view->setCamera(s.camera);
    s.view->setViewport({0, 0, w, h});
    s.view->setName("bench");

    Renderer::ClearOptions clear;
    clear.clearColor = {0, 0, 0, 1};
    clear.clear = true;
    clear.discard = true;
    s.renderer->setClearOptions(clear);
    // 表示のペーシングはしない（スコアはGPU/CPU時間で取るので、表示同期は計測に入らない）
    Renderer::DisplayInfo di;
    di.refreshRate = 0.0f;
    s.renderer->setDisplayInfo(di);
    return true;
}

void BenchApp::setNativeWindow(void* nativeWindow) {
    Impl& s = *impl_;
    if (!s.engine || s.headless) return;
    if (s.swapChain) {
        s.drainFences();
        s.engine->destroy(s.swapChain);
        s.swapChain = nullptr;
        s.engine->flushAndWait();
    }
    if (nativeWindow) s.swapChain = s.engine->createSwapChain(nativeWindow, 0);
}

bool BenchApp::load(const std::string& presetName, const std::function<void(float)>& progress, std::string* error) {
    Impl& s = *impl_;
    const Preset* p = config_.findPreset(presetName);
    if (!p) {
        if (error) *error = "unknown preset: " + presetName;
        return false;
    }
    s.preset = *p;
    auto report = [&](float v) {
        if (progress) progress(v);
    };
    report(0.0f);
    s.city = generateCity(config_);
    report(0.1f);

    // カメラパス：焼いたキー（scenes/camera_path_v1.bin）を使う。無ければ同じ手順でその場で作る
    std::vector<uint8_t> keys;
    bool haveKeys = platform_.readAsset("scenes/" + config_.cameraPath.keys, keys) &&
                    s.path.deserialize(keys.data(), keys.size());
    if (!haveKeys) {
        platform_.log("camera path keys not found; building from the scene definition");
        s.path = buildCameraPath(config_, s.city);
    }

    render::World::BuildInput in;
    in.config = &config_;
    in.preset = &s.preset;
    in.city = &s.city;
    in.path = &s.path;
    in.platform = &platform_;
    if (!s.world.build(*s.engine, *s.scene, *s.view, *s.camera, in, [&](float v) { report(0.1f + 0.85f * v); }, error)) {
        return false;
    }
    // 全マテリアルの変種をここで作らせる：シェーダのコンパイル待ちは読込中に済ませる
    s.world.update(0.0, 0);
    for (int i = 0; i < 3; ++i) renderAt(i * 20.0 + 5.0);
    s.engine->flushAndWait();
    s.loaded = true;
    report(1.0f);
    return true;
}

void BenchApp::start(const BenchSession::Options& options) {
    session_ = std::make_unique<BenchSession>(config_, impl_->preset, options);
    impl_->triSum = impl_->drawSum = 0;
    impl_->statFrames = 0;
    impl_->lightsMax = 0;
}

bool BenchApp::running() const {
    return session_ && session_->phase() != BenchSession::Phase::Done;
}

bool BenchApp::frame(double realDt, int64_t hostCpuNs, int64_t displayNs, Thermal thermal, float headroom) {
    Impl& s = *impl_;
    if (!session_ || !s.loaded || !s.swapChain) return running();
    if (thermal >= Thermal::Severe) {
        session_->abort("thermal_stop");
        return false;
    }
    const double t = session_->advance(realDt);
    s.world.update(t, s.frameCounter);

    s.waitPrevious();
    ++s.frameCounter;
    if (s.renderer->beginFrame(s.swapChain)) {
        s.renderer->render(s.view);
        s.renderer->endFrame();
        s.markSubmitted();
        ++s.renderedFrames;
        session_->record(s.frameCounter, hostCpuNs, displayNs, thermal, headroom);
        if (session_->phase() == BenchSession::Phase::Measure) {
            RenderStats st = s.world.stats();
            s.triSum += static_cast<double>(st.triangles);
            s.drawSum += static_cast<double>(st.drawCalls);
            s.lightsMax = std::max(s.lightsMax, st.lightsVisible);
            ++s.statFrames;
        }
    }
    // 数フレーム前の FrameInfo（GPU時間）を回収
    auto history = s.renderer->getFrameInfoHistory(s.renderer->getMaxFrameHistorySize());
    for (const auto& fi : history) {
        if (fi.gpuFrameDuration > 0 || (fi.endFrame > 0 && fi.beginFrame > 0)) {
            int64_t cpu = (fi.endFrame > 0 && fi.beginFrame > 0) ? fi.endFrame - fi.beginFrame : -1;
            session_->resolve(fi.frameId, fi.gpuFrameDuration > 0 ? fi.gpuFrameDuration : -1, cpu);
        }
    }
    return running();
}

bool BenchApp::renderAt(double sceneTime) {
    Impl& s = *impl_;
    if (!s.swapChain) return false;
    s.world.update(sceneTime, s.frameCounter);
    s.waitPrevious();
    ++s.frameCounter;
    if (!s.renderer->beginFrame(s.swapChain)) return false;
    s.renderer->render(s.view);
    if (s.headless) {
        const uint32_t w = static_cast<uint32_t>(config_.width), h = static_cast<uint32_t>(config_.height);
        s.pixels.assign(static_cast<size_t>(w) * h * 4, 0);
        s.renderer->readPixels(0, 0, w, h,
                               backend::PixelBufferDescriptor(s.pixels.data(), s.pixels.size(), backend::PixelDataFormat::RGBA,
                                                              backend::PixelDataType::UBYTE));
    }
    s.renderer->endFrame();
    s.markSubmitted();
    return true;
}

bool BenchApp::readPixels(std::vector<uint8_t>& rgba) {
    Impl& s = *impl_;
    if (!s.headless || s.pixels.empty()) return false;
    s.engine->flushAndWait();
    // Vulkan の読み出しは上下が逆（原点が左上）なので、PNG の行順に合わせる
    rgba = s.pixels;
    return true;
}

void BenchApp::abort(const std::string& reason) {
    if (session_) session_->abort(reason);
}

RunResult BenchApp::result(const DeviceInfo& device, const std::string& appVersion) {
    RunResult r = session_ ? session_->finish(device, appVersion) : RunResult{};
    Impl& s = *impl_;
    if (s.statFrames > 0) {
        r.trianglesAvg = s.triSum / s.statFrames;
        r.drawCallsAvg = s.drawSum / s.statFrames;
    }
    r.lightsVisibleMax = s.lightsMax;
    RenderStats st = s.world.stats();
    r.texturesFromAssets = st.texturesFromAssets;
    r.iblFromAssets = st.iblFromAssets;
    return r;
}

RenderStats BenchApp::stats() const { return impl_->world.stats(); }

}  // namespace bench

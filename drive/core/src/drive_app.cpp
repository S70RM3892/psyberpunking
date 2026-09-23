#include "drive/drive_app.h"

#include <algorithm>
#include <cmath>
#include <deque>

#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/Fence.h>
#include <filament/Options.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/SwapChain.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <backend/PixelBufferDescriptor.h>
#include <utils/EntityManager.h>

#include "bench/procgen.h"
#include "bench/traffic.h"
#include "render/world.h"

namespace drive {

using namespace filament;

namespace {

constexpr double kStep = 1.0 / 240.0;   // 物理の固定ステップ
constexpr int kMaxSteps = 48;            // 1フレームで回す上限（0.2s 分。止まった後の追いつき過ぎを防ぐ）

}  // namespace

// Filament の準備は BenchApp と同じ手順（ベンチの計測コードには手を入れないため、あえて共有しない）
struct DriveApp::Impl {
    Engine* engine = nullptr;
    Renderer* renderer = nullptr;
    SwapChain* swapChain = nullptr;
    Scene* scene = nullptr;
    View* view = nullptr;
    Camera* camera = nullptr;
    utils::Entity cameraEntity;
    bool headless = false;
    uint32_t frameCounter = 0;

    bench::CityData city;
    bench::Preset preset;
    bench::render::World world;
    std::unique_ptr<CityCollision> collision;
    bool loaded = false;

    // 交通・歩行者（描画側と同じ引数で作るので、同じ時刻なら同じ位置）
    std::vector<bench::Vehicle> traffic;
    std::vector<bench::Walker> walkers;

    Vehicle car;
    VehicleState prevState;     // 補間用（1ステップ前）
    ChaseCamera camera3p;
    double time = 0;            // 街の時刻（交通・雨・看板）
    double accumulator = 0;
    float3 spawn{0};
    float spawnYaw = 0;
    bool headlights = true;
    float lastDt = 0;           // 直前の simulate の実時間（カメラのばねに使う）
    float2 lastLook{0};

    std::vector<uint8_t> pixels;
    std::deque<Fence*> inFlight;
    void waitPrevious() {
        for (Fence* f : inFlight) Fence::waitAndDestroy(f);
        inFlight.clear();
    }
    void markSubmitted() { inFlight.push_back(engine->createFence()); }

    void updateDynamicBodies() {
        std::vector<DynamicBody> bodies;
        bodies.reserve(traffic.size() + 16);
        const double dtv = 0.05;
        for (const auto& v : traffic) {
            bench::ActorPose p = bench::vehiclePose(city, v, time);
            bench::ActorPose q = bench::vehiclePose(city, v, time + dtv);
            DynamicBody b;
            b.center = {p.position.x, p.position.z};
            b.axis = {std::sin(p.yaw), std::cos(p.yaw)};
            b.halfLength = 1.35f;
            b.radius = 1.0f;
            b.y = p.position.y;
            b.height = 1.6f;
            float2 vel{(q.position.x - p.position.x) / static_cast<float>(dtv), (q.position.z - p.position.z) / static_cast<float>(dtv)};
            // 周回の継ぎ目で瞬間移動する時は速度にしない
            if (length(vel) > 40.f) vel = float2{0};
            b.velocity = vel;
            bodies.push_back(b);
        }
        // 歩道の上の人は縁石の向こうなので当たらない。車道（路地）を歩く人だけ入れる
        for (const auto& w : walkers) {
            bench::ActorPose p = bench::walkerPose(city, w, time);
            if (p.position.y > 0.1f) continue;
            const float2 c{p.position.x, p.position.z};
            const float3 carPos = car.state().position;
            if (std::abs(c.x - carPos.x) > 30.f || std::abs(c.y - carPos.z) > 30.f) continue;
            DynamicBody b;
            b.center = c;
            b.radius = 0.35f;
            b.y = p.position.y;
            b.height = 1.8f;
            b.pedestrian = true;
            bodies.push_back(b);
        }
        collision->setDynamicBodies(std::move(bodies));
    }

    VehicleState interpolated(float alpha) const {
        VehicleState s = car.state();
        const VehicleState& a = prevState;
        auto lerp = [&](float x, float y) { return x + (y - x) * alpha; };
        float dyaw = s.yaw - a.yaw;
        while (dyaw > 3.14159265f) dyaw -= 6.2831853f;
        while (dyaw < -3.14159265f) dyaw += 6.2831853f;
        s.position = a.position + (s.position - a.position) * alpha;
        s.yaw = a.yaw + dyaw * alpha;
        s.pitch = lerp(a.pitch, s.pitch);
        s.roll = lerp(a.roll, s.roll);
        return s;
    }
};

DriveApp::DriveApp(bench::Platform& platform) : platform_(platform), impl_(std::make_unique<Impl>()) {}

DriveApp::~DriveApp() {
    Impl& s = *impl_;
    if (!s.engine) return;
    s.waitPrevious();
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

bool DriveApp::init(void* nativeWindow, bool headless, std::string* error) {
    std::vector<uint8_t> json;
    if (!platform_.readAsset("scenes/scene_v1.json", json)) {
        if (error) *error = "asset missing: scenes/scene_v1.json";
        return false;
    }
    auto cfg = bench::parseSceneConfig(std::string_view(reinterpret_cast<const char*>(json.data()), json.size()), error);
    if (!cfg) return false;
    config_ = *cfg;

    Impl& s = *impl_;
    s.headless = headless;
    Engine::Config ec;
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
    if (headless || !nativeWindow) s.swapChain = s.engine->createSwapChain(w, h, SwapChain::CONFIG_READABLE);
    else s.swapChain = s.engine->createSwapChain(nativeWindow, 0);
    s.scene = s.engine->createScene();
    s.view = s.engine->createView();
    s.cameraEntity = utils::EntityManager::get().create();
    s.camera = s.engine->createCamera(s.cameraEntity);
    s.view->setScene(s.scene);
    s.view->setCamera(s.camera);
    s.view->setViewport({0, 0, w, h});
    s.view->setName("drive");

    Renderer::ClearOptions clear;
    clear.clearColor = {0, 0, 0, 1};
    clear.clear = true;
    clear.discard = true;
    s.renderer->setClearOptions(clear);
    return true;
}

void DriveApp::setNativeWindow(void* nativeWindow) {
    Impl& s = *impl_;
    if (!s.engine || s.headless) return;
    if (s.swapChain) {
        s.waitPrevious();
        s.engine->destroy(s.swapChain);
        s.swapChain = nullptr;
        s.engine->flushAndWait();
    }
    if (nativeWindow) s.swapChain = s.engine->createSwapChain(nativeWindow, 0);
}

bool DriveApp::load(const std::string& presetName, const std::function<void(float)>& progress, std::string* error) {
    Impl& s = *impl_;
    const bench::Preset* p = config_.findPreset(presetName);
    if (!p) {
        if (error) *error = "unknown preset: " + presetName;
        return false;
    }
    s.preset = *p;
    auto report = [&](float v) {
        if (progress) progress(v);
    };
    report(0.f);
    s.city = bench::generateCity(config_);
    // 路地は車道と歩く所が同じ高さで、人が車の行く手をふさぐ。ドライブでは人は段差のある歩道だけを歩かせる
    std::erase_if(s.city.sidewalks, [](const bench::Lane& l) { return l.a.y < 0.05f && l.b.y < 0.05f; });
    s.collision = std::make_unique<CityCollision>(s.city);
    report(0.1f);

    bench::render::World::BuildInput in;
    in.config = &config_;
    in.preset = &s.preset;
    in.city = &s.city;
    in.path = nullptr;
    in.platform = &platform_;
    in.player = true;
    in.playerModel = 2;
    if (!s.world.build(*s.engine, *s.scene, *s.view, *s.camera, in, [&](float v) { report(0.1f + 0.8f * v); }, error)) {
        return false;
    }
    // ドライブは被写界深度を切る（前方の道がぼけると走りにくい）
    {
        DepthOfFieldOptions dof = s.view->getDepthOfFieldOptions();
        dof.enabled = false;
        s.view->setDepthOfFieldOptions(dof);
    }
    s.traffic = bench::makeVehicles(s.city, config_.vehicles.count, config_.seed, 3);
    s.walkers = bench::makeWalkers(s.city, config_.crowdCountFor(s.preset), config_.seed, config_.crowd.variants);

    // 出発点：大通りの南端、中央分離帯と交通の車線の間を北向き
    s.spawn = float3{s.city.boulevardX - 3.2f, 0.f, s.city.startZ};
    s.spawnYaw = 0.f;
    respawn();

    // 変種（シェーダ）を読込中に作らせる：数か所から描いておく
    for (int i = 0; i < 3; ++i) {
        s.time = i * 7.0;
        render();
    }
    s.time = 0;
    s.engine->flushAndWait();
    s.loaded = true;
    report(1.f);
    return true;
}

void DriveApp::respawn() {
    Impl& s = *impl_;
    s.car.reset(s.spawn, s.spawnYaw);
    s.prevState = s.car.state();
    s.camera3p.reset(s.car.state());
    s.accumulator = 0;
}

void DriveApp::simulate(double realDt, const DriveInput& input) {
    Impl& s = *impl_;
    if (!s.collision) return;
    realDt = std::clamp(realDt, 0.0, 0.25);
    s.time += realDt;
    s.headlights = input.headlights;
    if (input.toggleCamera) s.camera3p.toggleMode();
    if (input.reset) {
        float3 pos;
        float yaw = s.car.state().yaw;
        s.collision->nearestRoadPose(s.car.state().position, &pos, &yaw);
        s.car.reset(pos, yaw);
        s.prevState = s.car.state();
        s.camera3p.reset(s.car.state());
    }
    s.updateDynamicBodies();
    s.accumulator += realDt;
    int n = 0;
    while (s.accumulator >= kStep && n < kMaxSteps) {
        s.prevState = s.car.state();
        s.car.step(static_cast<float>(kStep), input.car, *s.collision);
        s.accumulator -= kStep;
        ++n;
    }
    if (n == kMaxSteps) s.accumulator = 0;
    s.lastDt = static_cast<float>(realDt);
    s.lastLook = input.look;
}

bool DriveApp::render() {
    Impl& s = *impl_;
    if (!s.swapChain) return false;
    const float alpha = static_cast<float>(std::clamp(s.accumulator / kStep, 0.0, 1.0));
    const VehicleState vs = s.interpolated(alpha);

    // カメラは補間した車の姿勢から（物理のステップ境界でカクつかない）
    const CameraOutput co = s.camera3p.update(s.lastDt, vs, *s.collision, s.lastLook);
    s.lastDt = 0.f;  // 同じ状態をもう一度描く時（撮影）はカメラを動かさない

    bench::render::ViewPose pose;
    pose.eye = co.eye;
    pose.forward = co.forward;
    pose.fovDeg = co.fovDeg;
    pose.focusDistance = co.focusDistance;
    s.world.update(s.time, s.frameCounter, pose);

    bench::render::PlayerCarState pc;
    pc.transform = vehicleTransform(vs);
    pc.brake = vs.brake;
    pc.headlights = s.headlights;
    s.world.setPlayerCar(pc);

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

void DriveApp::frame(double realDt, const DriveInput& input) {
    simulate(realDt, input);
    render();
}

bool DriveApp::readPixels(std::vector<uint8_t>& rgba) {
    Impl& s = *impl_;
    if (!s.headless || s.pixels.empty()) return false;
    s.engine->flushAndWait();
    rgba = s.pixels;
    return true;
}

DriveTelemetry DriveApp::telemetry() const {
    const Impl& s = *impl_;
    const VehicleState& v = s.car.state();
    DriveTelemetry t;
    t.speedKmh = std::abs(v.forwardSpeed) * 3.6f;
    t.gear = v.gear;
    t.rpm = v.rpm;
    t.throttle = v.throttle;
    t.slip = v.slip;
    t.impact = v.impact;
    t.onDeck = v.onDeck;
    t.bonnetCamera = s.camera3p.mode() == ChaseCamera::Mode::Bonnet;
    t.position = v.position;
    return t;
}

const Vehicle& DriveApp::vehicle() const { return impl_->car; }
const CityCollision& DriveApp::collision() const { return *impl_->collision; }
const bench::CityData& DriveApp::city() const { return impl_->city; }

}  // namespace drive

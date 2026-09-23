// ドライブの Linux 版（開発・Steam Deck 用）。ゲームパッド（SDL GameController）とキーボードで走る。
//
//   citydrive                          # ウィンドウ 1280×800、Deck プリセット
//   citydrive --preset high --fullscreen
//   citydrive --headless --demo 40 --shots out   # 自動運転で40秒走り、途中を撮影（CI・見た目の確認）
//
// ゲームパッド：左スティック=ハンドル、RT=アクセル、LT=ブレーキ/後退、A=サイドブレーキ、
//              Y=視点切替、B=道路に戻す、右スティック=見回し、BACK=ライト、START=終了
// キーボード：W/↑ アクセル、S/↓ ブレーキ、A/D/←/→ ハンドル、Space サイドブレーキ、C 視点、R 戻す、H ライト、Esc 終了
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "drive/autopilot.h"
#include "drive/drive_app.h"
#include "png_writer.h"

#include <SDL.h>
#include <SDL_syswm.h>
#undef None
#undef Status
#undef Bool

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

class FilePlatform : public bench::Platform {
public:
    explicit FilePlatform(std::vector<fs::path> roots) : roots_(std::move(roots)) {}
    bool readAsset(const std::string& path, std::vector<uint8_t>& out) override {
        for (const auto& r : roots_) {
            std::ifstream f(r / path, std::ios::binary);
            if (!f) continue;
            out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
            return true;
        }
        return false;
    }
    void log(const char* m) override { std::fprintf(stderr, "[drive] %s\n", m); }

private:
    std::vector<fs::path> roots_;
};

// スティックの遊びと、真ん中付近を細かく操作できるカーブ
float shapeAxis(Sint16 raw, float deadzone = 0.12f) {
    float v = static_cast<float>(raw) / 32767.f;
    const float a = std::abs(v);
    if (a < deadzone) return 0.f;
    const float n = (a - deadzone) / (1.f - deadzone);
    return std::copysign(n * (0.35f + 0.65f * n), v);
}

float shapeTrigger(Sint16 raw) { return std::clamp(static_cast<float>(raw) / 32767.f, 0.f, 1.f); }

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);  // CI のログに逐次出す
    std::string preset = "deck", shotsDir;
    bool headless = false, fullscreen = false;
    double demoSeconds = 0;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--preset") preset = next();
        else if (a == "--headless") headless = true;
        else if (a == "--fullscreen") fullscreen = true;
        else if (a == "--demo") demoSeconds = std::stod(next());
        else if (a == "--shots") shotsDir = next();
        else {
            std::fprintf(stderr, "usage: citydrive [--preset P] [--fullscreen] [--headless --demo SECONDS [--shots DIR]]\n");
            return 2;
        }
    }
    if (headless && demoSeconds <= 0) demoSeconds = 30;

    fs::path exe = fs::canonical("/proc/self/exe").parent_path();
    FilePlatform platform({exe / "data", exe, fs::path(BENCH_SOURCE_DIR), fs::path(BENCH_SOURCE_DIR) / "assets"});

    SDL_Window* window = nullptr;
    void* nativeWindow = nullptr;
    if (!headless) {
        SDL_SetHint(SDL_HINT_VIDEODRIVER, "x11");
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) {
            std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            return 1;
        }
        Uint32 flags = SDL_WINDOW_SHOWN | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        window = SDL_CreateWindow("Drive", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 800, flags);
        SDL_SysWMinfo wmi;
        SDL_VERSION(&wmi.version);
        if (!window || !SDL_GetWindowWMInfo(window, &wmi) || wmi.subsystem != SDL_SYSWM_X11) {
            std::fprintf(stderr, "no X11 window: %s\n", SDL_GetError());
            return 1;
        }
        nativeWindow = reinterpret_cast<void*>(wmi.info.x11.window);
    }

    drive::DriveApp app(platform);
    std::string err;
    if (!app.init(nativeWindow, headless, &err)) {
        std::fprintf(stderr, "init failed: %s\n", err.c_str());
        return 1;
    }
    auto t0 = Clock::now();
    if (!app.load(preset, [](float p) { std::fprintf(stderr, "\rloading %3d%%", static_cast<int>(p * 100)); }, &err)) {
        std::fprintf(stderr, "\nload failed: %s\n", err.c_str());
        return 1;
    }
    std::fprintf(stderr, "\rloaded in %.1f s\n", std::chrono::duration<double>(Clock::now() - t0).count());

    // ---- ヘッドレス：自動運転で走り、撮影 ----
    if (headless) {
        std::vector<drive::float3> route = app.city().pathPoints;
        // 路地の先まで延ばす
        route.push_back(route.back() + drive::float3{40.f, 0, 0});
        drive::Autopilot pilot(route, 16.f);
        const double dt = 1.0 / 30.0;
        const double shotEvery = shotsDir.empty() ? 1e9 : demoSeconds / 5.0;
        double nextShot = shotEvery * 0.5;
        int shot = 0;
        if (!shotsDir.empty()) fs::create_directories(shotsDir);
        std::vector<uint8_t> rgba;
        for (double t = 0; t < demoSeconds && !pilot.finished(); t += dt) {
            drive::DriveInput in;
            in.car = pilot.drive(app.vehicle().state(), app.vehicle().params().wheelbase);
            app.simulate(dt, in);
            if (t >= nextShot) {
                nextShot += shotEvery;
                // カメラのばねを落ち着かせてから撮る（同じ状態を数フレーム）
                for (int k = 0; k < 6; ++k) app.render();
                if (app.readPixels(rgba)) {
                    char name[64];
                    std::snprintf(name, sizeof(name), "drive_%d_t%04.1f.png", shot++, t);
                    benchlinux::writePng((fs::path(shotsDir) / name).string(), rgba.data(), 1280, 800, false);
                }
            }
            if (static_cast<int>(t * 30) % 60 == 0) {
                auto tm = app.telemetry();
                std::printf("t=%5.1f  pos=(%6.1f %5.1f %6.1f)  %5.1f km/h  gear %d  rpm %4.0f  wp %zu%s\n", t, tm.position.x,
                            tm.position.y, tm.position.z, tm.speedKmh, tm.gear, tm.rpm, pilot.index(), tm.onDeck ? "  [deck]" : "");
            }
        }
        auto tm = app.telemetry();
        std::printf("demo end: pos=(%.1f %.1f %.1f) finished=%d\n", tm.position.x, tm.position.y, tm.position.z,
                    pilot.finished() ? 1 : 0);
        return 0;
    }

    // ---- 対話：ゲームパッドとキーボード ----
    SDL_GameController* pad = nullptr;
    auto openPad = [&] {
        for (int i = 0; i < SDL_NumJoysticks() && !pad; ++i) {
            if (SDL_IsGameController(i)) pad = SDL_GameControllerOpen(i);
        }
        if (pad) std::fprintf(stderr, "gamepad: %s\n", SDL_GameControllerName(pad));
    };
    openPad();

    bool quit = false, headlights = true;
    float kbSteer = 0.f;
    auto last = Clock::now();
    double titleTimer = 0;
    int frames = 0;
    while (!quit) {
        drive::DriveInput in;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit = true;
            if (e.type == SDL_CONTROLLERDEVICEADDED && !pad) openPad();
            if (e.type == SDL_CONTROLLERDEVICEREMOVED && pad &&
                e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))) {
                SDL_GameControllerClose(pad);
                pad = nullptr;
            }
            if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                switch (e.key.keysym.sym) {
                    case SDLK_ESCAPE: quit = true; break;
                    case SDLK_c: in.toggleCamera = true; break;
                    case SDLK_r: in.reset = true; break;
                    case SDLK_h: headlights = !headlights; break;
                    default: break;
                }
            }
            if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                switch (e.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_Y: in.toggleCamera = true; break;
                    case SDL_CONTROLLER_BUTTON_B: in.reset = true; break;
                    case SDL_CONTROLLER_BUTTON_BACK: headlights = !headlights; break;
                    case SDL_CONTROLLER_BUTTON_START: quit = true; break;
                    default: break;
                }
            }
        }
        auto now = Clock::now();
        double dt = std::chrono::duration<double>(now - last).count();
        last = now;

        // キーボード：ハンドルは押している間じわっと切れる
        const Uint8* k = SDL_GetKeyboardState(nullptr);
        const bool left = k[SDL_SCANCODE_A] || k[SDL_SCANCODE_LEFT], right = k[SDL_SCANCODE_D] || k[SDL_SCANCODE_RIGHT];
        const float want = (right ? 1.f : 0.f) - (left ? 1.f : 0.f);
        kbSteer += (want - kbSteer) * std::min(1.f, static_cast<float>(dt) * (want == 0.f ? 8.f : 4.f));
        in.car.steer = kbSteer;
        in.car.throttle = (k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP]) ? 1.f : 0.f;
        in.car.brake = (k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN]) ? 1.f : 0.f;
        in.car.handbrake = k[SDL_SCANCODE_SPACE] != 0;
        if (pad) {
            const float steer = shapeAxis(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX));
            if (std::abs(steer) > 0.f) in.car.steer = steer;
            in.car.throttle = std::max(in.car.throttle, shapeTrigger(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT)));
            in.car.brake = std::max(in.car.brake, shapeTrigger(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT)));
            in.car.handbrake = in.car.handbrake || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A);
            in.look = {shapeAxis(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX), 0.2f),
                       shapeAxis(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY), 0.2f)};
        }
        in.headlights = headlights;
        app.frame(dt, in);

        ++frames;
        titleTimer += dt;
        if (titleTimer > 0.25) {
            auto tm = app.telemetry();
            char title[160];
            std::snprintf(title, sizeof(title), "Drive — %3.0f km/h  %s  %4.0f rpm  %.0f fps%s", tm.speedKmh,
                          tm.gear < 0 ? "R" : std::to_string(tm.gear).c_str(), tm.rpm, frames / titleTimer,
                          pad ? "" : "  (no gamepad: keyboard)");
            SDL_SetWindowTitle(window, title);
            titleTimer = 0;
            frames = 0;
        }
    }
    if (pad) SDL_GameControllerClose(pad);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

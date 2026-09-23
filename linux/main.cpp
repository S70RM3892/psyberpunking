// BenchDeck Linux版。Steam Deck（SteamOSデスクトップモード）での基準取りと、PCでの計測に使う。
//
//   benchdeck                                  # Deckプリセットで ウォームアップ1周 + 3周 を計測（ウィンドウ）
//   benchdeck --preset high --fullscreen
//   benchdeck --headless --laps 1 --no-warmup --lap-seconds 10   # CI の煙テスト
//   benchdeck --headless --shots out/shots     # 固定カメラ5点のスクリーンショット（画像差分用）
//   benchdeck --write-baseline scenes/scene_v1.json   # キャリブレーション確定：この計測値を baseline に書く
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "bench/bench_app.h"
#include "bench/vulkan_info.h"
#include "png_writer.h"

// SDL_syswm は X11 のヘッダを取り込み、None などのマクロを定義するので最後に入れて消す
#include <SDL.h>
#include <SDL_syswm.h>
#undef None
#undef Status
#undef Bool

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

constexpr const char* kAppVersion = BENCH_APP_VERSION;
// 画像差分に使う固定カメラの5点（各区間の見どころ）
constexpr double kShotTimes[] = {4.0, 14.0, 27.0, 44.0, 56.0};

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
    void log(const char* m) override { std::fprintf(stderr, "[benchdeck] %s\n", m); }

private:
    std::vector<fs::path> roots_;
};

std::string readText(const fs::path& p) {
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ' || s.back() == '"')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '"')) ++i;
    return s.substr(i);
}

bench::DeviceInfo deviceInfo(double refreshHz) {
    bench::DeviceInfo d;
    d.host = "linux";
    std::string product = trim(readText("/sys/devices/virtual/dmi/id/product_name"));
    std::string vendor = trim(readText("/sys/devices/virtual/dmi/id/sys_vendor"));
    // Steam Deck は LCD が "Jupiter"、OLED が "Galileo"
    if (product == "Jupiter") d.device = "Steam Deck LCD (Linux)";
    else if (product == "Galileo") d.device = "Steam Deck OLED (Linux)";
    else d.device = (vendor.empty() ? "" : vendor + " ") + (product.empty() ? "PC" : product) + " (Linux)";
    std::istringstream os(readText("/etc/os-release"));
    for (std::string line; std::getline(os, line);) {
        if (line.rfind("PRETTY_NAME=", 0) == 0) d.os = trim(line.substr(12));
    }
    bench::VulkanInfo vk = bench::queryVulkanInfo();
    d.gpu = vk.deviceName;
    d.driver = vk.driverVersion;
    d.vulkanVersion = vk.apiVersion;
    d.refreshHz = refreshHz;
    return d;
}

void usage() {
    std::puts(
        "usage: benchdeck [options]\n"
        "  --preset <low|deck|medium|high>  (default deck)\n"
        "  --laps N                          measured laps (default 3)\n"
        "  --no-warmup                       skip the warm-up lap\n"
        "  --lap-seconds S                   lap length in seconds (default: scene duration, 60)\n"
        "  --headless                        render offscreen (no window)\n"
        "  --fullscreen                      fullscreen window (Steam Deck)\n"
        "  --out DIR                         results directory (default ./results)\n"
        "  --shots DIR                       render the 5 fixed camera shots to DIR and exit\n"
        "  --shot T                          render one shot at time T (seconds) (with --shots)\n"
        "  --data DIR                        data root (default: next to the binary, then the source tree)\n"
        "  --write-baseline FILE             write this run's avg/1% low into FILE's baseline (calibration)\n");
}

}  // namespace

int main(int argc, char** argv) {
    std::string preset = "deck", outDir = "results", shotsDir, baselineFile;
    std::vector<double> shotTimes;
    std::vector<fs::path> roots;
    bool headless = false, fullscreen = false;
    bench::BenchSession::Options opt;
    double lapSeconds = 0;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                usage();
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--preset") preset = next();
        else if (a == "--laps") opt.laps = std::stoi(next());
        else if (a == "--no-warmup") opt.warmup = false;
        else if (a == "--lap-seconds") lapSeconds = std::stod(next());
        else if (a == "--headless") headless = true;
        else if (a == "--fullscreen") fullscreen = true;
        else if (a == "--out") outDir = next();
        else if (a == "--shots") shotsDir = next();
        else if (a == "--shot") shotTimes.push_back(std::stod(next()));
        else if (a == "--data") roots.emplace_back(next());
        else if (a == "--write-baseline") baselineFile = next();
        else if (a == "-h" || a == "--help") {
            usage();
            return 0;
        } else {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            usage();
            return 2;
        }
    }
    // データの置き場所：バイナリの隣の data/（配布物）→ ビルドディレクトリ → ソースツリー
    fs::path exe = fs::canonical("/proc/self/exe").parent_path();
    roots.push_back(exe / "data");
    roots.push_back(exe);
    roots.push_back(fs::path(BENCH_SOURCE_DIR));
    roots.push_back(fs::path(BENCH_SOURCE_DIR) / "assets");
    FilePlatform platform(roots);

    SDL_Window* window = nullptr;
    void* nativeWindow = nullptr;
    double refreshHz = 0;
    if (!shotsDir.empty()) headless = true;
    if (!headless) {
        SDL_SetHint(SDL_HINT_VIDEODRIVER, "x11");  // Filament の Linux/Vulkan は X11 のウィンドウを受け取る
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
            std::fprintf(stderr, "SDL_Init failed: %s (use --headless on machines without a display)\n", SDL_GetError());
            return 1;
        }
        Uint32 flags = SDL_WINDOW_SHOWN | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        // 描画は常に 1280×800。フルスクリーンでは画面へ拡大する（Deck の画面はちょうど 1280×800）
        window = SDL_CreateWindow("BenchDeck", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 800, flags);
        SDL_SysWMinfo wmi;
        SDL_VERSION(&wmi.version);
        if (!window || !SDL_GetWindowWMInfo(window, &wmi) || wmi.subsystem != SDL_SYSWM_X11) {
            std::fprintf(stderr, "no X11 window: %s\n", SDL_GetError());
            return 1;
        }
        nativeWindow = reinterpret_cast<void*>(wmi.info.x11.window);
        SDL_DisplayMode mode;
        if (SDL_GetCurrentDisplayMode(0, &mode) == 0) refreshHz = mode.refresh_rate;
    }

    bench::BenchApp app(platform);
    std::string err;
    if (!app.init(nativeWindow, headless, &err)) {
        std::fprintf(stderr, "init failed: %s\n", err.c_str());
        return 1;
    }
    auto t0 = Clock::now();
    int lastPct = -1;
    if (!app.load(preset, [&](float p) {
            int pct = static_cast<int>(p * 100);
            if (pct / 10 != lastPct / 10) std::fprintf(stderr, "\rloading %3d%%", pct);
            lastPct = pct;
        }, &err)) {
        std::fprintf(stderr, "\nload failed: %s\n", err.c_str());
        return 1;
    }
    std::fprintf(stderr, "\rloaded preset '%s' in %.1f s\n", preset.c_str(),
                 std::chrono::duration<double>(Clock::now() - t0).count());

    // ---- スクリーンショット ----
    if (!shotsDir.empty()) {
        fs::create_directories(shotsDir);
        if (shotTimes.empty()) shotTimes.assign(std::begin(kShotTimes), std::end(kShotTimes));
        std::vector<uint8_t> rgba;
        int idx = 0;
        for (double t : shotTimes) {
            // TAA の履歴を落ち着かせるため、同じ時刻を数フレーム描いてから読む
            for (int k = 0; k < 12; ++k) app.renderAt(t);
            if (!app.readPixels(rgba)) {
                std::fprintf(stderr, "readPixels failed\n");
                return 1;
            }
            char name[64];
            std::snprintf(name, sizeof(name), "shot_%d_t%05.1f.png", idx++, t);
            fs::path p = fs::path(shotsDir) / name;
            benchlinux::writePng(p.string(), rgba.data(), static_cast<uint32_t>(app.config().width),
                                 static_cast<uint32_t>(app.config().height), false);
            bench::RenderStats st = app.stats();
            std::printf("%s  lights=%d tris=%zu draws=%zu\n", p.c_str(), st.lightsVisible, st.triangles, st.drawCalls);
        }
        return 0;
    }

    // ---- 計測 ----
    opt.lapDuration = lapSeconds > 0 ? lapSeconds : app.config().cameraPath.duration;
    app.start(opt);
    auto last = Clock::now();
    bool quit = false;
    int64_t lastFrameNs = -1;
    while (!quit) {
        if (window) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) quit = true;
                // ウィンドウが隠れた・最小化された計測は信用できないので中断扱い
                if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_MINIMIZED) app.abort("interrupted");
            }
        }
        auto now = Clock::now();
        double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        auto c0 = Clock::now();
        bool more = app.frame(dt, lastFrameNs, static_cast<int64_t>(dt * 1e9), bench::Thermal::None);
        lastFrameNs = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - c0).count();
        if (!more) break;
        if (window) {
            static int lastSec = -1;
            auto* s = app.session();
            int sec = static_cast<int>(s->sceneTime());
            if (sec != lastSec) {
                lastSec = sec;
                char title[128];
                std::snprintf(title, sizeof(title), "BenchDeck — %s  lap %d  %02d s  %3.0f%%", preset.c_str(), s->lap(), sec,
                              s->progress() * 100.0f);
                SDL_SetWindowTitle(window, title);
            }
        }
    }
    if (quit) app.abort("interrupted");

    bench::RunResult r = app.result(deviceInfo(refreshHz), kAppVersion);
    fs::create_directories(outDir);
    auto stamp = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    fs::path base = fs::path(outDir) / ("benchdeck_" + preset + "_" + std::to_string(stamp));
    std::ofstream(base.string() + ".json") << bench::resultToJson(r);
    std::ofstream(base.string() + ".csv") << bench::resultToCsv(r);

    std::printf("\n%s  preset=%s  laps=%d valid=%d  method=%s\n", r.device.device.c_str(), r.preset.c_str(),
                static_cast<int>(r.laps.size()), r.validLaps(),
                r.method == bench::TimingMethod::Gpu ? "gpu" : "cpu+display");
    std::printf("avg %.1f fps   1%% low %.1f fps   p99 %.1f ms   gpu %.1f ms   cpu %.1f ms\n", r.all.avgFps, r.all.low1Fps,
                r.all.p99Ms, r.all.gpuMsAvg, r.all.cpuMsAvg);
    for (size_t i = 0; i < r.sections.size(); ++i) {
        std::printf("  %-10s avg %.1f fps  1%% low %.1f fps\n", r.sectionNames[i].c_str(), r.sections[i].avgFps,
                    r.sections[i].low1Fps);
    }
    if (r.score) std::printf("score %ld\n", std::lround(*r.score));
    else std::printf("score -  (%s)\n", r.scoreNote.c_str());
    std::printf("render: ~%.0f tris/frame, ~%.0f draws/frame, max %d lights in view\n", r.trianglesAvg, r.drawCallsAvg,
                r.lightsVisibleMax);
    std::printf("results: %s.{json,csv}\n", base.c_str());

    if (!baselineFile.empty()) {
        if (preset != "deck" || r.validLaps() == 0) {
            std::fprintf(stderr, "baseline not written: needs the deck preset and at least one valid lap\n");
            return 1;
        }
        std::string text = readText(baselineFile);
        std::string updated = bench::writeBaseline(text, {r.all.avgFps, r.all.low1Fps});
        if (updated.empty()) {
            std::fprintf(stderr, "cannot parse %s\n", baselineFile.c_str());
            return 1;
        }
        std::ofstream(baselineFile) << updated;
        std::printf("baseline written to %s (remember to bump \"version\")\n", baselineFile.c_str());
    }
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    return r.aborted ? 3 : 0;
}

#include "bench/measure.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

#include <nlohmann/json.hpp>

namespace bench {

using json = nlohmann::ordered_json;

const char* thermalName(Thermal t) {
    switch (t) {
        case Thermal::None: return "none";
        case Thermal::Light: return "light";
        case Thermal::Moderate: return "moderate";
        case Thermal::Severe: return "severe";
        case Thermal::Critical: return "critical";
        case Thermal::Emergency: return "emergency";
        case Thermal::Shutdown: return "shutdown";
    }
    return "unknown";
}

double FrameRecord::frameMs(TimingMethod m) const {
    if (m == TimingMethod::Gpu && gpuNs > 0) {
        int64_t cpu = cpuNs > 0 ? cpuNs : hostCpuNs;
        return static_cast<double>(std::max(gpuNs, cpu)) * 1e-6;
    }
    int64_t cpu = cpuNs > 0 ? cpuNs : hostCpuNs;
    return static_cast<double>(std::max(cpu, displayNs)) * 1e-6;
}

Stats computeStats(const std::vector<double>& frameMs, const std::vector<double>& gpuMs, const std::vector<double>& cpuMs) {
    Stats s;
    s.frames = static_cast<int>(frameMs.size());
    if (frameMs.empty()) return s;
    const double total = std::accumulate(frameMs.begin(), frameMs.end(), 0.0);
    s.avgFps = total > 0 ? 1000.0 * s.frames / total : 0;

    std::vector<double> sorted = frameMs;
    std::sort(sorted.begin(), sorted.end());
    auto worstMean = [&](double fraction) {
        size_t n = std::max<size_t>(1, static_cast<size_t>(std::floor(sorted.size() * fraction)));
        double sum = std::accumulate(sorted.end() - static_cast<long>(n), sorted.end(), 0.0);
        return sum / static_cast<double>(n);
    };
    s.low1Fps = 1000.0 / worstMean(0.01);
    s.low01Fps = 1000.0 / worstMean(0.001);
    // 最近順位法
    auto pct = [&](double p) {
        size_t rank = static_cast<size_t>(std::ceil(p * sorted.size()));
        return sorted[std::clamp<size_t>(rank, 1, sorted.size()) - 1];
    };
    s.medianMs = pct(0.5);
    s.p99Ms = pct(0.99);
    auto mean = [](const std::vector<double>& v) {
        return v.empty() ? -1.0 : std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
    };
    s.gpuMsAvg = mean(gpuMs);
    s.cpuMsAvg = mean(cpuMs);
    return s;
}

double computeScore(double avgFps, double low1Fps, const Baseline& deck) {
    if (deck.avgFps <= 0 || deck.low1Fps <= 0) return 0;
    return 1000.0 * (0.7 * avgFps / deck.avgFps + 0.3 * low1Fps / deck.low1Fps);
}

int RunResult::validLaps() const {
    int n = 0;
    for (const auto& l : laps) n += l.valid ? 1 : 0;
    return n;
}

// ---- セッション ---------------------------------------------------------------------------------

BenchSession::BenchSession(const SceneConfig& config, const Preset& preset, Options opt)
    : config_(config), preset_(preset), opt_(opt) {
    phase_ = opt_.warmup ? Phase::Warmup : Phase::Measure;
    lap_ = opt_.warmup ? 0 : 1;
}

float BenchSession::progress() const {
    const int total = opt_.laps + (opt_.warmup ? 1 : 0);
    if (phase_ == Phase::Done || phase_ == Phase::Draining) return 1.0f;
    const int done = (phase_ == Phase::Warmup) ? 0 : (opt_.warmup ? lap_ : lap_ - 1);
    return static_cast<float>((done + sceneTime_ / opt_.lapDuration) / total);
}

double BenchSession::advance(double realDt) {
    if (phase_ == Phase::Done) return sceneTime_;
    if (phase_ == Phase::Draining) {
        // FrameInfo（GPU時間）は数フレーム遅れて届く。直近のフレームが埋まるか、上限に達したら終える
        size_t pending = 0;
        for (auto it = frames_.rbegin(); it != frames_.rend() && it - frames_.rbegin() < 16; ++it) {
            pending += it->gpuNs > 0 ? 0 : 1;
        }
        if (--drainFrames_ <= 0 || pending == 0) phase_ = Phase::Done;
        return sceneTime_;
    }
    sceneTime_ += std::clamp(realDt, 0.0, opt_.maxFrameStep);
    if (sceneTime_ >= opt_.lapDuration) {
        sceneTime_ = 0;
        if (phase_ == Phase::Warmup) {
            phase_ = Phase::Measure;
            lap_ = 1;
        } else if (lap_ < opt_.laps) {
            ++lap_;
        } else {
            // 最後のフレームの FrameInfo が届くまで少し描き続ける
            phase_ = Phase::Draining;
            drainFrames_ = 30;
        }
    }
    return sceneTime_;
}

void BenchSession::record(uint32_t frameId, int64_t hostCpuNs, int64_t displayNs, Thermal thermal, float headroom) {
    if (phase_ != Phase::Measure) return;
    FrameRecord f;
    f.frameId = frameId;
    f.lap = lap_;
    f.section = config_.sectionAt(sceneTime_);
    f.sceneTime = sceneTime_;
    f.hostCpuNs = hostCpuNs;
    f.displayNs = displayNs;
    f.thermal = thermal;
    f.headroom = headroom;
    frames_.push_back(f);
}

void BenchSession::resolve(uint32_t frameId, int64_t gpuNs, int64_t cpuNs) {
    // 新しい方から探す（届くのは直近数フレーム分）
    for (auto it = frames_.rbegin(); it != frames_.rend() && it - frames_.rbegin() < 64; ++it) {
        if (it->frameId == frameId) {
            if (gpuNs > 0) it->gpuNs = gpuNs;
            if (cpuNs > 0) it->cpuNs = cpuNs;
            return;
        }
    }
}

void BenchSession::abort(const std::string& reason) {
    if (abortReason_.empty()) abortReason_ = reason;
    phase_ = Phase::Done;
}

RunResult BenchSession::finish(const DeviceInfo& device, const std::string& appVersion) {
    RunResult r;
    r.appVersion = appVersion;
    r.sceneVersion = config_.version;
    r.preset = preset_.name;
    r.backend = config_.backend;
    r.device = device;
    r.aborted = !abortReason_.empty();
    r.abortReason = abortReason_;
    for (const auto& s : config_.cameraPath.sections) r.sectionNames.push_back(s.name);
    const int nSec = static_cast<int>(r.sectionNames.size());

    // GPU時間が半分以上のフレームで取れていればGPU方式、そうでなければCPU＋表示間隔にフォールバック
    size_t withGpu = 0;
    for (const auto& f : frames_) withGpu += f.gpuNs > 0 ? 1 : 0;
    r.method = (!frames_.empty() && withGpu * 2 >= frames_.size()) ? TimingMethod::Gpu : TimingMethod::CpuDisplay;
    r.gpuCoverage = frames_.empty() ? 0.0 : static_cast<double>(withGpu) / static_cast<double>(frames_.size());

    struct Bucket {
        std::vector<double> frame, gpu, cpu;
        void add(const FrameRecord& f, TimingMethod m) {
            frame.push_back(f.frameMs(m));
            if (f.gpuNs > 0) gpu.push_back(f.gpuNs * 1e-6);
            int64_t c = f.cpuNs > 0 ? f.cpuNs : f.hostCpuNs;
            if (c > 0) cpu.push_back(c * 1e-6);
        }
        Stats stats() const { return computeStats(frame, gpu, cpu); }
    };

    const int lapsSeen = frames_.empty() ? 0 : frames_.back().lap;
    Bucket allValid;
    std::vector<Bucket> secValid(nSec);
    for (int lap = 1; lap <= lapsSeen; ++lap) {
        LapResult lr;
        lr.lap = lap;
        Bucket all;
        std::vector<Bucket> sec(nSec);
        for (const auto& f : frames_) {
            if (f.lap != lap) continue;
            all.add(f, r.method);
            if (f.section >= 0 && f.section < nSec) sec[f.section].add(f, r.method);
            lr.thermalMax = std::max(lr.thermalMax, f.thermal);
        }
        lr.all = all.stats();
        for (auto& b : sec) lr.sections.push_back(b.stats());
        // 周回の途中で打ち切られた最後の周回は無効
        const bool truncated = r.aborted && lap == lapsSeen;
        if (lr.thermalMax >= Thermal::Moderate) {
            lr.valid = false;
            lr.invalidReason = std::string("thermal_") + thermalName(lr.thermalMax);
        } else if (truncated) {
            lr.valid = false;
            lr.invalidReason = "incomplete";
        }
        r.thermalMax = std::max(r.thermalMax, lr.thermalMax);
        if (lr.valid) {
            for (const auto& f : frames_) {
                if (f.lap != lap) continue;
                allValid.add(f, r.method);
                if (f.section >= 0 && f.section < nSec) secValid[f.section].add(f, r.method);
            }
        }
        r.laps.push_back(std::move(lr));
    }
    r.all = allValid.stats();
    for (auto& b : secValid) r.sections.push_back(b.stats());

    if (preset_.name != "deck") {
        r.scoreNote = "score is only computed for the deck preset";
    } else if (!config_.baseline) {
        r.scoreNote = "baseline not set in scene json (run the Steam Deck calibration first)";
    } else if (r.validLaps() == 0) {
        r.scoreNote = "no valid laps";
    } else {
        r.score = computeScore(r.all.avgFps, r.all.low1Fps, *config_.baseline);
    }
    r.frames = frames_;
    return r;
}

// ---- 出力 --------------------------------------------------------------------------------------

namespace {

double round2(double v) { return std::round(v * 100.0) / 100.0; }

json statsJson(const Stats& s) {
    json j;
    j["frames"] = s.frames;
    j["avg_fps"] = round2(s.avgFps);
    j["low1_fps"] = round2(s.low1Fps);
    j["low01_fps"] = round2(s.low01Fps);
    j["median_ms"] = round2(s.medianMs);
    j["p99_ms"] = round2(s.p99Ms);
    j["gpu_ms_avg"] = s.gpuMsAvg >= 0 ? json(round2(s.gpuMsAvg)) : json(nullptr);
    j["cpu_ms_avg"] = s.cpuMsAvg >= 0 ? json(round2(s.cpuMsAvg)) : json(nullptr);
    return j;
}

std::string fmt(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", v);
    return buf;
}

std::string csvField(const std::string& s) {
    if (s.find_first_of(",\"\n") == std::string::npos) return s;
    std::string r = "\"";
    for (char c : s) {
        if (c == '"') r += '"';
        r += c;
    }
    return r + "\"";
}

}  // namespace

std::string resultToJson(const RunResult& r, bool includeFrames) {
    json j;
    j["format"] = "benchdeck-result/1";
    j["app_version"] = r.appVersion;
    j["scene_version"] = r.sceneVersion;
    j["preset"] = r.preset;
    j["backend"] = r.backend;
    j["device"] = {{"name", r.device.device}, {"gpu", r.device.gpu}, {"driver", r.device.driver},
                   {"vulkan", r.device.vulkanVersion}, {"os", r.device.os}, {"refresh_hz", round2(r.device.refreshHz)},
                   {"host", r.device.host}};
    j["timing_method"] = r.method == TimingMethod::Gpu ? "gpu_frame_duration" : "cpu_and_display_interval";
    // GPU時間が取れたフレームの割合。GPU方式でも欠けたフレームは max(CPU, 表示間隔) で埋めている
    j["gpu_coverage"] = round2(r.gpuCoverage);
    j["score"] = r.score ? json(std::lround(*r.score)) : json(nullptr);
    if (!r.scoreNote.empty()) j["score_note"] = r.scoreNote;
    j["aborted"] = r.aborted;
    if (r.aborted) j["abort_reason"] = r.abortReason;
    j["thermal_max"] = thermalName(r.thermalMax);
    j["valid_laps"] = r.validLaps();
    j["summary"] = statsJson(r.all);
    json secs = json::object();
    for (size_t i = 0; i < r.sections.size() && i < r.sectionNames.size(); ++i) secs[r.sectionNames[i]] = statsJson(r.sections[i]);
    j["sections"] = secs;
    json laps = json::array();
    for (const auto& l : r.laps) {
        json lj;
        lj["lap"] = l.lap;
        lj["valid"] = l.valid;
        if (!l.valid) lj["invalid_reason"] = l.invalidReason;
        lj["thermal_max"] = thermalName(l.thermalMax);
        lj["all"] = statsJson(l.all);
        json ls = json::object();
        for (size_t i = 0; i < l.sections.size() && i < r.sectionNames.size(); ++i) ls[r.sectionNames[i]] = statsJson(l.sections[i]);
        lj["sections"] = ls;
        laps.push_back(lj);
    }
    j["laps"] = laps;
    j["render_stats"] = {{"lights_visible_max", r.lightsVisibleMax},
                         {"triangles_avg", std::lround(r.trianglesAvg)},
                         {"draw_calls_avg", std::lround(r.drawCallsAvg)}};
    if (includeFrames) {
        // 列指向で持つ（行ごとのオブジェクトより3〜4倍小さい）
        json f;
        json lap = json::array(), t = json::array(), sec = json::array(), gpu = json::array(), cpu = json::array(),
             disp = json::array(), frame = json::array(), therm = json::array(), head = json::array();
        for (const auto& fr : r.frames) {
            lap.push_back(fr.lap);
            t.push_back(std::round(fr.sceneTime * 1000.0) / 1000.0);
            sec.push_back(fr.section);
            gpu.push_back(fr.gpuNs > 0 ? json(round2(fr.gpuNs * 1e-6)) : json(nullptr));
            int64_t c = fr.cpuNs > 0 ? fr.cpuNs : fr.hostCpuNs;
            cpu.push_back(c > 0 ? json(round2(c * 1e-6)) : json(nullptr));
            disp.push_back(fr.displayNs > 0 ? json(round2(fr.displayNs * 1e-6)) : json(nullptr));
            frame.push_back(round2(fr.frameMs(r.method)));
            therm.push_back(static_cast<int>(fr.thermal));
            head.push_back(fr.headroom >= 0 ? json(round2(fr.headroom)) : json(nullptr));
        }
        f["lap"] = lap;
        f["t"] = t;
        f["section"] = sec;
        f["frame_ms"] = frame;
        f["gpu_ms"] = gpu;
        f["cpu_ms"] = cpu;
        f["display_ms"] = disp;
        f["thermal"] = therm;
        f["headroom"] = head;
        j["frames"] = f;
    }
    return j.dump(2) + "\n";
}

std::string resultToCsv(const RunResult& r) {
    std::string out = "run,preset,section,avg_fps,low1_fps,p99_ms,gpu_ms_avg,cpu_ms_avg,backend,device,thermal_max\n";
    auto row = [&](const std::string& run, const std::string& section, const Stats& s, Thermal th) {
        out += run + "," + csvField(r.preset) + "," + csvField(section) + "," + fmt(s.avgFps) + "," + fmt(s.low1Fps) + "," +
               fmt(s.p99Ms) + "," + (s.gpuMsAvg >= 0 ? fmt(s.gpuMsAvg) : "") + "," + (s.cpuMsAvg >= 0 ? fmt(s.cpuMsAvg) : "") +
               "," + csvField(r.backend) + "," + csvField(r.device.device) + "," + thermalName(th) + "\n";
    };
    for (const auto& l : r.laps) {
        std::string run = std::to_string(l.lap) + (l.valid ? "" : "*");
        row(run, "all", l.all, l.thermalMax);
        for (size_t i = 0; i < l.sections.size() && i < r.sectionNames.size(); ++i) {
            if (l.sections[i].frames > 0) row(run, r.sectionNames[i], l.sections[i], l.thermalMax);
        }
    }
    row("mean", "all", r.all, r.thermalMax);
    return out;
}

}  // namespace bench

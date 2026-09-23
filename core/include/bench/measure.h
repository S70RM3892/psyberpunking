// 計測と集計。ホスト（Android / Linux）に依存しない部分だけを置く。
//
// フレーム時間は「GPU時間とCPU時間の大きい方」を使う（表示同期の排除。60Hz/120Hz画面の差を消す）。
// FilamentのFrameInfoは数フレーム遅れて届くので、フレームIDで後から埋める。
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "bench/config.h"

namespace bench {

// Android の PowerManager.THERMAL_STATUS_* と同じ番号
enum class Thermal : int { None = 0, Light = 1, Moderate = 2, Severe = 3, Critical = 4, Emergency = 5, Shutdown = 6 };
const char* thermalName(Thermal t);

enum class TimingMethod { Gpu, CpuDisplay };

struct FrameRecord {
    uint32_t frameId = 0;
    int lap = 0;          // 1始まり
    int section = 0;
    double sceneTime = 0; // 周回内の時刻[s]
    int64_t gpuNs = -1;   // Renderer::FrameInfo::gpuFrameDuration（未取得は -1）
    int64_t cpuNs = -1;   // FrameInfo の beginFrame〜endFrame
    int64_t hostCpuNs = -1;  // ホストが測ったCPU側のフレーム時間（FrameInfoが来ない場合の代替）
    int64_t displayNs = -1;  // Choreographer / 表示間隔
    Thermal thermal = Thermal::None;
    float headroom = -1.f;   // PowerManager.getThermalHeadroom()（1秒ごとに更新、未対応は -1）

    // スコア計算に使う時間[ms]。GPU時間が取れればmax(GPU, CPU)、取れなければmax(CPU, 表示間隔)
    double frameMs(TimingMethod m) const;
};

struct Stats {
    int frames = 0;
    double avgFps = 0, low1Fps = 0, low01Fps = 0;
    double medianMs = 0, p99Ms = 0;
    double gpuMsAvg = -1, cpuMsAvg = -1;
};

// 1% low = 遅い方から1%のフレームの平均フレーム時間の逆数（最低1フレーム）
Stats computeStats(const std::vector<double>& frameMs, const std::vector<double>& gpuMs, const std::vector<double>& cpuMs);

struct LapResult {
    int lap = 0;
    bool valid = true;
    std::string invalidReason;
    Thermal thermalMax = Thermal::None;
    Stats all;
    std::vector<Stats> sections;
};

// 計測前の冷却待ちの結果（Android のみ。Linux 版は reason が空で、JSON にも出さない）
struct CoolingInfo {
    std::string reason;          // 開始した理由："below_light_threshold" / "plateau" / "timeout" / "user_skip" / "unsupported" / "skipped"
    double startHeadroom = -1;   // 開始時のサーマルヘッドルーム（取れない端末は -1）
    double target = -1;          // 目標にしたヘッドルーム（LIGHT のしきい値 − 余裕。しきい値が無い端末は -1）
    double waitSeconds = 0;      // 冷却待ちにかかった秒数
};

struct DeviceInfo {
    std::string device;       // 端末名
    std::string gpu;          // GPU名
    std::string driver;       // ドライバ版
    std::string vulkanVersion;
    std::string os;
    double refreshHz = 0;
    std::string host;         // "android" / "linux"
    CoolingInfo cooling;
};

struct RunResult {
    std::string appVersion;
    std::string sceneVersion;
    std::string preset;
    std::string backend = "vulkan";
    DeviceInfo device;
    TimingMethod method = TimingMethod::Gpu;
    double gpuCoverage = 0;           // GPU時間が取れたフレームの割合 0..1
    std::vector<LapResult> laps;
    Stats all;                        // 有効な周回の全フレーム
    std::vector<Stats> sections;      // 同・区間別
    std::vector<std::string> sectionNames;
    std::optional<double> score;      // Deckプリセットかつ baseline がある時だけ
    std::string scoreNote;
    Thermal thermalMax = Thermal::None;
    bool aborted = false;
    std::string abortReason;          // "interrupted" / "thermal_stop" / "low_memory" / "engine_error" ...
    int validLaps() const;

    // 描画側の統計（CPUでの可視判定による推定値）
    int lightsVisibleMax = 0;
    double trianglesAvg = 0;
    double drawCallsAvg = 0;
    // 素材の有無（負荷が変わるので、比べる結果どうしで一致している必要がある）
    int texturesFromAssets = 0;
    bool iblFromAssets = false;

    std::vector<FrameRecord> frames;  // 全フレーム（無効周回も含む）
};

// Score = 1000 × (0.7 × avgFps / avgFps_Deck + 0.3 × low1Fps / low1Fps_Deck)
double computeScore(double avgFps, double low1Fps, const Baseline& deck);

// 周回の進行と記録。描画ループから毎フレーム呼ぶ
class BenchSession {
public:
    struct Options {
        int laps = 3;
        bool warmup = true;
        double lapDuration = 60.0;
        double maxFrameStep = 0.1;  // 1フレームで進めるシーン時間の上限[s]
    };
    enum class Phase { Warmup, Measure, Draining, Done };

    BenchSession(const SceneConfig& config, const Preset& preset, Options opt);

    Phase phase() const { return phase_; }
    int lap() const { return lap_; }          // 計測中の周回（1始まり）、ウォームアップ中は 0
    double sceneTime() const { return sceneTime_; }
    int section() const { return config_.sectionAt(sceneTime_); }
    // ウォームアップ込みの進捗 0..1
    float progress() const;

    // 1フレーム進める。realDt は実時間[s]。戻り値は、このフレームで描くべきシーン時刻
    double advance(double realDt);
    // 描いたフレームを記録（frameId は Renderer 側の値）
    void record(uint32_t frameId, int64_t hostCpuNs, int64_t displayNs, Thermal thermal, float headroom = -1.f);
    // FrameInfo が届いたら埋める
    void resolve(uint32_t frameId, int64_t gpuNs, int64_t cpuNs);
    // 計測を途中で打ち切る（サーマル停止・中断）
    void abort(const std::string& reason);

    RunResult finish(const DeviceInfo& device, const std::string& appVersion);

private:
    const SceneConfig& config_;
    Preset preset_;
    Options opt_;
    Phase phase_;
    int lap_ = 0;
    double sceneTime_ = 0;
    int drainFrames_ = 0;
    std::vector<FrameRecord> frames_;
    std::string abortReason_;
};

// 結果の書き出し
std::string resultToJson(const RunResult& r, bool includeFrames = true);
std::string resultToCsv(const RunResult& r);

}  // namespace bench

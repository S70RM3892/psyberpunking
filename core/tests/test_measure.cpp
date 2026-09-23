#include <gtest/gtest.h>

#include "bench/measure.h"
#include "test_util.h"

using namespace bench;

TEST(Stats, ConstantFrames) {
    std::vector<double> ms(1000, 25.0);
    Stats s = computeStats(ms, ms, {});
    EXPECT_DOUBLE_EQ(s.avgFps, 40.0);
    EXPECT_DOUBLE_EQ(s.low1Fps, 40.0);
    EXPECT_DOUBLE_EQ(s.medianMs, 25.0);
    EXPECT_DOUBLE_EQ(s.p99Ms, 25.0);
    EXPECT_DOUBLE_EQ(s.gpuMsAvg, 25.0);
    EXPECT_DOUBLE_EQ(s.cpuMsAvg, -1.0);
}

TEST(Stats, OnePercentLowUsesWorstFrames) {
    // 990フレーム 20ms、10フレーム 50ms → 1% low は 50ms の平均 = 20fps
    std::vector<double> ms(990, 20.0);
    ms.insert(ms.end(), 10, 50.0);
    Stats s = computeStats(ms, {}, {});
    EXPECT_NEAR(s.low1Fps, 20.0, 1e-9);
    EXPECT_NEAR(s.avgFps, 1000.0 * 1000 / (990 * 20.0 + 10 * 50.0), 1e-9);
    // 0.1% low は最悪1フレーム
    EXPECT_NEAR(s.low01Fps, 20.0, 1e-9);
    EXPECT_DOUBLE_EQ(s.p99Ms, 20.0);  // 99パーセンタイル（最近順位）は990番目 = 20ms
}

TEST(Stats, Empty) {
    Stats s = computeStats({}, {}, {});
    EXPECT_EQ(s.frames, 0);
    EXPECT_EQ(s.avgFps, 0);
}

TEST(Score, DeckIsThousand) {
    Baseline b{40.0, 32.0};
    EXPECT_DOUBLE_EQ(computeScore(40.0, 32.0, b), 1000.0);
    EXPECT_DOUBLE_EQ(computeScore(80.0, 64.0, b), 2000.0);
    // 平均だけ倍：0.7×2 + 0.3×1 = 1.7
    EXPECT_DOUBLE_EQ(computeScore(80.0, 32.0, b), 1700.0);
}

namespace {
void runSession(BenchSession& s, double dt, Thermal thermal, uint32_t& frameId) {
    while (s.phase() != BenchSession::Phase::Done) {
        s.advance(dt);
        s.record(frameId, static_cast<int64_t>(dt * 0.5e9), static_cast<int64_t>(dt * 1e9), thermal);
        s.resolve(frameId, static_cast<int64_t>(dt * 1e9), static_cast<int64_t>(dt * 0.4e9));
        ++frameId;
    }
}
}  // namespace

TEST(Session, ThreeLapsAfterWarmup) {
    SceneConfig cfg = loadScene();
    cfg.baseline = Baseline{40.0, 40.0};
    BenchSession::Options o;
    o.lapDuration = 2.0;
    BenchSession s(cfg, *cfg.findPreset("deck"), o);
    EXPECT_EQ(s.phase(), BenchSession::Phase::Warmup);
    uint32_t id = 0;
    runSession(s, 0.025, Thermal::None, id);
    RunResult r = s.finish({}, "test");
    ASSERT_EQ(r.laps.size(), 3u);
    EXPECT_EQ(r.validLaps(), 3);
    EXPECT_EQ(r.method, TimingMethod::Gpu);
    EXPECT_NEAR(r.all.avgFps, 40.0, 0.5);
    ASSERT_TRUE(r.score.has_value());
    EXPECT_NEAR(*r.score, 1000.0, 15.0);
}

TEST(Session, ThermalInvalidatesLap) {
    SceneConfig cfg = loadScene();
    BenchSession::Options o;
    o.lapDuration = 1.0;
    o.warmup = false;
    o.laps = 1;
    BenchSession s(cfg, *cfg.findPreset("deck"), o);
    uint32_t id = 0;
    runSession(s, 0.02, Thermal::Moderate, id);
    RunResult r = s.finish({}, "test");
    ASSERT_EQ(r.laps.size(), 1u);
    EXPECT_FALSE(r.laps[0].valid);
    EXPECT_EQ(r.laps[0].invalidReason, "thermal_moderate");
    EXPECT_FALSE(r.score.has_value());
}

TEST(Session, NoScoreForOtherPresets) {
    SceneConfig cfg = loadScene();
    cfg.baseline = Baseline{40.0, 40.0};
    BenchSession::Options o;
    o.lapDuration = 0.5;
    o.warmup = false;
    BenchSession s(cfg, *cfg.findPreset("high"), o);
    uint32_t id = 0;
    runSession(s, 0.02, Thermal::None, id);
    RunResult r = s.finish({}, "test");
    EXPECT_FALSE(r.score.has_value());
    EXPECT_GT(r.all.avgFps, 0);
}

TEST(Session, AbortMarksIncomplete) {
    SceneConfig cfg = loadScene();
    BenchSession::Options o;
    o.lapDuration = 1.0;
    o.warmup = false;
    BenchSession s(cfg, *cfg.findPreset("deck"), o);
    for (uint32_t i = 0; i < 20; ++i) {
        s.advance(0.02);
        s.record(i, 1000000, 16000000, Thermal::None);
    }
    s.abort("interrupted");
    RunResult r = s.finish({}, "test");
    EXPECT_TRUE(r.aborted);
    ASSERT_EQ(r.laps.size(), 1u);
    EXPECT_FALSE(r.laps[0].valid);
    // FrameInfoが来ていない → CPU＋表示間隔にフォールバック
    EXPECT_EQ(r.method, TimingMethod::CpuDisplay);
}

TEST(Output, CsvHeaderMatchesSpec) {
    RunResult r;
    r.preset = "deck";
    r.device.device = "Steam Deck (Linux)";
    r.sectionNames = {"boulevard", "overpass", "alley"};
    std::string csv = resultToCsv(r);
    EXPECT_EQ(csv.substr(0, csv.find('\n')),
              "run,preset,section,avg_fps,low1_fps,p99_ms,gpu_ms_avg,cpu_ms_avg,backend,device,thermal_max");
}

TEST(Output, JsonHasSummary) {
    RunResult r;
    r.preset = "deck";
    r.score = 987.4;
    std::string js = resultToJson(r, false);
    EXPECT_NE(js.find("\"score\": 987"), std::string::npos);
    EXPECT_NE(js.find("\"summary\""), std::string::npos);
}

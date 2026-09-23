#include "drive/audio_synth.h"

#include <algorithm>
#include <cmath>

namespace drive {

namespace {
constexpr double kTwoPi = 6.283185307179586;
}

AudioSynth::AudioSynth(int sampleRate) : rate_(sampleRate) {}

void AudioSynth::setState(const AudioState& s) {
    tRpm_.store(s.rpm, std::memory_order_relaxed);
    tThrottle_.store(s.throttle, std::memory_order_relaxed);
    tSpeed_.store(s.speed, std::memory_order_relaxed);
    tSlip_.store(s.slip, std::memory_order_relaxed);
    tImpact_.store(s.impact, std::memory_order_relaxed);
    tVolume_.store(s.volume, std::memory_order_relaxed);
}

float AudioSynth::noise() {
    // xorshift32 → [-1, 1)
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return static_cast<float>(rng_ >> 8) * (2.f / 16777216.f) - 1.f;
}

void AudioSynth::render(float* out, int frames) {
    const float dt = 1.f / static_cast<float>(rate_);
    // 目標値（ブロックの頭で1回読む）
    const float tRpm = std::clamp(tRpm_.load(std::memory_order_relaxed), 500.f, 8000.f);
    const float tThr = std::clamp(tThrottle_.load(std::memory_order_relaxed), 0.f, 1.f);
    const float tSpd = std::max(0.f, tSpeed_.load(std::memory_order_relaxed));
    const float tSlip = std::clamp(tSlip_.load(std::memory_order_relaxed), 0.f, 1.f);
    const float tVol = std::clamp(tVolume_.load(std::memory_order_relaxed), 0.f, 1.f);
    const float impact = tImpact_.load(std::memory_order_relaxed);
    // 衝突：強さが跳ね上がったら鳴らす
    if (impact > lastImpact_ + 1.0f && impact > 1.5f) {
        thump_ = std::max(thump_, std::min(1.f, impact / 12.f));
        thumpPhase_ = 0.0;
    }
    lastImpact_ = impact;

    // 追従の速さ（1サンプルあたり）
    const float kRpm = 1.f - std::exp(-dt * 18.f);
    const float kThr = 1.f - std::exp(-dt * 12.f);
    const float kSlow = 1.f - std::exp(-dt * 6.f);

    for (int i = 0; i < frames; ++i) {
        rpm_ += (tRpm - rpm_) * kRpm;
        throttle_ += (tThr - throttle_) * kThr;
        speed_ += (tSpd - speed_) * kSlow;
        slip_ += (tSlip - slip_) * kThr;
        volume_ += (tVol - volume_) * kSlow;

        // ---- エンジン：6気筒4ストローク（1回転に3回の爆発）。周期ごとに少し揺らす ----
        const double fire = rpm_ / 60.0 * 3.0;
        phase_ += fire * dt * cycleJitter_;
        if (phase_ >= 1.0) {
            phase_ -= 1.0;
            cycleJitter_ = 1.f + noise() * 0.018f;
        }
        const float ph = static_cast<float>(phase_);
        // 倍音の和（アクセルを踏むほど高い倍音が出る）
        float eng = 0.f;
        const float bright = 0.55f + throttle_ * 0.45f;
        float amp = 1.f;
        for (int k = 1; k <= 7; ++k) {
            eng += amp * std::sin(static_cast<float>(kTwoPi) * ph * static_cast<float>(k));
            amp *= 0.62f * bright + 0.1f;
        }
        // 半分の周波数のうなり（気筒ごとのばらつき）
        eng += 0.35f * std::sin(static_cast<float>(kTwoPi) * ph * 0.5f);
        // 爆発ごとのパルス感
        eng *= 0.75f + 0.25f * std::exp(-ph * 6.f);
        const float cutoff = 400.f + rpm_ * 0.35f + throttle_ * 1600.f;
        const float a = 1.f - std::exp(-static_cast<float>(kTwoPi) * cutoff * dt);
        engLp_ += (eng - engLp_) * a;
        // 吸気音（アクセルでシュッと鳴るノイズ）
        intakeLp_ += (noise() - intakeLp_) * 0.08f;
        const float engineGain = 0.10f + throttle_ * 0.10f + std::min(rpm_ / 7000.f, 1.f) * 0.06f;
        float s = engLp_ * engineGain + intakeLp_ * throttle_ * 0.05f * (rpm_ / 7000.f);

        // ---- 風（速さの2乗で強く）と雨（一定のさーっという音） ----
        const float n = noise();
        windLp1_ += (n - windLp1_) * 0.02f;
        windLp2_ += (windLp1_ - windLp2_) * 0.05f;
        const float wind = windLp2_ * std::min(1.f, speed_ * speed_ / 900.f) * 0.9f;
        rainHp_ = n - rainPrev_ + 0.97f * rainHp_;  // 高域通過（雨のざらつき）
        rainPrev_ = n;
        rainLp_ += (rainHp_ - rainLp_) * 0.45f;
        const float rain = rainLp_ * 0.025f;

        // ---- タイヤのスキール：1.2〜1.8kHz の帯域ノイズ＋少しの音程 ----
        const float squealF = 1300.f + 300.f * std::sin(static_cast<float>(squealPhase_ * 0.0007));
        squealPhase_ += 1.0;
        const float w0 = static_cast<float>(kTwoPi) * squealF * dt;
        const float q = 0.06f;
        squealBp1_ += w0 * (n - squealBp1_ - q * squealBp2_);
        squealBp2_ += w0 * squealBp1_;
        const float squealAmp = std::clamp((slip_ - 0.15f) * 1.4f, 0.f, 1.f) * std::min(1.f, speed_ / 8.f);
        const float squeal = squealBp2_ * squealAmp * 0.35f;

        // ---- 衝突：低いドンと、がしゃっという雑音 ----
        float thump = 0.f;
        if (thump_ > 1e-4f) {
            thumpPhase_ += 55.0 * dt;
            thump = (std::sin(static_cast<float>(kTwoPi * thumpPhase_)) * 0.8f + n * 0.5f) * thump_;
            thump_ *= 1.f - dt * 9.f;
        }

        s = (s + wind * 0.25f + squeal + thump * 0.6f) * volume_;
        // 雨は左右で少し変える（広がり）
        float l = s + rain * volume_, r = s + (rain * 0.8f + windLp1_ * 0.004f) * volume_;
        // 過大入力の丸め（音割れ防止）
        l = std::tanh(l * 1.2f) * 0.85f;
        r = std::tanh(r * 1.2f) * 0.85f;
        out[2 * i] = l;
        out[2 * i + 1] = r;
    }
}

}  // namespace drive

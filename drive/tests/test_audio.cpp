// 走行音の合成：範囲内・有限、回転とアクセルで大きく、止めれば無音、スキールで高域が増える
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "drive/audio_synth.h"

using namespace drive;

namespace {

struct Stats {
    float rms = 0, peak = 0, hf = 0;  // hf = 1次差分のRMS（高域の目安）
    bool finite = true;
};

Stats run(AudioSynth& a, const AudioState& s, float seconds) {
    a.setState(s);
    const int n = static_cast<int>(seconds * a.sampleRate());
    std::vector<float> buf(static_cast<size_t>(n) * 2);
    // 最初の半分はなじませる
    a.render(buf.data(), n);
    a.render(buf.data(), n);
    Stats st;
    double sum = 0, dsum = 0;
    for (int i = 0; i < n; ++i) {
        const float v = buf[2 * i];
        st.finite &= std::isfinite(v);
        st.peak = std::max(st.peak, std::abs(v));
        sum += v * v;
        if (i > 0) {
            const float d = v - buf[2 * (i - 1)];
            dsum += d * d;
        }
    }
    st.rms = static_cast<float>(std::sqrt(sum / n));
    st.hf = static_cast<float>(std::sqrt(dsum / n));
    return st;
}

}  // namespace

TEST(Audio, BoundedAndFinite) {
    AudioSynth a;
    AudioState s;
    s.rpm = 7200;
    s.throttle = 1;
    s.speed = 60;
    s.slip = 1;
    s.impact = 20;
    Stats st = run(a, s, 0.5f);
    EXPECT_TRUE(st.finite);
    EXPECT_LE(st.peak, 1.0f);
    EXPECT_GT(st.rms, 0.01f);
}

TEST(Audio, LouderWhenRevving) {
    AudioSynth a, b;
    AudioState idle;
    AudioState rev;
    rev.rpm = 6000;
    rev.throttle = 1;
    EXPECT_GT(run(b, rev, 0.4f).rms, run(a, idle, 0.4f).rms * 1.3f);
}

TEST(Audio, SilentWhenMuted) {
    AudioSynth a;
    AudioState s;
    s.rpm = 5000;
    s.throttle = 1;
    s.volume = 0;
    EXPECT_LT(run(a, s, 1.0f).rms, 1e-3f);
}

TEST(Audio, SquealAddsHighFrequencies) {
    AudioSynth a, b;
    AudioState grip;
    grip.speed = 20;
    grip.rpm = 3000;
    AudioState slide = grip;
    slide.slip = 1;
    EXPECT_GT(run(b, slide, 0.4f).hf, run(a, grip, 0.4f).hf * 1.3f);
}

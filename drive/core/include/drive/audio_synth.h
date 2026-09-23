// 走行音の合成（音源ファイルなし）。エンジン・タイヤのスキール・雨と風・衝突。
//
// ゲームのスレッドが setState() で状態を渡し、音声のスレッド（AAudio / SDL のコールバック）が render() で
// ステレオの float を作る。パラメータは atomic で受け渡し、render の中でなめらかに追う（ぷつぷつ鳴らない）。
#pragma once

#include <atomic>
#include <cstdint>

namespace drive {

struct AudioState {
    float rpm = 900.f;
    float throttle = 0.f;   // 0..1
    float speed = 0.f;      // m/s
    float slip = 0.f;       // 0..1
    float impact = 0.f;     // m/s（衝突の強さ。増えた瞬間に鳴らす）
    float volume = 1.f;     // 全体（一時停止で 0）
};

class AudioSynth {
public:
    explicit AudioSynth(int sampleRate = 48000);

    // ゲームのスレッドから
    void setState(const AudioState& s);

    // 音声のスレッドから：frames 個のステレオ（LRLR…）を書く
    void render(float* out, int frames);

    int sampleRate() const { return rate_; }

private:
    float noise();

    const int rate_;
    // ゲーム → 音声（atomic）
    std::atomic<float> tRpm_{900.f}, tThrottle_{0.f}, tSpeed_{0.f}, tSlip_{0.f}, tImpact_{0.f}, tVolume_{1.f};

    // 音声スレッドだけが触る
    float rpm_ = 900.f, throttle_ = 0.f, speed_ = 0.f, slip_ = 0.f, volume_ = 0.f;
    double phase_ = 0.0;          // 爆発の位相（周期ごとに 1 進む）
    float cycleJitter_ = 1.f;     // 周期ごとの揺らぎ
    float engLp_ = 0.f;           // エンジンの低域通過
    float intakeLp_ = 0.f;
    float windLp1_ = 0.f, windLp2_ = 0.f;
    float rainHp_ = 0.f, rainPrev_ = 0.f, rainLp_ = 0.f;
    float squealBp1_ = 0.f, squealBp2_ = 0.f;
    double squealPhase_ = 0.0;
    float lastImpact_ = 0.f;
    float thump_ = 0.f;           // 衝突音の残り（包絡）
    double thumpPhase_ = 0.0;
    uint32_t rng_ = 0x9E3779B9u;
};

}  // namespace drive

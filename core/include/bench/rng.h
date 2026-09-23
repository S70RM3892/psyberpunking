// 端末間で同じ乱数列を出すための自前RNG。
// std::uniform_*_distribution は標準ライブラリ実装（libstdc++ / libc++）で結果が変わるので使わない。
#pragma once

#include <cstdint>

namespace bench {

// PCG32 (O'Neill, pcg-random.org) の XSH-RR 版
class Rng {
public:
    explicit Rng(uint64_t seed, uint64_t stream = 0x5851f42d4c957f2dULL) {
        inc_ = (stream << 1u) | 1u;
        next();
        state_ += seed;
        next();
    }

    uint32_t next() {
        uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + inc_;
        uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
    }

    // [0, 1) を24bit精度で返す（floatで端末差が出ない範囲）
    float uniform() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }
    float range(float lo, float hi) { return lo + (hi - lo) * uniform(); }
    // [lo, hi] の整数
    int irange(int lo, int hi) {
        uint32_t span = static_cast<uint32_t>(hi - lo + 1);
        return lo + static_cast<int>(next() % span);
    }
    bool chance(float p) { return uniform() < p; }

    // 親の系列から独立した子RNGを作る（要素ごとに乱数の消費量が変わっても他へ波及させない）
    Rng fork(uint32_t salt) { return Rng(static_cast<uint64_t>(next()) << 32 | salt, salt * 2654435761u + 1u); }

private:
    uint64_t state_ = 0;
    uint64_t inc_ = 0;
};

// 64bit FNV-1a。頂点ハッシュ（決定性テスト）に使う
inline uint64_t fnv1a(const void* data, size_t size, uint64_t h = 0xcbf29ce484222325ULL) {
    auto p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

}  // namespace bench

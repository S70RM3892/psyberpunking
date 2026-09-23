#include "bench/camera_path.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace bench {

using namespace filament::math;

namespace {

float smoothstep(float e0, float e1, float x) {
    float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

// 速度プロファイル [m/s]。区間の距離は procgen_city.cpp の kSection*Distance と対応させてある
float speedAt(float t) {
    float v = 8.0f;
    v += 6.0f * smoothstep(20.f, 23.f, t);   // 高架へ上がりながら加速
    v -= 5.0f * smoothstep(38.f, 42.f, t);   // 下りランプ手前で減速
    v -= 4.0f * smoothstep(42.f, 46.f, t);   // 曲がり
    v -= 1.5f * smoothstep(46.f, 52.f, t);   // 路地はゆっくり
    return v;
}

// 中心化Catmull-Rom（alpha = 0.5）。行き過ぎ（オーバーシュート）が出にくい
float3 catmullRom(float3 p0, float3 p1, float3 p2, float3 p3, float t) {
    auto tj = [](float ti, float3 a, float3 b) { return ti + std::sqrt(std::max(length(b - a), 1e-4f)); };
    float t0 = 0, t1 = tj(t0, p0, p1), t2 = tj(t1, p1, p2), t3 = tj(t2, p2, p3);
    float tt = t1 + (t2 - t1) * t;
    float3 a1 = p0 * ((t1 - tt) / (t1 - t0)) + p1 * ((tt - t0) / (t1 - t0));
    float3 a2 = p1 * ((t2 - tt) / (t2 - t1)) + p2 * ((tt - t1) / (t2 - t1));
    float3 a3 = p2 * ((t3 - tt) / (t3 - t2)) + p3 * ((tt - t2) / (t3 - t2));
    float3 b1 = a1 * ((t2 - tt) / (t2 - t0)) + a2 * ((tt - t0) / (t2 - t0));
    float3 b2 = a2 * ((t3 - tt) / (t3 - t1)) + a3 * ((tt - t1) / (t3 - t1));
    return b1 * ((t2 - tt) / (t2 - t1)) + b2 * ((tt - t1) / (t2 - t1));
}

struct Spline {
    std::vector<float3> pts;   // 密にサンプルした点
    std::vector<float> arc;    // 累積弧長

    void build(const std::vector<float3>& ctrl, int perSegment) {
        pts.clear();
        for (size_t i = 0; i + 1 < ctrl.size(); ++i) {
            float3 p0 = ctrl[i == 0 ? 0 : i - 1], p1 = ctrl[i], p2 = ctrl[i + 1];
            float3 p3 = ctrl[std::min(i + 2, ctrl.size() - 1)];
            if (i == 0) p0 = p1 * 2.0f - p2;
            if (i + 2 >= ctrl.size()) p3 = p2 * 2.0f - p1;
            for (int k = 0; k < perSegment; ++k) pts.push_back(catmullRom(p0, p1, p2, p3, static_cast<float>(k) / perSegment));
        }
        pts.push_back(ctrl.back());
        arc.assign(pts.size(), 0.f);
        for (size_t i = 1; i < pts.size(); ++i) arc[i] = arc[i - 1] + length(pts[i] - pts[i - 1]);
    }
    float totalLength() const { return arc.empty() ? 0.f : arc.back(); }
    float3 at(float s) const {
        s = std::clamp(s, 0.0f, totalLength());
        size_t i = static_cast<size_t>(std::upper_bound(arc.begin(), arc.end(), s) - arc.begin());
        if (i == 0) return pts.front();
        if (i >= pts.size()) return pts.back();
        float seg = arc[i] - arc[i - 1];
        float f = seg > 1e-6f ? (s - arc[i - 1]) / seg : 0.f;
        return mix(pts[i - 1], pts[i], f);
    }
};

void put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
void putF(std::vector<uint8_t>& out, float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    put32(out, v);
}
uint32_t get32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
float getF(const uint8_t* p) {
    uint32_t v = get32(p);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

}  // namespace

CameraKey CameraPath::sample(double t) const {
    if (keys.empty()) return {};
    double x = std::clamp(t, 0.0, duration()) * rateHz;
    size_t i = std::min(static_cast<size_t>(x), keys.size() - 1);
    size_t j = std::min(i + 1, keys.size() - 1);
    float f = static_cast<float>(x - static_cast<double>(i));
    CameraKey k;
    k.position = mix(keys[i].position, keys[j].position, f);
    k.forward = normalize(mix(keys[i].forward, keys[j].forward, f));
    k.fovDeg = keys[i].fovDeg + (keys[j].fovDeg - keys[i].fovDeg) * f;
    return k;
}

std::vector<uint8_t> CameraPath::serialize() const {
    std::vector<uint8_t> out = {'B', 'D', 'C', 'P'};
    put32(out, 1);
    put32(out, static_cast<uint32_t>(rateHz));
    put32(out, static_cast<uint32_t>(keys.size()));
    for (const auto& k : keys) {
        putF(out, k.position.x);
        putF(out, k.position.y);
        putF(out, k.position.z);
        putF(out, k.forward.x);
        putF(out, k.forward.y);
        putF(out, k.forward.z);
        putF(out, k.fovDeg);
    }
    return out;
}

bool CameraPath::deserialize(const uint8_t* data, size_t size) {
    if (size < 16 || std::memcmp(data, "BDCP", 4) != 0 || get32(data + 4) != 1) return false;
    uint32_t rate = get32(data + 8), count = get32(data + 12);
    if (rate == 0 || size != 16 + static_cast<size_t>(count) * 28) return false;
    rateHz = static_cast<int>(rate);
    keys.resize(count);
    const uint8_t* p = data + 16;
    for (auto& k : keys) {
        k.position = {getF(p), getF(p + 4), getF(p + 8)};
        k.forward = {getF(p + 12), getF(p + 16), getF(p + 20)};
        k.fovDeg = getF(p + 24);
        p += 28;
    }
    return true;
}

CameraPath buildCameraPath(const SceneConfig& cfg, const CityData& city) {
    const float bx = city.boulevardX;
    const float eye = 2.1f;
    std::vector<float3> ctrl;
    // 区間1：大通りを地上で。中央分離帯寄りの車線の上
    ctrl.push_back({bx - 2.6f, eye, city.startZ});
    ctrl.push_back({bx - 2.9f, eye, city.startZ + 55.f});
    ctrl.push_back({bx - 2.2f, eye, city.startZ + 110.f});
    ctrl.push_back({bx - 1.4f, eye, city.rampStartZ - 12.f});
    // 区間2：ランプを上がって高架へ（床の高さに沿う）
    for (float z = city.rampStartZ; z <= city.rampStartZ + city.rampLength + 0.1f; z += 10.f) {
        float s = (z - city.rampStartZ) / city.rampLength;
        ctrl.push_back({bx - 1.2f * (1.0f - s), city.overpassHeightAt(z) + eye + 0.3f * s, z});
    }
    for (float z = city.rampStartZ + city.rampLength + 40.f; z < city.overpassEndZ - 10.f; z += 40.f) {
        // 高架の2車線（±1.9m）の間を走る。車とぶつからない範囲で少し揺らす
        float sway = 0.35f * std::sin(z * 0.021f);
        ctrl.push_back({bx + sway, city.overpassY + eye + 0.3f, z});
    }
    // 区間3：下りランプ → 右へ曲がって路地へ
    for (float z = city.overpassEndZ; z <= city.downRampEndZ + 0.1f; z += 12.f) {
        float s = (z - city.overpassEndZ) / city.rampLength;
        ctrl.push_back({bx + 0.5f * s, city.overpassHeightAt(z) + eye + 0.3f * (1 - s), z});
    }
    ctrl.push_back({bx + 3.5f, eye - 0.2f, city.alleyZ - 3.5f});
    ctrl.push_back({city.alleyX0 + 3.f, 1.8f, city.alleyZ - 0.2f});
    ctrl.push_back({city.alleyX0 + 18.f, 1.75f, city.alleyZ + 0.4f});
    ctrl.push_back({city.alleyX1 - 5.f, 1.8f, city.alleyZ - 0.2f});

    Spline spline;
    spline.build(ctrl, 64);

    // 速度プロファイルの積分が経路長に一致するよう倍率をかける（t=60で路地の端）
    const double duration = cfg.cameraPath.duration;
    const int rate = cfg.cameraPath.rateHz;
    const int count = static_cast<int>(std::lround(duration * rate)) + 1;
    const double dt = 1.0 / (rate * 16.0);
    double total = 0;
    for (double t = 0; t < duration; t += dt) total += speedAt(static_cast<float>(t * 60.0 / duration)) * dt;
    const double scale = spline.totalLength() / total;

    CameraPath path;
    path.rateHz = rate;
    path.keys.reserve(count);
    double s = 0, t = 0;
    for (int i = 0; i < count; ++i) {
        const double target = static_cast<double>(i) / rate;
        while (t < target - 1e-9) {
            double h = std::min(dt, target - t);
            s += speedAt(static_cast<float>(t * 60.0 / duration)) * h * scale;
            t += h;
        }
        const float tn = static_cast<float>(t * 60.0 / duration);  // 60秒に正規化した時刻
        float3 p = spline.at(static_cast<float>(s));
        float3 ahead = spline.at(static_cast<float>(s) + 7.0f);
        float3 fwd = ahead - p;
        if (length(fwd) < 1e-3f) fwd = path.keys.empty() ? float3{0, 0, 1} : path.keys.back().forward;
        fwd = normalize(fwd);

        // 視線の演出：地上では看板へ左右に振り、高架では少し上（スカイライン）、路地では見上げる
        float yaw = 0.16f * std::sin(tn * 0.33f) * (1.0f - smoothstep(18.f, 22.f, tn)) +
                    0.10f * std::sin(tn * 0.21f + 1.0f) * smoothstep(22.f, 26.f, tn) * (1.0f - smoothstep(37.f, 40.f, tn)) +
                    0.12f * std::sin(tn * 0.5f) * smoothstep(48.f, 52.f, tn);
        float pitch = 0.03f + 0.05f * smoothstep(22.f, 27.f, tn) * (1.0f - smoothstep(37.f, 41.f, tn)) +
                      0.14f * smoothstep(46.f, 52.f, tn);
        float cy = std::cos(yaw), sy = std::sin(yaw);
        float3 f{cy * fwd.x + sy * fwd.z, fwd.y, -sy * fwd.x + cy * fwd.z};
        float horiz = std::sqrt(f.x * f.x + f.z * f.z);
        float el = std::atan2(f.y, horiz) + pitch;
        f = normalize(float3{f.x / std::max(horiz, 1e-4f) * std::cos(el), std::sin(el), f.z / std::max(horiz, 1e-4f) * std::cos(el)});

        CameraKey k;
        k.position = p;
        k.forward = f;
        k.fovDeg = 58.f - 4.f * smoothstep(22.f, 26.f, tn) * (1.0f - smoothstep(38.f, 42.f, tn)) + 8.f * smoothstep(44.f, 50.f, tn);
        path.keys.push_back(k);
    }
    return path;
}

}  // namespace bench

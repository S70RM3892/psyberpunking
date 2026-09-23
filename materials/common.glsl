// 各マテリアル共通の小道具（ハッシュ・ノイズ・色）。ESSL 3.0 で通る書き方に限る。
// ハッシュとノイズは必ず highp で計算する：入力は実寸（数百m）やビルごとの種で大きく、
// mediump（モバイルGPUでは16bit）だと1区画の中で値がばらつき、窓や店内が斑点になる。

highp float bdHash11(highp float p) {
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

highp float bdHash21(highp vec2 p) {
    highp vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

highp float bdHash31(highp vec3 p3) {
    p3 = fract(p3 * 0.1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return fract((p3.x + p3.y) * p3.z);
}

highp vec2 bdHash22(highp vec2 p) {
    highp vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

highp float bdNoise2(highp vec2 p) {
    highp vec2 i = floor(p);
    highp vec2 f = fract(p);
    highp vec2 u = f * f * (3.0 - 2.0 * f);
    float a = bdHash21(i);
    float b = bdHash21(i + vec2(1.0, 0.0));
    float c = bdHash21(i + vec2(0.0, 1.0));
    float d = bdHash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

highp float bdNoise3(highp vec3 p) {
    highp vec3 i = floor(p);
    highp vec3 f = fract(p);
    highp vec3 u = f * f * (3.0 - 2.0 * f);
    float n000 = bdHash31(i);
    float n100 = bdHash31(i + vec3(1.0, 0.0, 0.0));
    float n010 = bdHash31(i + vec3(0.0, 1.0, 0.0));
    float n110 = bdHash31(i + vec3(1.0, 1.0, 0.0));
    float n001 = bdHash31(i + vec3(0.0, 0.0, 1.0));
    float n101 = bdHash31(i + vec3(1.0, 0.0, 1.0));
    float n011 = bdHash31(i + vec3(0.0, 1.0, 1.0));
    float n111 = bdHash31(i + vec3(1.0, 1.0, 1.0));
    return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
               mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
}

highp float bdFbm2(highp vec2 p) {
    highp float v = 0.0;
    highp float a = 0.5;
    for (int i = 0; i < 4; i++) {
        v += a * bdNoise2(p);
        p = p * 2.03 + vec2(17.1, 9.2);
        a *= 0.5;
    }
    return v;
}

vec3 bdHsv(float h, float s, float v) {
    vec3 k = clamp(abs(fract(h + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
    return v * mix(vec3(1.0), k, s);
}

// 看板・発光ストリップ用のネオン色（色相 → 彩度の高いリニア色）
vec3 bdNeon(float h) {
    return bdHsv(h, 0.9, 1.0);
}

// 雨の路面：水たまりのマスク（0 = 乾き気味, 1 = 水たまり）
highp float bdPuddle(highp vec2 xz, float wetness) {
    highp float n = bdFbm2(xz * 0.11) * 0.75 + bdNoise2(xz * 0.9) * 0.25;
    return smoothstep(0.62 - wetness * 0.28, 0.70 - wetness * 0.24, n);
}

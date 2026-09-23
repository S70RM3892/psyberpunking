// 各マテリアル共通の小道具（ハッシュ・ノイズ・色）。ESSL 3.0 で通る書き方に限る。

float bdHash11(float p) {
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

float bdHash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float bdHash31(vec3 p3) {
    p3 = fract(p3 * 0.1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 bdHash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

float bdNoise2(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = bdHash21(i);
    float b = bdHash21(i + vec2(1.0, 0.0));
    float c = bdHash21(i + vec2(0.0, 1.0));
    float d = bdHash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float bdNoise3(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
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

float bdFbm2(vec2 p) {
    float v = 0.0;
    float a = 0.5;
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
float bdPuddle(vec2 xz, float wetness) {
    float n = bdFbm2(xz * 0.11) * 0.75 + bdNoise2(xz * 0.9) * 0.25;
    return smoothstep(0.62 - wetness * 0.28, 0.70 - wetness * 0.24, n);
}

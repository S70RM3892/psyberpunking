// テクスチャと環境光。KTX2/KTX（tools/ で変換したCC0素材）があれば使い、無ければ同じ役割のものを実行時に作る。
#include <algorithm>
#include <cmath>
#include <cstring>

#include <filament-iblprefilter/IBLPrefilterContext.h>
#include <image/Ktx1Bundle.h>
#include <ktxreader/Ktx1Reader.h>
#include <ktxreader/Ktx2Reader.h>

#include "bench/rng.h"
#include "render.h"

namespace bench::render {

using namespace filament::math;

namespace {

// ---- 継ぎ目のない（周期的な）ノイズ -------------------------------------------------------------

struct TileNoise {
    int period;
    std::vector<float> lattice;
    TileNoise(int p, uint32_t seed) : period(p), lattice(static_cast<size_t>(p) * p) {
        Rng r(seed);
        for (auto& v : lattice) v = r.uniform();
    }
    float at(int x, int y) const {
        x = ((x % period) + period) % period;
        y = ((y % period) + period) % period;
        return lattice[static_cast<size_t>(y) * period + x];
    }
    // u, v は 0..1（1周期）
    float sample(float u, float v) const {
        float x = u * period, y = v * period;
        int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
        float fx = x - ix, fy = y - iy;
        fx = fx * fx * (3 - 2 * fx);
        fy = fy * fy * (3 - 2 * fy);
        float a = at(ix, iy), b = at(ix + 1, iy), c = at(ix, iy + 1), d = at(ix + 1, iy + 1);
        return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
    }
};

float fbm(const std::vector<TileNoise>& octaves, float u, float v) {
    float s = 0, a = 0.5f, norm = 0;
    for (const auto& o : octaves) {
        s += a * o.sample(u, v);
        norm += a;
        a *= 0.5f;
    }
    return s / norm;
}

std::vector<TileNoise> makeOctaves(uint32_t seed, int base, int count) {
    std::vector<TileNoise> o;
    for (int i = 0; i < count; ++i) o.emplace_back(base << i, seed + static_cast<uint32_t>(i) * 7919u);
    return o;
}

uint8_t u8(float v) { return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); }

// 高さ場から法線マップ（接空間、+Z 上）
std::vector<uint8_t> normalsFromHeight(const std::vector<float>& h, int size, float strength) {
    std::vector<uint8_t> out(static_cast<size_t>(size) * size * 4);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            auto H = [&](int xx, int yy) { return h[static_cast<size_t>((yy + size) % size) * size + (xx + size) % size]; };
            float dx = (H(x + 1, y) - H(x - 1, y)) * strength;
            float dy = (H(x, y + 1) - H(x, y - 1)) * strength;
            float3 n = normalize(float3{-dx, -dy, 1.0f});
            size_t i = (static_cast<size_t>(y) * size + x) * 4;
            out[i] = u8(n.x * 0.5f + 0.5f);
            out[i + 1] = u8(n.y * 0.5f + 0.5f);
            out[i + 2] = u8(n.z * 0.5f + 0.5f);
            out[i + 3] = 255;
        }
    }
    return out;
}

Texture* makeTexture(Engine& engine, int size, bool srgb, std::vector<uint8_t>&& rgba) {
    uint8_t levels = static_cast<uint8_t>(std::floor(std::log2(static_cast<float>(size)))) + 1;
    Texture* t = Texture::Builder()
                     .width(static_cast<uint32_t>(size))
                     .height(static_cast<uint32_t>(size))
                     .levels(levels)
                     .format(srgb ? Texture::InternalFormat::SRGB8_A8 : Texture::InternalFormat::RGBA8)
                     .usage(Texture::Usage::DEFAULT | Texture::Usage::GEN_MIPMAPPABLE)
                     .sampler(Texture::Sampler::SAMPLER_2D)
                     .build(engine);
    auto* heap = new std::vector<uint8_t>(std::move(rgba));
    t->setImage(engine, 0,
                Texture::PixelBufferDescriptor(heap->data(), heap->size(), Texture::Format::RGBA, Texture::Type::UBYTE,
                                               [](void*, size_t, void* u) { delete static_cast<std::vector<uint8_t>*>(u); }, heap));
    t->generateMipmaps(engine);
    return t;
}

// ---- 手続き生成（CC0素材が無い時の代わり。役割と周波数は素材に合わせてある） ----------------------

struct Generated {
    std::vector<uint8_t> albedo, normal, orm;
};

Generated genAsphalt(int s) {
    auto oct = makeOctaves(11, 8, 5);
    TileNoise grit(s / 2, 12);
    std::vector<float> h(static_cast<size_t>(s) * s);
    Generated g;
    g.albedo.resize(h.size() * 4);
    g.orm.resize(h.size() * 4);
    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            float u = (x + 0.5f) / s, v = (y + 0.5f) / s;
            float base = fbm(oct, u, v);
            float stones = grit.sample(u, v);
            float speck = stones > 0.82f ? (stones - 0.82f) * 4.0f : 0.0f;
            size_t i = static_cast<size_t>(y) * s + x;
            h[i] = base * 0.4f + stones * 0.6f;
            float a = 0.35f + 0.25f * base + 0.35f * speck;
            g.albedo[i * 4] = u8(a);
            g.albedo[i * 4 + 1] = u8(a);
            g.albedo[i * 4 + 2] = u8(a * 1.03f);
            g.albedo[i * 4 + 3] = 255;
            g.orm[i * 4] = u8(0.85f + 0.15f * stones);
            g.orm[i * 4 + 1] = u8(0.55f + 0.3f * (1.0f - stones));
            g.orm[i * 4 + 2] = 0;
            g.orm[i * 4 + 3] = 255;
        }
    }
    g.normal = normalsFromHeight(h, s, s * 0.004f);
    return g;
}

Generated genPaving(int s) {
    auto oct = makeOctaves(21, 4, 4);
    std::vector<float> h(static_cast<size_t>(s) * s);
    Generated g;
    g.albedo.resize(h.size() * 4);
    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            float u = (x + 0.5f) / s, v = (y + 0.5f) / s;
            // 1枚のテクスチャに 1×1 のタイル（0.6m 目地はシェーダ側）。縁に面取り
            float ex = std::min(u, 1 - u), ey = std::min(v, 1 - v);
            float bevel = std::clamp(std::min(ex, ey) * 25.0f, 0.0f, 1.0f);
            float n = fbm(oct, u, v);
            size_t i = static_cast<size_t>(y) * s + x;
            h[i] = bevel * 0.8f + n * 0.2f;
            float a = 0.42f + 0.18f * n;
            g.albedo[i * 4] = u8(a);
            g.albedo[i * 4 + 1] = u8(a * 0.98f);
            g.albedo[i * 4 + 2] = u8(a * 0.95f);
            g.albedo[i * 4 + 3] = 255;
        }
    }
    g.normal = normalsFromHeight(h, s, s * 0.01f);
    return g;
}

Generated genConcreteWall(int s) {
    auto oct = makeOctaves(31, 6, 5);
    TileNoise pores(s / 4, 33);
    std::vector<float> h(static_cast<size_t>(s) * s);
    Generated g;
    g.albedo.resize(h.size() * 4);
    g.orm.resize(h.size() * 4);
    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            float u = (x + 0.5f) / s, v = (y + 0.5f) / s;
            float n = fbm(oct, u, v);
            float p = pores.sample(u, v);
            // パネルの目地（4×4 分割）
            float gu = std::abs(std::fmod(u * 4.0f, 1.0f) - 0.5f), gv = std::abs(std::fmod(v * 4.0f, 1.0f) - 0.5f);
            float joint = (std::max(gu, gv) > 0.485f) ? 1.0f : 0.0f;
            size_t i = static_cast<size_t>(y) * s + x;
            h[i] = n * 0.5f + (p < 0.12f ? -0.5f : 0.0f) - joint * 0.8f;
            float a = (0.45f + 0.2f * n) * (joint > 0 ? 0.6f : 1.0f);
            g.albedo[i * 4] = u8(a);
            g.albedo[i * 4 + 1] = u8(a);
            g.albedo[i * 4 + 2] = u8(a);
            g.albedo[i * 4 + 3] = 255;
            g.orm[i * 4] = u8(joint > 0 ? 0.55f : 0.95f);
            g.orm[i * 4 + 1] = u8(0.5f + 0.2f * n);
            g.orm[i * 4 + 2] = 0;
            g.orm[i * 4 + 3] = 255;
        }
    }
    g.normal = normalsFromHeight(h, s, s * 0.003f);
    return g;
}

std::vector<uint8_t> genBrushedMetal(int s) {
    TileNoise streak(s / 4, 41);
    auto oct = makeOctaves(42, 4, 3);
    std::vector<uint8_t> out(static_cast<size_t>(s) * s * 4);
    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            float u = (x + 0.5f) / s, v = (y + 0.5f) / s;
            // 横方向に引き伸ばした筋
            float st = streak.sample(u * 0.05f, v);
            float a = 0.45f + 0.15f * st + 0.1f * fbm(oct, u, v);
            size_t i = (static_cast<size_t>(y) * s + x) * 4;
            out[i] = out[i + 1] = u8(a);
            out[i + 2] = u8(a * 1.02f);
            out[i + 3] = 255;
        }
    }
    return out;
}

// ---- KTX2 ---------------------------------------------------------------------------------------

Texture* loadKtx2(Engine& engine, Platform& platform, const std::string& path, bool srgb) {
    std::vector<uint8_t> data;
    if (!platform.readAsset(path, data)) return nullptr;
    ktxreader::Ktx2Reader reader(engine, /*quiet=*/true);
    // Android は ASTC / ETC2、デスクトップ（Deck）は BC を優先。どれも無ければ非圧縮
    if (srgb) {
        reader.requestFormat(Texture::InternalFormat::SRGB8_ALPHA8_ASTC_4x4);
        reader.requestFormat(Texture::InternalFormat::ETC2_EAC_SRGBA8);
        reader.requestFormat(Texture::InternalFormat::DXT5_SRGBA);
        reader.requestFormat(Texture::InternalFormat::SRGB8_A8);
    } else {
        reader.requestFormat(Texture::InternalFormat::RGBA_ASTC_4x4);
        reader.requestFormat(Texture::InternalFormat::ETC2_EAC_RGBA8);
        reader.requestFormat(Texture::InternalFormat::DXT5_RGBA);
        reader.requestFormat(Texture::InternalFormat::RGBA8);
    }
    return reader.load(data.data(), data.size(),
                       srgb ? ktxreader::Ktx2Reader::TransferFunction::sRGB : ktxreader::Ktx2Reader::TransferFunction::LINEAR);
}

}  // namespace

TextureSet loadTextures(Engine& engine, Platform& platform, int textureMax) {
    TextureSet set;
    // 素材は 1K 上限（仕様）。プリセットが 512 なら 512 版を使う
    const int size = textureMax >= 1024 ? 1024 : 512;
    const char* suffix = size >= 1024 ? "1k" : "512";
    struct Slot {
        Tex id;
        const char* name;
        bool srgb;
    };
    static const Slot kSlots[] = {
        {Tex::AsphaltAlbedo, "asphalt_albedo", true}, {Tex::AsphaltNormal, "asphalt_normal", false},
        {Tex::AsphaltOrm, "asphalt_orm", false},      {Tex::PavingAlbedo, "paving_albedo", true},
        {Tex::PavingNormal, "paving_normal", false},  {Tex::WallAlbedo, "concrete_albedo", true},
        {Tex::WallNormal, "concrete_normal", false},  {Tex::WallOrm, "concrete_orm", false},
        {Tex::MetalAlbedo, "metal_albedo", true},     {Tex::ConcreteAlbedo, "concrete_albedo", true},
    };
    for (const auto& s : kSlots) {
        std::string path = std::string("textures/") + s.name + "_" + suffix + ".ktx2";
        if (Texture* t = loadKtx2(engine, platform, path, s.srgb)) {
            set.tex[static_cast<int>(s.id)] = t;
            ++set.fromAssets;
        }
    }
    auto need = [&](Tex t) { return set.get(t) == nullptr; };
    if (need(Tex::AsphaltAlbedo) || need(Tex::AsphaltNormal) || need(Tex::AsphaltOrm)) {
        Generated g = genAsphalt(size);
        if (need(Tex::AsphaltAlbedo)) set.tex[static_cast<int>(Tex::AsphaltAlbedo)] = makeTexture(engine, size, true, std::move(g.albedo));
        if (need(Tex::AsphaltNormal)) set.tex[static_cast<int>(Tex::AsphaltNormal)] = makeTexture(engine, size, false, std::move(g.normal));
        if (need(Tex::AsphaltOrm)) set.tex[static_cast<int>(Tex::AsphaltOrm)] = makeTexture(engine, size, false, std::move(g.orm));
    }
    if (need(Tex::PavingAlbedo) || need(Tex::PavingNormal)) {
        Generated g = genPaving(size);
        if (need(Tex::PavingAlbedo)) set.tex[static_cast<int>(Tex::PavingAlbedo)] = makeTexture(engine, size, true, std::move(g.albedo));
        if (need(Tex::PavingNormal)) set.tex[static_cast<int>(Tex::PavingNormal)] = makeTexture(engine, size, false, std::move(g.normal));
    }
    if (need(Tex::WallAlbedo) || need(Tex::WallNormal) || need(Tex::WallOrm) || need(Tex::ConcreteAlbedo)) {
        Generated g = genConcreteWall(size);
        if (need(Tex::ConcreteAlbedo)) set.tex[static_cast<int>(Tex::ConcreteAlbedo)] = makeTexture(engine, size, true, std::vector<uint8_t>(g.albedo));
        if (need(Tex::WallAlbedo)) set.tex[static_cast<int>(Tex::WallAlbedo)] = makeTexture(engine, size, true, std::move(g.albedo));
        if (need(Tex::WallNormal)) set.tex[static_cast<int>(Tex::WallNormal)] = makeTexture(engine, size, false, std::move(g.normal));
        if (need(Tex::WallOrm)) set.tex[static_cast<int>(Tex::WallOrm)] = makeTexture(engine, size, false, std::move(g.orm));
    }
    if (need(Tex::MetalAlbedo)) set.tex[static_cast<int>(Tex::MetalAlbedo)] = makeTexture(engine, size, true, genBrushedMetal(size));

    set.sampler = TextureSampler(TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR, TextureSampler::MagFilter::LINEAR,
                                 TextureSampler::WrapMode::REPEAT);
    set.sampler.setAnisotropy(8.0f);
    return set;
}

void destroy(Engine& engine, TextureSet& t) {
    for (auto*& tex : t.tex) {
        if (tex) engine.destroy(tex);
        tex = nullptr;
    }
}

// ---- 環境光 -------------------------------------------------------------------------------------

namespace {

// 夜の街から見た空と周囲：地平線付近に街明かりとネオンの色の帯、上は濃紺
float3 environmentRadiance(float3 d, float3 moonDir) {
    float h = d.y;
    float3 zenith{0.010f, 0.012f, 0.035f};
    float3 horizon{0.16f, 0.055f, 0.10f};
    float t = std::clamp(h / 0.45f, 0.0f, 1.0f);
    t = t * t * (3 - 2 * t);
    float3 sky = horizon + (zenith - horizon) * t;
    if (h < 0.0f) {
        // 下半分：濡れた路面と周囲のビルの明かり（暗めの色の帯）
        float az = std::atan2(d.z, d.x);
        float band = 0.5f + 0.5f * std::sin(az * 7.0f) * std::sin(az * 3.0f + 1.0f);
        float3 neonA{0.35f, 0.02f, 0.25f}, neonB{0.0f, 0.2f, 0.35f};
        sky = (neonA + (neonB - neonA) * band) * (0.35f + 0.65f * std::exp(h * 6.0f)) * 0.6f;
    } else if (h < 0.3f) {
        // 地平線上のビル窓と看板（明るい点の帯）
        float az = std::atan2(d.z, d.x);
        float windows = std::pow(0.5f + 0.5f * std::sin(az * 90.0f) * std::sin(h * 160.0f), 8.0f);
        float3 warm{1.0f, 0.7f, 0.45f};
        sky += warm * windows * (1.0f - h / 0.3f) * 0.8f;
        float signs = std::pow(std::max(0.0f, std::sin(az * 11.0f + 0.5f)), 30.0f) * (1.0f - h / 0.3f);
        sky += float3{1.0f, 0.1f, 0.6f} * signs * 3.0f;
        float signs2 = std::pow(std::max(0.0f, std::sin(az * 13.0f + 2.0f)), 30.0f) * (1.0f - h / 0.3f);
        sky += float3{0.0f, 0.8f, 1.0f} * signs2 * 3.0f;
    }
    float mu = dot(d, moonDir);
    sky += float3{0.9f, 0.95f, 1.0f} * (std::pow(std::max(mu, 0.0f), 400.0f) * 30.0f);
    return sky;
}

float3 cubeDir(int face, float u, float v) {
    // OpenGL のキューブマップ面の向き（+X, -X, +Y, -Y, +Z, -Z）
    float a = 2 * u - 1, b = 2 * v - 1;
    switch (face) {
        case 0: return normalize(float3{1, -b, -a});
        case 1: return normalize(float3{-1, -b, a});
        case 2: return normalize(float3{a, 1, b});
        case 3: return normalize(float3{a, -1, -b});
        case 4: return normalize(float3{a, -b, 1});
        default: return normalize(float3{-a, -b, -1});
    }
}

}  // namespace

Ibl makeIbl(Engine& engine, Platform& platform, float3 moonDir) {
    Ibl ibl;
    // cmgen の出力（tools/convert_assets.py が Poly Haven のHDRIから作る）
    std::vector<uint8_t> data;
    if (platform.readAsset("ibl/night_ibl.ktx", data)) {
        auto* bundle = new image::Ktx1Bundle(data.data(), static_cast<uint32_t>(data.size()));
        float3 sh[9];
        bool hasSh = bundle->getSphericalHarmonics(sh);
        ibl.reflections = ktxreader::Ktx1Reader::createTexture(&engine, bundle, false);
        IndirectLight::Builder b;
        b.reflections(ibl.reflections);
        if (hasSh) b.irradiance(3, sh);
        ibl.light = b.intensity(60.0f).build(engine);
        ibl.fromAssets = true;
        return ibl;
    }

    // 手続き生成：128² のキューブマップ（RGBA16F）→ GPUでGGXプリフィルタ
    const int s = 128;
    ibl.environment = Texture::Builder()
                          .width(s)
                          .height(s)
                          .levels(static_cast<uint8_t>(std::log2(s) + 1))
                          .format(Texture::InternalFormat::RGBA16F)
                          .sampler(Texture::Sampler::SAMPLER_CUBEMAP)
                          .usage(Texture::Usage::DEFAULT | Texture::Usage::GEN_MIPMAPPABLE)
                          .build(engine);
    auto* pixels = new std::vector<uint16_t>(static_cast<size_t>(s) * s * 6 * 4);
    for (int f = 0; f < 6; ++f) {
        for (int y = 0; y < s; ++y) {
            for (int x = 0; x < s; ++x) {
                float3 c = environmentRadiance(cubeDir(f, (x + 0.5f) / s, (y + 0.5f) / s), moonDir);
                const size_t o = ((static_cast<size_t>(f) * s + y) * s + x) * 4;
                const math::half h[4] = {math::half(c.x), math::half(c.y), math::half(c.z), math::half(1.0f)};
                std::memcpy(&(*pixels)[o], h, sizeof(h));
            }
        }
    }
    const size_t faceBytes = static_cast<size_t>(s) * s * 4 * sizeof(uint16_t);
    ibl.environment->setImage(engine, 0, 0, 0, 0, s, s, 6,
                              Texture::PixelBufferDescriptor(pixels->data(), faceBytes * 6, Texture::Format::RGBA,
                                                             Texture::Type::HALF,
                                                             [](void*, size_t, void* u) { delete static_cast<std::vector<uint16_t>*>(u); },
                                                             pixels));
    ibl.environment->generateMipmaps(engine);
    {
        IBLPrefilterContext context(engine);
        IBLPrefilterContext::SpecularFilter filter(context);
        ibl.reflections = filter(ibl.environment);
    }
    // 放射輝度の単位は「露出後の相対値」なので、強度でcd/m²相当に揃える
    ibl.light = IndirectLight::Builder().reflections(ibl.reflections).intensity(45.0f).build(engine);
    return ibl;
}

void destroy(Engine& engine, Ibl& ibl) {
    if (ibl.light) engine.destroy(ibl.light);
    if (ibl.reflections) engine.destroy(ibl.reflections);
    if (ibl.environment) engine.destroy(ibl.environment);
    ibl = {};
}

}  // namespace bench::render

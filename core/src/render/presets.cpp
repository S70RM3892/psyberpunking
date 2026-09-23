// プリセット → Filament の View 設定。
// 内部解像度とアップスケール = DynamicResolutionOptions（FSR、スケール固定、自動調整OFF）
// 影 = ShadowOptions（光源側）/ SSR = ScreenSpaceReflectionsOptions / SSAO = AmbientOcclusionOptions
#include <algorithm>

#include <filament/ColorGrading.h>
#include <filament/Options.h>
#include <filament/ToneMapper.h>

#include "render.h"

namespace bench::render {

void applyPreset(Engine& engine, View& view, const Preset& p, const SceneConfig& cfg) {
    // ---- 内部解像度とFSR ----
    DynamicResolutionOptions dr;
    dr.enabled = p.renderScale < 0.999f;
    dr.homogeneousScaling = true;
    dr.minScale = {p.renderScale, p.renderScale};
    dr.maxScale = {p.renderScale, p.renderScale};
    // HIGH = AMD FidelityFX FSR1（EASU + RCAS）
    dr.quality = p.upscaler == "fsr" ? QualityLevel::HIGH : QualityLevel::LOW;
    dr.sharpness = 0.9f;
    view.setDynamicResolutionOptions(dr);

    // ---- アンチエイリアス（TAA） ----
    TemporalAntiAliasingOptions taa;
    taa.enabled = p.taa;
    taa.feedback = 0.1f;
    taa.filterWidth = 1.0f;
    taa.sharpness = 0.25f;
    taa.boxClipping = TemporalAntiAliasingOptions::BoxClipping::ACCURATE;
    taa.jitterPattern = TemporalAntiAliasingOptions::JitterPattern::HALTON_23_X16;
    view.setTemporalAntiAliasingOptions(taa);
    view.setAntiAliasing(p.taa ? AntiAliasing::NONE : AntiAliasing::FXAA);

    // ---- SSAO ----
    AmbientOcclusionOptions ao;
    ao.enabled = p.ssao.enabled;
    ao.resolution = p.ssao.halfRes ? 0.5f : 1.0f;
    ao.radius = 0.45f;
    ao.intensity = 1.1f;
    ao.power = 1.2f;
    ao.quality = p.ssao.quality == Quality::Low ? QualityLevel::LOW
               : p.ssao.quality == Quality::Medium ? QualityLevel::MEDIUM : QualityLevel::HIGH;
    ao.upsampling = p.ssao.halfRes ? QualityLevel::MEDIUM : QualityLevel::HIGH;
    view.setAmbientOcclusionOptions(ao);

    // ---- SSR ----
    // Filament の SSR には「解像度」「ステップ数」の直接の設定がない。
    // ステップ数は 最大距離 ÷ 刻み に比例するので、刻み（stride）と最大距離で表す。1/2解像度相当は刻み2。
    ScreenSpaceReflectionsOptions ssr;
    ssr.enabled = p.ssr.enabled;
    ssr.stride = p.ssr.halfRes ? 2.0f : 1.0f;
    ssr.maxDistance = 4.0f + 0.5f * static_cast<float>(p.ssr.steps);
    ssr.thickness = 0.35f;
    ssr.bias = 0.02f;
    view.setScreenSpaceReflectionsOptions(ssr);

    // ---- 距離フォグ（遠景のかすみ。ボリューメトリックとは別で、全プリセット共通） ----
    FogOptions fog;
    fog.enabled = true;
    fog.distance = 20.0f;
    fog.density = 0.012f;
    fog.height = 0.0f;
    // 高さ方向の減衰を緩めて、高層の上の方まで街明かりのもやをかける（遠景の奥行き）
    fog.heightFalloff = 0.02f;
    fog.maximumOpacity = 0.92f;
    fog.color = {0.30f, 0.16f, 0.26f};  // 雨雲に映るネオンの赤紫
    fog.inScatteringStart = 10.0f;
    fog.inScatteringSize = 40.0f;
    view.setFogOptions(fog);

    // ---- ブルーム（レンズフレアは省略：仕様） ----
    BloomOptions bloom;
    bloom.enabled = p.bloom;
    bloom.strength = 0.14f;
    bloom.levels = 7;
    bloom.resolution = 384;
    bloom.threshold = true;
    bloom.highlight = 900.0f;
    bloom.lensFlare = false;
    bloom.quality = QualityLevel::MEDIUM;
    view.setBloomOptions(bloom);

    // ---- 被写界深度 ----
    DepthOfFieldOptions dof;
    dof.enabled = p.dof;
    dof.cocScale = 0.35f;
    dof.maxApertureDiameter = 0.01f;
    dof.nativeResolution = false;
    dof.filter = DepthOfFieldOptions::Filter::MEDIAN;
    view.setDepthOfFieldOptions(dof);

    // ---- ビネット・トーンマップ ----
    VignetteOptions vig;
    vig.enabled = true;
    vig.midPoint = 0.55f;
    vig.roundness = 0.6f;
    vig.feather = 0.6f;
    view.setVignetteOptions(vig);

    RenderQuality rq;
    rq.hdrColorBuffer = QualityLevel::MEDIUM;  // R11G11B10F
    view.setRenderQuality(rq);

    // ---- 影の方式（PCF）とスクリーンスペース影の有無は光源側。ここでは View 全体の設定 ----
    view.setShadowType(ShadowType::PCF);
    view.setShadowingEnabled(true);
    view.setScreenSpaceRefractionEnabled(false);
    (void)engine;
    (void)cfg;
}

}  // namespace bench::render

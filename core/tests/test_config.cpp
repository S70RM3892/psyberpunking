#include <gtest/gtest.h>

#include "test_util.h"

using namespace bench;

TEST(Config, LoadsSceneV1) {
    SceneConfig c = loadScene();
    EXPECT_EQ(c.version, "1.0.0");
    EXPECT_EQ(c.seed, 2077u);
    EXPECT_EQ(c.width, 1280);
    EXPECT_EQ(c.height, 800);
    ASSERT_EQ(c.presets.size(), 4u);
    EXPECT_EQ(c.presets[0].name, "low");
    EXPECT_EQ(c.presets[1].name, "deck");
    EXPECT_EQ(c.presets[2].name, "medium");
    EXPECT_EQ(c.presets[3].name, "high");
    ASSERT_EQ(c.cameraPath.sections.size(), 3u);
    EXPECT_EQ(c.cameraPath.sections[1].name, "overpass");
}

TEST(Config, DeckPresetMatchesSpec) {
    SceneConfig c = loadScene();
    const Preset* d = c.findPreset("deck");
    ASSERT_NE(d, nullptr);
    EXPECT_FLOAT_EQ(d->renderScale, 0.67f);  // FSR Quality 相当
    EXPECT_EQ(d->shadow.cascades, 3);
    EXPECT_EQ(d->shadow.map, 1024);
    EXPECT_EQ(d->shadow.localLights, 8);
    EXPECT_EQ(d->shadow.localMap, 512);
    EXPECT_TRUE(d->shadow.contact);
    EXPECT_TRUE(d->ssao.halfRes);
    EXPECT_EQ(d->ssr.steps, 24);
    EXPECT_EQ(d->fog.grid[0], 120);
    EXPECT_EQ(d->fog.grid[1], 75);
    EXPECT_EQ(d->fog.grid[2], 64);
    EXPECT_EQ(c.crowdCountFor(*d), 60);
    EXPECT_EQ(d->textureMax, 1024);
    EXPECT_TRUE(d->dof);
    EXPECT_FALSE(d->motionBlur);
}

TEST(Config, PresetCrowdCounts) {
    SceneConfig c = loadScene();
    EXPECT_EQ(c.crowdCountFor(*c.findPreset("low")), 30);
    EXPECT_EQ(c.crowdCountFor(*c.findPreset("medium")), 120);
    EXPECT_EQ(c.crowdCountFor(*c.findPreset("high")), 200);
}

TEST(Config, SectionLookup) {
    SceneConfig c = loadScene();
    EXPECT_EQ(c.sectionAt(0.0), 0);
    EXPECT_EQ(c.sectionAt(19.99), 0);
    EXPECT_EQ(c.sectionAt(20.0), 1);
    EXPECT_EQ(c.sectionAt(45.0), 2);
    EXPECT_EQ(c.sectionAt(60.0), 2);
}

TEST(Config, ReportsMissingKey) {
    std::string err;
    auto c = parseSceneConfig(R"({"version":"1","seed":1})", &err);
    EXPECT_FALSE(c.has_value());
    EXPECT_NE(err.find("target"), std::string::npos) << err;
}

TEST(Config, ReportsBadType) {
    std::string text = readFile(BENCH_SCENE_JSON);
    auto pos = text.find("\"seed\": 2077");
    ASSERT_NE(pos, std::string::npos);
    text.replace(pos, 12, "\"seed\": \"x\"");
    std::string err;
    EXPECT_FALSE(parseSceneConfig(text, &err).has_value());
    EXPECT_NE(err.find("seed"), std::string::npos) << err;
}

TEST(Config, RejectsGarbage) {
    std::string err;
    EXPECT_FALSE(parseSceneConfig("{not json", &err).has_value());
    EXPECT_FALSE(err.empty());
}

TEST(Config, BaselineRoundTrip) {
    std::string text = readFile(BENCH_SCENE_JSON);
    std::string out = writeBaseline(text, {40.12, 32.5});
    std::string err;
    auto c = parseSceneConfig(out, &err);
    ASSERT_TRUE(c.has_value()) << err;
    ASSERT_TRUE(c->baseline.has_value());
    EXPECT_DOUBLE_EQ(c->baseline->avgFps, 40.12);
    EXPECT_DOUBLE_EQ(c->baseline->low1Fps, 32.5);
    // キーの順番は保たれる（version が先頭のまま）
    EXPECT_EQ(out.find("\"version\""), out.find_first_of('"'));
}

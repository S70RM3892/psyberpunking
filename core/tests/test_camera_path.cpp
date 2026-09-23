#include <gtest/gtest.h>

#include "bench/camera_path.h"
#include "test_util.h"

using namespace bench;

namespace {
const CameraPath& path() {
    static CameraPath p = [] {
        SceneConfig cfg = loadScene();
        return buildCameraPath(cfg, generateCity(cfg));
    }();
    return p;
}
}  // namespace

TEST(CameraPath, SixtySecondsAt30Hz) {
    EXPECT_EQ(path().rateHz, 30);
    EXPECT_EQ(path().keys.size(), 1801u);
    EXPECT_DOUBLE_EQ(path().duration(), 60.0);
}

TEST(CameraPath, SmoothMotion) {
    const auto& k = path().keys;
    for (size_t i = 1; i < k.size(); ++i) {
        // 1/30秒で最大 ~16m/s → 0.6m 未満、視線は 3度未満しか回らない
        ASSERT_LT(length(k[i].position - k[i - 1].position), 0.6f) << "key " << i;
        ASSERT_GT(dot(k[i].forward, k[i - 1].forward), std::cos(3.0f * 3.14159f / 180.f)) << "key " << i;
    }
}

TEST(CameraPath, SectionsVisitTheirPlaces) {
    SceneConfig cfg = loadScene();
    CityData city = generateCity(cfg);
    // 区間2の中ほどでは高架の上、区間3の最後は路地の中
    CameraKey mid = path().sample(30.0);
    EXPECT_GT(mid.position.y, city.overpassY);
    CameraKey end = path().sample(59.9);
    EXPECT_NEAR(end.position.z, city.alleyZ, 1.5f);
    EXPECT_GT(end.position.x, city.alleyX0);
    CameraKey start = path().sample(0.0);
    EXPECT_LT(start.position.y, 3.0f);
}

TEST(CameraPath, SerializeRoundTrip) {
    std::vector<uint8_t> bytes = path().serialize();
    CameraPath q;
    ASSERT_TRUE(q.deserialize(bytes.data(), bytes.size()));
    ASSERT_EQ(q.keys.size(), path().keys.size());
    EXPECT_EQ(q.keys[100].position, path().keys[100].position);
    bytes.pop_back();
    EXPECT_FALSE(q.deserialize(bytes.data(), bytes.size()));
}

TEST(CameraPath, CommittedBinaryIsUpToDate) {
    std::string bin = readFile(BENCH_CAMERA_BIN);
    ASSERT_FALSE(bin.empty()) << "scenes/camera_path_v1.bin is missing; run benchdeck --bake-camera-path";
    CameraPath q;
    ASSERT_TRUE(q.deserialize(reinterpret_cast<const uint8_t*>(bin.data()), bin.size()));
    ASSERT_EQ(q.keys.size(), path().keys.size());
    for (size_t i = 0; i < q.keys.size(); i += 50) {
        EXPECT_LT(length(q.keys[i].position - path().keys[i].position), 1e-3f) << "key " << i;
    }
}

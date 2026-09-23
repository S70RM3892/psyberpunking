#include <gtest/gtest.h>

#include "bench/animation.h"
#include "bench/procgen.h"
#include "bench/traffic.h"
#include "test_util.h"

using namespace bench;

namespace {
const CityData& city() {
    static CityData c = generateCity(loadScene());
    return c;
}
}  // namespace

TEST(Procgen, DeterministicForSameSeed) {
    SceneConfig cfg = loadScene();
    CityData a = generateCity(cfg);
    CityData b = generateCity(cfg);
    EXPECT_EQ(a.hash(), b.hash());
}

TEST(Procgen, DifferentSeedDifferentCity) {
    SceneConfig cfg = loadScene();
    uint64_t h0 = generateCity(cfg).hash();
    cfg.seed += 1;
    EXPECT_NE(generateCity(cfg).hash(), h0);
}

TEST(Procgen, BuildingCountMatchesConfig) {
    EXPECT_EQ(city().buildings.size(), 200u);
}

TEST(Procgen, LightsMatchConfig) {
    SceneConfig cfg = loadScene();
    int point = 0, spot = 0, street = 0;
    for (const auto& l : city().lights) {
        point += l.kind == LightKind::NeonPoint;
        spot += l.kind == LightKind::NeonSpot;
        street += l.kind == LightKind::Street;
    }
    EXPECT_EQ(point, cfg.lights.neonPoint);
    EXPECT_EQ(spot, cfg.lights.neonSpot);
    EXPECT_EQ(street, cfg.lights.street);
}

TEST(Procgen, LodsGetSimpler) {
    for (const auto& a : city().archetypes) {
        EXPECT_GT(a.lods[0].triangles(), a.lods[1].triangles());
        EXPECT_GT(a.lods[1].triangles(), a.lods[2].triangles());
        EXPECT_FALSE(a.tiers.empty());
    }
}

TEST(Procgen, MeshesAreConsistent) {
    auto check = [](const Mesh& m) {
        ASSERT_EQ(m.positions.size(), m.normals.size());
        ASSERT_EQ(m.positions.size(), m.uvs.size());
        ASSERT_EQ(m.positions.size(), m.custom.size());
        ASSERT_EQ(m.indices.size() % 3, 0u);
        for (uint32_t i : m.indices) ASSERT_LT(i, m.positions.size());
    };
    for (const auto& a : city().archetypes) {
        for (const auto& l : a.lods) {
            check(l.facade);
            check(l.detail);
        }
    }
    for (const auto& t : city().groundTiles) check(t.mesh);
    for (const auto& t : city().overpassTiles) check(t.mesh);
}

TEST(Procgen, GroundFacesUp) {
    for (const auto& t : city().groundTiles) {
        for (size_t i = 0; i < t.mesh.normals.size(); ++i) {
            // 縁石以外（custom.x != 2）は上向き
            if (t.mesh.custom[i].x != 2.0f) ASSERT_GT(t.mesh.normals[i].y, 0.99f);
        }
    }
    for (const auto& t : city().overpassTiles) {
        for (const auto& n : t.mesh.normals) ASSERT_GT(n.y, 0.9f);
    }
}

TEST(Procgen, PathLayout) {
    const CityData& c = city();
    EXPECT_LT(c.startZ, c.rampStartZ);
    EXPECT_LT(c.rampStartZ, c.overpassEndZ);
    EXPECT_LT(c.overpassEndZ, c.downRampEndZ);
    EXPECT_LT(c.downRampEndZ, c.alleyZ);
    EXPECT_GT(c.startZ, 0.f);
    EXPECT_FLOAT_EQ(c.overpassHeightAt(c.rampStartZ + c.rampLength + 1.f), c.overpassY);
    EXPECT_FLOAT_EQ(c.overpassHeightAt(c.startZ), 0.f);
}

TEST(Procgen, SignsNearPathGetLights) {
    const CityData& c = city();
    float maxLit = 0;
    for (const auto& l : c.lights) {
        if (l.kind == LightKind::NeonPoint) maxLit = std::max(maxLit, l.pathDistance);
    }
    // 光源は経路に近い看板から割り当てる
    EXPECT_LT(maxLit, 150.f);
}

TEST(Procgen, CharacterBudget) {
    for (int v = 0; v < 4; ++v) {
        CharacterMesh m = generateCharacter(v);
        EXPECT_NEAR(static_cast<double>(m.mesh.triangleCount()), 13000.0, 2000.0);
        ASSERT_EQ(m.joints.size(), m.mesh.vertexCount());
        for (const auto& w : m.weights) EXPECT_NEAR(w.x + w.y + w.z + w.w, 1.0f, 1e-5f);
    }
}

TEST(Procgen, VehicleBudget) {
    for (int m = 0; m < 3; ++m) {
        Mesh v = generateVehicle(m);
        EXPECT_LE(v.triangleCount(), 5000u);
        EXPECT_GT(v.triangleCount(), 2000u);
    }
}

TEST(Procgen, WalkPoseIsIdentityAtRestish) {
    CharacterRig rig = makeRig();
    std::array<mat4f, CharacterRig::kBoneCount> bones;
    walkPose(rig, 0.0f, 1.0f, bones);
    // 位相0では脚の振りは0（腰のわずかな上下のみ）
    float3 knee = (bones[CharacterRig::LowerLegL] * float4{rig.joint[CharacterRig::LowerLegL], 1}).xyz;
    EXPECT_NEAR(knee.z, 0.0f, 0.02f);
    EXPECT_NEAR(knee.y, rig.joint[CharacterRig::LowerLegL].y, 0.05f);
}

TEST(Traffic, VehiclesDoNotOverlapOnLane) {
    const CityData& c = city();
    auto cars = makeVehicles(c, 30, 2077, 3);
    EXPECT_EQ(cars.size(), 30u);
    for (double t : {0.0, 17.3, 42.0}) {
        for (size_t i = 0; i < cars.size(); ++i) {
            for (size_t j = i + 1; j < cars.size(); ++j) {
                if (cars[i].overpass != cars[j].overpass || cars[i].lane != cars[j].lane) continue;
                float3 a = vehiclePose(c, cars[i], t).position, b = vehiclePose(c, cars[j], t).position;
                EXPECT_GT(length(float2{a.x - b.x, a.z - b.z}), 5.0f);
            }
        }
    }
}

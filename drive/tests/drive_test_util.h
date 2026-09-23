#pragma once

#include <memory>

#include "bench/procgen.h"
#include "drive/collision.h"
#include "test_util.h"

// 何も無い平らな広い土地（車の運動だけを見るテスト用）
inline bench::CityData openField() {
    bench::CityData c;
    c.bounds.min = {-5000.f, 0.f, -5000.f};
    c.bounds.max = {5000.f, 100.f, 5000.f};
    c.rampStartZ = c.overpassEndZ = c.downRampEndZ = 1e9f;  // 高架なし
    c.boulevardX = 1e9f;
    return c;
}

// 本物の街（シーン定義から生成。テスト全体で1回）
inline const bench::CityData& realCity() {
    static bench::CityData c = bench::generateCity(loadScene());
    return c;
}

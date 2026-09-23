// 歩行アニメーション（CPUでボーン行列を作り、スキニングはGPUで行う）
#pragma once

#include <array>

#include <math/mat4.h>

#include "bench/procgen.h"

namespace bench {

using filament::math::mat4f;

// phase は歩行周期の位相 [0, 1)。out はモデル空間のスキニング行列（バインドポーズは単位行列）
void walkPose(const CharacterRig& rig, float phase, float stride, std::array<mat4f, CharacterRig::kBoneCount>& out);

}  // namespace bench

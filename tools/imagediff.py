#!/usr/bin/env python3
"""Linux版とAPKの描画の一致チェック（固定カメラ5点の画像差分）。

  tools/imagediff.py DIR_A DIR_B [--threshold 29] [--downsample 4] [--max-bias 5.5] [--diff OUT_DIR]

DIR_A と DIR_B の同名PNG（shot_*.png）を比べる。画像ごとに
  - PSNR[dB]：--downsample 倍に縮小（平均）してから計算。1画素単位の揺れ（雨粒・ディザ・TAA のジッタ・
    ハッシュのセル境界）ではなく、物の欠け・色や明るさのずれ・カメラ違いを見るため
  - 色の偏り：チャンネルごとの平均差の最大（0〜255）。露出やトーンマップの狂いを見る
を出し、どちらか1つでも外れた画像があれば終了コード 1。--diff を付けると差の強調画像を書き出す。

基準の根拠（docs/SPEC_NOTES.md「描画一致」）：同じ Linux 版を別の Vulkan 実装（lavapipe と SwiftShader）で
描くだけで等倍 PSNR は 25.6dB まで落ちる。仕様の「等倍 40dB」は同じ GPU 実装どうしでしか成り立たないので、
基準画像はエミュレータと同じ SwiftShader で描き、縮小 PSNR と色の偏りで判定する。
"""
from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

try:
    import numpy as np
    from PIL import Image
except ImportError:
    sys.exit("numpy と Pillow が必要です: pip install numpy pillow")


def load(p: Path, factor: int) -> "np.ndarray":
    im = Image.open(p).convert("RGB")
    if factor > 1:
        im = im.resize((im.width // factor, im.height // factor), Image.BOX)
    return np.asarray(im, dtype=np.float64)


def psnr(a: "np.ndarray", b: "np.ndarray") -> float:
    mse = float(np.mean((a - b) ** 2))
    return math.inf if mse == 0 else 10.0 * math.log10(255.0 ** 2 / mse)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a", type=Path)
    ap.add_argument("b", type=Path)
    ap.add_argument("--threshold", type=float, default=29.0, help="縮小後の PSNR の下限 [dB]")
    ap.add_argument("--downsample", type=int, default=4, help="比べる前の縮小倍率（1 で等倍）")
    ap.add_argument("--max-bias", type=float, default=5.5, help="チャンネル平均差の上限（0〜255）")
    ap.add_argument("--diff", type=Path)
    args = ap.parse_args()

    names = sorted(p.name for p in args.a.glob("shot_*.png"))
    if not names:
        print(f"no shot_*.png in {args.a}")
        return 1
    worst = math.inf
    worst_bias = 0.0
    failed = False
    for n in names:
        pb = args.b / n
        if not pb.exists():
            print(f"{n}: missing in {args.b}")
            failed = True
            continue
        full_a, full_b = load(args.a / n, 1), load(pb, 1)
        if full_a.shape != full_b.shape:
            print(f"{n}: size differs {full_a.shape} vs {full_b.shape}")
            failed = True
            continue
        v = psnr(load(args.a / n, args.downsample), load(pb, args.downsample))
        bias = float(np.max(np.abs((full_b - full_a).mean(axis=(0, 1)))))
        worst, worst_bias = min(worst, v), max(worst_bias, bias)
        ok = v >= args.threshold and bias <= args.max_bias
        failed |= not ok
        print(f"{n}: PSNR(1/{args.downsample}) {v:6.2f} dB  full {psnr(full_a, full_b):6.2f} dB  "
              f"bias {bias:4.1f}  {'OK' if ok else 'FAIL'}")
        if args.diff:
            args.diff.mkdir(parents=True, exist_ok=True)
            d = np.clip(np.abs(full_a - full_b) * 8.0, 0, 255).astype(np.uint8)
            Image.fromarray(d).save(args.diff / n)
    print(f"worst PSNR {worst:.2f} dB (>= {args.threshold}), worst bias {worst_bias:.1f} (<= {args.max_bias})"
          f" -> {'FAIL' if failed else 'PASS'}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

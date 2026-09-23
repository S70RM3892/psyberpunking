#!/usr/bin/env python3
"""Linux版とAPKの描画の同一性チェック（仕様：固定カメラ5点の画像差分、PSNR 40dB以上）。

  tools/imagediff.py DIR_A DIR_B [--threshold 40] [--diff OUT_DIR]

DIR_A と DIR_B の同名PNG（shot_*.png）を比べ、画像ごとの PSNR[dB] を表示する。
1枚でも閾値を下回れば終了コード 1。--diff を付けると差の強調画像を書き出す。
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


def load(p: Path) -> "np.ndarray":
    return np.asarray(Image.open(p).convert("RGB"), dtype=np.float64)


def psnr(a: "np.ndarray", b: "np.ndarray") -> float:
    mse = float(np.mean((a - b) ** 2))
    return math.inf if mse == 0 else 10.0 * math.log10(255.0 ** 2 / mse)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a", type=Path)
    ap.add_argument("b", type=Path)
    ap.add_argument("--threshold", type=float, default=40.0)
    ap.add_argument("--diff", type=Path)
    args = ap.parse_args()

    names = sorted(p.name for p in args.a.glob("shot_*.png"))
    if not names:
        print(f"no shot_*.png in {args.a}")
        return 1
    worst = math.inf
    failed = False
    for n in names:
        pb = args.b / n
        if not pb.exists():
            print(f"{n}: missing in {args.b}")
            failed = True
            continue
        a, b = load(args.a / n), load(pb)
        if a.shape != b.shape:
            print(f"{n}: size differs {a.shape} vs {b.shape}")
            failed = True
            continue
        v = psnr(a, b)
        worst = min(worst, v)
        ok = v >= args.threshold
        failed |= not ok
        print(f"{n}: PSNR {v:6.2f} dB  {'OK' if ok else 'FAIL'}")
        if args.diff:
            args.diff.mkdir(parents=True, exist_ok=True)
            d = np.clip(np.abs(a - b) * 8.0, 0, 255).astype(np.uint8)
            Image.fromarray(d).save(args.diff / n)
    print(f"worst {worst:.2f} dB (threshold {args.threshold} dB) -> {'FAIL' if failed else 'PASS'}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

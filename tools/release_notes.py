#!/usr/bin/env python3
"""リリースノートを作る（仕様：シーン定義の版、スコア互換性の有無、Deck基準値、既知の問題）。

  tools/release_notes.py v1.2.0 [--signing release|debug] > NOTES.md

スコア互換性は、前のタグのシーン定義の version と比べて決める（version が同じなら互換）。
"""
from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCENE = "scenes/scene_v1.json"


def git(*args: str) -> str:
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, check=False).stdout.strip()


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("tag", nargs="?", default="HEAD")
    ap.add_argument("--signing", choices=["release", "debug"], default="release")
    args = ap.parse_args()
    tag = args.tag
    scene = json.loads((ROOT / SCENE).read_text())
    prev_tag = git("describe", "--tags", "--abbrev=0", f"{tag}^") if tag != "HEAD" else ""
    prev_version = None
    if prev_tag:
        blob = git("show", f"{prev_tag}:{SCENE}")
        if blob:
            prev_version = json.loads(blob).get("version")

    baseline = scene.get("baseline")
    if prev_version is None:
        compat = "初回リリース（比較対象なし）"
    elif prev_version == scene["version"]:
        compat = f"あり（シーン定義 {scene['version']} は {prev_tag} と同じ。スコアはそのまま比べられる）"
    else:
        compat = f"**なし**（シーン定義が {prev_version} → {scene['version']}。{prev_tag} 以前のスコアとは比べられない）"

    known = []
    if args.signing == "debug":
        known.append("この APK は Android SDK の **debug 鍵**で署名している（正式な署名鍵がまだ無いため。中身は最適化済みのリリース版）。"
                     "正式な鍵で署名した版が出たら上書き更新はできないので、一度アンインストールしてから入れ直すこと。")
    if not baseline:
        known.append("Deck 基準値（baseline）が未設定のため、スコアは表示されない（fps のみ）。"
                     "Steam Deck でキャリブレーション後に baseline を書き込んだ版から表示される。")
    known.append("計測の比較は Deck プリセットのみ。Low / Medium / High は fps のみ。")
    known.append("Filament の制約：1ビューの光源は最大255灯（本ベンチはカメラに近い250灯を使う）。")

    lines = [
        f"## BenchDeck {tag}",
        "",
        "| 項目 | 値 |",
        "| --- | --- |",
        f"| シーン定義の版 | {scene['version']} |",
        f"| スコア互換性 | {compat} |",
        "| 署名 | " + ("Android SDK の debug 鍵（暫定）" if args.signing == "debug" else "正式なリリース鍵（APK Signature Scheme v2/v3）") + " |",
        f"| Deck 基準値 | " + (f"平均 {baseline['avg_fps']} fps / 1% low {baseline['low1_fps']} fps" if baseline else "未設定") + " |",
        "| 描画エンジン | Filament v1.77.1（Vulkan） |",
        "| Android | 11 以上（API 30+）、Vulkan 1.1 以上、arm64-v8a、16KBページ対応 |",
        "",
        "### インストール",
        "",
        "1. このページから `benchdeck-*-arm64" + ("-debugkey" if args.signing == "debug" else "") + ".apk` をダウンロード（`.sha256` で改ざんがないか確認できる）",
        "2. ブラウザやファイラーに「提供元不明のアプリ」のインストールを許可",
        "3. APK を開いてインストール",
        "",
        "Linux 版（Steam Deck の基準取り用）は `benchdeck-*-linux-x86_64.tar.gz` を展開して `./benchdeck`。",
        "",
        "### 既知の問題",
        "",
        *[f"- {k}" for k in known],
        "",
    ]
    print("\n".join(lines))


if __name__ == "__main__":
    main()

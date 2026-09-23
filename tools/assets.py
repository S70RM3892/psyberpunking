#!/usr/bin/env python3
"""CC0 素材の取得と変換。手作業を残さないため、全工程をこのスクリプトで行う。

  tools/assets.py fetch     # assets_src/ にダウンロードし、CREDITS.md を更新
  tools/assets.py convert   # assets/ に KTX2（UASTC＋Zstd、1K と 512）と IBL（cmgen）を書き出す
  tools/assets.py all       # 両方

素材が無くてもベンチは動く（同じ役割のテクスチャ・環境光を実行時に手続き生成する）。
変換済みの assets/ は Linux版（--data）と APK（Gradle の benchAssets タスク）の両方にそのまま入る。

必要なもの: Python 3.9+, Pillow（pip install pillow）, tools/fetch_filament.sh 済みの Filament（basisu, cmgen）
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "assets_src"
OUT = ROOT / "assets"
FILAMENT_BIN = ROOT / "third_party" / "filament" / "linux" / "bin"
CREDITS = ROOT / "CREDITS.md"

# ---- 素材の一覧（すべて CC0 1.0） -------------------------------------------------------------------
# role: 実行時に使う名前（core/src/render/textures.cpp の kSlots と一致させる）
TEXTURES = [
    {"id": "Asphalt026C", "role": "asphalt", "maps": ["albedo", "normal", "orm"]},
    {"id": "PavingStones130", "role": "paving", "maps": ["albedo", "normal"]},
    {"id": "Concrete034", "role": "concrete", "maps": ["albedo", "normal", "orm"]},
    {"id": "MetalPlates006", "role": "metal", "maps": ["albedo"]},
]
HDRI = {"id": "shanghai_bund", "name": "Shanghai Bund", "resolution": "1k"}
SIZES = {"1k": 1024, "512": 512}


def log(msg: str) -> None:
    print(f"[assets] {msg}", flush=True)


def download(url: str, retries: int = 4) -> bytes:
    last = None
    for attempt in range(retries):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "benchdeck-assets/1.0"})
            with urllib.request.urlopen(req, timeout=120) as r:
                return r.read()
        except Exception as e:  # noqa: BLE001 - ネットワークの一時的な失敗は再試行する
            last = e
            log(f"retry {attempt + 1}/{retries}: {url} ({e})")
    raise RuntimeError(f"download failed: {url}: {last}")


def ambientcg_link(asset_id: str) -> str:
    api = f"https://ambientcg.com/api/v2/full_json?id={asset_id}&include=downloadData"
    data = json.loads(download(api))
    for asset in data.get("foundAssets", []):
        for folder in asset["downloadFolders"].values():
            for cat in folder["downloadFiletypeCategories"].values():
                for d in cat["downloads"]:
                    if d["attribute"] == "1K-JPG":
                        return d["downloadLink"]
    raise RuntimeError(f"ambientCG: no 1K-JPG download for {asset_id}")


# ---- fetch ------------------------------------------------------------------------------------------

def fetch() -> None:
    SRC.mkdir(exist_ok=True)
    credits = []
    for t in TEXTURES:
        dest = SRC / t["id"]
        if not dest.exists():
            link = ambientcg_link(t["id"])
            log(f"download {t['id']} ({link})")
            blob = download(link)
            dest.mkdir()
            with zipfile.ZipFile(io.BytesIO(blob)) as z:
                for name in z.namelist():
                    if name.endswith(".jpg"):
                        (dest / Path(name).name).write_bytes(z.read(name))
            (dest / "SOURCE.txt").write_text(f"{link}\nsha256 {hashlib.sha256(blob).hexdigest()}\n")
        credits.append((t["id"], "ambientCG", f"https://ambientcg.com/view?id={t['id']}", "CC0 1.0", f"{t['role']} テクスチャ"))

    hdr = SRC / f"{HDRI['id']}_{HDRI['resolution']}.hdr"
    if not hdr.exists():
        info = json.loads(download(f"https://api.polyhaven.com/files/{HDRI['id']}"))
        url = info["hdri"][HDRI["resolution"]]["hdr"]["url"]
        log(f"download {HDRI['id']} ({url})")
        hdr.write_bytes(download(url))
    credits.append((HDRI["name"], "Poly Haven", f"https://polyhaven.com/a/{HDRI['id']}", "CC0 1.0", "夜景HDRI（環境光）"))
    write_credits(credits)
    log(f"fetched into {SRC.relative_to(ROOT)}/")


def write_credits(rows) -> None:
    lines = [
        "# 素材の出典",
        "",
        "ベンチの街・人・車・看板はすべてコードで手続き生成している。外部の素材は下のテクスチャと環境光だけで、",
        "どれも CC0 1.0（パブリックドメイン相当）。`tools/assets.py fetch` がこの表を書き直す。",
        "実ゲームのアセット・名称・ロゴは一切使っていない。",
        "",
        "| 素材 | 配布元 | URL | ライセンス | 用途 |",
        "| --- | --- | --- | --- | --- |",
    ]
    lines += [f"| {a} | {b} | {c} | {d} | {e} |" for a, b, c, d, e in rows]
    lines += [
        "",
        "## ライブラリ",
        "",
        "| 名前 | ライセンス | 用途 |",
        "| --- | --- | --- |",
        "| [Filament](https://github.com/google/filament) v1.77.1 | Apache-2.0 | 描画エンジン（matc, cmgen, basisu を含む） |",
        "| [nlohmann/json](https://github.com/nlohmann/json) 3.11.3 | MIT | シーン定義・結果のJSON |",
        "| [SDL2](https://www.libsdl.org/) | zlib | Linux版のウィンドウ |",
        "| [GoogleTest](https://github.com/google/googletest) 1.15.2 | BSD-3-Clause | 単体テスト |",
        "| AndroidX / Jetpack Compose | Apache-2.0 | Android版のUI |",
        "",
    ]
    CREDITS.write_text("\n".join(lines))


# ---- convert ----------------------------------------------------------------------------------------

def need_pillow():
    try:
        from PIL import Image  # noqa: F401
    except ImportError:
        sys.exit("Pillow が必要です: pip install pillow")


def find_map(folder: Path, suffix: str) -> Path | None:
    hits = sorted(folder.glob(f"*_{suffix}.jpg"))
    return hits[0] if hits else None


def build_maps(folder: Path, size: int) -> dict[str, "Image.Image"]:
    from PIL import Image

    def load(suffix, mode):
        p = find_map(folder, suffix)
        return Image.open(p).convert(mode).resize((size, size), Image.LANCZOS) if p else None

    maps = {}
    color = load("Color", "RGB")
    if color:
        maps["albedo"] = color.convert("RGBA")
    normal = load("NormalGL", "RGB")  # 接空間 +Y 上（Filament と同じ OpenGL 流儀）
    if normal:
        maps["normal"] = normal.convert("RGBA")
    ao = load("AmbientOcclusion", "L")
    rough = load("Roughness", "L")
    metal = load("Metalness", "L")
    if rough:
        # ORM：R = AO, G = 粗さ, B = 金属度（シェーダの読み方と一致）
        black = Image.new("L", (size, size), 0)
        white = Image.new("L", (size, size), 255)
        maps["orm"] = Image.merge("RGBA", (ao or white, rough, metal or black, white))
    return maps


def basisu(src_png: Path, dst: Path, linear: bool) -> None:
    exe = FILAMENT_BIN / "basisu"
    if not exe.exists():
        sys.exit(f"{exe} がありません。tools/fetch_filament.sh linux を先に実行してください")
    args = [str(exe), "-ktx2", "-uastc", "-uastc_level", "2", "-mipmap", "-output_file", str(dst)]
    if linear:
        args.insert(1, "-linear")
    args.append(str(src_png))
    subprocess.run(args, check=True, stdout=subprocess.DEVNULL)


def convert() -> None:
    need_pillow()
    (OUT / "textures").mkdir(parents=True, exist_ok=True)
    count = 0
    with tempfile.TemporaryDirectory() as tmp:
        for t in TEXTURES:
            folder = SRC / t["id"]
            if not folder.exists():
                sys.exit(f"{folder} がありません。先に tools/assets.py fetch を実行してください")
            for tag, size in SIZES.items():
                maps = build_maps(folder, size)
                for m in t["maps"]:
                    if m not in maps:
                        log(f"skip {t['id']} {m} (source map missing)")
                        continue
                    png = Path(tmp) / f"{t['role']}_{m}_{tag}.png"
                    maps[m].save(png)
                    dst = OUT / "textures" / f"{t['role']}_{m}_{tag}.ktx2"
                    basisu(png, dst, linear=(m != "albedo"))
                    count += 1
    log(f"{count} textures → {(OUT / 'textures').relative_to(ROOT)}/ (UASTC + Zstd, mipmapped)")
    if count > 24:
        sys.exit(f"テクスチャが {count} 枚。仕様の上限（合計24枚）を超えています")

    hdr = SRC / f"{HDRI['id']}_{HDRI['resolution']}.hdr"
    if hdr.exists():
        cmgen = FILAMENT_BIN / "cmgen"
        ibl = OUT / "ibl"
        with tempfile.TemporaryDirectory() as tmp:
            subprocess.run([str(cmgen), "--format=ktx", "--size=256", f"--deploy={tmp}", str(hdr)], check=True,
                           stdout=subprocess.DEVNULL)
            produced = list(Path(tmp).rglob("*_ibl.ktx"))
            if not produced:
                sys.exit("cmgen の出力（*_ibl.ktx）が見つかりません")
            ibl.mkdir(parents=True, exist_ok=True)
            shutil.copy(produced[0], ibl / "night_ibl.ktx")
        log(f"IBL → {(ibl / 'night_ibl.ktx').relative_to(ROOT)}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["fetch", "convert", "all"])
    a = ap.parse_args()
    if a.command in ("fetch", "all"):
        fetch()
    if a.command in ("convert", "all"):
        convert()


if __name__ == "__main__":
    main()

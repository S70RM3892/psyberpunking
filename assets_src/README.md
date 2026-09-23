# assets_src/

`tools/assets.py fetch` が CC0 素材（ambientCG のテクスチャ、Poly Haven の夜景 HDRI）をここへ落とす。
各フォルダの `SOURCE.txt` に取得元 URL と SHA-256、全体の出典は `CREDITS.md`。中身はリポジトリに入れない（.gitignore）。

`tools/assets.py convert` で `assets/`（KTX2 UASTC＋Zstd の 1K / 512 版、cmgen の IBL）に変換される。
素材が無くてもベンチは動く（同じ役割のテクスチャ・環境光を実行時に手続き生成する）が、
**スコアを比べる計測は素材ありの配布物（Release の APK / tar.gz）で行う**こと。

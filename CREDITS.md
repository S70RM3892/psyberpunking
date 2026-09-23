# 素材の出典

ベンチの街・人・車・看板はすべてコードで手続き生成している。外部の素材は下のテクスチャと環境光だけで、
どれも CC0 1.0（パブリックドメイン相当）。`tools/assets.py fetch` がこの表を書き直す。
実ゲームのアセット・名称・ロゴは一切使っていない。

| 素材 | 配布元 | URL | ライセンス | 用途 |
| --- | --- | --- | --- | --- |
| Asphalt026C | ambientCG | https://ambientcg.com/view?id=Asphalt026C | CC0 1.0 | asphalt テクスチャ |
| PavingStones130 | ambientCG | https://ambientcg.com/view?id=PavingStones130 | CC0 1.0 | paving テクスチャ |
| Concrete034 | ambientCG | https://ambientcg.com/view?id=Concrete034 | CC0 1.0 | concrete テクスチャ |
| MetalPlates006 | ambientCG | https://ambientcg.com/view?id=MetalPlates006 | CC0 1.0 | metal テクスチャ |
| Shanghai Bund | Poly Haven | https://polyhaven.com/a/shanghai_bund | CC0 1.0 | 夜景HDRI（環境光） |

## ライブラリ

| 名前 | ライセンス | 用途 |
| --- | --- | --- |
| [Filament](https://github.com/google/filament) v1.77.1 | Apache-2.0 | 描画エンジン（matc, cmgen, basisu を含む） |
| [nlohmann/json](https://github.com/nlohmann/json) 3.11.3 | MIT | シーン定義・結果のJSON |
| [SDL2](https://www.libsdl.org/) | zlib | Linux版のウィンドウ |
| [GoogleTest](https://github.com/google/googletest) 1.15.2 | BSD-3-Clause | 単体テスト |
| AndroidX / Jetpack Compose | Apache-2.0 | Android版のUI |

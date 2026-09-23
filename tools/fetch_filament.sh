#!/usr/bin/env bash
# 固定版のFilament（ランタイム・matc・cmgen・mipgen）を third_party/filament/ に展開する。
# ランタイムとツールの版が違うとマテリアルの読込に失敗するので、必ずこのスクリプト経由で入れる。
#
#   tools/fetch_filament.sh            # linux と android の両方
#   tools/fetch_filament.sh linux      # Linux版だけ（CIの単体テストなど）
set -euo pipefail

VERSION="v1.77.1"
declare -A SHA256=(
  [linux]="ec0f5287a3a2fb801a93fd7b0ffd80c894aac980716d6f91ec48e5370d2d0674"
  [android-native]="3214dbc11c909ba5e6a363ee8e6875b26c7613798b608647f5790aa5f9e6af53"
)
declare -A DEST=( [linux]="linux" [android-native]="android" )

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/third_party/filament"
CACHE="${FILAMENT_CACHE:-$OUT/.cache}"
mkdir -p "$OUT" "$CACHE"

targets=("$@")
if [ ${#targets[@]} -eq 0 ]; then targets=(linux android-native); fi

for t in "${targets[@]}"; do
  [ "$t" = "android" ] && t="android-native"
  file="filament-${VERSION}-${t}.tgz"
  dest="$OUT/${DEST[$t]}"
  if [ -f "$dest/.version" ] && [ "$(cat "$dest/.version")" = "$VERSION" ]; then
    echo "filament $t $VERSION: already installed"
    continue
  fi
  if [ ! -f "$CACHE/$file" ]; then
    echo "downloading $file"
    curl -fsSL --retry 4 -o "$CACHE/$file.part" \
      "https://github.com/google/filament/releases/download/${VERSION}/${file}"
    mv "$CACHE/$file.part" "$CACHE/$file"
  fi
  echo "${SHA256[$t]}  $CACHE/$file" | sha256sum -c - >/dev/null
  rm -rf "$dest" && mkdir -p "$dest"
  tar -xzf "$CACHE/$file" -C "$dest" --strip-components=1
  echo "$VERSION" > "$dest/.version"
  echo "filament $t $VERSION: installed to ${dest#$ROOT/}"
done

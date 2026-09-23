#!/usr/bin/env bash
# エミュレータ（または実機）での自動試験。adb がつながっている端末に対して：
#   1. APK を入れて、Low・1周10秒で計測を自動実行し、結果JSONが出るまで待つ（クラッシュなし）
#   2. 固定カメラ5点を撮って取り出し、Linux版の撮影結果と PSNR を比べる（40dB 以上）
#
#   tools/emulator_test.sh <app.apk> <linux-shots-dir> [out-dir]
set -euo pipefail

APK="$1"
LINUX_SHOTS="$2"
OUT="${3:-emulator-out}"
PKG=dev.zat.benchdeck
ACT=$PKG/.MainActivity
mkdir -p "$OUT"

adb wait-for-device
echo "page size: $(adb shell getconf PAGE_SIZE)"
adb shell getprop ro.build.version.release
adb install -r -g "$APK"

wait_for_log() {  # $1 = 正規表現, $2 = 秒
  local pattern="$1" deadline=$(( $(date +%s) + $2 ))
  while [ "$(date +%s)" -lt "$deadline" ]; do
    if adb logcat -d -s BenchDeck:I AndroidRuntime:E | grep -E "$pattern"; then return 0; fi
    if adb logcat -d -s AndroidRuntime:E | grep -q "FATAL EXCEPTION"; then
      adb logcat -d > "$OUT/logcat.txt"; echo "app crashed"; return 1
    fi
    sleep 5
  done
  adb logcat -d > "$OUT/logcat.txt"
  echo "timeout waiting for: $pattern"
  return 1
}

# ---- 1. 起動〜計測完了 ----
adb logcat -c
adb shell am start -n "$ACT" --ez autorun true --es preset low --ei laps 1 --ez warmup false \
  --ef lapSeconds 10 --ez skipCooling true
line=$(wait_for_log "BENCHDECK_RESULT" 900)
result=$(echo "$line" | sed -n 's/.*BENCHDECK_RESULT //p' | tail -1 | tr -d '\r')
if [ -z "$result" ] || [ "$result" = "none" ]; then
  adb logcat -d > "$OUT/logcat.txt"; echo "no result"; exit 1
fi
adb exec-out run-as $PKG cat "$result" > "$OUT/result.json"
python3 - "$OUT/result.json" <<'EOF'
import json, sys
j = json.load(open(sys.argv[1]))
print("summary:", j["summary"], "method:", j["timing_method"], "coverage:", j.get("gpu_coverage"))
assert j["summary"]["frames"] > 0, "no frames recorded"
assert not j["aborted"], j.get("abort_reason")
EOF

# ---- 2. 描画一致 ----
adb logcat -c
adb shell am start -n "$ACT" --es shots shots
wait_for_log "shots written to" 900 >/dev/null
adb shell am force-stop $PKG
mkdir -p "$OUT/shots"
for f in $(adb shell ls /sdcard/Android/data/$PKG/files/shots | tr -d '\r'); do
  adb pull "/sdcard/Android/data/$PKG/files/shots/$f" "$OUT/shots/$f" >/dev/null
done
python3 -m pip install -q numpy pillow >/dev/null 2>&1 || true
python3 tools/imagediff.py "$LINUX_SHOTS" "$OUT/shots" --threshold 40 --diff "$OUT/diff"

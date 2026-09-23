package dev.zat.benchdeck

import android.os.PowerManager

/**
 * 計測前の冷却待ちの判定。
 *
 * サーマルヘッドルームは 1.0 が SEVERE 相当のスロットリングで、0.0 は「1.0 から一定の距離」でしかない
 * （室温や特定の状態には対応しない。AOSP PowerManager#getThermalHeadroom の説明）。
 * そのため端末によっては十分冷えていても 0.5 を下回らない。固定値で待つと永遠に始まらないので、
 * 「その端末がこれ以上冷えない所まで来たら始める」を基本にする：
 *
 *  1. サーマル状態が MODERATE 以上なら待つ（そのまま測ると周回が無効になる）
 *  2. すぐ始めてよい：LIGHT のしきい値 − [THRESHOLD_MARGIN] 未満（しきい値は Android 15 以降で
 *     メーカーが定義している端末だけ取れる）。しきい値が無い端末は目安として [QUICK_START] 未満
 *  3. 下がり止まった：[PLATEAU_WINDOW_MS] の間の低下が [PLATEAU_DROP] 未満
 *  4. [TIMEOUT_MS] 待っても条件を満たさなければ打ち切って始める
 *
 * 開始理由と開始時のヘッドルームは結果 JSON の "cooling" に残すので、条件の違う計測は後から見分けられる。
 */
object CoolingPolicy {
    const val QUICK_START = 0.5f
    const val THRESHOLD_MARGIN = 0.05f
    const val PLATEAU_WINDOW_MS = 60_000L
    const val PLATEAU_DROP = 0.02f
    const val TIMEOUT_MS = 10 * 60_000L

    /** ヘッドルームの記録（elapsedRealtime のミリ秒, 値）。値は NaN を含まない */
    data class Sample(val atMs: Long, val headroom: Float)

    sealed interface Decision {
        /** 待つ。drop は直近 [PLATEAU_WINDOW_MS] の低下量（まだ窓が埋まっていなければ null） */
        data class Wait(val headroom: Float, val target: Float?, val drop: Float?) : Decision
        data class Start(val reason: String, val headroom: Float, val target: Float?) : Decision
    }

    /** LIGHT のしきい値から目標を決める（しきい値が無い・おかしい値なら null） */
    fun targetFrom(lightThreshold: Float?): Float? =
        lightThreshold?.takeIf { !it.isNaN() && it > THRESHOLD_MARGIN && it <= 1f }?.minus(THRESHOLD_MARGIN)

    /**
     * @param status  現在のサーマル状態（PowerManager.THERMAL_STATUS_*）
     * @param samples 冷却待ちを始めてからの記録（古い順）
     * @param startMs 冷却待ちを始めた時刻
     * @param nowMs   現在時刻
     */
    fun decide(status: Int, samples: List<Sample>, target: Float?, startMs: Long, nowMs: Long): Decision {
        val latest = samples.lastOrNull()
        // 1回目の取得は待ち始めとほぼ同時。数秒たっても値が無ければ非対応の端末
        if (latest == null) {
            return if (nowMs - startMs >= 15_000L) Decision.Start("unsupported", Float.NaN, target)
            else Decision.Wait(Float.NaN, target, null)
        }
        val h = latest.headroom
        if (nowMs - startMs >= TIMEOUT_MS) return Decision.Start("timeout", h, target)
        if (status >= PowerManager.THERMAL_STATUS_MODERATE) return Decision.Wait(h, target, null)

        if (target != null && h < target) return Decision.Start("below_light_threshold", h, target)
        // しきい値のある端末はそちらを優先し、固定の目安は使わない
        if (target == null && h < QUICK_START) return Decision.Start("below_quick_start", h, target)

        // 窓の始まりの値（窓より前の最後の記録）と今の値を比べる。窓がまだ埋まっていなければ待つ
        val windowStart = latest.atMs - PLATEAU_WINDOW_MS
        val before = samples.lastOrNull { it.atMs <= windowStart } ?: return Decision.Wait(h, target, null)
        val drop = before.headroom - h
        return if (drop < PLATEAU_DROP) Decision.Start("plateau", h, target) else Decision.Wait(h, target, drop)
    }
}

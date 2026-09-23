package dev.zat.benchdeck

import android.content.Context
import android.os.Build
import android.os.PowerManager
import android.os.SystemClock
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/**
 * サーマル状態の監視。
 *  - addThermalStatusListener の通知 → 状態（NONE..SHUTDOWN）
 *  - 10秒ごとに getThermalHeadroom(10) → ヘッドルーム（0.0 でスロットリングなし、1.0 で SEVERE 相当）
 *    公式の制約：呼び出しは10秒に1回まで。それより頻繁だと NaN が返る
 *    （https://developer.android.com/games/optimize/adpf/thermal）
 * どちらもネイティブへ転送し、フレームごとの記録に入る。冷却待ちの判定用に直近の値も残す。
 */
class ThermalMonitor(context: Context, private val onChange: (status: Int, headroom: Float) -> Unit) {
    private val pm = context.getSystemService(PowerManager::class.java)
    private val _status = MutableStateFlow(pm.currentThermalStatus)
    private val _headroom = MutableStateFlow(Float.NaN)
    val status: StateFlow<Int> = _status
    val headroom: StateFlow<Float> = _headroom
    private var job: Job? = null

    private val history = ArrayDeque<CoolingPolicy.Sample>()

    /** [sinceMs]（elapsedRealtime）以降に取れたヘッドルーム（古い順、NaN は含まない） */
    fun samplesSince(sinceMs: Long): List<CoolingPolicy.Sample> =
        synchronized(history) { history.filter { it.atMs >= sinceMs } }

    /**
     * LIGHT（軽いスロットリング）が始まるヘッドルーム。Android 15 以降で、メーカーがしきい値を
     * 定義している端末だけ取れる（getThermalHeadroomThresholds）。それ以外は null
     */
    val lightThreshold: Float? by lazy {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.VANILLA_ICE_CREAM) null
        else runCatching { pm.thermalHeadroomThresholds[PowerManager.THERMAL_STATUS_LIGHT] }.getOrNull()
    }

    private val listener = PowerManager.OnThermalStatusChangedListener { s ->
        _status.value = s
        onChange(s, _headroom.value)
    }

    fun start(scope: CoroutineScope) {
        if (job != null) return
        pm.addThermalStatusListener(context.mainExecutor, listener)
        job = scope.launch {
            while (isActive) {
                val h = pm.getThermalHeadroom(10)
                // 一時的な NaN（呼び出し間隔の制約など）では直前の有効値を保つ。
                // 一度も値が取れない端末は NaN のまま＝非対応として扱う
                if (!h.isNaN()) {
                    _headroom.value = h
                    val now = SystemClock.elapsedRealtime()
                    synchronized(history) {
                        history.addLast(CoolingPolicy.Sample(now, h))
                        // 判定に使うのは直近 [CoolingPolicy.PLATEAU_WINDOW_MS] ＋余裕だけ
                        while (history.isNotEmpty() && history.first().atMs < now - 3 * CoolingPolicy.PLATEAU_WINDOW_MS) history.removeFirst()
                    }
                }
                val v = _headroom.value
                onChange(_status.value, if (v.isNaN()) -1f else v)
                delay(10_000)
            }
        }
    }

    fun stop() {
        pm.removeThermalStatusListener(listener)
        job?.cancel()
        job = null
    }

    private val context = context.applicationContext
}

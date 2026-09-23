package dev.zat.benchdeck

import android.content.Context
import android.os.PowerManager
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
 * どちらもネイティブへ転送し、フレームごとの記録に入る。
 */
class ThermalMonitor(context: Context, private val onChange: (status: Int, headroom: Float) -> Unit) {
    private val pm = context.getSystemService(PowerManager::class.java)
    private val _status = MutableStateFlow(pm.currentThermalStatus)
    private val _headroom = MutableStateFlow(Float.NaN)
    val status: StateFlow<Int> = _status
    val headroom: StateFlow<Float> = _headroom
    private var job: Job? = null

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
                if (!h.isNaN()) _headroom.value = h
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

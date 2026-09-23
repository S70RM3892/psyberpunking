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
 *  - 1秒ごとに getThermalHeadroom(10) → ヘッドルーム（1.0 で SEVERE 相当のスロットリング）
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
                // 呼び出し間隔は1秒以上あける（それより短いと NaN が返る）
                val h = pm.getThermalHeadroom(10)
                _headroom.value = h
                onChange(_status.value, if (h.isNaN()) -1f else h)
                delay(1000)
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

package dev.zat.benchdeck

import android.app.Application
import android.os.Build
import android.view.Surface
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/** 画面の状態遷移：起動チェック → ホーム → 冷却待ち → 読込 → ウォームアップ → 計測 → 結果（途中離脱は中断） */
sealed interface Screen {
    data object Home : Screen
    data class Cooling(val headroom: Float) : Screen
    data class Running(val state: NativeBench.State) : Screen
    data class Result(val summary: ResultSummary) : Screen
    data class Interrupted(val reason: String) : Screen
    data class Error(val message: String) : Screen
}

data class DeviceCard(val name: String, val gpu: String, val vulkan: String, val driver: String, val refreshHz: Float)

class BenchViewModel(app: Application) : AndroidViewModel(app) {
    val presets = listOf("low", "deck", "medium", "high")

    private val _screen = MutableStateFlow<Screen>(Screen.Home)
    val screen: StateFlow<Screen> = _screen
    private val _preset = MutableStateFlow("deck")
    val preset: StateFlow<String> = _preset
    private val _history = MutableStateFlow<List<ResultSummary>>(emptyList())
    val history: StateFlow<List<ResultSummary>> = _history
    private val _device = MutableStateFlow(DeviceCard(deviceName(), "", "", "", 60f))
    val device: StateFlow<DeviceCard> = _device

    private val store = ResultStore(app)
    private val handle: Long = NativeBench.nativeCreate(app.assets, BuildConfig.VERSION_NAME)
    val thermal = ThermalMonitor(app) { status, headroom ->
        NativeBench.nativeSetThermal(handle, status)
        NativeBench.nativeSetHeadroom(handle, headroom)
    }
    private var runJob: Job? = null

    init {
        val vk = NativeBench.nativeVulkanInfo()
        _device.value = _device.value.copy(gpu = vk[0], driver = vk[1], vulkan = vk[2])
        refreshHistory()
        thermal.start(viewModelScope)
    }

    fun setRefreshRate(hz: Float) {
        _device.value = _device.value.copy(refreshHz = hz)
    }

    fun selectPreset(p: String) {
        _preset.value = p
    }

    fun refreshHistory() {
        _history.value = store.list()
    }

    val isRunning: Boolean get() = _screen.value is Screen.Running || _screen.value is Screen.Cooling

    // ---- Surface（計測画面の SurfaceView から） ----
    fun surfaceCreated(surface: Surface) = NativeBench.nativeSetSurface(handle, surface)
    fun surfaceDestroyed() = NativeBench.nativeSetSurface(handle, null)
    fun onVsync(frameTimeNanos: Long) = NativeBench.nativeOnVsync(handle, frameTimeNanos)

    /** 自動試験用の設定（adb の intent extra から）。通常は 3周＋ウォームアップ、1周60秒 */
    data class RunOptions(val laps: Int = 3, val warmup: Boolean = true, val lapSeconds: Double = 0.0, val skipCooling: Boolean = false)
    var runOptions = RunOptions()
    /** 結果が保存されたら呼ばれる（自動試験で logcat に出してアプリを閉じる） */
    var onFinished: ((java.io.File?) -> Unit)? = null

    /** 計測開始：冷却待ち（ヘッドルーム 0.5 未満）→ ネイティブへ開始を指示 → 状態を 100ms ごとに見る */
    fun start() {
        if (runJob?.isActive == true) return
        runJob = viewModelScope.launch {
            // ---- 冷却待ち ----
            while (isActive && !runOptions.skipCooling) {
                val h = thermal.headroom.value
                if (h.isNaN() || h < 0.5f) break
                _screen.value = Screen.Cooling(h)
                delay(1000)
            }
            val d = _device.value
            val os = "Android ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT})"
            val o = runOptions
            NativeBench.nativeStart(handle, _preset.value, o.laps, o.warmup, o.lapSeconds, arrayOf(d.name, os, d.refreshHz.toString()))
            _screen.value = Screen.Running(NativeBench.State(NativeBench.Phase.LOADING, 0f, 0, 0, 0f))
            // ---- 計測中 ----
            while (isActive) {
                val s = NativeBench.poll(handle)
                when (s.phase) {
                    NativeBench.Phase.DONE -> {
                        finishRun()
                        return@launch
                    }
                    NativeBench.Phase.ABORTED -> {
                        val reason = NativeBench.nativeMessage(handle)
                        // 中断でも完了した周回はJSONに残す（無効周回として記録される）
                        val (file, _) = store.save(_preset.value, NativeBench.nativeResultJson(handle), NativeBench.nativeResultCsv(handle))
                        refreshHistory()
                        _screen.value = Screen.Interrupted(reason)
                        onFinished?.invoke(file)
                        return@launch
                    }
                    NativeBench.Phase.ERROR -> {
                        _screen.value = Screen.Error(NativeBench.nativeMessage(handle))
                        onFinished?.invoke(null)
                        return@launch
                    }
                    else -> _screen.value = Screen.Running(s)
                }
                delay(100)
            }
        }
    }

    private fun finishRun() {
        val json = NativeBench.nativeResultJson(handle)
        val csv = NativeBench.nativeResultCsv(handle)
        val (file, _) = store.save(_preset.value, json, csv)
        refreshHistory()
        val summary = ResultSummary.parse(json, file, System.currentTimeMillis())
        _screen.value = if (summary != null) Screen.Result(summary) else Screen.Error("result parse error")
        onFinished?.invoke(file)
    }

    /** 戻る＝中断確認のあと、あるいは onPause / メモリ不足 / サーマル停止 */
    fun abort(reason: String) {
        if (_screen.value is Screen.Cooling) {
            runJob?.cancel()
            _screen.value = Screen.Home
            return
        }
        if (_screen.value is Screen.Running) NativeBench.nativeAbort(handle, reason)
    }

    fun showResult(r: ResultSummary) {
        _screen.value = Screen.Result(r)
    }

    fun home() {
        _screen.value = Screen.Home
        refreshHistory()
    }

    fun csvFor(r: ResultSummary) = r.file?.let { store.csvFor(it) }

    /** 画像差分用（adb から起動）：固定カメラ5点のRGBA */
    fun renderShots(times: DoubleArray): Array<ByteArray>? = NativeBench.nativeRenderShots(handle, times)

    override fun onCleared() {
        thermal.stop()
        NativeBench.nativeDestroy(handle)
    }

    private fun deviceName(): String {
        val m = Build.MANUFACTURER.replaceFirstChar { it.uppercase() }
        val soc = if (Build.VERSION.SDK_INT >= 31) " / ${Build.SOC_MANUFACTURER} ${Build.SOC_MODEL}" else ""
        return "$m ${Build.MODEL}$soc"
    }
}

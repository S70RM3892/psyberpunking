package dev.zat.benchdeck

import android.content.res.AssetManager
import android.view.Surface

/** C++ コア（libbenchdeck.so）への入口。描画スレッドはネイティブ側が持つ。 */
object NativeBench {
    init {
        System.loadLibrary("benchdeck")
    }

    enum class Phase { IDLE, LOADING, WARMUP, MEASURE, DONE, ABORTED, ERROR }

    data class State(val phase: Phase, val progress: Float, val lap: Int, val section: Int, val fps: Float)

    @JvmStatic external fun nativeCreate(assets: AssetManager, version: String): Long
    @JvmStatic external fun nativeDestroy(handle: Long)
    @JvmStatic external fun nativeSetSurface(handle: Long, surface: Surface?)
    @JvmStatic external fun nativeStart(handle: Long, preset: String, laps: Int, warmup: Boolean, lapSeconds: Double, device: Array<String>)
    @JvmStatic external fun nativeAbort(handle: Long, reason: String)
    @JvmStatic external fun nativeSetThermal(handle: Long, status: Int)
    @JvmStatic external fun nativeSetHeadroom(handle: Long, headroom: Float)
    @JvmStatic external fun nativeOnVsync(handle: Long, frameTimeNanos: Long)
    @JvmStatic external fun nativePoll(handle: Long): IntArray
    @JvmStatic external fun nativeMessage(handle: Long): String
    @JvmStatic external fun nativeResultJson(handle: Long): String
    @JvmStatic external fun nativeResultCsv(handle: Long): String
    @JvmStatic external fun nativeVulkanInfo(): Array<String>
    @JvmStatic external fun nativeRenderShots(handle: Long, times: DoubleArray): Array<ByteArray>?

    fun poll(handle: Long): State {
        val v = nativePoll(handle)
        return State(Phase.entries[v[0].coerceIn(0, Phase.entries.size - 1)], v[1] / 1000f, v[2], v[3], v[4] / 10f)
    }
}

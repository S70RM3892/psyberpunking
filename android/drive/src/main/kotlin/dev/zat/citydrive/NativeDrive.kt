package dev.zat.citydrive

import android.content.res.AssetManager
import android.view.Surface

/** ネイティブ（C++ の DriveHost）への入口。描画と物理は C++ の専用スレッドで動く */
object NativeDrive {
    init {
        System.loadLibrary("citydrive")
    }

    enum class Phase { IDLE, LOADING, RUNNING, PAUSED, ERROR }

    data class State(
        val phase: Phase,
        val progress: Float,
        val speedKmh: Float,
        val gear: Int,
        val rpm: Float,
        val fps: Float,
        val onDeck: Boolean,
        val bonnet: Boolean,
        val slip: Float,
    )

    private val buf = FloatArray(9)

    fun poll(handle: Long): State {
        nativePoll(handle, buf)
        return State(
            phase = Phase.entries[buf[0].toInt().coerceIn(0, Phase.entries.size - 1)],
            progress = buf[1],
            speedKmh = buf[2],
            gear = buf[3].toInt(),
            rpm = buf[4],
            fps = buf[5],
            onDeck = buf[6] > 0.5f,
            bonnet = buf[7] > 0.5f,
            slip = buf[8],
        )
    }

    @JvmStatic external fun nativeCreate(assets: AssetManager): Long
    @JvmStatic external fun nativeDestroy(handle: Long)
    @JvmStatic external fun nativeSetSurface(handle: Long, surface: Surface?)
    @JvmStatic external fun nativeStart(handle: Long, preset: String)
    @JvmStatic external fun nativeSetPaused(handle: Long, paused: Boolean)
    @JvmStatic external fun nativeSetFrameInterval(handle: Long, seconds: Double)
    @JvmStatic external fun nativeSetPad(handle: Long, axes: FloatArray, buttons: IntArray)
    @JvmStatic external fun nativePoll(handle: Long, out: FloatArray)
    @JvmStatic external fun nativeMessage(handle: Long): String
}

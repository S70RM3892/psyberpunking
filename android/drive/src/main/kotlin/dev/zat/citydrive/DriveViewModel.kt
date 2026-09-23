package dev.zat.citydrive

import android.app.Application
import android.view.Surface
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/** 画面：タイトル → 読込 → 走行（一時停止）。エラーはタイトルへ戻れる */
sealed interface Screen {
    data object Title : Screen
    data class Loading(val progress: Float) : Screen
    data object Driving : Screen
    data object Paused : Screen
    data class Error(val message: String) : Screen
}

class DriveViewModel(app: Application) : AndroidViewModel(app) {
    /** 画質：プリセット名と表示名の組 */
    val presets = listOf("low" to R.string.quality_low, "deck" to R.string.quality_standard, "high" to R.string.quality_high)

    private val _screen = MutableStateFlow<Screen>(Screen.Title)
    val screen: StateFlow<Screen> = _screen
    private val _preset = MutableStateFlow("deck")
    val preset: StateFlow<String> = _preset
    private val _hud = MutableStateFlow<NativeDrive.State?>(null)
    val hud: StateFlow<NativeDrive.State?> = _hud
    private val _controller = MutableStateFlow(false)
    val controller: StateFlow<Boolean> = _controller
    private val _controllerName = MutableStateFlow("")
    val controllerName: StateFlow<String> = _controllerName

    private val handle: Long = NativeDrive.nativeCreate(app.assets)
    val gamepad: Gamepad = Gamepad(app) { connected -> onControllerChanged(connected) }

    init {
        gamepad.start()
        _controller.value = gamepad.connected
        _controllerName.value = gamepad.name
        viewModelScope.launch {
            while (isActive) {
                val s = NativeDrive.poll(handle)
                _hud.value = s
                when (s.phase) {
                    NativeDrive.Phase.LOADING -> if (_screen.value is Screen.Loading) _screen.value = Screen.Loading(s.progress)
                    NativeDrive.Phase.RUNNING -> if (_screen.value is Screen.Loading) _screen.value = Screen.Driving
                    NativeDrive.Phase.ERROR -> if (_screen.value !is Screen.Error && _screen.value !is Screen.Title) {
                        _screen.value = Screen.Error(NativeDrive.nativeMessage(handle))
                    }
                    else -> {}
                }
                delay(50)
            }
        }
    }

    private fun onControllerChanged(connected: Boolean) {
        _controller.value = connected
        _controllerName.value = gamepad.name
        // 走行中に抜けたら止める
        if (!connected && _screen.value == Screen.Driving) pause()
    }

    fun setRefreshRate(hz: Float) {
        // 120Hz などの画面では 60fps に揃える（ばらつくより一定の方が滑らかに見える）
        NativeDrive.nativeSetFrameInterval(handle, if (hz > 70f) 1.0 / 60.0 else 0.0)
    }

    fun selectPreset(p: String) {
        _preset.value = p
    }

    fun cyclePreset(dir: Int) {
        val i = presets.indexOfFirst { it.first == _preset.value }
        _preset.value = presets[(i + dir + presets.size) % presets.size].first
    }

    fun start() {
        _screen.value = Screen.Loading(0f)
        NativeDrive.nativeStart(handle, _preset.value)
    }

    fun pause() {
        if (_screen.value != Screen.Driving) return
        NativeDrive.nativeSetPaused(handle, true)
        _screen.value = Screen.Paused
    }

    fun resume() {
        if (_screen.value != Screen.Paused) return
        NativeDrive.nativeSetPaused(handle, false)
        _screen.value = Screen.Driving
    }

    fun quitToTitle() {
        NativeDrive.nativeSetPaused(handle, true)
        _screen.value = Screen.Title
    }

    fun pushPad() = NativeDrive.nativeSetPad(handle, gamepad.axes(), gamepad.buttons())

    fun surfaceCreated(surface: Surface) = NativeDrive.nativeSetSurface(handle, surface)
    fun surfaceDestroyed() = NativeDrive.nativeSetSurface(handle, null)

    override fun onCleared() {
        gamepad.stop()
        NativeDrive.nativeDestroy(handle)
    }
}

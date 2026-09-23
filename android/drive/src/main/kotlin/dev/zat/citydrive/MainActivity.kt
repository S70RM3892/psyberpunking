package dev.zat.citydrive

import android.content.pm.PackageManager
import android.os.Bundle
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import dev.zat.citydrive.ui.DriveUi
import dev.zat.citydrive.ui.UnsupportedScreen
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {
    private val vm: DriveViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        if (!packageManager.hasSystemFeature(PackageManager.FEATURE_VULKAN_HARDWARE_VERSION, 0x401000)) {
            setContent { UnsupportedScreen() }
            return
        }
        vm.setRefreshRate(display?.refreshRate ?: 60f)
        setContent { DriveUi(vm) }

        // 走行中・一時停止中は全画面・画面を点けたまま
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                vm.screen.collect { s ->
                    val immersive = s is Screen.Driving || s is Screen.Paused || s is Screen.Loading
                    val c = WindowCompat.getInsetsController(window, window.decorView)
                    if (immersive) {
                        c.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
                        c.hide(WindowInsetsCompat.Type.systemBars())
                        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                    } else {
                        c.show(WindowInsetsCompat.Type.systemBars())
                        window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                    }
                }
            }
        }
    }

    override fun onPause() {
        super.onPause()
        vm.pause()  // 裏に回ったら止める（戻ったら START で再開）
    }

    override fun dispatchGenericMotionEvent(ev: MotionEvent): Boolean {
        if (vm.gamepad.onMotion(ev)) {
            if (vm.screen.value == Screen.Driving) vm.pushPad()
            return true
        }
        return super.dispatchGenericMotionEvent(ev)
    }

    override fun dispatchKeyEvent(ev: KeyEvent): Boolean {
        val down = ev.action == KeyEvent.ACTION_DOWN && ev.repeatCount == 0
        val code = ev.keyCode
        val isPad = KeyEvent.isGamepadButton(code) || ev.isFromSource(android.view.InputDevice.SOURCE_GAMEPAD) ||
            ev.isFromSource(android.view.InputDevice.SOURCE_DPAD)
        if (!isPad) return super.dispatchKeyEvent(ev)
        when (val s = vm.screen.value) {
            Screen.Driving -> {
                if (down && code == KeyEvent.KEYCODE_BUTTON_START) {
                    vm.pause()
                    return true
                }
                if (vm.gamepad.onKey(ev)) {
                    vm.pushPad()
                    return true
                }
            }
            Screen.Paused -> if (down) when (code) {
                KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_B -> {
                    vm.gamepad.releaseButtons()
                    vm.pushPad()
                    vm.resume()
                    return true
                }
                KeyEvent.KEYCODE_BUTTON_X -> {
                    vm.quitToTitle()
                    return true
                }
            }
            Screen.Title -> if (down) when (code) {
                KeyEvent.KEYCODE_DPAD_LEFT -> { vm.cyclePreset(-1); return true }
                KeyEvent.KEYCODE_DPAD_RIGHT -> { vm.cyclePreset(1); return true }
                KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_START -> { vm.start(); return true }
            }
            is Screen.Error -> if (down && (code == KeyEvent.KEYCODE_BUTTON_A || code == KeyEvent.KEYCODE_BUTTON_B)) {
                vm.quitToTitle()
                return true
            }
            is Screen.Loading -> {}
        }
        // B が「戻る」になってアプリが閉じないよう、ゲームパッドのボタンは全部ここで止める
        return if (KeyEvent.isGamepadButton(code)) true else super.dispatchKeyEvent(ev)
    }
}

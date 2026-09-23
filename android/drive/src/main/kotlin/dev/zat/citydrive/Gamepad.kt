package dev.zat.citydrive

import android.content.Context
import android.hardware.input.InputManager
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.sign

/**
 * ゲームパッドの状態。Activity から MotionEvent / KeyEvent を渡すと、ハンドル・アクセル・ブレーキなどに変換する。
 *
 * 割り当て：左スティック=ハンドル、RT=アクセル、LT=ブレーキ/後退、A=サイドブレーキ（押している間）、
 *         Y=視点切替、B=道路に戻す、右スティック=見回し、SELECT=ライト、START=一時停止
 */
class Gamepad(context: Context, private val onConnectionChanged: (Boolean) -> Unit) : InputManager.InputDeviceListener {
    private val im = context.getSystemService(InputManager::class.java)

    var steer = 0f; private set
    var throttle = 0f; private set
    var brake = 0f; private set
    var lookX = 0f; private set
    var lookY = 0f; private set
    var handbrake = false; private set
    var headlights = true; private set
    var cameraPresses = 0; private set
    var resetPresses = 0; private set

    var connected = false; private set
    var name: String = ""; private set

    fun start() {
        im.registerInputDeviceListener(this, null)
        refresh()
    }

    fun stop() {
        im.unregisterInputDeviceListener(this)
    }

    private fun refresh() {
        val pad = im.inputDeviceIds.map { im.getInputDevice(it) }.firstOrNull { it != null && isGamepad(it) }
        val was = connected
        connected = pad != null
        name = pad?.name ?: ""
        if (!connected) {
            steer = 0f; throttle = 0f; brake = 0f; lookX = 0f; lookY = 0f; handbrake = false
        }
        if (was != connected) onConnectionChanged(connected)
    }

    override fun onInputDeviceAdded(deviceId: Int) = refresh()
    override fun onInputDeviceRemoved(deviceId: Int) = refresh()
    override fun onInputDeviceChanged(deviceId: Int) = refresh()

    /** スティック・トリガー。処理したら true */
    fun onMotion(ev: MotionEvent): Boolean {
        if (ev.source and InputDevice.SOURCE_JOYSTICK != InputDevice.SOURCE_JOYSTICK || ev.action != MotionEvent.ACTION_MOVE) return false
        val dev = ev.device ?: return false
        steer = shape(axis(ev, dev, MotionEvent.AXIS_X), 0.12f)
        lookX = shape(axis(ev, dev, MotionEvent.AXIS_Z), 0.2f)
        lookY = shape(axis(ev, dev, MotionEvent.AXIS_RZ), 0.2f)
        // トリガーは機種によって RTRIGGER/LTRIGGER か GAS/BRAKE のどちらか（両方のことも）
        throttle = max(ev.getAxisValue(MotionEvent.AXIS_RTRIGGER), ev.getAxisValue(MotionEvent.AXIS_GAS)).coerceIn(0f, 1f)
        brake = max(ev.getAxisValue(MotionEvent.AXIS_LTRIGGER), ev.getAxisValue(MotionEvent.AXIS_BRAKE)).coerceIn(0f, 1f)
        return true
    }

    /** ボタン。処理したら true（B などで Activity が閉じないように消費する） */
    fun onKey(ev: KeyEvent): Boolean {
        val fromPad = ev.source and InputDevice.SOURCE_GAMEPAD == InputDevice.SOURCE_GAMEPAD ||
            KeyEvent.isGamepadButton(ev.keyCode)
        if (!fromPad) return false
        val down = ev.action == KeyEvent.ACTION_DOWN
        val first = down && ev.repeatCount == 0
        when (ev.keyCode) {
            KeyEvent.KEYCODE_BUTTON_A -> handbrake = down
            KeyEvent.KEYCODE_BUTTON_Y -> if (first) cameraPresses++
            KeyEvent.KEYCODE_BUTTON_B -> if (first) resetPresses++
            KeyEvent.KEYCODE_BUTTON_SELECT -> if (first) headlights = !headlights
            // トリガーをボタンとして送る機種（アナログ無し）
            KeyEvent.KEYCODE_BUTTON_R2 -> throttle = if (down) 1f else 0f
            KeyEvent.KEYCODE_BUTTON_L2 -> brake = if (down) 1f else 0f
            else -> return KeyEvent.isGamepadButton(ev.keyCode)
        }
        return true
    }

    /** 押しっぱなしの扱いを解く（一時停止から戻る時、サイドブレーキが掛かったままにならないように） */
    fun releaseButtons() {
        handbrake = false
    }

    fun axes() = floatArrayOf(steer, throttle, brake, lookX, lookY)
    fun buttons() = intArrayOf(if (handbrake) 1 else 0, if (headlights) 1 else 0, cameraPresses, resetPresses)

    private fun axis(ev: MotionEvent, dev: InputDevice, a: Int): Float {
        val v = ev.getAxisValue(a)
        val flat = dev.getMotionRange(a, ev.source)?.flat ?: 0f
        return if (abs(v) <= flat) 0f else v
    }

    /** 遊び（deadzone）と、真ん中付近を細かく操作できるカーブ */
    private fun shape(v: Float, deadzone: Float): Float {
        val a = abs(v)
        if (a < deadzone) return 0f
        val n = (a - deadzone) / (1f - deadzone)
        return sign(v) * n * (0.35f + 0.65f * n)
    }

    companion object {
        fun isGamepad(d: InputDevice): Boolean {
            val s = d.sources
            return !d.isVirtual && (s and InputDevice.SOURCE_GAMEPAD == InputDevice.SOURCE_GAMEPAD ||
                s and InputDevice.SOURCE_JOYSTICK == InputDevice.SOURCE_JOYSTICK)
        }
    }
}

package dev.zat.benchdeck

import android.content.ComponentCallbacks2
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.os.Bundle
import android.util.Log
import android.view.Choreographer
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
import dev.zat.benchdeck.ui.BenchDeckApp
import dev.zat.benchdeck.ui.UnsupportedScreen
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.nio.ByteBuffer

class MainActivity : ComponentActivity() {
    private val vm: BenchViewModel by viewModels()
    private var vsyncOn = false
    private val vsync = object : Choreographer.FrameCallback {
        override fun doFrame(frameTimeNanos: Long) {
            vm.onVsync(frameTimeNanos)
            if (vsyncOn) Choreographer.getInstance().postFrameCallback(this)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        // マニフェストの uses-feature で通常はインストール自体を防ぐが、念のため起動時にも確かめる
        if (!packageManager.hasSystemFeature(PackageManager.FEATURE_VULKAN_HARDWARE_VERSION, 0x401000)) {
            setContent { UnsupportedScreen() }
            return
        }
        vm.setRefreshRate(display?.refreshRate ?: 60f)

        // 画像差分用：adb shell am start -n dev.zat.benchdeck/.MainActivity --es shots shots
        intent.getStringExtra("shots")?.let { dir ->
            lifecycleScope.launch { renderShots(dir) }
        }

        setContent { BenchDeckApp(vm) }

        // 自動試験：adb shell am start -n dev.zat.benchdeck/.MainActivity --ez autorun true
        //          [--es preset low] [--ei laps 1] [--ez warmup false] [--ef lapSeconds 10]
        // 結果の保存先を logcat に "BENCHDECK_RESULT <path>" で出して終了する
        if (savedInstanceState == null && intent.getBooleanExtra("autorun", false)) {
            vm.selectPreset(intent.getStringExtra("preset") ?: "deck")
            vm.runOptions = BenchViewModel.RunOptions(
                laps = intent.getIntExtra("laps", 3),
                warmup = intent.getBooleanExtra("warmup", true),
                lapSeconds = intent.getFloatExtra("lapSeconds", 0f).toDouble(),
                skipCooling = intent.getBooleanExtra("skipCooling", false),
            )
            vm.onFinished = { file ->
                Log.i("BenchDeck", "BENCHDECK_RESULT ${file?.path ?: "none"}")
                finish()
            }
            vm.start()
        }

        // 計測中だけ画面を点けたまま・全画面・表示間隔を取る
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                vm.screen.collect { s ->
                    val running = s is Screen.Running
                    val controller = WindowCompat.getInsetsController(window, window.decorView)
                    if (running) {
                        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                        controller.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
                        controller.hide(WindowInsetsCompat.Type.systemBars())
                        if (!vsyncOn) {
                            vsyncOn = true
                            Choreographer.getInstance().postFrameCallback(vsync)
                        }
                    } else {
                        window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                        controller.show(WindowInsetsCompat.Type.systemBars())
                        vsyncOn = false
                    }
                }
            }
        }
    }

    // 計測中にアプリが裏へ回ったら、その計測は捨てる（途中から再開すると数値が信用できない）
    override fun onPause() {
        super.onPause()
        if (vm.isRunning) vm.abort("interrupted")
    }

    @Deprecated("Deprecated in Android 14, still delivered for critical levels")
    override fun onTrimMemory(level: Int) {
        super.onTrimMemory(level)
        @Suppress("DEPRECATION")
        if (level >= ComponentCallbacks2.TRIM_MEMORY_RUNNING_CRITICAL && vm.isRunning) vm.abort("low_memory")
    }

    private suspend fun renderShots(dirName: String) {
        val times = doubleArrayOf(4.0, 14.0, 27.0, 44.0, 56.0)
        val out = File(getExternalFilesDir(null), dirName).apply { mkdirs() }
        val shots = withContext(Dispatchers.Default) { vm.renderShots(times) }
        if (shots == null) {
            Log.e("BenchDeck", "shots failed")
            return
        }
        withContext(Dispatchers.IO) {
            shots.forEachIndexed { i, rgba ->
                val bmp = Bitmap.createBitmap(1280, 800, Bitmap.Config.ARGB_8888)
                bmp.copyPixelsFromBuffer(ByteBuffer.wrap(rgba))
                File(out, "shot_%d_t%05.1f.png".format(i, times[i])).outputStream().use {
                    bmp.compress(Bitmap.CompressFormat.PNG, 100, it)
                }
            }
        }
        Log.i("BenchDeck", "shots written to ${out.path}")
        finish()
    }
}

package dev.zat.citydrive.ui

import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import dev.zat.citydrive.DriveViewModel
import dev.zat.citydrive.R
import dev.zat.citydrive.Screen

@Composable
fun DriveUi(vm: DriveViewModel) {
    val screen by vm.screen.collectAsState()
    DriveTheme {
        Box(Modifier.fillMaxSize().background(Bg)) {
            // 描画面は読込中から置く（ネイティブはウィンドウが来てから読み込む）
            val showWorld = screen is Screen.Loading || screen == Screen.Driving || screen == Screen.Paused
            if (showWorld) WorldView(vm)
            when (val s = screen) {
                Screen.Title -> TitleScreen(vm)
                is Screen.Loading -> LoadingOverlay(s.progress)
                Screen.Driving -> Hud(vm)
                Screen.Paused -> { Hud(vm); PauseOverlay(vm) }
                is Screen.Error -> ErrorScreen(s.message) { vm.quitToTitle() }
            }
            if (screen == Screen.Driving || screen == Screen.Paused) NoControllerBanner(vm)
        }
    }
}

// ---- 描画面：常に 1280×800 で描き、16:10 の枠に収める（端末差は黒帯） ----------------------------

@Composable
private fun WorldView(vm: DriveViewModel) {
    Box(Modifier.fillMaxSize().background(Color.Black), contentAlignment = Alignment.Center) {
        AndroidView(
            modifier = Modifier.aspectRatio(16f / 10f),
            factory = { ctx ->
                SurfaceView(ctx).apply {
                    holder.setFixedSize(1280, 800)
                    holder.addCallback(object : SurfaceHolder.Callback {
                        override fun surfaceCreated(h: SurfaceHolder) = vm.surfaceCreated(h.surface)
                        override fun surfaceChanged(h: SurfaceHolder, f: Int, w: Int, hh: Int) {}
                        override fun surfaceDestroyed(h: SurfaceHolder) = vm.surfaceDestroyed()
                    })
                }
            },
        )
    }
}

// ---- タイトル ------------------------------------------------------------------------------------

@Composable
private fun TitleScreen(vm: DriveViewModel) {
    val preset by vm.preset.collectAsState()
    val connected by vm.controller.collectAsState()
    val padName by vm.controllerName.collectAsState()
    Box(
        Modifier.fillMaxSize().background(
            Brush.verticalGradient(listOf(Color(0xFF0B0A12), Color(0xFF1A0F2A), Color(0xFF2A0F24)))
        ).safeDrawingPadding().padding(40.dp),
    ) {
        Column(Modifier.align(Alignment.CenterStart).width(520.dp)) {
            Text(stringResource(R.string.app_name), style = MaterialTheme.typography.displayMedium, fontWeight = FontWeight.Black, color = Cyan)
            Text(stringResource(R.string.tagline), style = MaterialTheme.typography.titleMedium, color = Muted)
            Spacer(Modifier.height(36.dp))

            Text(stringResource(R.string.quality), style = MaterialTheme.typography.labelLarge, color = Muted)
            Spacer(Modifier.height(8.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                vm.presets.forEach { (name, label) ->
                    val on = name == preset
                    Box(
                        Modifier.clip(RoundedCornerShape(10.dp))
                            .background(if (on) PanelHi else Panel)
                            .border(1.5.dp, if (on) Cyan else Color.Transparent, RoundedCornerShape(10.dp))
                            .clickable { vm.selectPreset(name) }
                            .padding(horizontal = 18.dp, vertical = 10.dp),
                    ) { Text(stringResource(label), color = if (on) Cyan else Muted) }
                }
            }
            Text(stringResource(R.string.quality_hint), style = MaterialTheme.typography.bodySmall, color = Muted, modifier = Modifier.padding(top = 6.dp))
            Spacer(Modifier.height(28.dp))

            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(Modifier.width(10.dp).height(10.dp).clip(RoundedCornerShape(5.dp)).background(if (connected) Cyan else Magenta))
                Spacer(Modifier.width(10.dp))
                Text(
                    if (connected) stringResource(R.string.controller_connected, padName) else stringResource(R.string.controller_missing),
                    color = if (connected) Color.White else Magenta,
                )
            }
            Spacer(Modifier.height(20.dp))
            Button(
                onClick = { vm.start() },
                colors = ButtonDefaults.buttonColors(containerColor = Magenta),
                shape = RoundedCornerShape(12.dp),
                modifier = Modifier.width(260.dp).height(54.dp),
            ) { Text(stringResource(R.string.start), fontSize = 18.sp, fontWeight = FontWeight.Bold) }
        }
        ControlsCard(Modifier.align(Alignment.CenterEnd))
    }
}

@Composable
private fun ControlsCard(modifier: Modifier) {
    val rows = listOf(
        R.string.ctl_steer to "L",
        R.string.ctl_throttle to "RT",
        R.string.ctl_brake to "LT",
        R.string.ctl_handbrake to "A",
        R.string.ctl_camera to "Y",
        R.string.ctl_reset to "B",
        R.string.ctl_look to "R",
        R.string.ctl_lights to "SELECT",
        R.string.ctl_pause to "START",
    )
    Column(modifier.width(320.dp).clip(RoundedCornerShape(14.dp)).background(Panel.copy(alpha = 0.85f)).padding(20.dp)) {
        Text(stringResource(R.string.controls), style = MaterialTheme.typography.titleSmall, color = Cyan)
        Spacer(Modifier.height(10.dp))
        rows.forEach { (label, key) ->
            Row(Modifier.fillMaxWidth().padding(vertical = 3.dp)) {
                Text(key, color = Amber, fontFamily = FontFamily.Monospace, modifier = Modifier.width(78.dp))
                Text(stringResource(label), color = Color.White)
            }
        }
    }
}

// ---- 読込 --------------------------------------------------------------------------------------

@Composable
private fun LoadingOverlay(progress: Float) {
    Box(Modifier.fillMaxSize().background(Bg), contentAlignment = Alignment.Center) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text(stringResource(R.string.loading), color = Muted)
            Spacer(Modifier.height(12.dp))
            LinearProgressIndicator(progress = { progress }, modifier = Modifier.width(320.dp), color = Cyan)
        }
    }
}

// ---- 走行中の表示 ------------------------------------------------------------------------------

@Composable
private fun Hud(vm: DriveViewModel) {
    val s by vm.hud.collectAsState()
    val st = s ?: return
    Box(Modifier.fillMaxSize().safeDrawingPadding().padding(24.dp)) {
        // 右下：速度（大）とギア・回転計
        Column(Modifier.align(Alignment.BottomEnd), horizontalAlignment = Alignment.End) {
            Row(verticalAlignment = Alignment.Bottom) {
                Text(
                    "%d".format(st.speedKmh.toInt()),
                    fontSize = 64.sp, fontWeight = FontWeight.Black, fontFamily = FontFamily.Monospace, color = Color.White,
                )
                Spacer(Modifier.width(6.dp))
                Text("km/h", color = Muted, modifier = Modifier.padding(bottom = 12.dp))
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(if (st.gear < 0) "R" else "${st.gear}", fontSize = 26.sp, fontWeight = FontWeight.Bold, color = Cyan,
                    fontFamily = FontFamily.Monospace)
                Spacer(Modifier.width(12.dp))
                RpmBar(st.rpm)
            }
        }
        // 左上：高架・視点・fps（小さく）
        Column(Modifier.align(Alignment.TopStart)) {
            if (st.onDeck) Text(stringResource(R.string.on_overpass), color = Cyan, style = MaterialTheme.typography.labelMedium)
            Text("%.0f fps".format(st.fps), color = Muted.copy(alpha = 0.7f), style = MaterialTheme.typography.labelSmall)
        }
    }
}

@Composable
private fun RpmBar(rpm: Float) {
    val f = ((rpm - 800f) / (7200f - 800f)).coerceIn(0f, 1f)
    Box(Modifier.width(180.dp).height(8.dp).clip(RoundedCornerShape(4.dp)).background(Panel)) {
        Box(
            Modifier.fillMaxWidth(f).height(8.dp).background(
                Brush.horizontalGradient(listOf(Cyan, if (f > 0.85f) Magenta else Cyan))
            )
        )
    }
}

// ---- 一時停止 ----------------------------------------------------------------------------------

@Composable
private fun PauseOverlay(vm: DriveViewModel) {
    Box(Modifier.fillMaxSize().background(Color(0xCC0B0A12)), contentAlignment = Alignment.Center) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text(stringResource(R.string.paused), style = MaterialTheme.typography.headlineMedium, color = Color.White)
            Spacer(Modifier.height(24.dp))
            Button(onClick = { vm.resume() }, colors = ButtonDefaults.buttonColors(containerColor = Magenta),
                modifier = Modifier.width(240.dp)) { Text(stringResource(R.string.resume)) }
            Spacer(Modifier.height(10.dp))
            OutlinedButton(onClick = { vm.quitToTitle() }, modifier = Modifier.width(240.dp)) { Text(stringResource(R.string.to_title)) }
            Spacer(Modifier.height(14.dp))
            Text(stringResource(R.string.pause_hint), color = Muted, style = MaterialTheme.typography.bodySmall)
        }
    }
}

@Composable
private fun NoControllerBanner(vm: DriveViewModel) {
    val connected by vm.controller.collectAsState()
    AnimatedVisibility(!connected, enter = fadeIn(), exit = fadeOut()) {
        Box(Modifier.fillMaxSize().safeDrawingPadding().padding(top = 24.dp), contentAlignment = Alignment.TopCenter) {
            Text(
                stringResource(R.string.controller_lost),
                color = Color.White,
                modifier = Modifier.clip(RoundedCornerShape(10.dp)).background(Magenta.copy(alpha = 0.9f)).padding(horizontal = 18.dp, vertical = 10.dp),
            )
        }
    }
}

@Composable
private fun ErrorScreen(message: String, onBack: () -> Unit) {
    Box(Modifier.fillMaxSize().background(Bg).padding(40.dp), contentAlignment = Alignment.Center) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text(stringResource(R.string.error_title), style = MaterialTheme.typography.headlineSmall, color = Magenta)
            Spacer(Modifier.height(10.dp))
            Text(message, color = Muted)
            Spacer(Modifier.height(20.dp))
            OutlinedButton(onClick = onBack) { Text(stringResource(R.string.to_title)) }
        }
    }
}

@Composable
fun UnsupportedScreen() {
    DriveTheme {
        Box(Modifier.fillMaxSize().background(Bg).padding(40.dp), contentAlignment = Alignment.Center) {
            Text(stringResource(R.string.unsupported), color = Muted)
        }
    }
}

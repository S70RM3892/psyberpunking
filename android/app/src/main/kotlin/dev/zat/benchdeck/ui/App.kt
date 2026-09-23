package dev.zat.benchdeck.ui

import android.content.Context
import android.content.Intent
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.FileProvider
import dev.zat.benchdeck.BenchViewModel
import dev.zat.benchdeck.CoolingPolicy
import dev.zat.benchdeck.NativeBench
import dev.zat.benchdeck.R
import dev.zat.benchdeck.ResultSummary
import dev.zat.benchdeck.Screen
import java.io.File
import java.text.DateFormat
import java.util.Date

@Composable
fun BenchDeckApp(vm: BenchViewModel) {
    BenchTheme {
        val screen by vm.screen.collectAsState()
        Box(Modifier.fillMaxSize().background(Bg)) {
            AnimatedContent(
                targetState = screen,
                contentKey = { it::class },
                transitionSpec = { fadeIn() togetherWith fadeOut() },
                label = "screen",
            ) { s ->
                when (s) {
                    is Screen.Home -> HomeScreen(vm)
                    is Screen.Cooling -> CoolingScreen(s, onSkip = vm::skipCooling) { vm.abort("cancel") }
                    is Screen.Running -> RunScreen(vm)
                    is Screen.Result -> ResultScreen(vm, s.summary)
                    is Screen.Interrupted -> MessageScreen(
                        title = stringResource(R.string.interrupted),
                        text = if (s.reason == "thermal_stop") stringResource(R.string.thermal_stop)
                               else stringResource(R.string.interrupted_text, s.reason),
                        onHome = vm::home,
                        onRetry = vm::start,
                    )
                    is Screen.Error -> MessageScreen(stringResource(R.string.error), s.message, vm::home, null)
                }
            }
        }
    }
}

// ---- ホーム ---------------------------------------------------------------------------------------

@Composable
private fun HomeScreen(vm: BenchViewModel) {
    val device by vm.device.collectAsState()
    val preset by vm.preset.collectAsState()
    val history by vm.history.collectAsState()
    Row(Modifier.fillMaxSize().safeDrawingPadding().padding(24.dp), horizontalArrangement = Arrangement.spacedBy(24.dp)) {
        Column(Modifier.weight(1.15f).fillMaxHeight().verticalScroll(rememberScrollState())) {
            Text("BENCHDECK", style = MaterialTheme.typography.labelMedium, color = Cyan, letterSpacing = 4.sp)
            Text(stringResource(R.string.tagline), style = MaterialTheme.typography.headlineMedium)
            Spacer(Modifier.height(16.dp))
            Panel {
                InfoRow(stringResource(R.string.device), device.name)
                InfoRow(stringResource(R.string.gpu), device.gpu.ifEmpty { "—" })
                InfoRow(stringResource(R.string.vulkan), listOf(device.vulkan, device.driver).filter { it.isNotEmpty() }.joinToString("  ·  driver "))
                InfoRow(stringResource(R.string.refresh), stringResource(R.string.refresh_hz, device.refreshHz))
            }
            Spacer(Modifier.height(16.dp))
            Text(stringResource(R.string.preset), style = MaterialTheme.typography.titleMedium, color = Muted)
            Spacer(Modifier.height(8.dp))
            SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
                vm.presets.forEachIndexed { i, p ->
                    SegmentedButton(
                        selected = preset == p,
                        onClick = { vm.selectPreset(p) },
                        shape = SegmentedButtonDefaults.itemShape(i, vm.presets.size),
                    ) { Text(presetLabel(p)) }
                }
            }
            Spacer(Modifier.height(6.dp))
            Text(
                if (preset == "deck") stringResource(R.string.preset_note_deck) else stringResource(R.string.preset_note_other),
                style = MaterialTheme.typography.bodyMedium, color = Muted,
            )
            Spacer(Modifier.height(20.dp))
            Button(
                onClick = vm::start,
                modifier = Modifier.fillMaxWidth().height(56.dp),
                colors = ButtonDefaults.buttonColors(containerColor = Magenta),
                shape = RoundedCornerShape(12.dp),
            ) { Text(stringResource(R.string.start), fontSize = 18.sp, fontWeight = FontWeight.Bold) }
            Spacer(Modifier.height(6.dp))
            Text(stringResource(R.string.duration_note), style = MaterialTheme.typography.bodyMedium, color = Muted)
        }
        Column(Modifier.weight(1f).fillMaxHeight()) {
            Text(stringResource(R.string.history), style = MaterialTheme.typography.titleMedium, color = Muted)
            Spacer(Modifier.height(8.dp))
            if (history.isEmpty()) {
                Text(stringResource(R.string.history_empty), color = Muted)
            }
            LazyColumn(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                items(history, key = { it.file?.name ?: it.timestamp.toString() }) { r -> HistoryItem(r) { vm.showResult(r) } }
            }
        }
    }
}

@Composable
private fun HistoryItem(r: ResultSummary, onClick: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp)).background(Panel).clickable(onClick = onClick).padding(12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(Modifier.width(72.dp)) {
            Text(presetLabel(r.preset).uppercase(), style = MaterialTheme.typography.labelMedium, color = if (r.preset == "deck") Magenta else Cyan)
        }
        Column(Modifier.weight(1f)) {
            Text(
                r.score?.let { "$it" } ?: "%.1f fps".format(r.avgFps),
                fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold, fontSize = 20.sp,
            )
            Text(
                DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT).format(Date(r.timestamp)) +
                    if (r.aborted) "  ·  ${r.abortReason}" else "",
                style = MaterialTheme.typography.bodyMedium, color = Muted,
            )
        }
        Text("%.1f / %.1f".format(r.avgFps, r.low1Fps), style = MaterialTheme.typography.labelMedium, color = Muted)
    }
}

// ---- 冷却待ち -------------------------------------------------------------------------------------

@Composable
private fun CoolingScreen(c: Screen.Cooling, onSkip: () -> Unit, onCancel: () -> Unit) {
    Column(Modifier.fillMaxSize().padding(48.dp), verticalArrangement = Arrangement.Center, horizontalAlignment = Alignment.CenterHorizontally) {
        Text(stringResource(R.string.cooling), style = MaterialTheme.typography.headlineMedium)
        Spacer(Modifier.height(16.dp))
        // 大きく今の値。1.0 で重いスロットリング
        Text(
            if (c.headroom.isNaN()) "—" else "%.2f".format(c.headroom),
            style = MaterialTheme.typography.displayMedium,
            color = if (c.hot) Magenta else Cyan,
        )
        Text(stringResource(R.string.cooling_headroom_label), style = MaterialTheme.typography.labelMedium, color = Muted)
        Spacer(Modifier.height(20.dp))
        // 何を待っているか
        val detail = when {
            c.hot -> stringResource(R.string.cooling_hot)
            c.target != null -> stringResource(R.string.cooling_target, c.target)
            c.drop != null -> stringResource(R.string.cooling_plateau_drop, c.drop, CoolingPolicy.PLATEAU_DROP)
            else -> stringResource(R.string.cooling_plateau_wait)
        }
        Text(detail, color = Muted)
        Spacer(Modifier.height(20.dp))
        // 打ち切りまでの経過（最大10分）
        val limitS = (CoolingPolicy.TIMEOUT_MS / 1000).toInt()
        LinearProgressIndicator(
            progress = { (c.elapsedS.toFloat() / limitS).coerceIn(0f, 1f) },
            modifier = Modifier.width(360.dp),
            color = if (c.hot) Magenta else Cyan,
        )
        Spacer(Modifier.height(6.dp))
        Text(
            stringResource(R.string.cooling_elapsed, c.elapsedS / 60, c.elapsedS % 60, limitS / 60),
            style = MaterialTheme.typography.labelMedium, color = Muted,
        )
        Spacer(Modifier.height(28.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(16.dp)) {
            OutlinedButton(onClick = onCancel) { Text(stringResource(R.string.cancel)) }
            Button(onClick = onSkip, colors = ButtonDefaults.buttonColors(containerColor = Cyan, contentColor = Bg)) {
                Text(stringResource(R.string.cooling_skip))
            }
        }
        Spacer(Modifier.height(10.dp))
        Text(stringResource(R.string.cooling_skip_note), style = MaterialTheme.typography.labelSmall, color = Muted)
    }
}

// ---- 計測 ----------------------------------------------------------------------------------------

@Composable
private fun RunScreen(vm: BenchViewModel) {
    val screen by vm.screen.collectAsState()
    val state = (screen as? Screen.Running)?.state ?: NativeBench.State(NativeBench.Phase.LOADING, 0f, 0, 0, 0f)
    var confirm by remember { mutableStateOf(false) }
    BackHandler { confirm = true }

    Box(Modifier.fillMaxSize().background(Color.Black), contentAlignment = Alignment.Center) {
        // 描画は常に 1280×800（setFixedSize）。16:10 の枠に収め、端末差は黒帯で吸収する
        AndroidView(
            modifier = Modifier.aspectRatio(1.6f).fillMaxSize(),
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
        if (state.phase == NativeBench.Phase.LOADING) {
            Column(Modifier.fillMaxSize().background(Bg), verticalArrangement = Arrangement.Center, horizontalAlignment = Alignment.CenterHorizontally) {
                Text(stringResource(R.string.loading), style = MaterialTheme.typography.headlineMedium)
                Spacer(Modifier.height(16.dp))
                LinearProgressIndicator(progress = { state.progress }, modifier = Modifier.width(360.dp), color = Magenta)
            }
        } else {
            // 右上に小さく：区間名と fps（描画は1秒に1回更新。計測値には影響しない）
            Column(Modifier.align(Alignment.TopEnd).padding(12.dp).clip(RoundedCornerShape(6.dp)).background(Color(0x99000000)).padding(horizontal = 10.dp, vertical = 6.dp), horizontalAlignment = Alignment.End) {
                val phase = if (state.phase == NativeBench.Phase.WARMUP) stringResource(R.string.warmup) else stringResource(R.string.lap, state.lap)
                Text("$phase · ${sectionLabel(state.section)}", style = MaterialTheme.typography.labelMedium, color = Color.White)
                Text("%.0f fps".format(state.fps), style = MaterialTheme.typography.labelMedium, color = Cyan)
            }
            LinearProgressIndicator(
                progress = { state.progress },
                modifier = Modifier.align(Alignment.BottomCenter).fillMaxWidth().height(2.dp),
                color = Magenta, trackColor = Color.Transparent,
            )
        }
    }
    if (confirm) {
        AlertDialog(
            onDismissRequest = { confirm = false },
            title = { Text(stringResource(R.string.abort_title)) },
            text = { Text(stringResource(R.string.abort_text)) },
            confirmButton = { TextButton(onClick = { confirm = false; vm.abort("interrupted") }) { Text(stringResource(R.string.abort_yes)) } },
            dismissButton = { TextButton(onClick = { confirm = false }) { Text(stringResource(R.string.abort_no)) } },
        )
    }
}

// ---- 結果 ----------------------------------------------------------------------------------------

@Composable
private fun ResultScreen(vm: BenchViewModel, r: ResultSummary) {
    val ctx = LocalContext.current
    BackHandler { vm.home() }
    Row(Modifier.fillMaxSize().safeDrawingPadding().padding(24.dp), horizontalArrangement = Arrangement.spacedBy(28.dp)) {
        Column(Modifier.weight(1f).fillMaxHeight().verticalScroll(rememberScrollState())) {
            Text("${stringResource(R.string.result)} · ${presetLabel(r.preset).uppercase()}", style = MaterialTheme.typography.labelMedium, color = Cyan, letterSpacing = 3.sp)
            if (r.score != null) {
                Text(stringResource(R.string.score), color = Muted)
                Text(
                    "${r.score}",
                    style = MaterialTheme.typography.displayLarge.copy(brush = Brush.horizontalGradient(listOf(Magenta, Cyan))),
                )
                Text("Steam Deck = 1000", style = MaterialTheme.typography.labelMedium, color = Muted)
            } else {
                Text(stringResource(R.string.score_none), style = MaterialTheme.typography.headlineMedium)
                Text(r.scoreNote, style = MaterialTheme.typography.bodyMedium, color = Muted)
            }
            Spacer(Modifier.height(20.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                Metric(stringResource(R.string.avg_fps), "%.1f".format(r.avgFps), "fps", Modifier.weight(1f))
                Metric(stringResource(R.string.low1_fps), "%.1f".format(r.low1Fps), "fps", Modifier.weight(1f))
            }
            Spacer(Modifier.height(12.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                Metric(stringResource(R.string.gpu_ms), r.gpuMs?.let { "%.1f".format(it) } ?: "—", "ms", Modifier.weight(1f))
                Metric(stringResource(R.string.cpu_ms), r.cpuMs?.let { "%.1f".format(it) } ?: "—", "ms", Modifier.weight(1f))
            }
            Spacer(Modifier.height(12.dp))
            Panel {
                InfoRow(stringResource(R.string.thermal_max), r.thermalMax)
                InfoRow(stringResource(R.string.valid_laps), "${r.validLaps} / ${r.laps}")
                InfoRow(stringResource(R.string.device), r.device)
            }
        }
        Column(Modifier.weight(1f).fillMaxHeight()) {
            Text(stringResource(R.string.sections), style = MaterialTheme.typography.titleMedium, color = Muted)
            Spacer(Modifier.height(12.dp))
            SectionBars(r.sections, Modifier.fillMaxWidth().height(180.dp))
            Spacer(Modifier.weight(1f))
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                OutlinedButton(onClick = { r.file?.let { share(ctx, it, "application/json") } }, modifier = Modifier.weight(1f)) { Text(stringResource(R.string.share_json)) }
                OutlinedButton(onClick = { vm.csvFor(r)?.let { share(ctx, it, "text/csv") } }, modifier = Modifier.weight(1f)) { Text(stringResource(R.string.share_csv)) }
            }
            Spacer(Modifier.height(10.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                OutlinedButton(onClick = vm::home, modifier = Modifier.weight(1f)) { Text(stringResource(R.string.home)) }
                Button(onClick = { vm.selectPreset(r.preset); vm.start() }, modifier = Modifier.weight(1f), colors = ButtonDefaults.buttonColors(containerColor = Magenta)) {
                    Text(stringResource(R.string.rerun))
                }
            }
        }
    }
}

@Composable
private fun SectionBars(sections: List<Pair<String, Double>>, modifier: Modifier) {
    val max = (sections.maxOfOrNull { it.second } ?: 1.0).coerceAtLeast(1.0)
    val colors = listOf(Magenta, Cyan, Amber)
    Column(modifier, verticalArrangement = Arrangement.spacedBy(14.dp)) {
        sections.forEachIndexed { i, (name, fps) ->
            Column {
                Row {
                    Text(sectionLabel(name), Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
                    Text("%.1f fps".format(fps), style = MaterialTheme.typography.labelMedium, color = Muted)
                }
                Spacer(Modifier.height(4.dp))
                val c = colors[i % colors.size]
                Canvas(Modifier.fillMaxWidth().height(14.dp)) {
                    drawRoundRect(PanelHi, size = size, cornerRadius = CornerRadius(7f, 7f))
                    drawRoundRect(
                        Brush.horizontalGradient(listOf(c.copy(alpha = 0.6f), c)),
                        topLeft = Offset.Zero,
                        size = Size((size.width * (fps / max)).toFloat(), size.height),
                        cornerRadius = CornerRadius(7f, 7f),
                    )
                }
            }
        }
    }
}

// ---- 共通部品 ------------------------------------------------------------------------------------

@Composable
private fun MessageScreen(title: String, text: String, onHome: () -> Unit, onRetry: (() -> Unit)?) {
    Column(Modifier.fillMaxSize().padding(48.dp), verticalArrangement = Arrangement.Center, horizontalAlignment = Alignment.CenterHorizontally) {
        Text(title, style = MaterialTheme.typography.headlineMedium, color = Amber)
        Spacer(Modifier.height(12.dp))
        Text(text, color = Muted)
        Spacer(Modifier.height(24.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            OutlinedButton(onClick = onHome) { Text(stringResource(R.string.home)) }
            if (onRetry != null) Button(onClick = onRetry, colors = ButtonDefaults.buttonColors(containerColor = Magenta)) { Text(stringResource(R.string.rerun)) }
        }
    }
}

@Composable
fun UnsupportedScreen() {
    BenchTheme {
        Box(Modifier.fillMaxSize().background(Bg), contentAlignment = Alignment.Center) {
            Text(stringResource(R.string.unsupported), style = MaterialTheme.typography.headlineMedium, color = Amber)
        }
    }
}

@Composable
private fun Panel(content: @Composable () -> Unit) {
    Column(
        Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp)).background(Panel)
            .border(1.dp, Color(0xFF2A2440), RoundedCornerShape(12.dp)).padding(14.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) { content() }
}

@Composable
private fun InfoRow(label: String, value: String) {
    Row {
        Text(label, Modifier.width(120.dp), style = MaterialTheme.typography.bodyMedium, color = Muted)
        Text(value, style = MaterialTheme.typography.bodyMedium)
    }
}

@Composable
private fun Metric(label: String, value: String, unit: String, modifier: Modifier) {
    Column(modifier.clip(RoundedCornerShape(12.dp)).background(Panel).padding(14.dp)) {
        Text(label, style = MaterialTheme.typography.bodyMedium, color = Muted)
        Row(verticalAlignment = Alignment.Bottom) {
            Text(value, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold, fontSize = 30.sp)
            Spacer(Modifier.size(4.dp))
            Text(unit, style = MaterialTheme.typography.labelMedium, color = Muted, modifier = Modifier.padding(bottom = 6.dp))
        }
    }
}

@Composable
private fun presetLabel(p: String) = when (p) {
    "low" -> stringResource(R.string.preset_low)
    "deck" -> stringResource(R.string.preset_deck)
    "medium" -> stringResource(R.string.preset_medium)
    "high" -> stringResource(R.string.preset_high)
    else -> p
}

@Composable
private fun sectionLabel(i: Int) = sectionLabel(listOf("boulevard", "overpass", "alley").getOrElse(i) { "" })

@Composable
private fun sectionLabel(name: String) = when (name) {
    "boulevard" -> stringResource(R.string.section_boulevard)
    "overpass" -> stringResource(R.string.section_overpass)
    "alley" -> stringResource(R.string.section_alley)
    else -> name
}

private fun share(ctx: Context, file: File, mime: String) {
    if (!file.exists()) return
    val uri = FileProvider.getUriForFile(ctx, ctx.packageName + ".files", file)
    val intent = Intent(Intent.ACTION_SEND).apply {
        type = mime
        putExtra(Intent.EXTRA_STREAM, uri)
        addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
    }
    ctx.startActivity(Intent.createChooser(intent, file.name))
}

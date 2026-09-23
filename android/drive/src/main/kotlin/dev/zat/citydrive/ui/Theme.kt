package dev.zat.citydrive.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp

val Magenta = Color(0xFFFF2E97)
val Cyan = Color(0xFF22E4FF)
val Amber = Color(0xFFFFB02E)
val Bg = Color(0xFF0B0A12)
val Panel = Color(0xFF15121F)
val PanelHi = Color(0xFF1F1A2E)
val Muted = Color(0xFF9A93B0)

private val scheme = darkColorScheme(
    primary = Magenta,
    onPrimary = Color.White,
    secondary = Cyan,
    onSecondary = Color.Black,
    background = Bg,
    onBackground = Color(0xFFECE8F5),
    surface = Panel,
    onSurface = Color(0xFFECE8F5),
    surfaceVariant = PanelHi,
    onSurfaceVariant = Muted,
    outline = Color(0xFF3A3350),
)

private val typography = Typography(
    displayLarge = TextStyle(fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold, fontSize = 72.sp, letterSpacing = (-1).sp),
    headlineMedium = TextStyle(fontWeight = FontWeight.Bold, fontSize = 28.sp),
    titleMedium = TextStyle(fontWeight = FontWeight.SemiBold, fontSize = 16.sp, letterSpacing = 0.5.sp),
    bodyMedium = TextStyle(fontSize = 14.sp, lineHeight = 20.sp),
    labelMedium = TextStyle(fontFamily = FontFamily.Monospace, fontSize = 12.sp, letterSpacing = 0.8.sp),
)

@Composable
fun DriveTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = scheme, typography = typography, content = content)
}

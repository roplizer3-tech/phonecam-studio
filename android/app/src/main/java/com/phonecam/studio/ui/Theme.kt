package com.phonecam.studio.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

private val DarkScheme = darkColorScheme(
    primary = Color(0xFF3B82F6),
    onPrimary = Color.White,
    secondary = Color(0xFF10B981),
    onSecondary = Color.White,
    background = Color(0xFF0A0D14),
    surface = Color(0xFF121722),
    surfaceVariant = Color(0xFF1C2333),
    onBackground = Color(0xFFF8FAFC),
    onSurface = Color(0xFFF8FAFC),
    error = Color(0xFFEF4444)
)

@Composable
fun PhoneCamTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = DarkScheme,
        content = content
    )
}

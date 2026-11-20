package com.phonecam.studio.ui

import android.graphics.SurfaceTexture
import android.view.Surface
import android.view.TextureView
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.BatteryChargingFull
import androidx.compose.material.icons.rounded.Cameraswitch
import androidx.compose.material.icons.rounded.DarkMode
import androidx.compose.material.icons.rounded.FlashOff
import androidx.compose.material.icons.rounded.FlashOn
import androidx.compose.material.icons.rounded.Language
import androidx.compose.material.icons.rounded.Lock
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Settings
import androidx.compose.material.icons.rounded.Stop
import androidx.compose.material.icons.rounded.Videocam
import androidx.compose.material.icons.rounded.Wifi
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface as M3Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import com.phonecam.studio.camera.StreamConfig
import com.phonecam.studio.camera.VideoCodecMode
import com.phonecam.studio.service.ServiceSnapshot
import kotlin.math.roundToInt

@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
fun MainScreen(
    snapshot: ServiceSnapshot,
    isArabic: Boolean,
    onToggleLanguage: () -> Unit,
    onSurfaceReady: (Surface?) -> Unit,
    onStartStop: () -> Unit,
    onSwitchCamera: (String?) -> Unit,
    onToggleTorch: () -> Unit,
    onUpdateConfig: (StreamConfig) -> Unit,
    onTogglePin: (Boolean) -> Unit,
    onRegeneratePin: () -> Unit,
    onTapFocus: (Float, Float) -> Unit,
    onDismissError: () -> Unit
) {
    var showSettings by remember { mutableStateOf(false) }
    var blackoutMode by remember { mutableStateOf(false) }
    var lastTapPoint by remember { mutableStateOf<Pair<Float, Float>?>(null) }

    val layoutDir = if (isArabic) LayoutDirection.Rtl else LayoutDirection.Ltr
    fun t(ar: String, en: String): String = if (isArabic) ar else en

    CompositionLocalProvider(LocalLayoutDirection provides layoutDir) {
        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(Color.Black)
        ) {
            // Live Camera2 TextureView preview
            AndroidView(
                modifier = Modifier
                    .fillMaxSize()
                    .pointerInput(Unit) {
                        detectTapGestures { offset ->
                            val nx = (offset.x / size.width.toFloat()).coerceIn(0f, 1f)
                            val ny = (offset.y / size.height.toFloat()).coerceIn(0f, 1f)
                            lastTapPoint = nx to ny
                            onTapFocus(nx, ny)
                        }
                    },
                factory = { ctx ->
                    TextureView(ctx).apply {
                        surfaceTextureListener = object : TextureView.SurfaceTextureListener {
                            override fun onSurfaceTextureAvailable(st: SurfaceTexture, w: Int, h: Int) {
                                st.setDefaultBufferSize(snapshot.config.width, snapshot.config.height)
                                onSurfaceReady(Surface(st))
                            }

                            override fun onSurfaceTextureSizeChanged(st: SurfaceTexture, w: Int, h: Int) {
                                st.setDefaultBufferSize(snapshot.config.width, snapshot.config.height)
                            }

                            override fun onSurfaceTextureDestroyed(st: SurfaceTexture): Boolean {
                                onSurfaceReady(null)
                                return true
                            }

                            override fun onSurfaceTextureUpdated(st: SurfaceTexture) {}
                        }
                    }
                }
            )

            // Top HUD Card
            Column(
                modifier = Modifier
                    .align(Alignment.TopCenter)
                    .fillMaxWidth()
                    .statusBarsPadding()
                    .padding(12.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                M3Surface(
                    shape = RoundedCornerShape(18.dp),
                    color = Color(0xCC121722),
                    tonalElevation = 4.dp,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Column(
                        modifier = Modifier.padding(horizontal = 14.dp, vertical = 10.dp),
                        verticalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(8.dp)
                            ) {
                                Box(
                                    modifier = Modifier
                                        .size(10.dp)
                                        .clip(CircleShape)
                                        .background(
                                            when {
                                                snapshot.clientStatus.connected -> Color(0xFF10B981)
                                                snapshot.isStreaming -> Color(0xFFF59E0B)
                                                else -> Color(0xFF64748B)
                                            }
                                        )
                                )
                                Text(
                                    text = when {
                                        snapshot.clientStatus.connected ->
                                            t("متصل بـ ${snapshot.clientStatus.clientAddress}", "Connected: ${snapshot.clientStatus.clientAddress}")
                                        snapshot.isStreaming ->
                                            t("جاري البث — في انتظار ويندوز", "Streaming — Waiting for Windows PC")
                                        else ->
                                            t("جاهز للبث", "Ready to Stream")
                                    },
                                    color = Color.White,
                                    fontWeight = FontWeight.SemiBold,
                                    fontSize = 14.sp
                                )
                            }

                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Icon(
                                    imageVector = Icons.Rounded.BatteryChargingFull,
                                    contentDescription = null,
                                    tint = Color(0xFF94A3B8),
                                    modifier = Modifier.size(16.dp)
                                )
                                Text(
                                    text = "${snapshot.batteryPct}%",
                                    color = Color(0xFFCBD5E1),
                                    fontSize = 12.sp
                                )
                                Spacer(Modifier.width(6.dp))
                                IconButton(
                                    onClick = onToggleLanguage,
                                    modifier = Modifier.size(28.dp)
                                ) {
                                    Icon(
                                        imageVector = Icons.Rounded.Language,
                                        contentDescription = "Language",
                                        tint = Color(0xFF93C5FD),
                                        modifier = Modifier.size(18.dp)
                                    )
                                }
                            }
                        }

                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(6.dp)
                            ) {
                                Icon(
                                    imageVector = Icons.Rounded.Wifi,
                                    contentDescription = null,
                                    tint = Color(0xFF60A5FA),
                                    modifier = Modifier.size(16.dp)
                                )
                                val ipLabel = snapshot.localIps.firstOrNull() ?: "USB ADB / Wi-Fi"
                                Text(
                                    text = "$ipLabel : 54820",
                                    color = Color(0xFFE2E8F0),
                                    fontSize = 12.sp
                                )
                            }

                            if (snapshot.pinEnabled) {
                                Row(
                                    verticalAlignment = Alignment.CenterVertically,
                                    horizontalArrangement = Arrangement.spacedBy(4.dp),
                                    modifier = Modifier
                                        .clip(RoundedCornerShape(8.dp))
                                        .background(Color(0x333B82F6))
                                        .clickable { onRegeneratePin() }
                                        .padding(horizontal = 8.dp, vertical = 3.dp)
                                ) {
                                    Icon(
                                        imageVector = Icons.Rounded.Lock,
                                        contentDescription = null,
                                        tint = Color(0xFF93C5FD),
                                        modifier = Modifier.size(13.dp)
                                    )
                                    Text(
                                        text = "PIN: ${snapshot.pinCode}",
                                        color = Color(0xFFBFDBFE),
                                        fontWeight = FontWeight.Bold,
                                        fontSize = 12.sp
                                    )
                                    Icon(
                                        imageVector = Icons.Rounded.Refresh,
                                        contentDescription = null,
                                        tint = Color(0xFF93C5FD),
                                        modifier = Modifier.size(13.dp)
                                    )
                                }
                            }
                        }

                        // Camera lens switcher chips
                        if (snapshot.cameras.size > 1) {
                            FlowRow(
                                horizontalArrangement = Arrangement.spacedBy(6.dp),
                                verticalArrangement = Arrangement.spacedBy(4.dp)
                            ) {
                                snapshot.cameras.forEach { cam ->
                                    val selected = cam.id == snapshot.currentCamera?.id
                                    val camLabel = when {
                                        cam.facing == "front" && isArabic -> "أمامية"
                                        cam.label == "Back Main" && isArabic -> "خلفية رئيسية"
                                        cam.label == "Back Ultra-Wide" && isArabic -> "واسعة"
                                        cam.label == "Back Telephoto" && isArabic -> "زوم"
                                        else -> cam.label
                                    }
                                    FilterChip(
                                        selected = selected,
                                        onClick = { onSwitchCamera(cam.id) },
                                        label = { Text(camLabel, fontSize = 11.sp) }
                                    )
                                }
                            }
                        }
                    }
                }

                snapshot.lastError?.let { err ->
                    M3Surface(
                        shape = RoundedCornerShape(12.dp),
                        color = Color(0xDD7F1D1D),
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable { onDismissError() }
                    ) {
                        Text(
                            text = err,
                            color = Color.White,
                            fontSize = 12.sp,
                            modifier = Modifier.padding(10.dp)
                        )
                    }
                }
            }

            // Bottom Control Dock
            M3Surface(
                shape = RoundedCornerShape(24.dp),
                color = Color(0xCC121722),
                modifier = Modifier
                    .align(Alignment.BottomCenter)
                    .fillMaxWidth()
                    .navigationBarsPadding()
                    .padding(14.dp)
            ) {
                Column(
                    modifier = Modifier.padding(14.dp),
                    verticalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            text = "${snapshot.config.width}x${snapshot.config.height} • ${snapshot.config.fps} FPS • ${snapshot.config.codec.name}",
                            color = Color(0xFF94A3B8),
                            fontSize = 12.sp
                        )
                        Text(
                            text = if (snapshot.currentCamera?.facing == "front")
                                t("الكاميرا الأمامية", "Front Camera")
                            else
                                t("الكاميرا الخلفية", "Rear Camera"),
                            color = Color(0xFF60A5FA),
                            fontWeight = FontWeight.Medium,
                            fontSize = 12.sp
                        )
                    }

                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        IconButton(
                            onClick = { onSwitchCamera(null) },
                            modifier = Modifier
                                .size(48.dp)
                                .clip(CircleShape)
                                .background(Color(0xFF1E293B))
                        ) {
                            Icon(Icons.Rounded.Cameraswitch, contentDescription = "Switch", tint = Color.White)
                        }

                        IconButton(
                            onClick = onToggleTorch,
                            enabled = snapshot.currentCamera?.hasFlash == true && snapshot.currentCamera.facing == "back",
                            modifier = Modifier
                                .size(48.dp)
                                .clip(CircleShape)
                                .background(if (snapshot.torchEnabled) Color(0xFFF59E0B) else Color(0xFF1E293B))
                        ) {
                            Icon(
                                imageVector = if (snapshot.torchEnabled) Icons.Rounded.FlashOn else Icons.Rounded.FlashOff,
                                contentDescription = "Torch",
                                tint = Color.White
                            )
                        }

                        Button(
                            onClick = onStartStop,
                            shape = RoundedCornerShape(16.dp),
                            colors = ButtonDefaults.buttonColors(
                                containerColor = if (snapshot.isStreaming) Color(0xFFEF4444) else Color(0xFF3B82F6)
                            ),
                            modifier = Modifier.height(50.dp)
                        ) {
                            Icon(
                                imageVector = if (snapshot.isStreaming) Icons.Rounded.Stop else Icons.Rounded.PlayArrow,
                                contentDescription = null
                            )
                            Spacer(Modifier.width(6.dp))
                            Text(
                                text = if (snapshot.isStreaming) t("إيقاف البث", "Stop") else t("بدء البث", "Start"),
                                fontWeight = FontWeight.Bold
                            )
                        }

                        IconButton(
                            onClick = { blackoutMode = true },
                            modifier = Modifier
                                .size(48.dp)
                                .clip(CircleShape)
                                .background(Color(0xFF1E293B))
                        ) {
                            Icon(Icons.Rounded.DarkMode, contentDescription = "OLED Blackout", tint = Color.White)
                        }

                        IconButton(
                            onClick = { showSettings = true },
                            modifier = Modifier
                                .size(48.dp)
                                .clip(CircleShape)
                                .background(Color(0xFF1E293B))
                        ) {
                            Icon(Icons.Rounded.Settings, contentDescription = "Settings", tint = Color.White)
                        }
                    }
                }
            }

            // OLED Blackout battery saver overlay
            if (blackoutMode) {
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                        .background(Color.Black)
                        .pointerInput(Unit) {
                            detectTapGestures(onDoubleTap = { blackoutMode = false })
                        },
                    contentAlignment = Alignment.Center
                ) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Icon(
                            imageVector = Icons.Rounded.Videocam,
                            contentDescription = null,
                            tint = Color(0xFF334155),
                            modifier = Modifier.size(36.dp)
                        )
                        Spacer(Modifier.height(10.dp))
                        Text(
                            text = t(
                                "وضع توفير البطارية نشط\nاضغط مرتين للعودة للمعاينة",
                                "OLED Saver Active\nDouble-tap to wake preview"
                            ),
                            color = Color(0xFF475569),
                            fontSize = 13.sp
                        )
                    }
                }
            }

            if (showSettings) {
                ModalBottomSheet(
                    onDismissRequest = { showSettings = false },
                    containerColor = Color(0xFF121722)
                ) {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(20.dp),
                        verticalArrangement = Arrangement.spacedBy(14.dp)
                    ) {
                        Text(
                            text = t("إعدادات البث والأمان", "Stream & Security Settings"),
                            style = MaterialTheme.typography.titleMedium,
                            color = Color.White,
                            fontWeight = FontWeight.Bold
                        )

                        Text(t("الدقة (Resolution)", "Resolution"), color = Color(0xFFCBD5E1), fontSize = 13.sp)
                        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            listOf(
                                Triple(854, 480, "480p"),
                                Triple(1280, 720, "720p"),
                                Triple(1920, 1080, "1080p")
                            ).forEach { (w, h, lbl) ->
                                FilterChip(
                                    selected = snapshot.config.width == w && snapshot.config.height == h,
                                    onClick = { onUpdateConfig(snapshot.config.copy(width = w, height = h)) },
                                    label = { Text(lbl) }
                                )
                            }
                        }

                        Text(t("معدل الإطارات والترميز", "FPS & Codec"), color = Color(0xFFCBD5E1), fontSize = 13.sp)
                        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            listOf(30, 60).forEach { fps ->
                                FilterChip(
                                    selected = snapshot.config.fps == fps,
                                    onClick = { onUpdateConfig(snapshot.config.copy(fps = fps)) },
                                    label = { Text("$fps FPS") }
                                )
                            }
                            FilterChip(
                                selected = snapshot.config.codec == VideoCodecMode.H264,
                                onClick = { onUpdateConfig(snapshot.config.copy(codec = VideoCodecMode.H264)) },
                                label = { Text("H.264 HW") }
                            )
                            FilterChip(
                                selected = snapshot.config.codec == VideoCodecMode.MJPEG,
                                onClick = { onUpdateConfig(snapshot.config.copy(codec = VideoCodecMode.MJPEG)) },
                                label = { Text("MJPEG") }
                            )
                        }

                        val mbps = (snapshot.config.bitrateBps / 1_000_000f)
                        Text(
                            text = t("معدل البت: ${"%.1f".format(mbps)} Mbps", "Bitrate: ${"%.1f".format(mbps)} Mbps"),
                            color = Color(0xFFCBD5E1),
                            fontSize = 13.sp
                        )
                        Slider(
                            value = mbps,
                            onValueChange = {
                                val valBps = (it.roundToInt().coerceIn(1, 12)) * 1_000_000
                                onUpdateConfig(snapshot.config.copy(bitrateBps = valBps))
                            },
                            valueRange = 1f..12f
                        )

                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(
                                text = t("طلب رمز PIN عند اتصال ويندوز", "Require 4-digit PIN"),
                                color = Color.White,
                                fontSize = 14.sp
                            )
                            Switch(
                                checked = snapshot.pinEnabled,
                                onCheckedChange = onTogglePin
                            )
                        }

                        Spacer(Modifier.height(12.dp))
                    }
                }
            }

            lastTapPoint?.let { (nx, ny) ->
                Box(
                    modifier = Modifier
                        .align(Alignment.TopStart)
                        .padding(top = 120.dp, start = 16.dp)
                        .clip(RoundedCornerShape(10.dp))
                        .background(Color(0x8810B981))
                        .border(1.dp, Color(0xFF34D399), RoundedCornerShape(10.dp))
                        .clickable { lastTapPoint = null }
                        .padding(horizontal = 10.dp, vertical = 4.dp)
                ) {
                    Text(
                        text = t(
                            "تم ضبط التركيز (${(nx * 100).roundToInt()}%, ${(ny * 100).roundToInt()}%)",
                            "Focus locked (${(nx * 100).roundToInt()}%, ${(ny * 100).roundToInt()}%)"
                        ),
                        color = Color.White,
                        fontSize = 11.sp
                    )
                }
            }
        }
    }
}

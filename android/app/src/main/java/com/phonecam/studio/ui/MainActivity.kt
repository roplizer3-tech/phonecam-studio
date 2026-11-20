package com.phonecam.studio.ui

import android.Manifest
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.IBinder
import android.view.OrientationEventListener
import android.view.Surface
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.Text
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import com.phonecam.studio.R
import com.phonecam.studio.service.CameraForegroundService
import com.phonecam.studio.service.ServiceSnapshot
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.launch
import java.util.Locale

class MainActivity : ComponentActivity() {
    private var service: CameraForegroundService? = null
    private var bound = false
    private var collectJob: Job? = null
    private val snapshotFlow = MutableStateFlow(ServiceSnapshot())
    private var pendingSurface: Surface? = null
    private var orientationListener: OrientationEventListener? = null

    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
            val svc = (binder as CameraForegroundService.LocalBinder).getService()
            service = svc
            bound = true
            pendingSurface?.let { svc.setPreviewSurface(it) }
            svc.setDeviceRotation(currentDisplayRotationDeg())
            collectJob?.cancel()
            collectJob = lifecycleScope.launch {
                svc.state.collect { snapshotFlow.value = it }
            }
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            collectJob?.cancel()
            collectJob = null
            bound = false
            service = null
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        orientationListener = object : OrientationEventListener(this) {
            override fun onOrientationChanged(orientation: Int) {
                service?.setDeviceRotation(currentDisplayRotationDeg())
            }
        }
        orientationListener?.enable()

        val svcIntent = Intent(this, CameraForegroundService::class.java)
        startService(svcIntent)
        bindService(svcIntent, connection, Context.BIND_AUTO_CREATE)

        setContent {
            PhoneCamTheme {
                var hasCameraPerm by remember {
                    mutableStateOf(
                        ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) ==
                            PackageManager.PERMISSION_GRANTED
                    )
                }
                var isArabic by remember {
                    mutableStateOf(Locale.getDefault().language == "ar")
                }

                val permLauncher = rememberLauncherForActivityResult(
                    ActivityResultContracts.RequestMultiplePermissions()
                ) { grants ->
                    hasCameraPerm = grants[Manifest.permission.CAMERA] == true ||
                        ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) ==
                        PackageManager.PERMISSION_GRANTED
                    service?.refreshBluetoothAvailability()
                }

                LaunchedEffect(Unit) {
                    val needed = mutableListOf<String>()
                    if (!hasCameraPerm) needed.add(Manifest.permission.CAMERA)
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
                        ContextCompat.checkSelfPermission(this@MainActivity, Manifest.permission.POST_NOTIFICATIONS) !=
                        PackageManager.PERMISSION_GRANTED
                    ) {
                        needed.add(Manifest.permission.POST_NOTIFICATIONS)
                    }
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S &&
                        ContextCompat.checkSelfPermission(this@MainActivity, Manifest.permission.BLUETOOTH_CONNECT) !=
                        PackageManager.PERMISSION_GRANTED
                    ) {
                        needed.add(Manifest.permission.BLUETOOTH_CONNECT)
                    }
                    if (needed.isNotEmpty()) {
                        permLauncher.launch(needed.toTypedArray())
                    }
                }

                if (!hasCameraPerm) {
                    Column(
                        modifier = Modifier
                            .fillMaxSize()
                            .background(Color(0xFF0A0D14))
                            .padding(24.dp),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.Center
                    ) {
                        Text(
                            text = stringResource(R.string.perm_camera_needed),
                            color = Color.White,
                            textAlign = TextAlign.Center
                        )
                        Spacer(Modifier.height(16.dp))
                        Button(onClick = { permLauncher.launch(arrayOf(Manifest.permission.CAMERA)) }) {
                            Text(stringResource(R.string.perm_grant_button))
                        }
                    }
                } else {
                    val snap by snapshotFlow.collectAsState()
                    MainScreen(
                        snapshot = snap,
                        isArabic = isArabic,
                        onToggleLanguage = { isArabic = !isArabic },
                        onSurfaceReady = { surf ->
                            pendingSurface = surf
                            service?.setPreviewSurface(surf)
                        },
                        onStartStop = {
                            val svc = service ?: return@MainScreen
                            if (snap.isStreaming) {
                                svc.stopStreaming()
                            } else {
                                val startIntent = Intent(this, CameraForegroundService::class.java).apply {
                                    action = CameraForegroundService.ACTION_START
                                }
                                ContextCompat.startForegroundService(this, startIntent)
                            }
                        },
                        onSwitchCamera = { id -> service?.switchCamera(cameraId = id) },
                        onToggleTorch = { service?.toggleTorch() },
                        onUpdateConfig = { cfg -> service?.updateConfig(cfg) },
                        onTogglePin = { enabled -> service?.setPinEnabled(enabled) },
                        onRegeneratePin = { service?.regeneratePin() },
                        onTapFocus = { nx, ny -> service?.tapFocus(nx, ny) },
                        onDismissError = { service?.clearError() }
                    )
                }
            }
        }
    }

    @Suppress("DEPRECATION")
    private fun currentDisplayRotationDeg(): Int {
        val rot = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            display?.rotation ?: Surface.ROTATION_0
        } else {
            windowManager.defaultDisplay.rotation
        }
        return when (rot) {
            Surface.ROTATION_90 -> 90
            Surface.ROTATION_180 -> 180
            Surface.ROTATION_270 -> 270
            else -> 0
        }
    }

    override fun onDestroy() {
        orientationListener?.disable()
        if (bound) {
            unbindService(connection)
            bound = false
        }
        super.onDestroy()
    }
}

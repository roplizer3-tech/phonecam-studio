package com.phonecam.studio.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.BatteryManager
import android.os.Binder
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import android.view.Surface
import androidx.core.app.NotificationCompat
import com.phonecam.studio.R
import com.phonecam.studio.camera.CameraStreamEngine
import com.phonecam.studio.camera.EncodedFrameListener
import com.phonecam.studio.camera.StreamConfig
import com.phonecam.studio.camera.VideoCodecMode
import com.phonecam.studio.network.ClientStatus
import com.phonecam.studio.network.DiscoveryBroadcaster
import com.phonecam.studio.network.StreamServer
import com.phonecam.studio.protocol.Protocol.CameraDescriptor
import com.phonecam.studio.ui.MainActivity
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import org.json.JSONObject
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledExecutorService
import java.util.concurrent.TimeUnit
import kotlin.random.Random

data class ServiceSnapshot(
    val isStreaming: Boolean = false,
    val clientStatus: ClientStatus = ClientStatus(),
    val cameras: List<CameraDescriptor> = emptyList(),
    val currentCamera: CameraDescriptor? = null,
    val config: StreamConfig = StreamConfig(),
    val torchEnabled: Boolean = false,
    val zoomRatio: Float = 1.0f,
    val pinCode: String = "",
    val pinEnabled: Boolean = true,
    val localIps: List<String> = emptyList(),
    val batteryPct: Int = 100,
    val lastError: String? = null
)

class CameraForegroundService : Service() {
    companion object {
        const val ACTION_START = "com.phonecam.studio.action.START"
        const val ACTION_STOP = "com.phonecam.studio.action.STOP"
        private const val CHANNEL_ID = "phonecam_stream_channel"
        private const val NOTIF_ID = 54820
    }

    inner class LocalBinder : Binder() {
        fun getService(): CameraForegroundService = this@CameraForegroundService
    }

    private val binder = LocalBinder()
    private val _state = MutableStateFlow(
        ServiceSnapshot(
            pinCode = "%04d".format(Random.nextInt(1000, 10000))
        )
    )
    val state: StateFlow<ServiceSnapshot> = _state.asStateFlow()

    private lateinit var cameraEngine: CameraStreamEngine
    private lateinit var streamServer: StreamServer
    private lateinit var discovery: DiscoveryBroadcaster
    private var wakeLock: PowerManager.WakeLock? = null
    private var scheduler: ScheduledExecutorService? = null

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()

        cameraEngine = CameraStreamEngine(
            context = this,
            listener = object : EncodedFrameListener {
                override fun onEncodedFrame(
                    packetType: Byte,
                    flags: Int,
                    data: ByteArray,
                    timestampMs: Long
                ) {
                    streamServer.sendVideoFrame(packetType, flags, data, timestampMs)
                }

                override fun onEngineError(message: String) {
                    _state.value = _state.value.copy(lastError = message)
                }
            }
        )

        streamServer = StreamServer(
            getPin = {
                val s = _state.value
                if (s.pinEnabled) s.pinCode else ""
            },
            getCameras = { cameraEngine.refreshCatalog() },
            getCodecConfig = { cameraEngine.cachedCodecConfig },
            onControlCommand = { cmd -> handleRemoteCommand(cmd) },
            onClientChanged = { status ->
                _state.value = _state.value.copy(clientStatus = status)
            }
        )

        discovery = DiscoveryBroadcaster(
            getPinRequired = { _state.value.pinEnabled },
            getFacing = { cameraEngine.currentCamera?.facing ?: "front" },
            isStreaming = { _state.value.isStreaming }
        )

        syncSnapshot()
        discovery.start()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> {
                stopStreaming()
                stopSelf()
                return START_NOT_STICKY
            }
            ACTION_START -> startStreaming()
        }
        return START_STICKY
    }

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onDestroy() {
        stopStreaming()
        discovery.stop()
        super.onDestroy()
    }

    fun startStreaming() {
        if (_state.value.isStreaming) return
        startForeground(NOTIF_ID, buildNotification())
        acquireWakeLock()
        streamServer.start()
        cameraEngine.start(_state.value.config)
        startTelemetryTimer()
        _state.value = _state.value.copy(isStreaming = true, lastError = null)
        syncSnapshot()
    }

    fun stopStreaming() {
        if (!_state.value.isStreaming) return
        stopTelemetryTimer()
        cameraEngine.stop()
        streamServer.stop()
        releaseWakeLock()
        stopForeground(STOP_FOREGROUND_REMOVE)
        _state.value = _state.value.copy(
            isStreaming = false,
            clientStatus = ClientStatus(false),
            torchEnabled = false
        )
        syncSnapshot()
    }

    fun setPreviewSurface(surface: Surface?) {
        cameraEngine.setPreviewSurface(surface)
    }

    fun setDeviceRotation(deg: Int) {
        cameraEngine.setDeviceRotation(deg)
    }

    fun switchCamera(cameraId: String? = null, facing: String? = null) {
        cameraEngine.switchCamera(cameraId, facing)
        syncSnapshot()
    }

    fun updateConfig(config: StreamConfig) {
        cameraEngine.updateConfig(config)
        syncSnapshot()
    }

    fun toggleTorch() {
        cameraEngine.setTorch(!cameraEngine.torchEnabled)
        syncSnapshot()
    }

    fun setTorch(enabled: Boolean) {
        cameraEngine.setTorch(enabled)
        syncSnapshot()
    }

    fun setZoom(ratio: Float) {
        cameraEngine.setZoom(ratio)
        syncSnapshot()
    }

    fun tapFocus(nx: Float, ny: Float) {
        cameraEngine.tapFocus(nx, ny)
    }

    fun setPinEnabled(enabled: Boolean) {
        _state.value = _state.value.copy(pinEnabled = enabled)
    }

    fun regeneratePin() {
        _state.value = _state.value.copy(pinCode = "%04d".format(Random.nextInt(1000, 10000)))
    }

    fun clearError() {
        _state.value = _state.value.copy(lastError = null)
    }

    private fun handleRemoteCommand(json: JSONObject) {
        when (json.optString("cmd")) {
            "switch_camera" -> {
                val id = json.optString("cameraId").ifBlank { null }
                val facing = json.optString("facing").ifBlank { null }
                switchCamera(id, facing)
            }
            "set_config" -> {
                val cur = cameraEngine.config
                val codecStr = json.optString("codec", cur.codec.name.lowercase())
                val codec = if (codecStr == "mjpeg") VideoCodecMode.MJPEG else VideoCodecMode.H264
                updateConfig(
                    StreamConfig(
                        width = json.optInt("width", cur.width),
                        height = json.optInt("height", cur.height),
                        fps = json.optInt("fps", cur.fps),
                        bitrateBps = json.optInt("bitrate", cur.bitrateBps),
                        codec = codec
                    )
                )
            }
            "set_torch" -> setTorch(json.optBoolean("enabled", false))
            "tap_focus" -> tapFocus(
                json.optDouble("nx", 0.5).toFloat(),
                json.optDouble("ny", 0.5).toFloat()
            )
            "set_zoom" -> setZoom(json.optDouble("ratio", 1.0).toFloat())
            "request_keyframe" -> cameraEngine.requestKeyFrame()
        }
    }

    private fun startTelemetryTimer() {
        if (scheduler != null) return
        scheduler = Executors.newSingleThreadScheduledExecutor().apply {
            scheduleAtFixedRate({
                try {
                    syncSnapshot()
                    sendTelemetryPacket()
                } catch (_: Exception) {}
            }, 0, 1000, TimeUnit.MILLISECONDS)
        }
    }

    private fun stopTelemetryTimer() {
        scheduler?.shutdownNow()
        scheduler = null
    }

    private fun sendTelemetryPacket() {
        val snap = _state.value
        val cam = snap.currentCamera ?: return
        val json = JSONObject().apply {
            put("cameraId", cam.id)
            put("facing", cam.facing)
            put("sensorOrientation", cam.sensorOrientation)
            put("uprightRotation", cameraEngine.currentUprightRotation())
            put("width", snap.config.width)
            put("height", snap.config.height)
            put("fps", snap.config.fps)
            put("batteryPct", snap.batteryPct)
            put("torchOn", snap.torchEnabled)
            put("codec", snap.config.codec.name.lowercase())
        }.toString()
        streamServer.sendTelemetry(json)
    }

    private fun syncSnapshot() {
        _state.value = _state.value.copy(
            cameras = cameraEngine.cameras,
            currentCamera = cameraEngine.currentCamera,
            config = cameraEngine.config,
            torchEnabled = cameraEngine.torchEnabled,
            zoomRatio = cameraEngine.zoomRatio,
            localIps = DiscoveryBroadcaster.getLocalIpv4Addresses(),
            batteryPct = readBatteryPct()
        )
    }

    private fun readBatteryPct(): Int {
        val intent = registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
        val level = intent?.getIntExtra(BatteryManager.EXTRA_LEVEL, -1) ?: -1
        val scale = intent?.getIntExtra(BatteryManager.EXTRA_SCALE, 100) ?: 100
        return if (level >= 0 && scale > 0) (level * 100) / scale else 100
    }

    private fun acquireWakeLock() {
        if (wakeLock == null) {
            val pm = getSystemService(Context.POWER_SERVICE) as PowerManager
            wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "PhoneCamStudio::StreamLock")
        }
        if (wakeLock?.isHeld == false) {
            wakeLock?.acquire(6 * 60 * 60 * 1000L)
        }
    }

    private fun releaseWakeLock() {
        try {
            if (wakeLock?.isHeld == true) wakeLock?.release()
        } catch (_: Exception) {}
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val ch = NotificationChannel(
                CHANNEL_ID,
                getString(R.string.notif_channel_name),
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = getString(R.string.notif_channel_desc)
            }
            val nm = getSystemService(NotificationManager::class.java)
            nm.createNotificationChannel(ch)
        }
    }

    private fun buildNotification(): Notification {
        val openIntent = Intent(this, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_SINGLE_TOP
        }
        val openPending = PendingIntent.getActivity(
            this, 0, openIntent, PendingIntent.FLAG_IMMUTABLE
        )

        val stopIntent = Intent(this, CameraForegroundService::class.java).apply {
            action = ACTION_STOP
        }
        val stopPending = PendingIntent.getService(
            this, 1, stopIntent, PendingIntent.FLAG_IMMUTABLE
        )

        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_notification)
            .setContentTitle(getString(R.string.notif_title_streaming))
            .setContentText("Port 54820 • ${DiscoveryBroadcaster.getLocalIpv4Addresses().firstOrNull() ?: "USB/Wi-Fi"}")
            .setContentIntent(openPending)
            .setOngoing(true)
            .addAction(0, getString(R.string.notif_action_stop), stopPending)
            .build()
    }
}

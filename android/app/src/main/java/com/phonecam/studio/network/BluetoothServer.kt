package com.phonecam.studio.network

import android.Manifest
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothServerSocket
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import androidx.core.content.ContextCompat
import com.phonecam.studio.protocol.Protocol
import com.phonecam.studio.protocol.Protocol.CameraDescriptor
import org.json.JSONObject
import java.util.UUID
import java.util.concurrent.atomic.AtomicBoolean

class BluetoothServer(
    private val context: Context,
    private val getPin: () -> String,
    private val getCameras: () -> List<CameraDescriptor>,
    private val getCodecConfig: () -> ByteArray?,
    private val onControlCommand: (JSONObject) -> Unit,
    private val onClientChanged: (ClientStatus) -> Unit
) {
    private val running = AtomicBoolean(false)
    private var serverSocket: BluetoothServerSocket? = null
    private var acceptThread: Thread? = null

    @Volatile
    private var activeClient: ClientTransportSession? = null

    fun hasPermissionAndEnabled(): Boolean {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            if (ContextCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_CONNECT) !=
                PackageManager.PERMISSION_GRANTED
            ) {
                return false
            }
        }
        val mgr = context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
        return mgr?.adapter?.isEnabled == true
    }

    fun start(): Boolean {
        if (!hasPermissionAndEnabled()) return false
        if (running.getAndSet(true)) return true
        acceptThread = Thread({ acceptLoop() }, "PhoneCam-BtAccept").apply {
            isDaemon = true
            start()
        }
        return true
    }

    fun stop() {
        if (!running.getAndSet(false)) return
        try {
            serverSocket?.close()
        } catch (_: Exception) {}
        serverSocket = null
        activeClient?.close()
        activeClient = null
    }

    fun sendVideoFrame(type: Byte, flags: Int, data: ByteArray, timestampMs: Long) {
        activeClient?.enqueueVideo(OutboundPacket(type, flags, data, timestampMs))
    }

    fun sendTelemetry(json: String) {
        activeClient?.enqueueControl(
            OutboundPacket(
                Protocol.TYPE_TELEMETRY,
                0,
                json.toByteArray(Charsets.UTF_8),
                System.currentTimeMillis()
            )
        )
    }

    private fun acceptLoop() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S &&
            ContextCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_CONNECT) !=
            PackageManager.PERMISSION_GRANTED
        ) {
            running.set(false)
            return
        }

        val mgr = context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
        val adapter = mgr?.adapter ?: run {
            running.set(false)
            return
        }

        try {
            val uuid = UUID.fromString(Protocol.BLUETOOTH_SERVICE_UUID)
            val srv = adapter.listenUsingRfcommWithServiceRecord(Protocol.BLUETOOTH_SERVICE_NAME, uuid)
            serverSocket = srv

            while (running.get()) {
                val btSock = try {
                    srv.accept()
                } catch (_: Exception) {
                    break
                }
                activeClient?.close()
                val remoteAddr = try {
                    btSock.remoteDevice?.name ?: btSock.remoteDevice?.address ?: "Bluetooth PC"
                } catch (_: SecurityException) {
                    "Bluetooth PC"
                }
                val session = ClientTransportSession(
                    inputStream = btSock.inputStream,
                    outputStream = btSock.outputStream,
                    transportLabel = "Bluetooth",
                    remoteLabel = remoteAddr,
                    closeHandle = btSock,
                    getPin = getPin,
                    getCameras = getCameras,
                    getCodecConfig = getCodecConfig,
                    onControlCommand = onControlCommand,
                    onAuthenticatedCallback = { st ->
                        onClientChanged(st)
                        // Switch encoder automatically to Bluetooth-safe bandwidth profile (640x360 @ 20 FPS, 850kbps)
                        onControlCommand(JSONObject().apply {
                            put("cmd", "set_config")
                            put("width", 640)
                            put("height", 360)
                            put("fps", 20)
                            put("bitrate", 850_000)
                            put("codec", "h264")
                        })
                    },
                    onClosedCallback = { closedSession ->
                        if (activeClient === closedSession) {
                            activeClient = null
                            onClientChanged(ClientStatus(connected = false))
                        }
                    }
                )
                activeClient = session
                session.start()
            }
        } catch (_: SecurityException) {
        } catch (_: Exception) {
        } finally {
            running.set(false)
        }
    }
}

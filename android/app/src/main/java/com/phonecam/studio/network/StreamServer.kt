package com.phonecam.studio.network

import com.phonecam.studio.protocol.Protocol
import com.phonecam.studio.protocol.Protocol.CameraDescriptor
import org.json.JSONObject
import java.net.InetSocketAddress
import java.net.ServerSocket
import java.util.concurrent.atomic.AtomicBoolean

class StreamServer(
    private val getPin: () -> String,
    private val getCameras: () -> List<CameraDescriptor>,
    private val getCodecConfig: () -> ByteArray?,
    private val onControlCommand: (JSONObject) -> Unit,
    private val onClientChanged: (ClientStatus) -> Unit
) {
    private val running = AtomicBoolean(false)
    private var serverSocket: ServerSocket? = null
    private var acceptThread: Thread? = null

    @Volatile
    private var activeClient: ClientTransportSession? = null

    fun start() {
        if (running.getAndSet(true)) return
        acceptThread = Thread({ acceptLoop() }, "PhoneCam-TcpAccept").apply {
            isDaemon = true
            start()
        }
    }

    fun stop() {
        if (!running.getAndSet(false)) return
        try {
            serverSocket?.close()
        } catch (_: Exception) {}
        serverSocket = null
        activeClient?.close()
        activeClient = null
        onClientChanged(ClientStatus(connected = false))
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
        try {
            val server = ServerSocket().apply {
                reuseAddress = true
                bind(InetSocketAddress(Protocol.TCP_PORT))
            }
            serverSocket = server

            while (running.get()) {
                val socket = try {
                    server.accept()
                } catch (_: Exception) {
                    break
                }
                socket.tcpNoDelay = true
                socket.sendBufferSize = 65536
                socket.soTimeout = 15_000

                activeClient?.close()
                val remoteAddr = socket.inetAddress?.hostAddress ?: "Wi-Fi/USB"
                val label = if (remoteAddr == "127.0.0.1") "USB" else "Wi-Fi"
                val session = ClientTransportSession(
                    inputStream = socket.getInputStream(),
                    outputStream = socket.getOutputStream(),
                    transportLabel = label,
                    remoteLabel = remoteAddr,
                    closeHandle = socket,
                    getPin = getPin,
                    getCameras = getCameras,
                    getCodecConfig = getCodecConfig,
                    onControlCommand = onControlCommand,
                    onAuthenticatedCallback = { st -> onClientChanged(st) },
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
        } catch (_: Exception) {
        }
    }
}

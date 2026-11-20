package com.phonecam.studio.network

import android.os.Build
import com.phonecam.studio.protocol.Protocol
import com.phonecam.studio.protocol.Protocol.CameraDescriptor
import org.json.JSONObject
import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.io.DataInputStream
import java.io.EOFException
import java.net.InetSocketAddress
import java.net.ServerSocket
import java.net.Socket
import java.util.concurrent.LinkedBlockingDeque
import java.util.concurrent.atomic.AtomicBoolean

data class ClientStatus(
    val connected: Boolean = false,
    val clientAddress: String = ""
)

class StreamServer(
    private val getPin: () -> String,
    private val getCameras: () -> List<CameraDescriptor>,
    private val getCodecConfig: () -> ByteArray?,
    private val onControlCommand: (JSONObject) -> Unit,
    private val onClientChanged: (ClientStatus) -> Unit
) {
    private data class OutboundPacket(
        val type: Byte,
        val flags: Int,
        val data: ByteArray,
        val timestampMs: Long
    )

    private val running = AtomicBoolean(false)
    private var serverSocket: ServerSocket? = null
    private var acceptThread: Thread? = null

    @Volatile
    private var activeClient: ClientSession? = null

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
                socket.sendBufferSize = 256 * 1024
                socket.soTimeout = 15_000

                // Single active desktop studio client at a time
                activeClient?.close()
                val session = ClientSession(socket)
                activeClient = session
                session.start()
            }
        } catch (_: Exception) {
            // Stopped
        }
    }

    private inner class ClientSession(private val socket: Socket) {
        private val closed = AtomicBoolean(false)
        private val authenticated = AtomicBoolean(false)
        private val videoQueue = LinkedBlockingDeque<OutboundPacket>(3)
        private val controlQueue = LinkedBlockingDeque<OutboundPacket>(16)
        private val wakeSignal = Object()

        fun start() {
            Thread({ runSession() }, "PhoneCam-ClientWrite").apply {
                isDaemon = true
                start()
            }
            Thread({ runReader() }, "PhoneCam-ClientRead").apply {
                isDaemon = true
                start()
            }
        }

        fun enqueueVideo(pkt: OutboundPacket) {
            if (closed.get() || !authenticated.get()) return
            val isConfig = (pkt.flags and Protocol.FLAG_CODEC_CONFIG) != 0
            if (!isConfig) {
                while (videoQueue.size >= 2) {
                    val dropped = videoQueue.peekFirst()
                    if (dropped != null && (dropped.flags and Protocol.FLAG_CODEC_CONFIG) == 0) {
                        videoQueue.pollFirst()
                    } else {
                        break
                    }
                }
            }
            videoQueue.offerLast(pkt)
            synchronized(wakeSignal) { wakeSignal.notifyAll() }
        }

        fun enqueueControl(pkt: OutboundPacket) {
            if (closed.get()) return
            controlQueue.offerLast(pkt)
            synchronized(wakeSignal) { wakeSignal.notifyAll() }
        }

        fun close() {
            if (closed.getAndSet(true)) return
            try {
                socket.close()
            } catch (_: Exception) {}
            synchronized(wakeSignal) { wakeSignal.notifyAll() }
            if (activeClient === this) {
                activeClient = null
                onClientChanged(ClientStatus(connected = false))
            }
        }

        private val nonce = Protocol.generateNonceHex()

        private fun runSession() {
            try {
                val out = BufferedOutputStream(socket.getOutputStream(), 65536)
                val pin = getPin().trim()
                val pinRequired = pin.isNotEmpty()
                val hello = Protocol.buildHelloJson(
                    deviceName = "${Build.MANUFACTURER} ${Build.MODEL}".trim(),
                    pinRequired = pinRequired,
                    nonce = nonce,
                    cameras = getCameras()
                ).toByteArray(Charsets.UTF_8)
                writePacket(out, Protocol.TYPE_HELLO, 0, hello, System.currentTimeMillis())

                if (!pinRequired) {
                    onAuthenticated()
                }

                while (!closed.get() && running.get()) {
                    val pkt = controlQueue.pollFirst() ?: videoQueue.pollFirst()
                    if (pkt == null) {
                        synchronized(wakeSignal) {
                            if (!closed.get() && controlQueue.isEmpty() && videoQueue.isEmpty()) {
                                wakeSignal.wait(250)
                            }
                        }
                        continue
                    }
                    writePacket(out, pkt.type, pkt.flags, pkt.data, pkt.timestampMs)
                    if (controlQueue.isEmpty() && videoQueue.isEmpty()) {
                        out.flush()
                    }
                }
            } catch (_: Exception) {
                // Disconnected
            } finally {
                close()
            }
        }

        private fun onAuthenticated() {
            if (authenticated.getAndSet(true)) return
            val addr = socket.inetAddress?.hostAddress ?: "USB/Wi-Fi"
            onClientChanged(ClientStatus(connected = true, clientAddress = addr))

            getCodecConfig()?.let { cfg ->
                enqueueVideo(
                    OutboundPacket(
                        Protocol.TYPE_VIDEO_H264,
                        Protocol.FLAG_CODEC_CONFIG,
                        cfg,
                        System.currentTimeMillis()
                    )
                )
            }
            onControlCommand(JSONObject().apply { put("cmd", "request_keyframe") })
        }

        private fun runReader() {
            try {
                val input = DataInputStream(BufferedInputStream(socket.getInputStream()))
                val headerBuf = ByteArray(Protocol.HEADER_SIZE)
                while (!closed.get() && running.get()) {
                    input.readFully(headerBuf)
                    val hdr = Protocol.decodeHeader(headerBuf) ?: break
                    val payload = ByteArray(hdr.payloadSize)
                    if (hdr.payloadSize > 0) {
                        input.readFully(payload)
                    }
                    handleIncomingPacket(hdr.type, payload)
                }
            } catch (_: EOFException) {
                // Clean disconnect
            } catch (_: Exception) {
                // Socket error
            } finally {
                close()
            }
        }

        private fun handleIncomingPacket(type: Byte, payload: ByteArray) {
            when (type) {
                Protocol.TYPE_AUTH -> {
                    val json = JSONObject(String(payload, Charsets.UTF_8))
                    val supplied = json.optString("pinHash", "")
                    val expectedPin = getPin().trim()
                    val ok = if (expectedPin.isEmpty()) {
                        true
                    } else {
                        val expectedHash = Protocol.computePinHash(nonce, expectedPin)
                        Protocol.constantTimeEquals(supplied.lowercase(), expectedHash)
                    }
                    val resp = JSONObject().apply {
                        put("ok", ok)
                        put("reason", if (ok) "ok" else "Invalid PIN")
                    }.toString().toByteArray(Charsets.UTF_8)
                    enqueueControl(OutboundPacket(Protocol.TYPE_AUTH, 0, resp, System.currentTimeMillis()))
                    if (ok) {
                        onAuthenticated()
                    } else {
                        Thread {
                            Thread.sleep(200)
                            close()
                        }.start()
                    }
                }
                Protocol.TYPE_CONTROL -> {
                    if (!authenticated.get()) return
                    val cmd = JSONObject(String(payload, Charsets.UTF_8))
                    onControlCommand(cmd)
                }
                Protocol.TYPE_PING -> {
                    enqueueControl(OutboundPacket(Protocol.TYPE_PONG, 0, payload, System.currentTimeMillis()))
                }
            }
        }

        private fun writePacket(
            out: BufferedOutputStream,
            type: Byte,
            flags: Int,
            data: ByteArray,
            timestampMs: Long
        ) {
            val header = Protocol.encodeHeader(type, flags, data.size, timestampMs)
            out.write(header)
            if (data.isNotEmpty()) {
                out.write(data)
            }
        }
    }
}

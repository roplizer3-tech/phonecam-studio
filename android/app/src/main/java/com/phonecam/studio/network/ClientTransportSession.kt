package com.phonecam.studio.network

import android.os.Build
import com.phonecam.studio.protocol.Protocol
import com.phonecam.studio.protocol.Protocol.CameraDescriptor
import org.json.JSONObject
import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.io.Closeable
import java.io.DataInputStream
import java.io.EOFException
import java.io.InputStream
import java.io.OutputStream
import java.util.concurrent.LinkedBlockingDeque
import java.util.concurrent.atomic.AtomicBoolean

data class ClientStatus(
    val connected: Boolean = false,
    val clientAddress: String = "",
    val transport: String = ""
)

data class OutboundPacket(
    val type: Byte,
    val flags: Int,
    val data: ByteArray,
    val timestampMs: Long
)

/**
 * GOP-safe frame queue: never drops individual P-frames mid-GOP.
 * When backpressure fills the queue, it flushes stale queued frames, sets awaitingKeyframe,
 * and resumes strictly from the next SPS/PPS or IDR keyframe.
 */
class GopSafeFrameQueue(private val maxQueuedFrames: Int = 6) {
    private val queue = LinkedBlockingDeque<OutboundPacket>()
    @Volatile
    var awaitingKeyframe: Boolean = false
        private set

    fun enqueue(pkt: OutboundPacket): Boolean {
        val isConfig = (pkt.flags and Protocol.FLAG_CODEC_CONFIG) != 0
        val isKey = (pkt.flags and Protocol.FLAG_KEYFRAME) != 0
        val isMjpeg = pkt.type == Protocol.TYPE_VIDEO_MJPEG

        if (isConfig) {
            queue.offerLast(pkt)
            return false
        }

        if (awaitingKeyframe) {
            if (!isKey && !isMjpeg) {
                return false
            }
            awaitingKeyframe = false
        }

        var needKeyframe = false
        if (queue.size >= maxQueuedFrames) {
            // Preserve only latest codec config while discarding stale partial GOP
            val preservedConfig = queue.lastOrNull { (it.flags and Protocol.FLAG_CODEC_CONFIG) != 0 }
            queue.clear()
            preservedConfig?.let { queue.offerLast(it) }
            if (!isKey && !isMjpeg) {
                awaitingKeyframe = true
                needKeyframe = true
                return needKeyframe
            }
        }

        queue.offerLast(pkt)
        return needKeyframe
    }

    fun poll(): OutboundPacket? = queue.pollFirst()
    fun isEmpty(): Boolean = queue.isEmpty()
    fun size(): Int = queue.size
    fun clear() = queue.clear()
}

class ClientTransportSession(
    private val inputStream: InputStream,
    private val outputStream: OutputStream,
    private val transportLabel: String,
    private val remoteLabel: String,
    private val closeHandle: Closeable,
    private val getPin: () -> String,
    private val getCameras: () -> List<CameraDescriptor>,
    private val getCodecConfig: () -> ByteArray?,
    private val onControlCommand: (JSONObject) -> Unit,
    private val onAuthenticatedCallback: (ClientStatus) -> Unit,
    private val onClosedCallback: (ClientTransportSession) -> Unit
) {
    private val closed = AtomicBoolean(false)
    private val authenticated = AtomicBoolean(false)
    private val videoQueue = GopSafeFrameQueue(maxQueuedFrames = 6)
    private val controlQueue = LinkedBlockingDeque<OutboundPacket>(24)
    private val wakeSignal = Object()
    private val nonce = Protocol.generateNonceHex()

    fun start() {
        Thread({ runWriter() }, "PhoneCam-$transportLabel-Write").apply {
            isDaemon = true
            priority = Thread.MAX_PRIORITY
            start()
        }
        Thread({ runReader() }, "PhoneCam-$transportLabel-Read").apply {
            isDaemon = true
            start()
        }
    }

    fun enqueueVideo(pkt: OutboundPacket) {
        if (closed.get() || !authenticated.get()) return
        val requestIdr = synchronized(wakeSignal) {
            val needIdr = videoQueue.enqueue(pkt)
            wakeSignal.notifyAll()
            needIdr
        }
        if (requestIdr) {
            onControlCommand(JSONObject().apply { put("cmd", "request_keyframe") })
        }
    }

    fun enqueueControl(pkt: OutboundPacket) {
        if (closed.get()) return
        synchronized(wakeSignal) {
            controlQueue.offerLast(pkt)
            wakeSignal.notifyAll()
        }
    }

    fun close() {
        if (closed.getAndSet(true)) return
        try {
            closeHandle.close()
        } catch (_: Exception) {}
        synchronized(wakeSignal) {
            videoQueue.clear()
            controlQueue.clear()
            wakeSignal.notifyAll()
        }
        onClosedCallback(this)
    }

    private fun runWriter() {
        try {
            val out = BufferedOutputStream(outputStream, 65536)
            val pin = getPin().trim()
            val pinRequired = pin.isNotEmpty()
            val hello = Protocol.buildHelloJson(
                deviceName = "${Build.MANUFACTURER} ${Build.MODEL}".trim(),
                pinRequired = pinRequired,
                nonce = nonce,
                cameras = getCameras()
            ).toByteArray(Charsets.UTF_8)
            writePacket(out, Protocol.TYPE_HELLO, 0, hello, System.currentTimeMillis())
            out.flush()

            if (!pinRequired) {
                markAuthenticated()
            }

            while (!closed.get()) {
                val pkt: OutboundPacket? = synchronized(wakeSignal) {
                    val p = controlQueue.pollFirst() ?: videoQueue.poll()
                    if (p == null && !closed.get()) {
                        wakeSignal.wait(200)
                    }
                    p
                }
                if (pkt == null) continue

                writePacket(out, pkt.type, pkt.flags, pkt.data, pkt.timestampMs)
                // Always flush immediately so H.264 NAL units never sit inside BufferedOutputStream
                out.flush()
            }
        } catch (_: Exception) {
        } finally {
            close()
        }
    }

    private fun markAuthenticated() {
        if (authenticated.getAndSet(true)) return
        onAuthenticatedCallback(ClientStatus(connected = true, clientAddress = remoteLabel, transport = transportLabel))
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
            val input = DataInputStream(BufferedInputStream(inputStream))
            val headerBuf = ByteArray(Protocol.HEADER_SIZE)
            while (!closed.get()) {
                input.readFully(headerBuf)
                val hdr = Protocol.decodeHeader(headerBuf) ?: break
                val payload = ByteArray(hdr.payloadSize)
                if (hdr.payloadSize > 0) {
                    input.readFully(payload)
                }
                handleIncoming(hdr.type, payload)
            }
        } catch (_: EOFException) {
        } catch (_: Exception) {
        } finally {
            close()
        }
    }

    private fun handleIncoming(type: Byte, payload: ByteArray) {
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
                    markAuthenticated()
                } else {
                    Thread {
                        Thread.sleep(200)
                        close()
                    }.start()
                }
            }
            Protocol.TYPE_CONTROL -> {
                if (!authenticated.get()) return
                onControlCommand(JSONObject(String(payload, Charsets.UTF_8)))
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
        out.write(Protocol.encodeHeader(type, flags, data.size, timestampMs))
        if (data.isNotEmpty()) {
            out.write(data)
        }
    }
}

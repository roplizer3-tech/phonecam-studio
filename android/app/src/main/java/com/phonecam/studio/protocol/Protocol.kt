package com.phonecam.studio.protocol

import org.json.JSONArray
import org.json.JSONObject
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest
import java.security.SecureRandom

object Protocol {
    const val TCP_PORT = 54820
    const val UDP_DISCOVERY_PORT = 54821
    const val MAGIC: Int = 0x5043414D // "PCAM" LE
    const val HEADER_SIZE = 16
    const val MAX_PAYLOAD_BYTES = 8 * 1024 * 1024
    const val PROTOCOL_VERSION = 1

    const val TYPE_HELLO: Byte = 1
    const val TYPE_AUTH: Byte = 2
    const val TYPE_VIDEO_H264: Byte = 3
    const val TYPE_VIDEO_MJPEG: Byte = 4
    const val TYPE_CONTROL: Byte = 5
    const val TYPE_TELEMETRY: Byte = 6
    const val TYPE_PING: Byte = 7
    const val TYPE_PONG: Byte = 8

    const val FLAG_KEYFRAME: Int = 0x01
    const val FLAG_CODEC_CONFIG: Int = 0x02
    const val FLAG_FRONT_CAMERA: Int = 0x04
    const val FLAG_ROTATION_MASK: Int = 0x18
    const val FLAG_ROTATION_SHIFT: Int = 3
    const val FLAG_MIRRORED_BY_SENSOR: Int = 0x20

    data class PacketHeader(
        val magic: Int,
        val type: Byte,
        val flags: Int,
        val reserved: Int,
        val payloadSize: Int,
        val timestampMs: Long
    )

    data class CameraDescriptor(
        val id: String,
        val facing: String,
        val sensorOrientation: Int,
        val label: String,
        val hasFlash: Boolean,
        val maxZoom: Float
    )

    fun encodeHeader(
        type: Byte,
        flags: Int,
        payloadSize: Int,
        timestampMs: Long
    ): ByteArray {
        val buf = ByteBuffer.allocate(HEADER_SIZE).order(ByteOrder.LITTLE_ENDIAN)
        buf.putInt(MAGIC)
        buf.put(type)
        buf.put((flags and 0xFF).toByte())
        buf.putShort(0)
        buf.putInt(payloadSize)
        buf.putInt((timestampMs and 0xFFFFFFFFL).toInt())
        return buf.array()
    }

    fun decodeHeader(bytes: ByteArray): PacketHeader? {
        if (bytes.size < HEADER_SIZE) return null
        val buf = ByteBuffer.wrap(bytes, 0, HEADER_SIZE).order(ByteOrder.LITTLE_ENDIAN)
        val magic = buf.int
        if (magic != MAGIC) return null
        val type = buf.get()
        val flags = buf.get().toInt() and 0xFF
        val reserved = buf.short.toInt() and 0xFFFF
        val payloadSize = buf.int
        if (payloadSize < 0 || payloadSize > MAX_PAYLOAD_BYTES) return null
        val timestampMs = buf.int.toLong() and 0xFFFFFFFFL
        return PacketHeader(magic, type, flags, reserved, payloadSize, timestampMs)
    }

    fun rotationDegToCode(degreesCw: Int): Int {
        val norm = ((degreesCw % 360) + 360) % 360
        return when (norm) {
            90 -> 1
            180 -> 2
            270 -> 3
            else -> 0
        }
    }

    fun rotationCodeToDeg(code: Int): Int = (code and 0x03) * 90

    fun buildVideoFlags(
        isKeyFrame: Boolean,
        isCodecConfig: Boolean,
        isFrontCamera: Boolean,
        uprightRotationDeg: Int,
        mirroredBySensor: Boolean = false
    ): Int {
        var f = 0
        if (isKeyFrame) f = f or FLAG_KEYFRAME
        if (isCodecConfig) f = f or FLAG_CODEC_CONFIG
        if (isFrontCamera) f = f or FLAG_FRONT_CAMERA
        val rotCode = rotationDegToCode(uprightRotationDeg)
        f = f or ((rotCode shl FLAG_ROTATION_SHIFT) and FLAG_ROTATION_MASK)
        if (mirroredBySensor) f = f or FLAG_MIRRORED_BY_SENSOR
        return f
    }

    /**
     * Computes clockwise degrees needed to orient the raw sensor output upright
     * given the camera's SENSOR_ORIENTATION and current device display rotation (0, 90, 180, 270).
     */
    fun computeUprightRotation(
        sensorOrientation: Int,
        deviceRotationDeg: Int,
        isFrontCamera: Boolean
    ): Int {
        val sign = if (isFrontCamera) 1 else -1
        return ((sensorOrientation + deviceRotationDeg * sign) % 360 + 360) % 360
    }

    fun generateNonceHex(byteCount: Int = 16): String {
        val bytes = ByteArray(byteCount)
        SecureRandom().nextBytes(bytes)
        return bytes.joinToString("") { "%02x".format(it) }
    }

    fun computePinHash(nonce: String, pin: String): String {
        val digest = MessageDigest.getInstance("SHA-256")
            .digest("$nonce:$pin".toByteArray(Charsets.UTF_8))
        return digest.joinToString("") { "%02x".format(it) }
    }

    fun constantTimeEquals(a: String, b: String): Boolean {
        if (a.length != b.length) return false
        var diff = 0
        for (i in a.indices) {
            diff = diff or (a[i].code xor b[i].code)
        }
        return diff == 0
    }

    fun buildHelloJson(
        deviceName: String,
        pinRequired: Boolean,
        nonce: String,
        cameras: List<CameraDescriptor>
    ): String {
        val arr = JSONArray()
        cameras.forEach { cam ->
            arr.put(
                JSONObject().apply {
                    put("id", cam.id)
                    put("facing", cam.facing)
                    put("sensorOrientation", cam.sensorOrientation)
                    put("label", cam.label)
                    put("hasFlash", cam.hasFlash)
                    put("maxZoom", cam.maxZoom.toDouble())
                }
            )
        }
        return JSONObject().apply {
            put("deviceName", deviceName)
            put("protocolVersion", PROTOCOL_VERSION)
            put("pinRequired", pinRequired)
            put("nonce", nonce)
            put("cameras", arr)
        }.toString()
    }

    fun buildDiscoveryJson(
        deviceName: String,
        pinRequired: Boolean,
        facing: String,
        streaming: Boolean
    ): String {
        return JSONObject().apply {
            put("magic", "PCAM_DISC")
            put("version", PROTOCOL_VERSION)
            put("deviceName", deviceName)
            put("tcpPort", TCP_PORT)
            put("pinRequired", pinRequired)
            put("facing", facing)
            put("streaming", streaming)
        }.toString()
    }
}

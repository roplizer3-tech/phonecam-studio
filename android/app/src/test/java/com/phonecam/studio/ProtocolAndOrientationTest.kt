package com.phonecam.studio

import com.phonecam.studio.camera.StreamConfig
import com.phonecam.studio.network.GopSafeFrameQueue
import com.phonecam.studio.network.OutboundPacket
import com.phonecam.studio.protocol.Protocol
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.util.UUID

class ProtocolAndOrientationTest {

    @Test
    fun headerEncodeDecodeRoundtrip() {
        val flags = Protocol.buildVideoFlags(
            isKeyFrame = true,
            isCodecConfig = false,
            isFrontCamera = true,
            uprightRotationDeg = 270,
            mirroredBySensor = false
        )
        val raw = Protocol.encodeHeader(
            type = Protocol.TYPE_VIDEO_H264,
            flags = flags,
            payloadSize = 65536,
            timestampMs = 123456789L
        )
        assertEquals(Protocol.HEADER_SIZE, raw.size)

        val decoded = Protocol.decodeHeader(raw)
        assertNotNull(decoded)
        decoded!!
        assertEquals(Protocol.MAGIC, decoded.magic)
        assertEquals(Protocol.TYPE_VIDEO_H264, decoded.type)
        assertEquals(65536, decoded.payloadSize)
        assertEquals(123456789L, decoded.timestampMs)
        assertTrue((decoded.flags and Protocol.FLAG_KEYFRAME) != 0)
        assertTrue((decoded.flags and Protocol.FLAG_FRONT_CAMERA) != 0)
        assertFalse((decoded.flags and Protocol.FLAG_CODEC_CONFIG) != 0)
        val rotCode = (decoded.flags and Protocol.FLAG_ROTATION_MASK) shr Protocol.FLAG_ROTATION_SHIFT
        assertEquals(270, Protocol.rotationCodeToDeg(rotCode))
    }

    @Test
    fun headerRejectsOversizedPayloadAndBadMagic() {
        val badMagic = Protocol.encodeHeader(Protocol.TYPE_VIDEO_H264, 0, 1024, 1L)
        badMagic[0] = 0x00
        assertNull(Protocol.decodeHeader(badMagic))

        val oversized = Protocol.encodeHeader(
            Protocol.TYPE_VIDEO_H264,
            0,
            Protocol.MAX_PAYLOAD_BYTES + 1,
            1L
        )
        assertNull(Protocol.decodeHeader(oversized))
    }

    @Test
    fun pinChallengeResponseVerification() {
        val nonce = "0123456789abcdef0123456789abcdef"
        val hash1 = Protocol.computePinHash(nonce, "4829")
        val hash2 = Protocol.computePinHash(nonce, "4829")
        val wrongHash = Protocol.computePinHash(nonce, "9999")

        assertEquals(64, hash1.length)
        assertTrue(Protocol.constantTimeEquals(hash1, hash2))
        assertFalse(Protocol.constantTimeEquals(hash1, wrongHash))
    }

    @Test
    fun uprightRotationMathCoversFrontAndBackAcrossDeviceRotations() {
        // Back camera (sensorOrientation = 90)
        assertEquals(90, Protocol.computeUprightRotation(90, 0, isFrontCamera = false))
        assertEquals(0, Protocol.computeUprightRotation(90, 90, isFrontCamera = false))
        assertEquals(270, Protocol.computeUprightRotation(90, 180, isFrontCamera = false))
        assertEquals(180, Protocol.computeUprightRotation(90, 270, isFrontCamera = false))

        // Front camera (sensorOrientation = 270)
        assertEquals(270, Protocol.computeUprightRotation(270, 0, isFrontCamera = true))
        assertEquals(0, Protocol.computeUprightRotation(270, 90, isFrontCamera = true))
        assertEquals(90, Protocol.computeUprightRotation(270, 180, isFrontCamera = true))
        assertEquals(180, Protocol.computeUprightRotation(270, 270, isFrontCamera = true))
    }

    @Test
    fun helloAndDiscoveryJsonSerializeCleanly() {
        val cam = Protocol.CameraDescriptor("0", "back", 90, "Back Main", true, 8.0f)
        val hello = Protocol.buildHelloJson("TestPhone", true, "abc123", listOf(cam))
        assertTrue(hello.contains("\"deviceName\":\"TestPhone\""))
        assertTrue(hello.contains("\"pinRequired\":true"))
        assertTrue(hello.contains("\"sensorOrientation\":90"))

        val disc = Protocol.buildDiscoveryJson("TestPhone", false, "front", true)
        assertTrue(disc.contains("PCAM_DISC"))
        assertTrue(disc.contains("54820"))
    }

    @Test
    fun bluetoothServiceUuidAndDefaultConfigAreValid() {
        val uuid = UUID.fromString(Protocol.BLUETOOTH_SERVICE_UUID)
        assertNotNull(uuid)
        val cfg = StreamConfig()
        assertEquals(60, cfg.fps)
        assertEquals(1280, cfg.width)
        assertEquals(720, cfg.height)
    }

    @Test
    fun gopSafeQueueDropsCleanlyAndRecoversAtNextKeyframe() {
        val queue = GopSafeFrameQueue(maxQueuedFrames = 3)
        val sps = OutboundPacket(Protocol.TYPE_VIDEO_H264, Protocol.FLAG_CODEC_CONFIG, byteArrayOf(1), 1L)
        val idr = OutboundPacket(Protocol.TYPE_VIDEO_H264, Protocol.FLAG_KEYFRAME, byteArrayOf(2), 2L)
        val pFrame = OutboundPacket(Protocol.TYPE_VIDEO_H264, 0, byteArrayOf(3), 3L)

        assertFalse(queue.enqueue(sps))
        assertFalse(queue.enqueue(idr))
        assertFalse(queue.enqueue(pFrame))
        // Queue size is now 3 -> overflowing with a P-frame flushes stale P-frames, keeps SPS/PPS, and triggers awaitingKeyframe
        assertTrue(queue.enqueue(pFrame))
        assertTrue(queue.awaitingKeyframe)
        // Trailing P-frame must be skipped while awaitingKeyframe
        assertFalse(queue.enqueue(pFrame))
        assertTrue(queue.awaitingKeyframe)
        // Next IDR keyframe resumes queue cleanly
        assertFalse(queue.enqueue(idr))
        assertFalse(queue.awaitingKeyframe)
        assertEquals(2, queue.size()) // preserved SPS/PPS + new IDR
    }
}

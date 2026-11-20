package com.phonecam.studio

import com.phonecam.studio.protocol.Protocol
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

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
}

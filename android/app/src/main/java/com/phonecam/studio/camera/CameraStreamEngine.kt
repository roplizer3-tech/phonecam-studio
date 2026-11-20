package com.phonecam.studio.camera

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.ImageFormat
import android.graphics.Rect
import android.graphics.YuvImage
import android.hardware.camera2.CameraAccessException
import android.hardware.camera2.CameraCaptureSession
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraDevice
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CaptureRequest
import android.hardware.camera2.params.MeteringRectangle
import android.hardware.camera2.params.OutputConfiguration
import android.hardware.camera2.params.SessionConfiguration
import android.media.Image
import android.media.ImageReader
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import android.util.Range
import android.view.Surface
import com.phonecam.studio.protocol.Protocol
import com.phonecam.studio.protocol.Protocol.CameraDescriptor
import java.io.ByteArrayOutputStream
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

enum class VideoCodecMode { H264, MJPEG }

data class StreamConfig(
    val width: Int = 1280,
    val height: Int = 720,
    val fps: Int = 30,
    val bitrateBps: Int = 4_000_000,
    val codec: VideoCodecMode = VideoCodecMode.H264
)

interface EncodedFrameListener {
    fun onEncodedFrame(
        packetType: Byte,
        flags: Int,
        data: ByteArray,
        timestampMs: Long
    )

    fun onEngineError(message: String)
}

class CameraStreamEngine(
    private val context: Context,
    private val listener: EncodedFrameListener
) {
    companion object {
        private const val TAG = "CameraStreamEngine"
    }

    private val cameraManager = context.getSystemService(Context.CAMERA_SERVICE) as CameraManager
    private val lock = Any()

    private var cameraThread: HandlerThread? = null
    private var cameraHandler: Handler? = null
    private var encoderThread: Thread? = null

    private var cameraDevice: CameraDevice? = null
    private var captureSession: CameraCaptureSession? = null
    private var requestBuilder: CaptureRequest.Builder? = null
    private var previewSurface: Surface? = null

    private var mediaCodec: MediaCodec? = null
    private var codecInputSurface: Surface? = null
    private var imageReader: ImageReader? = null

    private val running = AtomicBoolean(false)
    private val deviceRotationDeg = AtomicInteger(0)

    @Volatile
    var cameras: List<CameraDescriptor> = CameraCatalog.enumerate(cameraManager)
        private set

    @Volatile
    var currentCamera: CameraDescriptor? =
        cameras.firstOrNull { it.facing == "front" } ?: cameras.firstOrNull()
        private set

    @Volatile
    var config: StreamConfig = StreamConfig()
        private set

    @Volatile
    var torchEnabled: Boolean = false
        private set

    @Volatile
    var zoomRatio: Float = 1.0f
        private set

    @Volatile
    var cachedCodecConfig: ByteArray? = null
        private set

    private var currentCharacteristics: CameraCharacteristics? = null
    private var activeArrayRect: Rect? = null

    fun refreshCatalog(): List<CameraDescriptor> {
        cameras = CameraCatalog.enumerate(cameraManager)
        if (currentCamera == null) {
            currentCamera = cameras.firstOrNull { it.facing == "front" } ?: cameras.firstOrNull()
        }
        return cameras
    }

    fun setDeviceRotation(rotationDeg: Int) {
        deviceRotationDeg.set(((rotationDeg % 360) + 360) % 360)
    }

    fun setPreviewSurface(surface: Surface?) {
        val restartNeeded: Boolean
        synchronized(lock) {
            previewSurface = surface
            restartNeeded = running.get()
        }
        if (restartNeeded) {
            restartSession()
        }
    }

    fun start(initialConfig: StreamConfig = config) {
        synchronized(lock) {
            config = initialConfig
            if (running.get()) return
            running.set(true)
            startThreadsLocked()
        }
        openSelectedCamera()
    }

    fun stop() {
        synchronized(lock) {
            if (!running.getAndSet(false)) return
            closePipelineLocked()
            stopThreadsLocked()
        }
    }

    fun switchCamera(targetCameraId: String? = null, targetFacing: String? = null) {
        refreshCatalog()
        val chosen = when {
            !targetCameraId.isNullOrBlank() -> cameras.firstOrNull { it.id == targetCameraId }
            !targetFacing.isNullOrBlank() -> cameras.firstOrNull { it.facing == targetFacing }
            else -> {
                val wantFacing = if (currentCamera?.facing == "front") "back" else "front"
                cameras.firstOrNull { it.facing == wantFacing } ?: cameras.firstOrNull()
            }
        } ?: return

        if (chosen.id == currentCamera?.id && running.get()) return
        currentCamera = chosen
        torchEnabled = false
        zoomRatio = 1.0f
        if (running.get()) {
            restartPipeline()
        }
    }

    fun updateConfig(newConfig: StreamConfig) {
        val sanitized = newConfig.copy(
            width = newConfig.width.coerceIn(320, 1920),
            height = newConfig.height.coerceIn(240, 1080),
            fps = if (newConfig.fps >= 45) 60 else 30,
            bitrateBps = newConfig.bitrateBps.coerceIn(500_000, 16_000_000)
        )
        if (sanitized == config) return
        config = sanitized
        if (running.get()) {
            restartPipeline()
        }
    }

    fun setTorch(enabled: Boolean) {
        val cam = currentCamera ?: return
        torchEnabled = enabled && cam.hasFlash && cam.facing == "back"
        applyRepeatingRequest()
    }

    fun setZoom(ratio: Float) {
        val maxZ = currentCamera?.maxZoom ?: 4.0f
        zoomRatio = ratio.coerceIn(1.0f, maxZ)
        applyRepeatingRequest()
    }

    fun tapFocus(normX: Float, normY: Float) {
        val session = captureSession ?: return
        val builder = requestBuilder ?: return
        val array = activeArrayRect ?: return
        val x = (normX.coerceIn(0f, 1f) * array.width()).toInt()
        val y = (normY.coerceIn(0f, 1f) * array.height()).toInt()
        val box = (array.width() * 0.1f).toInt().coerceAtLeast(64)
        val focusRect = Rect(
            (x - box).coerceIn(0, array.width() - 1),
            (y - box).coerceIn(0, array.height() - 1),
            (x + box).coerceIn(1, array.width()),
            (y + box).coerceIn(1, array.height())
        )
        try {
            val metering = arrayOf(MeteringRectangle(focusRect, MeteringRectangle.METERING_WEIGHT_MAX))
            builder.set(CaptureRequest.CONTROL_AF_REGIONS, metering)
            builder.set(CaptureRequest.CONTROL_AE_REGIONS, metering)
            builder.set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_AUTO)
            builder.set(CaptureRequest.CONTROL_AF_TRIGGER, CaptureRequest.CONTROL_AF_TRIGGER_START)
            session.capture(builder.build(), null, cameraHandler)
            builder.set(CaptureRequest.CONTROL_AF_TRIGGER, CaptureRequest.CONTROL_AF_TRIGGER_IDLE)
            builder.set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_VIDEO)
            session.setRepeatingRequest(builder.build(), null, cameraHandler)
        } catch (e: Exception) {
            Log.w(TAG, "tapFocus failed: ${e.message}")
        }
    }

    fun requestKeyFrame() {
        val codec = mediaCodec ?: return
        try {
            val params = Bundle().apply {
                putInt(MediaCodec.PARAMETER_KEY_REQUEST_SYNC_FRAME, 0)
            }
            codec.setParameters(params)
        } catch (e: Exception) {
            Log.w(TAG, "requestKeyFrame failed: ${e.message}")
        }
    }

    fun currentUprightRotation(): Int {
        val cam = currentCamera ?: return 0
        return Protocol.computeUprightRotation(
            sensorOrientation = cam.sensorOrientation,
            deviceRotationDeg = deviceRotationDeg.get(),
            isFrontCamera = cam.facing == "front"
        )
    }

    private fun startThreadsLocked() {
        if (cameraThread == null) {
            cameraThread = HandlerThread("PhoneCam-Camera2").apply { start() }
            cameraHandler = Handler(cameraThread!!.looper)
        }
    }

    private fun stopThreadsLocked() {
        cameraThread?.quitSafely()
        cameraThread = null
        cameraHandler = null
    }

    private fun restartPipeline() {
        synchronized(lock) {
            closePipelineLocked()
            if (!running.get()) return
        }
        openSelectedCamera()
    }

    private fun restartSession() {
        val dev = synchronized(lock) { cameraDevice } ?: return
        createSession(dev)
    }

    @SuppressLint("MissingPermission")
    private fun openSelectedCamera() {
        val cam = currentCamera ?: run {
            listener.onEngineError("No camera available on device")
            return
        }
        try {
            currentCharacteristics = cameraManager.getCameraCharacteristics(cam.id)
            activeArrayRect = currentCharacteristics?.get(CameraCharacteristics.SENSOR_INFO_ACTIVE_ARRAY_SIZE)
            setupEncoder()
            cameraManager.openCamera(cam.id, object : CameraDevice.StateCallback() {
                override fun onOpened(device: CameraDevice) {
                    synchronized(lock) {
                        if (!running.get()) {
                            device.close()
                            return
                        }
                        cameraDevice = device
                    }
                    createSession(device)
                }

                override fun onDisconnected(device: CameraDevice) {
                    device.close()
                    synchronized(lock) {
                        if (cameraDevice === device) cameraDevice = null
                    }
                }

                override fun onError(device: CameraDevice, error: Int) {
                    device.close()
                    synchronized(lock) {
                        if (cameraDevice === device) cameraDevice = null
                    }
                    listener.onEngineError("Camera2 error code: $error")
                }
            }, cameraHandler)
        } catch (e: SecurityException) {
            listener.onEngineError("Missing CAMERA permission")
        } catch (e: Exception) {
            listener.onEngineError("Failed opening camera: ${e.message}")
        }
    }

    private fun setupEncoder() {
        cachedCodecConfig = null
        if (config.codec == VideoCodecMode.MJPEG) {
            val reader = ImageReader.newInstance(
                config.width,
                config.height,
                ImageFormat.YUV_420_888,
                3
            )
            reader.setOnImageAvailableListener({ r ->
                val img = r.acquireLatestImage() ?: return@setOnImageAvailableListener
                try {
                    encodeMjpegFrame(img)
                } finally {
                    img.close()
                }
            }, cameraHandler)
            imageReader = reader
            return
        }

        val codec = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC)
        val format = createH264Format(config, lowLatencyHints = true)
        try {
            codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
        } catch (_: Exception) {
            // Fallback without optional low-latency vendor keys on older SoCs
            codec.configure(createH264Format(config, lowLatencyHints = false), null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
        }
        codecInputSurface = codec.createInputSurface()
        codec.start()
        mediaCodec = codec

        encoderThread = Thread({ drainH264Encoder(codec) }, "PhoneCam-H264Drain").apply {
            isDaemon = true
            start()
        }
    }

    private fun createH264Format(cfg: StreamConfig, lowLatencyHints: Boolean): MediaFormat {
        return MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, cfg.width, cfg.height).apply {
            setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
            setInteger(MediaFormat.KEY_BIT_RATE, cfg.bitrateBps)
            setInteger(MediaFormat.KEY_FRAME_RATE, cfg.fps)
            setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1)
            if (lowLatencyHints) {
                setInteger(MediaFormat.KEY_BITRATE_MODE, MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_CBR)
                setInteger(MediaFormat.KEY_MAX_B_FRAMES, 0)
                setInteger("latency", 0)
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                    setInteger(MediaFormat.KEY_LOW_LATENCY, 1)
                }
            }
        }
    }

    private fun drainH264Encoder(codec: MediaCodec) {
        val info = MediaCodec.BufferInfo()
        while (running.get() && mediaCodec === codec) {
            val idx = try {
                codec.dequeueOutputBuffer(info, 15_000)
            } catch (_: Exception) {
                break
            }
            if (idx >= 0) {
                try {
                    val buf = codec.getOutputBuffer(idx)
                    if (buf != null && info.size > 0) {
                        buf.position(info.offset)
                        buf.limit(info.offset + info.size)
                        val bytes = ByteArray(info.size)
                        buf.get(bytes)

                        val isConfig = (info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0
                        val isKey = (info.flags and MediaCodec.BUFFER_FLAG_KEY_FRAME) != 0
                        if (isConfig) {
                            cachedCodecConfig = bytes
                        }

                        val flags = Protocol.buildVideoFlags(
                            isKeyFrame = isKey,
                            isCodecConfig = isConfig,
                            isFrontCamera = currentCamera?.facing == "front",
                            uprightRotationDeg = currentUprightRotation()
                        )
                        listener.onEncodedFrame(
                            Protocol.TYPE_VIDEO_H264,
                            flags,
                            bytes,
                            System.currentTimeMillis()
                        )
                    }
                } finally {
                    try {
                        codec.releaseOutputBuffer(idx, false)
                    } catch (_: Exception) {
                        // Codec stopped
                    }
                }
            }
        }
    }

    private fun encodeMjpegFrame(image: Image) {
        val nv21 = yuv420888ToNv21(image)
        val out = ByteArrayOutputStream(image.width * image.height / 4)
        val yuv = YuvImage(nv21, ImageFormat.NV21, image.width, image.height, null)
        yuv.compressToJpeg(Rect(0, 0, image.width, image.height), 82, out)
        val jpegBytes = out.toByteArray()
        val flags = Protocol.buildVideoFlags(
            isKeyFrame = true,
            isCodecConfig = false,
            isFrontCamera = currentCamera?.facing == "front",
            uprightRotationDeg = currentUprightRotation()
        )
        listener.onEncodedFrame(
            Protocol.TYPE_VIDEO_MJPEG,
            flags,
            jpegBytes,
            System.currentTimeMillis()
        )
    }

    private fun yuv420888ToNv21(image: Image): ByteArray {
        val w = image.width
        val h = image.height
        val ySize = w * h
        val uvSize = w * h / 2
        val nv21 = ByteArray(ySize + uvSize)

        val yPlane = image.planes[0]
        val uPlane = image.planes[1]
        val vPlane = image.planes[2]

        val yBuf = yPlane.buffer
        val yStride = yPlane.rowStride
        var dst = 0
        for (row in 0 until h) {
            yBuf.position(row * yStride)
            yBuf.get(nv21, dst, w)
            dst += w
        }

        val uBuf = uPlane.buffer
        val vBuf = vPlane.buffer
        val uvRowStride = uPlane.rowStride
        val uvPixelStride = uPlane.pixelStride
        val uvH = h / 2
        val uvW = w / 2
        for (row in 0 until uvH) {
            val rowOff = row * uvRowStride
            for (col in 0 until uvW) {
                val idx = rowOff + col * uvPixelStride
                nv21[dst++] = vBuf.get(idx)
                nv21[dst++] = uBuf.get(idx)
            }
        }
        return nv21
    }

    @Suppress("DEPRECATION")
    private fun createSession(device: CameraDevice) {
        val targets = mutableListOf<Surface>()
        codecInputSurface?.let { targets.add(it) }
        imageReader?.surface?.let { targets.add(it) }
        previewSurface?.takeIf { it.isValid }?.let { targets.add(it) }
        if (targets.isEmpty()) return

        val callback = object : CameraCaptureSession.StateCallback() {
            override fun onConfigured(session: CameraCaptureSession) {
                synchronized(lock) {
                    if (!running.get()) {
                        session.close()
                        return
                    }
                    captureSession = session
                }
                try {
                    val builder = device.createCaptureRequest(CameraDevice.TEMPLATE_RECORD)
                    targets.forEach { builder.addTarget(it) }
                    requestBuilder = builder
                    applyParamsToBuilder(builder)
                    session.setRepeatingRequest(builder.build(), null, cameraHandler)
                } catch (e: CameraAccessException) {
                    listener.onEngineError("Failed starting repeating capture: ${e.message}")
                }
            }

            override fun onConfigureFailed(session: CameraCaptureSession) {
                listener.onEngineError("Camera capture session configuration failed")
            }
        }

        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                val outputs = targets.map { OutputConfiguration(it) }
                val cfg = SessionConfiguration(
                    SessionConfiguration.SESSION_REGULAR,
                    outputs,
                    Executors.newSingleThreadExecutor(),
                    callback
                )
                device.createCaptureSession(cfg)
            } else {
                device.createCaptureSession(targets, callback, cameraHandler)
            }
        } catch (e: Exception) {
            listener.onEngineError("createCaptureSession error: ${e.message}")
        }
    }

    private fun applyRepeatingRequest() {
        val session = captureSession ?: return
        val builder = requestBuilder ?: return
        try {
            applyParamsToBuilder(builder)
            session.setRepeatingRequest(builder.build(), null, cameraHandler)
        } catch (e: Exception) {
            Log.w(TAG, "applyRepeatingRequest failed: ${e.message}")
        }
    }

    private fun applyParamsToBuilder(builder: CaptureRequest.Builder) {
        builder.set(CaptureRequest.CONTROL_MODE, CaptureRequest.CONTROL_MODE_AUTO)
        builder.set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_VIDEO)
        builder.set(CaptureRequest.CONTROL_AE_MODE, CaptureRequest.CONTROL_AE_MODE_ON)

        val fpsRange = selectFpsRange(config.fps)
        if (fpsRange != null) {
            builder.set(CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE, fpsRange)
        }

        val flashMode = if (torchEnabled && currentCamera?.hasFlash == true && currentCamera?.facing == "back") {
            CaptureRequest.FLASH_MODE_TORCH
        } else {
            CaptureRequest.FLASH_MODE_OFF
        }
        builder.set(CaptureRequest.FLASH_MODE, flashMode)

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            builder.set(CaptureRequest.CONTROL_ZOOM_RATIO, zoomRatio)
        } else {
            activeArrayRect?.let { active ->
                val cropW = (active.width() / zoomRatio).toInt().coerceAtLeast(2)
                val cropH = (active.height() / zoomRatio).toInt().coerceAtLeast(2)
                val left = (active.width() - cropW) / 2
                val top = (active.height() - cropH) / 2
                builder.set(CaptureRequest.SCALER_CROP_REGION, Rect(left, top, left + cropW, top + cropH))
            }
        }
    }

    private fun selectFpsRange(targetFps: Int): Range<Int>? {
        val ranges = currentCharacteristics?.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES)
            ?: return Range(targetFps, targetFps)
        return ranges.firstOrNull { it.lower == targetFps && it.upper == targetFps }
            ?: ranges.filter { it.upper == targetFps }.maxByOrNull { it.lower }
            ?: ranges.minByOrNull { kotlin.math.abs(it.upper - targetFps) }
    }

    private fun closePipelineLocked() {
        try {
            captureSession?.close()
        } catch (_: Exception) {}
        captureSession = null
        requestBuilder = null

        try {
            cameraDevice?.close()
        } catch (_: Exception) {}
        cameraDevice = null

        try {
            mediaCodec?.stop()
        } catch (_: Exception) {}
        try {
            mediaCodec?.release()
        } catch (_: Exception) {}
        mediaCodec = null
        codecInputSurface?.release()
        codecInputSurface = null
        encoderThread = null

        try {
            imageReader?.close()
        } catch (_: Exception) {}
        imageReader = null
    }
}

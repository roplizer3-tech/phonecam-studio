package com.phonecam.studio.camera

import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraManager
import android.os.Build
import com.phonecam.studio.protocol.Protocol.CameraDescriptor

object CameraCatalog {
    fun enumerate(manager: CameraManager): List<CameraDescriptor> {
        val list = mutableListOf<CameraDescriptor>()
        val ids = try {
            manager.cameraIdList
        } catch (_: Exception) {
            return emptyList()
        }

        var backIndex = 0
        var frontIndex = 0
        for (id in ids) {
            try {
                val chars = manager.getCameraCharacteristics(id)
                val facingInt = chars.get(CameraCharacteristics.LENS_FACING)
                    ?: CameraCharacteristics.LENS_FACING_BACK
                val facing = when (facingInt) {
                    CameraCharacteristics.LENS_FACING_FRONT -> "front"
                    else -> "back"
                }
                val sensorOrientation = chars.get(CameraCharacteristics.SENSOR_ORIENTATION) ?: 90
                val hasFlash = chars.get(CameraCharacteristics.FLASH_INFO_AVAILABLE) ?: false
                val maxZoom = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                    chars.get(CameraCharacteristics.CONTROL_ZOOM_RATIO_RANGE)?.upper ?: 4.0f
                } else {
                    chars.get(CameraCharacteristics.SCALER_AVAILABLE_MAX_DIGITAL_ZOOM) ?: 4.0f
                }
                val focal = chars.get(CameraCharacteristics.LENS_INFO_AVAILABLE_FOCAL_LENGTHS)
                    ?.firstOrNull()
                val label = if (facing == "front") {
                    frontIndex++
                    if (frontIndex == 1) "Front Camera" else "Front #$frontIndex"
                } else {
                    backIndex++
                    when {
                        backIndex == 1 -> "Back Main"
                        focal != null && focal < 2.5f -> "Back Ultra-Wide"
                        focal != null && focal > 6.5f -> "Back Telephoto"
                        else -> "Back #$backIndex"
                    }
                }
                list.add(
                    CameraDescriptor(
                        id = id,
                        facing = facing,
                        sensorOrientation = sensorOrientation,
                        label = label,
                        hasFlash = hasFlash,
                        maxZoom = maxZoom.coerceAtLeast(1.0f)
                    )
                )
            } catch (_: Exception) {
                // Skip inaccessible logical/physical camera
            }
        }
        return list
    }
}

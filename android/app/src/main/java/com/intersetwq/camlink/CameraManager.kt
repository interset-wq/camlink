package com.intersetwq.camlink

import android.Manifest
import android.content.pm.PackageManager
import android.graphics.ImageFormat
import android.hardware.camera2.*
import android.media.ImageReader
import android.os.Handler
import android.os.HandlerThread
import android.util.Size
import android.view.Surface
import androidx.core.app.ActivityCompat

class CameraManager(private val activity: MainActivity) {

    interface FrameCallback {
        fun onFrameAvailable(jpegData: ByteArray)
        fun onCameraError(error: String)
    }

    private var cameraDevice: CameraDevice? = null
    private var captureSession: CameraCaptureSession? = null
    private var imageReader: ImageReader? = null
    private var backgroundThread: HandlerThread? = null
    private var backgroundHandler: Handler? = null
    private var callback: FrameCallback? = null

    private val cameraManager = activity.getSystemService(android.content.Context.CAMERA_SERVICE) as android.hardware.camera2.CameraManager

    fun start(callback: FrameCallback) {
        this.callback = callback
        startBackgroundThread()
        openCamera()
    }

    fun stop() {
        captureSession?.close()
        captureSession = null
        cameraDevice?.close()
        cameraDevice = null
        imageReader?.close()
        imageReader = null
        stopBackgroundThread()
    }

    fun getPreviewSurface(): Surface? {
        return imageReader?.surface
    }

    private fun startBackgroundThread() {
        backgroundThread = HandlerThread("CameraBackground").also { it.start() }
        backgroundHandler = Handler(backgroundThread!!.looper)
    }

    private fun stopBackgroundThread() {
        backgroundThread?.quitSafely()
        try {
            backgroundThread?.join()
        } catch (e: InterruptedException) {
            e.printStackTrace()
        }
        backgroundThread = null
        backgroundHandler = null
    }

    private fun openCamera() {
        if (ActivityCompat.checkSelfPermission(activity, Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            callback?.onCameraError("Camera permission not granted")
            return
        }

        val cameraId = findBackCamera() ?: run {
            callback?.onCameraError("No back camera found")
            return
        }

        imageReader = ImageReader.newInstance(
            1920, 1080,
            ImageFormat.JPEG,
            2
        ).apply {
            setOnImageAvailableListener({ reader ->
                val image = reader.acquireLatestImage() ?: return@setOnImageAvailableListener
                val buffer = image.planes[0].buffer
                val bytes = ByteArray(buffer.remaining())
                buffer.get(bytes)
                image.close()
                callback?.onFrameAvailable(bytes)
            }, backgroundHandler)
        }

        cameraManager.openCamera(cameraId, object : CameraDevice.StateCallback() {
            override fun onOpened(camera: CameraDevice) {
                cameraDevice = camera
                createCaptureSession()
            }

            override fun onDisconnected(camera: CameraDevice) {
                camera.close()
                cameraDevice = null
                callback?.onCameraError("Camera disconnected")
            }

            override fun onError(camera: CameraDevice, error: Int) {
                camera.close()
                cameraDevice = null
                callback?.onCameraError("Camera error: $error")
            }
        }, backgroundHandler)
    }

    private fun findBackCamera(): String? {
        for (id in cameraManager.cameraIdList) {
            val characteristics = cameraManager.getCameraCharacteristics(id)
            val facing = characteristics.get(CameraCharacteristics.LENS_FACING)
            if (facing == CameraCharacteristics.LENS_FACING_BACK) {
                return id
            }
        }
        return null
    }

    private fun createCaptureSession() {
        val camera = cameraDevice ?: return
        val surface = imageReader?.surface ?: return

        val request = camera.createCaptureRequest(CameraDevice.TEMPLATE_RECORD).apply {
            addTarget(surface)
            set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_VIDEO)
            set(CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE, android.util.Range(30, 30))
        }

        camera.createCaptureSession(
            listOf(surface),
            object : CameraCaptureSession.StateCallback() {
                override fun onConfigured(session: CameraCaptureSession) {
                    captureSession = session
                    session.setRepeatingRequest(request.build(), null, backgroundHandler)
                }

                override fun onConfigureFailed(session: CameraCaptureSession) {
                    callback?.onCameraError("Failed to configure camera session")
                }
            },
            backgroundHandler
        )
    }
}

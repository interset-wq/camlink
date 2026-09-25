package com.intersetwq.camlink

import android.Manifest
import android.content.ComponentName
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.os.Bundle
import android.os.IBinder
import android.widget.Button
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.net.Socket

class MainActivity : AppCompatActivity(), CameraManager.FrameCallback {

    companion object {
        private const val REQUEST_PERMISSIONS = 100
        private val REQUIRED_PERMISSIONS = arrayOf(
            Manifest.permission.CAMERA,
            Manifest.permission.POST_NOTIFICATIONS
        )
    }

    private lateinit var cameraManager: CameraManager
    private lateinit var statusText: TextView
    private lateinit var fpsText: TextView
    private lateinit var toggleButton: Button

    private var streamService: StreamService? = null
    private var isBound = false
    private var isStreaming = false
    private var frameCount = 0
    private var lastFpsTime = System.currentTimeMillis()

    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
            val binder = service as StreamService.LocalBinder
            streamService = binder.getService()
            isBound = true
            streamService?.onStatusChanged = { status ->
                runOnUiThread { statusText.text = status }
            }
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            streamService = null
            isBound = false
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        statusText = findViewById(R.id.statusText)
        fpsText = findViewById(R.id.fpsText)
        toggleButton = findViewById(R.id.toggleButton)

        cameraManager = CameraManager(this)

        toggleButton.setOnClickListener {
            if (isStreaming) {
                stopStreaming()
            } else {
                startStreaming()
            }
        }

        if (allPermissionsGranted()) {
            initCamera()
        } else {
            ActivityCompat.requestPermissions(this, REQUIRED_PERMISSIONS, REQUEST_PERMISSIONS)
        }

        // Start and bind to stream service
        val serviceIntent = Intent(this, StreamService::class.java)
        startForegroundService(serviceIntent)
        bindService(serviceIntent, connection, BIND_AUTO_CREATE)
    }

    override fun onDestroy() {
        super.onDestroy()
        cameraManager.stop()
        if (isBound) {
            unbindService(connection)
            isBound = false
        }
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == REQUEST_PERMISSIONS) {
            if (allPermissionsGranted()) {
                initCamera()
            } else {
                Toast.makeText(this, "Permissions required", Toast.LENGTH_LONG).show()
                finish()
            }
        }
    }

    private fun allPermissionsGranted() = REQUIRED_PERMISSIONS.all {
        ContextCompat.checkSelfPermission(this, it) == PackageManager.PERMISSION_GRANTED
    }

    private fun initCamera() {
        cameraManager.start(this)
        statusText.text = "Camera ready. Tap Start to stream."
    }

    private fun startStreaming() {
        isStreaming = true
        toggleButton.text = "Stop"
        statusText.text = "Starting stream..."

        lifecycleScope.launch {
            val connected = withContext(Dispatchers.IO) {
                checkServerConnection()
            }
            if (connected) {
                streamService?.startStreaming()
                cameraManager.start(this@MainActivity)
            } else {
                statusText.text = "Cannot reach PC server. Start PC client first."
                isStreaming = false
                toggleButton.text = "Start"
            }
        }
    }

    private fun stopStreaming() {
        isStreaming = false
        toggleButton.text = "Start"
        streamService?.stopStreaming()
        cameraManager.stop()
        statusText.text = "Stopped."
    }

    private fun checkServerConnection(): Boolean {
        return try {
            val socket = Socket("127.0.0.1", Protocol.DEFAULT_PORT)
            socket.close()
            true
        } catch (e: Exception) {
            false
        }
    }

    override fun onFrameAvailable(jpegData: ByteArray) {
        streamService?.sendFrame(jpegData)

        frameCount++
        val now = System.currentTimeMillis()
        if (now - lastFpsTime >= 1000) {
            val fps = frameCount * 1000 / (now - lastFpsTime)
            runOnUiThread { fpsText.text = "${fps} FPS" }
            frameCount = 0
            lastFpsTime = now
        }
    }

    override fun onCameraError(error: String) {
        runOnUiThread {
            statusText.text = error
            Toast.makeText(this, error, Toast.LENGTH_LONG).show()
        }
    }
}

package com.intersetwq.camlink

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.os.Binder
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import kotlinx.coroutines.*
import java.io.OutputStream
import java.net.Socket
import java.net.InetSocketAddress

class StreamService : Service() {

    companion object {
        private const val TAG = "StreamService"
        private const val CHANNEL_ID = "camlink_stream"
        private const val NOTIFICATION_ID = 1
        private const val RECONNECT_DELAY_MS = 3000L
    }

    inner class LocalBinder : Binder() {
        fun getService(): StreamService = this@StreamService
    }

    private val binder = LocalBinder()
    private var socket: Socket? = null
    private var outputStream: OutputStream? = null
    private val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())
    private var isStreaming = false

    var onStatusChanged: ((String) -> Unit)? = null

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        startForeground(NOTIFICATION_ID, createNotification("Waiting for connection..."))
        return START_STICKY
    }

    override fun onDestroy() {
        super.onDestroy()
        stopStreaming()
        scope.cancel()
    }

    fun startStreaming(host: String = "127.0.0.1", port: Int = Protocol.DEFAULT_PORT) {
        if (isStreaming) return
        isStreaming = true

        scope.launch {
            connectWithRetry(host, port)
        }
    }

    fun stopStreaming() {
        isStreaming = false
        try {
            outputStream?.close()
            socket?.close()
        } catch (e: Exception) {
            Log.e(TAG, "Error closing socket", e)
        }
        outputStream = null
        socket = null
        updateNotification("Disconnected")
        onStatusChanged?.invoke("Disconnected")
    }

    fun sendFrame(jpegData: ByteArray) {
        val os = outputStream ?: return
        try {
            val frame = Protocol.buildFrame(jpegData)
            os.write(frame)
            os.flush()
        } catch (e: Exception) {
            Log.e(TAG, "Error sending frame", e)
            reconnect()
        }
    }

    private suspend fun connectWithRetry(host: String, port: Int) {
        while (isStreaming) {
            try {
                updateNotification("Connecting to $host:$port...")
                onStatusChanged?.invoke("Connecting...")

                val sock = Socket()
                sock.connect(InetSocketAddress(host, port), 5000)
                sock.soTimeout = 0
                sock.tcpNoDelay = true

                socket = sock
                outputStream = sock.getOutputStream()

                updateNotification("Streaming to $host:$port")
                onStatusChanged?.invoke("Connected")

                // Keep connection alive
                while (isStreaming && socket?.isConnected == true) {
                    delay(1000)
                }
            } catch (e: Exception) {
                Log.e(TAG, "Connection failed: ${e.message}")
                updateNotification("Connection failed, retrying...")
                onStatusChanged?.invoke("Reconnecting...")
                delay(RECONNECT_DELAY_MS)
            }
        }
    }

    private fun reconnect() {
        try {
            outputStream?.close()
            socket?.close()
        } catch (e: Exception) {
            // ignore
        }
        outputStream = null
        socket = null
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "Camera Stream",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "CamLink streaming notification"
            }
            val manager = getSystemService(NotificationManager::class.java)
            manager.createNotificationChannel(channel)
        }
    }

    private fun createNotification(text: String): Notification {
        val intent = Intent(this, MainActivity::class.java)
        val pendingIntent = PendingIntent.getActivity(
            this, 0, intent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("CamLink")
            .setContentText(text)
            .setSmallIcon(android.R.drawable.ic_menu_camera)
            .setContentIntent(pendingIntent)
            .setOngoing(true)
            .build()
    }

    private fun updateNotification(text: String) {
        val manager = getSystemService(NotificationManager::class.java)
        manager.notify(NOTIFICATION_ID, createNotification(text))
    }
}

package com.intersetwq.camlink

import java.nio.ByteBuffer
import java.nio.ByteOrder

object Protocol {
    const val DEFAULT_PORT = 5555
    const val HEADER_SIZE = 4

    fun encodeLength(length: Int): ByteArray {
        return ByteBuffer.allocate(HEADER_SIZE).apply {
            order(ByteOrder.BIG_ENDIAN)
            putInt(length)
        }.array()
    }

    fun decodeLength(header: ByteArray): Int {
        return ByteBuffer.wrap(header).apply {
            order(ByteOrder.BIG_ENDIAN)
        }.int
    }

    fun buildFrame(jpegData: ByteArray): ByteArray {
        val header = encodeLength(jpegData.size)
        return header + jpegData
    }
}

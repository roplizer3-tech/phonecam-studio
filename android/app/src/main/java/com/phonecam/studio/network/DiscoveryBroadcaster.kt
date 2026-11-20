package com.phonecam.studio.network

import android.os.Build
import com.phonecam.studio.protocol.Protocol
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.Inet4Address
import java.net.InetAddress
import java.net.NetworkInterface
import java.util.concurrent.atomic.AtomicBoolean

class DiscoveryBroadcaster(
    private val getPinRequired: () -> Boolean,
    private val getFacing: () -> String,
    private val isStreaming: () -> Boolean
) {
    private val running = AtomicBoolean(false)
    private var thread: Thread? = null

    fun start() {
        if (running.getAndSet(true)) return
        thread = Thread({ loop() }, "PhoneCam-Discovery").apply {
            isDaemon = true
            start()
        }
    }

    fun stop() {
        running.set(false)
        thread?.interrupt()
        thread = null
    }

    private fun loop() {
        var socket: DatagramSocket? = null
        try {
            socket = DatagramSocket().apply { broadcast = true }
            while (running.get()) {
                try {
                    val payload = Protocol.buildDiscoveryJson(
                        deviceName = "${Build.MANUFACTURER} ${Build.MODEL}".trim(),
                        pinRequired = getPinRequired(),
                        facing = getFacing(),
                        streaming = isStreaming()
                    ).toByteArray(Charsets.UTF_8)

                    val targets = collectBroadcastAddresses()
                    for (addr in targets) {
                        val pkt = DatagramPacket(payload, payload.size, addr, Protocol.UDP_DISCOVERY_PORT)
                        try {
                            socket.send(pkt)
                        } catch (_: Exception) {}
                    }
                } catch (_: Exception) {}

                try {
                    Thread.sleep(1500)
                } catch (_: InterruptedException) {
                    break
                }
            }
        } catch (_: Exception) {
            // Socket setup failure
        } finally {
            socket?.close()
        }
    }

    companion object {
        fun getLocalIpv4Addresses(): List<String> {
            val result = mutableListOf<String>()
            try {
                val interfaces = NetworkInterface.getNetworkInterfaces() ?: return result
                while (interfaces.hasMoreElements()) {
                    val ni = interfaces.nextElement()
                    if (!ni.isUp || ni.isLoopback) continue
                    val addrs = ni.inetAddresses
                    while (addrs.hasMoreElements()) {
                        val addr = addrs.nextElement()
                        if (addr is Inet4Address && !addr.isLoopbackAddress) {
                            addr.hostAddress?.let { result.add(it) }
                        }
                    }
                }
            } catch (_: Exception) {}
            return result.distinct()
        }

        private fun collectBroadcastAddresses(): List<InetAddress> {
            val targets = mutableSetOf<InetAddress>()
            try {
                targets.add(InetAddress.getByName("255.255.255.255"))
                val interfaces = NetworkInterface.getNetworkInterfaces() ?: return targets.toList()
                while (interfaces.hasMoreElements()) {
                    val ni = interfaces.nextElement()
                    if (!ni.isUp || ni.isLoopback) continue
                    for (ia in ni.interfaceAddresses) {
                        val bcast = ia.broadcast
                        if (bcast is Inet4Address) {
                            targets.add(bcast)
                        }
                    }
                }
            } catch (_: Exception) {}
            return targets.toList()
        }
    }
}

package com.magistralacan4.exp31

import android.location.Location
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.SocketTimeoutException

/**
 * Test WiFi dla Eksperymentu 3.1: protokol ping-pong przez UDP z firmware
 * esp_experiment_3_1_wifi.ino (ESP32 jako SoftAP). Telefon wysyla "PING:<seq>"
 * i mierzy RTT na WLASNYM zegarze (czas wyslania -> czas odebrania echa
 * "PONG:<seq>") - bez potrzeby synchronizacji zegarow z ESP32. Brak
 * odpowiedzi w oknie timeoutu = pakiet utracony.
 */
class WifiPingPongTester(
    private val espIp: String,
    private val espPort: Int,
    private val distanceM: Double,
    private val scenario: String,
    private val packetCount: Int,
    private val resultLogger: ResultLogger,
    private val locationProvider: () -> Location?,
    private val referenceLocationProvider: () -> Location?,
    private val rssiProvider: () -> Int?,
    private val onProgress: (
        sent: Int,
        received: Int,
        lost: Int,
        avgRttMs: Double?,
        lastRssi: Int?,
        distFromRefM: Double?,
    ) -> Unit,
) {
    companion object {
        private const val RECEIVE_TIMEOUT_MS = 500
        private const val PROGRESS_EVERY_N = 20
    }

    suspend fun run() = withContext(Dispatchers.IO) {
        val socket = DatagramSocket()
        socket.soTimeout = RECEIVE_TIMEOUT_MS
        val address = InetAddress.getByName(espIp)

        var sent = 0
        var received = 0
        var lost = 0
        val rttSamples = mutableListOf<Long>()
        var lastRssi: Int? = null
        var lastDistFromRef: Double? = null

        for (seq in 0 until packetCount) {
            val sendTimeNs = System.nanoTime()
            val payload = "PING:$seq".toByteArray()
            socket.send(DatagramPacket(payload, payload.size, address, espPort))
            sent++

            var rttMs: Long? = null
            try {
                val buf = ByteArray(64)
                val respPacket = DatagramPacket(buf, buf.size)
                socket.receive(respPacket)
                val recvTimeNs = System.nanoTime()
                val respStr = String(respPacket.data, 0, respPacket.length)
                if (respStr == "PONG:$seq") {
                    rttMs = (recvTimeNs - sendTimeNs) / 1_000_000
                    rttSamples.add(rttMs)
                    received++
                } else {
                    lost++
                }
            } catch (e: SocketTimeoutException) {
                lost++
            }

            lastRssi = rssiProvider()

            val refLoc = referenceLocationProvider()
            val curLoc = locationProvider()
            lastDistFromRef = if (refLoc != null && curLoc != null) {
                refLoc.distanceTo(curLoc).toDouble()
            } else {
                null
            }

            resultLogger.appendSample(
                PacketSample(
                    timestampMs = System.currentTimeMillis(),
                    distanceM = distanceM,
                    scenario = scenario,
                    technology = "WIFI",
                    seq = seq,
                    rttMs = rttMs,
                    rssiDbm = lastRssi,
                    lat = curLoc?.latitude,
                    lon = curLoc?.longitude,
                    gpsAccuracyM = curLoc?.accuracy,
                    distFromReferenceM = lastDistFromRef,
                ),
            )

            if (seq % PROGRESS_EVERY_N == 0) {
                val avgRtt = if (rttSamples.isNotEmpty()) rttSamples.average() else null
                onProgress(sent, received, lost, avgRtt, lastRssi, lastDistFromRef)
            }
        }

        socket.close()
        val avgRtt = if (rttSamples.isNotEmpty()) rttSamples.average() else null
        onProgress(sent, received, lost, avgRtt, lastRssi, lastDistFromRef)
    }
}

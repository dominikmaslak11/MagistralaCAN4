package com.magistralacan4.exp31

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothStatusCodes
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.location.Location
import android.os.Build
import android.os.ParcelUuid
import kotlin.coroutines.coroutineContext
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import java.util.UUID

/**
 * Test BLE dla Eksperymentu 3.1: analogiczny protokol ping-pong do
 * WifiPingPongTester, ale przez GATT (write charakterystyki -> notify
 * odpowiedzi) z firmware esp_experiment_3_1_ble.ino. RTT liczone na WLASNYM
 * zegarze telefonu (moment write() -> moment onCharacteristicChanged), bez
 * potrzeby synchronizacji zegarow z ESP32 - ta sama zasada co w WiFi.
 *
 * Operacje GATT w Androidzie sa kolejkowane (jedna naraz, sterowana
 * callbackami) - stad most na coroutines przez suspendCancellableCoroutine +
 * Channel dla powtarzajacych sie notyfikacji.
 */
@SuppressLint("MissingPermission") // uprawnienia BLUETOOTH_SCAN/CONNECT sprawdzane w MainActivity przed startem
class BlePingPongTester(
    private val context: Context,
    private val distanceM: Double,
    private val scenario: String,
    private val packetCount: Int,
    private val resultLogger: ResultLogger,
    private val locationProvider: () -> Location?,
    private val referenceLocationProvider: () -> Location?,
    private val onProgress: (
        sent: Int,
        received: Int,
        lost: Int,
        avgRttMs: Double?,
        lastRssi: Int?,
        distFromRefM: Double?,
    ) -> Unit,
    private val onStatus: (String) -> Unit,
) {
    companion object {
        private const val DEVICE_NAME = "MagistralaCAN4_Exp31_BLE"
        private val SERVICE_UUID: UUID = UUID.fromString("6e400001-b5a3-f393-e0a9-e50e24dcca9e")
        private val CHAR_UUID: UUID = UUID.fromString("6e400002-b5a3-f393-e0a9-e50e24dcca9e")
        private val CCCD_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
        private const val SCAN_TIMEOUT_MS = 15_000L
        private const val CONNECT_TIMEOUT_MS = 10_000L
        private const val RESPONSE_TIMEOUT_MS = 1_000L
        private const val RSSI_TIMEOUT_MS = 300L
        private const val PROGRESS_EVERY_N = 20
    }

    private class GattSession(
        val gatt: BluetoothGatt,
        val characteristic: BluetoothGattCharacteristic,
        val notifications: Channel<String>,
        val rssiUpdates: Channel<Int>,
    )

    suspend fun run() {
        val adapter = (context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
        if (adapter == null || !adapter.isEnabled) {
            onStatus("Bluetooth wylaczony na telefonie.")
            onProgress(0, 0, 0, null, null, null)
            return
        }

        onStatus("Skanowanie BLE (szukam $DEVICE_NAME)...")
        val device = scanForDevice(adapter)
        if (device == null) {
            onStatus("Nie znaleziono ESP32 BLE w zasiegu (timeout skanowania).")
            onProgress(0, 0, 0, null, null, null)
            return
        }
        if (!coroutineContext.isActive) return

        onStatus("Laczenie GATT...")
        val session = connectAndPrepare(device)
        if (session == null) {
            onStatus("Nie udalo sie polaczyc/przygotowac GATT.")
            onProgress(0, 0, 0, null, null, null)
            return
        }

        onStatus("Test BLE w toku...")
        try {
            runPingPongLoop(session)
        } finally {
            try {
                session.gatt.disconnect()
                session.gatt.close()
            } catch (_: Exception) {
                // urzadzenie moglo juz wyjsc z zasiegu - nic wiecej do zrobienia
            }
        }
    }

    private suspend fun scanForDevice(adapter: BluetoothAdapter): BluetoothDevice? {
        val scanner = adapter.bluetoothLeScanner ?: return null
        return withTimeoutOrNull(SCAN_TIMEOUT_MS) {
            suspendCancellableCoroutine { cont ->
                val callback = object : ScanCallback() {
                    override fun onScanResult(callbackType: Int, result: ScanResult) {
                        if (cont.isActive) {
                            cont.resume(result.device) {}
                            try {
                                scanner.stopScan(this)
                            } catch (_: Exception) {
                            }
                        }
                    }

                    override fun onScanFailed(errorCode: Int) {
                        if (cont.isActive) cont.resume(null) {}
                    }
                }
                val filters = listOf(ScanFilter.Builder().setServiceUuid(ParcelUuid(SERVICE_UUID)).build())
                val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
                scanner.startScan(filters, settings, callback)
                cont.invokeOnCancellation {
                    try {
                        scanner.stopScan(callback)
                    } catch (_: Exception) {
                    }
                }
            }
        }
    }

    private suspend fun connectAndPrepare(device: BluetoothDevice): GattSession? {
        val notifications = Channel<String>(capacity = Channel.UNLIMITED)
        val rssiUpdates = Channel<Int>(capacity = Channel.CONFLATED)

        val gatt = withTimeoutOrNull(CONNECT_TIMEOUT_MS) {
            suspendCancellableCoroutine<BluetoothGatt?> { cont ->
                val callback = object : BluetoothGattCallback() {
                    override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
                        when (newState) {
                            BluetoothGatt.STATE_CONNECTED -> g.discoverServices()
                            BluetoothGatt.STATE_DISCONNECTED -> if (cont.isActive) cont.resume(null) {}
                        }
                    }

                    override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
                        if (cont.isActive) {
                            cont.resume(if (status == BluetoothGatt.GATT_SUCCESS) g else null) {}
                        }
                    }

                    override fun onCharacteristicChanged(
                        g: BluetoothGatt,
                        characteristic: BluetoothGattCharacteristic,
                        value: ByteArray,
                    ) {
                        notifications.trySend(String(value))
                    }

                    @Deprecated("Deprecated in Java")
                    override fun onCharacteristicChanged(g: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
                        @Suppress("DEPRECATION")
                        notifications.trySend(String(characteristic.value ?: ByteArray(0)))
                    }

                    override fun onReadRemoteRssi(g: BluetoothGatt, rssi: Int, status: Int) {
                        rssiUpdates.trySend(rssi)
                    }
                }
                val g = device.connectGatt(context, false, callback, BluetoothDevice.TRANSPORT_LE)
                cont.invokeOnCancellation {
                    g.disconnect()
                    g.close()
                }
            }
        }
        if (gatt == null) return null

        val service = gatt.getService(SERVICE_UUID) ?: return null.also { gatt.close() }
        val characteristic = service.getCharacteristic(CHAR_UUID) ?: return null.also { gatt.close() }

        gatt.setCharacteristicNotification(characteristic, true)
        val cccd = characteristic.getDescriptor(CCCD_UUID)
        if (cccd != null) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                gatt.writeDescriptor(cccd, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
            } else {
                @Suppress("DEPRECATION")
                cccd.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                @Suppress("DEPRECATION")
                gatt.writeDescriptor(cccd)
            }
            // writeDescriptor jest asynchroniczny; krotka pauza wystarcza w
            // praktyce zeby ESP32 zdazyl wlaczyc notyfikacje przed pierwszym PING.
            delay(200)
        }

        return GattSession(gatt, characteristic, notifications, rssiUpdates)
    }

    private suspend fun runPingPongLoop(session: GattSession) {
        var sent = 0
        var received = 0
        var lost = 0
        val rttSamples = mutableListOf<Long>()
        var lastRssi: Int? = null
        var lastDistFromRef: Double? = null

        for (seq in 0 until packetCount) {
            if (!coroutineContext.isActive) break

            while (session.notifications.tryReceive().isSuccess) {
                // odsiej ewentualne spoznione/przeterminowane notyfikacje
            }

            val sendTimeNs = System.nanoTime()
            val writeOk = writeCharacteristic(session, "PING:$seq".toByteArray())

            var rttMs: Long? = null
            if (writeOk) {
                val response = withTimeoutOrNull(RESPONSE_TIMEOUT_MS) { session.notifications.receive() }
                if (response == "PONG:$seq") {
                    rttMs = (System.nanoTime() - sendTimeNs) / 1_000_000
                    rttSamples.add(rttMs)
                    received++
                } else {
                    lost++
                }
            } else {
                lost++
            }
            sent++

            session.gatt.readRemoteRssi()
            lastRssi = withTimeoutOrNull(RSSI_TIMEOUT_MS) { session.rssiUpdates.receive() } ?: lastRssi

            val refLoc = referenceLocationProvider()
            val curLoc = locationProvider()
            lastDistFromRef = if (refLoc != null && curLoc != null) refLoc.distanceTo(curLoc).toDouble() else null

            // GATT callbacks (i wiec caly ten coroutine) dzialaja na Main
            // looperze - appendSample robi synchroniczny zapis na dysk, wiec
            // zrzucamy go na Dispatchers.IO zeby nie blokowac UI w petli.
            withContext(Dispatchers.IO) {
                resultLogger.appendSample(
                    PacketSample(
                        timestampMs = System.currentTimeMillis(),
                        distanceM = distanceM,
                        scenario = scenario,
                        technology = "BLE",
                        seq = seq,
                        rttMs = rttMs,
                        rssiDbm = lastRssi,
                        lat = curLoc?.latitude,
                        lon = curLoc?.longitude,
                        gpsAccuracyM = curLoc?.accuracy,
                        distFromReferenceM = lastDistFromRef,
                    ),
                )
            }

            if (seq % PROGRESS_EVERY_N == 0) {
                val avgRtt = if (rttSamples.isNotEmpty()) rttSamples.average() else null
                onProgress(sent, received, lost, avgRtt, lastRssi, lastDistFromRef)
            }
        }

        val avgRtt = if (rttSamples.isNotEmpty()) rttSamples.average() else null
        onProgress(sent, received, lost, avgRtt, lastRssi, lastDistFromRef)
    }

    private fun writeCharacteristic(session: GattSession, payload: ByteArray): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            session.gatt.writeCharacteristic(
                session.characteristic,
                payload,
                BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT,
            ) == BluetoothStatusCodes.SUCCESS
        } else {
            @Suppress("DEPRECATION")
            session.characteristic.value = payload
            @Suppress("DEPRECATION")
            session.characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            @Suppress("DEPRECATION")
            session.gatt.writeCharacteristic(session.characteristic)
        }
    }
}

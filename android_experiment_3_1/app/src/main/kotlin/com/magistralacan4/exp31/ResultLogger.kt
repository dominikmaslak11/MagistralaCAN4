package com.magistralacan4.exp31

import android.content.Context
import android.net.Uri
import androidx.core.content.FileProvider
import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.TimeZone
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

data class PacketSample(
    val timestampMs: Long,
    val distanceM: Double,
    val scenario: String,
    val technology: String,
    val seq: Int,
    val rttMs: Long?,
    val rssiDbm: Int?,
    val lat: Double?,
    val lon: Double?,
    val gpsAccuracyM: Float?,
    val distFromReferenceM: Double?,
)

/**
 * Zapisuje probki pomiarowe lokalnie do CSV (zrodlo prawdy, odporne na
 * przerwanie lacza pod testem - patrz
 * Pytania_Do_Wykladowcy_Eksperyment_3.1_20260729.md pkt 9 dla uzasadnienia,
 * dlaczego NIE wysylamy wynikow "na zywo" w trakcie samego testu).
 */
class ResultLogger(context: Context) {

    private val outputDir: File = File(context.getExternalFilesDir(null), "wyniki").apply { mkdirs() }
    private val csvFile: File = File(outputDir, "eksperyment_3_1_wyniki.csv")

    private val isoFormat = SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss.SSS'Z'", Locale.US).apply {
        timeZone = TimeZone.getTimeZone("UTC")
    }

    init {
        if (!csvFile.exists()) {
            csvFile.writeText(
                "timestamp_iso,distance_m,scenario,technology,seq,rtt_ms,rssi_dbm,lat,lon,gps_accuracy_m,dist_from_reference_m\n"
            )
        }
    }

    @Synchronized
    fun appendSample(sample: PacketSample) {
        val line = buildString {
            append(isoFormat.format(Date(sample.timestampMs))).append(',')
            append(sample.distanceM).append(',')
            append(sample.scenario).append(',')
            append(sample.technology).append(',')
            append(sample.seq).append(',')
            append(sample.rttMs?.toString() ?: "").append(',')
            append(sample.rssiDbm?.toString() ?: "").append(',')
            append(sample.lat?.toString() ?: "").append(',')
            append(sample.lon?.toString() ?: "").append(',')
            append(sample.gpsAccuracyM?.toString() ?: "").append(',')
            append(sample.distFromReferenceM?.toString() ?: "")
        }
        csvFile.appendText(line + "\n")
    }

    fun getShareableUri(context: Context): Uri? {
        if (!csvFile.exists() || csvFile.length() == 0L) return null
        return FileProvider.getUriForFile(context, "com.magistralacan4.exp31.fileprovider", csvFile)
    }

    /** Wysyla caly plik CSV jako cialo POST do prostego serwera domowego (patrz server_receiver.py). */
    suspend fun uploadToServer(serverAddress: String): Boolean = withContext(Dispatchers.IO) {
        if (!csvFile.exists()) return@withContext false
        try {
            val url = URL("http://$serverAddress/upload")
            val connection = url.openConnection() as HttpURLConnection
            connection.requestMethod = "POST"
            connection.doOutput = true
            connection.setRequestProperty("Content-Type", "text/csv")
            connection.connectTimeout = 5000
            connection.readTimeout = 5000
            connection.outputStream.use { it.write(csvFile.readBytes()) }
            val code = connection.responseCode
            connection.disconnect()
            code in 200..299
        } catch (e: IOException) {
            false
        }
    }
}

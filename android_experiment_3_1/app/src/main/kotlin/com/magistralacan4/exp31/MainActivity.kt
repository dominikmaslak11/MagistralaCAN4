package com.magistralacan4.exp31

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.location.Location
import android.net.wifi.WifiManager
import android.os.Bundle
import android.os.Looper
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import com.google.android.gms.location.FusedLocationProviderClient
import com.google.android.gms.location.LocationCallback
import com.google.android.gms.location.LocationRequest
import com.google.android.gms.location.LocationResult
import com.google.android.gms.location.LocationServices
import com.google.android.gms.location.Priority
import com.google.android.gms.tasks.CancellationTokenSource
import com.magistralacan4.exp31.databinding.ActivityMainBinding
import kotlinx.coroutines.launch

/**
 * Eksperyment 3.1 (Pomiary dla CAN-Edge AI.md, Grupa 3): pomiar RSSI, RTT i
 * stratnosci pakietow w funkcji odleglosci od ESP32 (SoftAP dla WiFi).
 *
 * Kazda probka zapisywana jest lokalnie (data/czas, dystans, scenariusz
 * LOS/NLOS, numer sekwencyjny, RTT lub status "lost", RSSI, wspolrzedne GPS)
 * - patrz ResultLogger. Zapis "na zywo" na serwer NIE jest uzywany celowo,
 * bo lacze pod testem moze byc zbyt zdegradowane akurat wtedy, gdy najbardziej
 * potrzebujemy pomiaru (patrz Pytania_Do_Wykladowcy_Eksperyment_3.1_20260729.md
 * pkt 9) - eksport nastepuje na zadanie, po zakonczeniu serii pomiarow.
 *
 * Lokalizacja: `lastKnownLocation` jest utrzymywana SWIEZA przez ciagle,
 * wysokodokladnosciowe `requestLocationUpdates` (PRIORITY_HIGH_ACCURACY, co
 * LOCATION_UPDATE_INTERVAL_MS) uruchamiane od momentu przyznania uprawnien -
 * NIE przez pojedynczy pasywny odczyt `lastLocation` (ktory moze zwrocic
 * dowolnie stary/nieaktualny fix, nawet sprzed wyjscia w teren). Punkt
 * referencyjny (`referenceLocation`) dodatkowo wymusza jeden switezy,
 * jednorazowy odczyt `getCurrentLocation()` w momencie klikniecia - to
 * najwazniejszy pojedynczy pomiar calego testu, wiec nie polegamy tam nawet
 * na buforze z ciaglych aktualizacji.
 */
class MainActivity : AppCompatActivity() {

    companion object {
        private const val LOCATION_UPDATE_INTERVAL_MS = 1000L
    }

    private lateinit var binding: ActivityMainBinding
    private lateinit var fusedLocationClient: FusedLocationProviderClient
    private lateinit var resultLogger: ResultLogger
    private lateinit var wifiManager: WifiManager

    private var referenceLocation: Location? = null
    private var lastKnownLocation: Location? = null
    private var wifiTester: WifiPingPongTester? = null
    private var locationCallback: LocationCallback? = null

    private val requiredPermissions = arrayOf(
        Manifest.permission.ACCESS_FINE_LOCATION,
        Manifest.permission.ACCESS_COARSE_LOCATION,
    )

    private val permissionLauncher = registerForActivityResult(
        androidx.activity.result.contract.ActivityResultContracts.RequestMultiplePermissions()
    ) { results ->
        if (results.values.all { it }) {
            startLocationUpdates()
        } else {
            Toast.makeText(this, "Bez uprawnien lokalizacji GPS nie bedzie dostepny.", Toast.LENGTH_LONG).show()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        fusedLocationClient = LocationServices.getFusedLocationProviderClient(this)
        resultLogger = ResultLogger(applicationContext)
        wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager

        ensurePermissions()

        binding.btnSetReference.setOnClickListener { onSetReferenceClicked() }
        binding.btnStartTest.setOnClickListener { onStartTestClicked() }
        binding.btnExportServer.setOnClickListener { onExportToServerClicked() }
        binding.btnExportShare.setOnClickListener { onExportShareClicked() }
    }

    private fun ensurePermissions() {
        val missing = requiredPermissions.filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }
        if (missing.isEmpty()) {
            startLocationUpdates()
        } else {
            permissionLauncher.launch(missing.toTypedArray())
        }
    }

    private fun startLocationUpdates() {
        if (ActivityCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION)
            != PackageManager.PERMISSION_GRANTED
        ) {
            return
        }
        stopLocationUpdates()

        val request = LocationRequest.Builder(Priority.PRIORITY_HIGH_ACCURACY, LOCATION_UPDATE_INTERVAL_MS)
            .setMinUpdateIntervalMillis(LOCATION_UPDATE_INTERVAL_MS)
            .build()
        val callback = object : LocationCallback() {
            override fun onLocationResult(result: LocationResult) {
                result.lastLocation?.let { lastKnownLocation = it }
            }
        }
        locationCallback = callback
        fusedLocationClient.requestLocationUpdates(request, callback, Looper.getMainLooper())
    }

    private fun stopLocationUpdates() {
        locationCallback?.let { fusedLocationClient.removeLocationUpdates(it) }
        locationCallback = null
    }

    override fun onDestroy() {
        stopLocationUpdates()
        super.onDestroy()
    }

    private fun onSetReferenceClicked() {
        if (ActivityCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION)
            != PackageManager.PERMISSION_GRANTED
        ) {
            Toast.makeText(this, "Brak uprawnien GPS.", Toast.LENGTH_SHORT).show()
            return
        }
        Toast.makeText(this, "Pobieranie precyzyjnej pozycji...", Toast.LENGTH_SHORT).show()
        fusedLocationClient.getCurrentLocation(Priority.PRIORITY_HIGH_ACCURACY, CancellationTokenSource().token)
            .addOnSuccessListener { loc ->
                if (loc == null) {
                    Toast.makeText(this, "Brak odczytu GPS - sprobuj ponownie za chwile (na zewnatrz).", Toast.LENGTH_LONG).show()
                    return@addOnSuccessListener
                }
                referenceLocation = loc
                binding.textReferenceStatus.text =
                    "Punkt bazowy: %.6f, %.6f (dokladnosc ~%.0fm)".format(loc.latitude, loc.longitude, loc.accuracy)
            }
    }

    private fun onStartTestClicked() {
        val espIp = binding.editEspIp.text?.toString()?.trim().orEmpty()
        val espPort = binding.editEspPort.text?.toString()?.trim()?.toIntOrNull()
        val distance = binding.editDistance.text?.toString()?.trim()?.toDoubleOrNull()
        val packetCount = binding.editPacketCount.text?.toString()?.trim()?.toIntOrNull()
        val scenario = if (binding.radioNlos.isChecked) "NLOS" else "LOS"

        if (espIp.isEmpty() || espPort == null || distance == null || packetCount == null || packetCount <= 0) {
            Toast.makeText(this, "Uzupelnij poprawnie wszystkie pola przed startem.", Toast.LENGTH_LONG).show()
            return
        }

        val tester = WifiPingPongTester(
            espIp = espIp,
            espPort = espPort,
            distanceM = distance,
            scenario = scenario,
            packetCount = packetCount,
            resultLogger = resultLogger,
            locationProvider = { lastKnownLocation },
            referenceLocationProvider = { referenceLocation },
            rssiProvider = { wifiManager.connectionInfo?.rssi },
            onProgress = { sent, received, lost, avgRttMs, lastRssi, distFromRefM ->
                runOnUiThread {
                    binding.textLiveStats.text = buildString {
                        append("Wyslane: $sent  Odebrane: $received  Utracone: $lost\n")
                        append("RTT sr.: ${avgRttMs?.let { "%.1f".format(it) } ?: "--"} ms   ")
                        append("RSSI: ${lastRssi ?: "--"} dBm\n")
                        append("Dystans GPS od bazy: ${distFromRefM?.let { "%.1f".format(it) } ?: "--"} m")
                    }
                }
            },
        )
        wifiTester = tester

        binding.btnStartTest.isEnabled = false
        binding.btnStartTest.text = "Test w toku..."
        lifecycleScope.launch {
            // lastKnownLocation jest juz utrzymywane na biezaco przez ciagle
            // requestLocationUpdates (patrz startLocationUpdates()) - nie
            // trzeba tu dodatkowego pasywnego odczytu.
            tester.run()
            binding.btnStartTest.isEnabled = true
            binding.btnStartTest.text = "Start testu WiFi"
            Toast.makeText(this@MainActivity, "Test zakonczony (dystans=${distance}m, $scenario).", Toast.LENGTH_LONG).show()
        }
    }

    private fun onExportToServerClicked() {
        val serverAddress = binding.editServerAddress.text?.toString()?.trim().orEmpty()
        if (serverAddress.isEmpty()) {
            Toast.makeText(this, "Podaj adres serwera domowego (IP:port).", Toast.LENGTH_SHORT).show()
            return
        }
        lifecycleScope.launch {
            val ok = resultLogger.uploadToServer(serverAddress)
            Toast.makeText(
                this@MainActivity,
                if (ok) "Wyniki wyslane do serwera." else "Blad wysylki - sprawdz adres/siec.",
                Toast.LENGTH_LONG,
            ).show()
        }
    }

    private fun onExportShareClicked() {
        val uri = resultLogger.getShareableUri(this)
        if (uri == null) {
            Toast.makeText(this, "Brak zapisanych wynikow do udostepnienia.", Toast.LENGTH_SHORT).show()
            return
        }
        val intent = android.content.Intent(android.content.Intent.ACTION_SEND).apply {
            type = "text/csv"
            putExtra(android.content.Intent.EXTRA_STREAM, uri)
            addFlags(android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        startActivity(android.content.Intent.createChooser(intent, "Udostepnij wyniki CSV"))
    }
}

package com.magistralacan4.exp31

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.location.Location
import android.net.Uri
import android.net.wifi.WifiManager
import android.os.Build
import android.os.Bundle
import android.os.Looper
import android.os.PowerManager
import android.provider.Settings
import android.view.WindowManager
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
import kotlinx.coroutines.Job
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

        /**
         * Orientacyjny "procent jakosci sygnalu" wyliczony z surowego RSSI
         * [dBm] - WYLACZNIE do podgladu na ekranie w terenie (latwiej ocenic
         * "z daleka" podczas chodzenia). Dane zapisywane do CSV zostaja w
         * surowym dBm, zgodnie z metodyka (Pomiary dla CAN-Edge AI.md,
         * Eksperyment 3.1) - ten procent NIGDY nie trafia do ResultLoggera.
         * Standardowa, powszechnie uzywana liniowa aproksymacja: -100dBm=0%,
         * -50dBm i mocniej=100%.
         */
        fun rssiToQualityPercent(rssiDbm: Int): Int = (2 * (rssiDbm + 100)).coerceIn(0, 100)
    }

    private lateinit var binding: ActivityMainBinding
    private lateinit var fusedLocationClient: FusedLocationProviderClient
    private lateinit var resultLogger: ResultLogger
    private lateinit var wifiManager: WifiManager

    private var referenceLocation: Location? = null
    private var lastKnownLocation: Location? = null
    private var testJob: Job? = null
    private var locationCallback: LocationCallback? = null

    private val requiredPermissions: Array<String>
        get() = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(
                Manifest.permission.ACCESS_FINE_LOCATION,
                Manifest.permission.ACCESS_COARSE_LOCATION,
                Manifest.permission.BLUETOOTH_SCAN,
                Manifest.permission.BLUETOOTH_CONNECT,
            )
        } else {
            arrayOf(
                Manifest.permission.ACCESS_FINE_LOCATION,
                Manifest.permission.ACCESS_COARSE_LOCATION,
            )
        }

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

        // Pojedynczy przebieg testu moze trwac kilka minut (do 10 000
        // pakietow) - ekran nie moze zgasnac/zablokowac sie w trakcie.
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        ensurePermissions()
        requestIgnoreBatteryOptimizations()

        binding.btnSetReference.setOnClickListener { onSetReferenceClicked() }
        binding.btnStartTest.setOnClickListener { onStartTestClicked() }
        binding.btnStopTest.setOnClickListener { onStopTestClicked() }
        binding.btnExportServer.setOnClickListener { onExportToServerClicked() }
        binding.btnExportShare.setOnClickListener { onExportShareClicked() }
        binding.btnStopTest.isEnabled = false
    }

    /**
     * Prosi system o wylaczenie optymalizacji baterii (Doze/App Standby) dla
     * tej aplikacji - bez tego dlugi przebieg testu (tysiace pakietow, kilka
     * minut) w tle/przy zgaszonym ekranie mogloby zostac ubite przez system.
     * Pokazuje natywny dialog Androida - wymaga recznego zatwierdzenia przez
     * uzytkownika, nie da sie tego zrobic bez interakcji.
     */
    private fun requestIgnoreBatteryOptimizations() {
        val powerManager = getSystemService(Context.POWER_SERVICE) as PowerManager
        if (!powerManager.isIgnoringBatteryOptimizations(packageName)) {
            try {
                startActivity(
                    android.content.Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS).apply {
                        data = Uri.parse("package:$packageName")
                    },
                )
            } catch (e: android.content.ActivityNotFoundException) {
                Toast.makeText(this, "Nie udalo sie otworzyc ustawien baterii - wylacz optymalizacje recznie.", Toast.LENGTH_LONG).show()
            }
        }
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
        val distance = binding.editDistance.text?.toString()?.trim()?.toDoubleOrNull()
        val packetCount = binding.editPacketCount.text?.toString()?.trim()?.toIntOrNull()
        val scenario = if (binding.radioNlos.isChecked) "NLOS" else "LOS"
        val useBle = binding.radioTechBle.isChecked

        if (distance == null || packetCount == null || packetCount <= 0) {
            Toast.makeText(this, "Uzupelnij poprawnie wszystkie pola przed startem.", Toast.LENGTH_LONG).show()
            return
        }

        val onProgress = { sent: Int, received: Int, lost: Int, avgRttMs: Double?, lastRssi: Int?, distFromRefM: Double? ->
            runOnUiThread {
                binding.textLiveStats.text = buildString {
                    append("Wyslane: $sent  Odebrane: $received  Utracone: $lost\n")
                    append("RTT sr.: ${avgRttMs?.let { "%.1f".format(it) } ?: "--"} ms   ")
                    val rssiQuality = lastRssi?.let { " (~${rssiToQualityPercent(it)}% orientacyjnie)" } ?: ""
                    append("RSSI: ${lastRssi ?: "--"} dBm$rssiQuality\n")
                    append("Dystans GPS od bazy: ${distFromRefM?.let { "%.1f".format(it) } ?: "--"} m")
                }
            }
        }

        val runBlock: suspend () -> Unit
        if (useBle) {
            val tester = BlePingPongTester(
                context = applicationContext,
                distanceM = distance,
                scenario = scenario,
                packetCount = packetCount,
                resultLogger = resultLogger,
                locationProvider = { lastKnownLocation },
                referenceLocationProvider = { referenceLocation },
                onProgress = onProgress,
                onStatus = { msg -> runOnUiThread { binding.textLog.text = msg } },
            )
            runBlock = { tester.run() }
        } else {
            val espIp = binding.editEspIp.text?.toString()?.trim().orEmpty()
            val espPort = binding.editEspPort.text?.toString()?.trim()?.toIntOrNull()
            if (espIp.isEmpty() || espPort == null) {
                Toast.makeText(this, "Uzupelnij adres IP i port ESP32.", Toast.LENGTH_LONG).show()
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
                onProgress = onProgress,
            )
            runBlock = { tester.run() }
        }

        binding.btnStartTest.isEnabled = false
        binding.btnStopTest.isEnabled = true
        binding.btnStartTest.text = "Test w toku..."
        testJob = lifecycleScope.launch {
            // lastKnownLocation jest juz utrzymywane na biezaco przez ciagle
            // requestLocationUpdates (patrz startLocationUpdates()) - nie
            // trzeba tu dodatkowego pasywnego odczytu.
            try {
                runBlock()
                Toast.makeText(
                    this@MainActivity,
                    "Test zakonczony (${if (useBle) "BLE" else "WiFi"}, dystans=${distance}m, $scenario).",
                    Toast.LENGTH_LONG,
                ).show()
            } catch (e: Exception) {
                Toast.makeText(this@MainActivity, "Test przerwany/blad: ${e.message}", Toast.LENGTH_LONG).show()
            } finally {
                binding.btnStartTest.isEnabled = true
                binding.btnStopTest.isEnabled = false
                binding.btnStartTest.text = "Start testu"
                testJob = null
            }
        }
    }

    private fun onStopTestClicked() {
        testJob?.cancel()
        binding.textLog.text = "Test przerwany przez uzytkownika."
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

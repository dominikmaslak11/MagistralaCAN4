# MagistralaCAN4 — REST API

**Zdalne sterowanie i monitoring przez HTTP.** Endpointy REST API umożliwiają kontrolowanie aplikacji i pobieranie danych CAN z dowolnego urządzenia w sieci — przeglądarki, terminala, skryptu `curl`, własnej aplikacji.

---

## Co to jest REST API?

REST (Representational State Transfer) API to interfejs komunikacyjny oparty na protokole **HTTP**. Działa na zasadzie żądań i odpowiedzi — klient wysyła zapytanie (GET, POST), serwer zwraca dane w formacie **JSON**.

W MagistralaCAN4 REST API nasłuchuje domyślnie na porcie **8080** i udostępnia cztery endpointy:

```
http://localhost:8080/api/status   →  statystyki aplikacji
http://localhost:8080/api/frames   →  ostatnie ramki CAN
http://localhost:8080/api/start    →  uruchom sniffing
http://localhost:8080/api/stop     →  zatrzymaj sniffing
```

---

## Dlaczego REST API jest ważne?

### 1. Zdalne sterowanie

Nie musisz siedzieć przy komputerze z MagistralaCAN4. Możesz uruchomić lub zatrzymać sniffing z dowolnego miejsca — z telefonu, z drugiego komputera, z automatyzacji CI/CD.

```bash
# Uruchom sniffing zdalnie
curl -X POST http://192.168.1.10:8080/api/start

# Sprawdź status
curl http://192.168.1.10:8080/api/status
```

### 2. Integracja z zewnętrznymi systemami

Dane CAN w formacie JSON mogą być konsumowane przez:
- **Dashboardy** (Grafana, własna strona HTML)
- **Skrypty automatyzacji** (Python, Bash)
- **Systemy monitoringu** (Prometheus, Nagios)
- **Aplikacje mobilne** (przez przeglądarkę lub natywne)

### 3. Lekkość i prostota

REST API w MagistralaCAN4 to tylko ~100 linii kodu (`HttpRestServer`), żadnych zewnętrznych zależności. Używa natywnego `QTcpServer` Qt6. Działa natychmiast po kliknięciu przycisku w toolbarze.

### 4. Uzupełnienie WebSocket

| Cecha | WebSocket | REST API |
|-------|-----------|----------|
| Kierunek | Strumień ciągły | Żądanie→odpowiedź |
| Format | JSON (każda ramka) | JSON (agregacja) |
| Przypadek | Podgląd LIVE | Zapytania na żądanie |
| Port | 9000 (WSS) | 8080 (HTTP) |

---

## Dlaczego warto tego używać?

**Scenariusz 1 — Warsztat:** Mechanik z telefonem sprawdza aktualne parametry silnika przez OBD-II, nie podchodząc do laptopa z MagistralaCAN4.

```
Telefon → WiFi → http://192.168.1.10:8080/api/frames → JSON z RPM, temperaturą
```

**Scenariusz 2 — Automatyzacja testów:** Skrypt Pythona uruchamia sniffing, zbiera 10 sekund danych, zatrzymuje, analizuje — wszystko bez klikania.

```python
import requests, time
requests.post("http://localhost:8080/api/start")
time.sleep(10)
data = requests.get("http://localhost:8080/api/frames").json()
requests.post("http://localhost:8080/api/stop")
print(f"Zebrano {len(data)} ramek")
```

**Scenariusz 3 — Monitoring 24/7:** System monitoringu co 30 sekund sprawdza `/api/status` i alarmuje gdy FPS spadnie poniżej progu.

---

## Jak używać — krok po kroku

### Krok 1: Uruchom REST API

W MagistralaCAN4 kliknij przycisk **🌐 REST API** w górnym toolbarze.

Serwer uruchomi się na porcie **8080**. Komunikat w logu:

```
REST API nasłuchuje na porcie 8080
```

### Krok 2: Sprawdź status (GET /api/status)

Otwórz przeglądarkę lub terminal:

```bash
curl http://localhost:8080/api/status
```

Odpowiedź JSON:

```json
{
  "running": true,
  "frames": 1247,
  "fps": 82.0,
  "uniqueIds": 12
}
```

| Pole | Znaczenie |
|------|-----------|
| `running` | Czy sniffing jest aktywny |
| `frames` | Liczba ramek w tabeli |
| `fps` | Ramki na sekundę (średnia z ostatnich 500ms) |
| `uniqueIds` | Liczba unikalnych CAN ID w ostatnich 500ms |

### Krok 3: Pobierz ramki (GET /api/frames)

```bash
curl http://localhost:8080/api/frames
```

Zwraca tablicę JSON z ostatnimi 100 ramkami:

```json
[
  {
    "id": 291,
    "dlc": 8,
    "timestamp": 1715123456789,
    "data": "A1B2C3D4E5F60708"
  },
  {
    "id": 466,
    "dlc": 6,
    "timestamp": 1715123457012,
    "data": "FF0011223344"
  }
]
```

### Krok 4: Steruj sniffingiem (POST /api/start, /api/stop)

```bash
# Uruchom sniffing
curl -X POST http://localhost:8080/api/start
# → {"status":"ok"}

# Zatrzymaj sniffing
curl -X POST http://localhost:8080/api/stop
# → {"status":"ok"}
```

### Krok 5: Przez przeglądarkę

Otwórz w przeglądarce:

```
http://localhost:8080/api/status
```

Zobaczysz surowy JSON. Możesz też stworzyć prostą stronę HTML, która odświeża dane co sekundę:

```html
<!DOCTYPE html>
<html><body>
<h1>MagistralaCAN4 Dashboard</h1>
<div id="status">Ładowanie...</div>
<script>
setInterval(async () => {
    const r = await fetch('http://192.168.1.10:8080/api/status');
    const d = await r.json();
    document.getElementById('status').innerHTML =
        `Ramki: ${d.frames} | FPS: ${d.fps} | ID: ${d.uniqueIds}`;
}, 1000);
</script>
</body></html>
```

---

## Endpointy — pełne zestawienie

| Metoda | Ścieżka | Opis | Odpowiedź |
|--------|---------|------|-----------|
| GET | `/api/status` | Statystyki aplikacji | JSON: running, frames, fps, uniqueIds |
| GET | `/api/frames` | Ostatnie 100 ramek CAN | JSON array: id, dlc, timestamp, data |
| POST | `/api/start` | Uruchom sniffing | JSON: `{"status":"ok"}` |
| POST | `/api/stop` | Zatrzymaj sniffing | JSON: `{"status":"ok"}` |

---

## Bezpieczeństwo

- REST API działa na **localhost** — domyślnie dostępne tylko z lokalnej maszyny
- Dla dostępu sieciowego nasłuchuje na `0.0.0.0:8080`
- Brak autoryzacji (założenie: sieć LAN, zaufana)
- Można ograniczyć firewall-em: `sudo ufw allow from 192.168.1.0/24 to any port 8080`
- W przyszłości: opcjonalny token w nagłówku `Authorization: Bearer <token>`

---

## Architektura techniczna

```
┌─────────────────┐     HTTP GET/POST     ┌─────────────────┐
│  Klient (curl,   │ ──────────────────► │  MagistralaCAN4  │
│  przeglądarka,   │ ◄────────────────── │  HttpRestServer  │
│  Python, itp.)   │     JSON response   │  (port 8080)     │
└─────────────────┘                       └────────┬────────┘
                                                   │
                                          ┌────────┴────────┐
                                          │  CanFrameModel   │
                                          │  (dane ramek)    │
                                          └─────────────────┘
```

`HttpRestServer` to `QTcpServer` + parser HTTP + routing. Parsuje pierwszą linię żądania (`GET /api/status HTTP/1.1`), wywołuje odpowiedni handler i zwraca JSON.

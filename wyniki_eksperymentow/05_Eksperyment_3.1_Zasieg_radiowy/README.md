# Eksperyment 3.1 — zasięg radiowy WiFi/BLE

## STATUS: PRZYGOTOWANY — POMIAR TERENOWY NIE ZOSTAŁ WYKONANY

**W tym katalogu nie ma plików CSV z wynikami, ponieważ pomiaru jeszcze nie
przeprowadzono.** Ten dokument opisuje, co jest gotowe i dlaczego eksperyment
czeka.

## 1. Co miało być badane

Urządzenie w maszynie wysyła dane bezprzewodowo. Pytanie: **jak odległość
i przeszkody wpływają na opóźnienia i utratę pakietów?** Od tego zależy, czy
łączność wystarczy w realnym zastosowaniu.

## 2. Co zostało przygotowane

| Element | Stan | Opis |
|---|---|---|
| Firmware ESP32 | gotowy | `esp_experiment_3_1_wifi/` — ESP32 jako **własny punkt dostępowy** (SoftAP), protokół ping-pong przez UDP (`PING:<nr>` → `PONG:<nr>`) |
| Aplikacja mobilna | gotowa, kompiluje się | `android_experiment_3_1/` (Kotlin) — **pierwsza aplikacja mobilna w projekcie** |
| Serwer odbiorczy | gotowy | `server_receiver.py` — przyjmuje wyeksportowane serie pomiarowe |

**Co mierzy aplikacja:** czas obiegu pakietu (RTT) **na zegarze telefonu** —
dzięki temu nie trzeba synchronizować zegarów obu urządzeń; siłę sygnału (RSSI);
oraz pozycję GPS jako **niezależną weryfikację** deklarowanej odległości.

**Dlaczego ESP32 jako własny punkt dostępowy, a nie przez router:** żeby mierzyć
surowy zasięg radia ESP32, niezależnie od jakości konkretnego routera.

**Zapis danych:** lokalnie do pliku CSV na telefonie jako źródło prawdy —
odporne na zerwanie łącza w trakcie testu. Eksport dopiero po zakończeniu serii.

## 3. Dlaczego pomiar nie ruszył

Przed napisaniem kodu spisano **10 otwartych kwestii metodologicznych** do
rozstrzygnięcia z prowadzącym (`Pytania_Do_Wykladowcy_Eksperyment_3.1_20260729.md`).
Najważniejsze:

1. **Architektura testu** — SoftAP (mierzy radio ESP32) czy router pośredniczący
   (bliższy docelowemu wdrożeniu)? To dwa różne eksperymenty.
2. **Role fizyczne urządzeń** — w metodyce zapisano odwrotnie niż wynika
   z praktyki (ESP32 stacjonarne, telefon mobilny).
3. **Liczebność próby** — metodyka mówi o 10 000 pakietów na punkt pomiarowy;
   zaproponowano redukcję do 1000–2000 z uzasadnieniem statystycznym.
4. **Interpretacja zapisu „Liczba pomiarów do weryfikacji: 10"** — 10 niezależnych
   powtórzeń całego przebiegu, czy ogólna wskazówka co do liczebności?

**Świadoma decyzja:** nie uruchamiać pomiaru terenowego przed rozstrzygnięciem
tych kwestii, żeby nie wykonać pracy, którą trzeba będzie powtórzyć.

## 4. Znane ograniczenie przygotowanej metody

Dokładność GPS wynosi **3–5 m**, a w scenariuszu bez linii widzenia (za
budynkiem) jest zwykle gorsza z powodu odbić sygnału — czyli **najgorsza
dokładnie tam, gdzie GPS miałby najbardziej pomóc**. Ograniczenie zostało
udokumentowane jawnie, zanim wykonano pomiar.

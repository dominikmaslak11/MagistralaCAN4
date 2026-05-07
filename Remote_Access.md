# MagistralaCAN4 — Zdalny dostęp przez internet (WSS + token)

Tutorial krok po kroku: jak przesyłać ramki CAN przez internet między dwoma komputerami z zainstalowanym MagistralaCAN4.

---

## Zarys architektury

```
┌─────────────────────┐         internet (WSS)        ┌─────────────────────┐
│     PC-A (serwer)   │ ◄══════════════════════════► │    PC-B (klient)     │
│                     │    szyfrowany TLS 1.3         │                      │
│  CanSniffer → CAN   │    + token 256-bit            │  RemoteCanClient     │
│  WebSocketServer ───┼───────────────────────────────┼──► newFrame          │
│  (port 9001, WSS)   │                               │  → CanFrameModel     │
│                     │                               │  → AssociativeLearner│
│                     │                               │  → Dashboard         │
└─────────────────────┘                               └──────────────────────┘
```

- **PC-A** sniffuje lokalny CAN i streamuje ramki przez szyfrowany WebSocket (WSS)
- **PC-B** łączy się zdalnie, autoryzuje tokenem i odbiera ramki bezpośrednio do pipeline'u analitycznego
- Ramki zdalne **nie** są wysyłane na lokalną magistralę CAN (aby uniknąć pętli)

---

## Wymagania

- MagistralaCAN4 w wersji **2.1.0+** na obu komputerach
- Otwarty port **9001** (lub inny wybrany) na firewallu PC-A
- `openssl` zainstalowany na PC-A (do generacji certyfikatu TLS)
- Komunikacja internetowa między PC-A a PC-B (publiczny IP lub VPN)

---

## Krok 1: Przygotowanie PC-A (serwer)

### 1.1 Uruchom MagistralaCAN4

```bash
cd MagistralaCAN4/build
./MagistralaCAN4
```

### 1.2 Przejdź do zakładki **"Zdalny CAN"**

Znajdziesz ją jako ostatnią zakładkę w górnym pasku (po "Symulacja CAN").

### 1.3 Uruchom serwer WSS

W sekcji **"Serwer WSS (wysyłanie ramek)"**:

1. Ustaw port (domyślnie `9001` — możesz zmienić jeśli zajęty)
2. Kliknij **"Uruchom serwer WSS"**

Przy pierwszym uruchomieniu MagistralaCAN4 automatycznie:
- Wygeneruje certyfikat self-signed TLS (`~/.magistrala_can4/server.crt`)
- Wygeneruje 256-bitowy token (`~/.magistrala_can4/token`)

Token pojawi się w polu tekstowym obok przycisku **"Kopiuj"**:

```
Token: a3f7b2c9e1d4f5a6b7c8d9e0f1a2b3c4d5e6f7a8b9c0d1e2f3a4b5c6d7e8f9a0
       [Kopiuj]
```

3. Kliknij **"Kopiuj"** i przekaż token do PC-B (przez komunikator, e-mail, telefon — bezpiecznym kanałem)

Status powinien zmienić się na:

```
Serwer: NASŁUCHUJE na porcie 9001 (klientów: 0)
```

### 1.4 Skonfiguruj firewall na PC-A

Musisz otworzyć port 9001 (lub wybrany) dla połączeń przychodzących:

```bash
# Jeśli używasz ufw:
sudo ufw allow 9001/tcp

# Jeśli używasz firewalld:
sudo firewall-cmd --add-port=9001/tcp --permanent
sudo firewall-cmd --reload
```

### 1.5 Sprawdź adres IP PC-A

```bash
ip addr show | grep 'inet ' | grep -v 127.0.0.1
```

Przykład: `192.168.1.10` (lokalne) lub `203.0.113.5` (publiczne).

---

## Krok 2: Przygotowanie PC-B (klient)

### 2.1 Uruchom MagistralaCAN4 na PC-B

```bash
cd MagistralaCAN4/build
./MagistralaCAN4
```

### 2.2 Przejdź do zakładki **"Zdalny CAN"**

### 2.3 Połącz się z serwerem

W sekcji **"Klient zdalny (odbieranie ramek)"**:

1. Wpisz URL serwera:
   ```
   wss://192.168.1.10:9001
   ```
   (użyj adresu IP PC-A z kroku 1.5)

2. Wpisz token (64 znaki hex, skopiowany z PC-A). Pole jest domyślnie ukryte (•••) — kliknij 👁 aby zobaczyć.

3. Kliknij **"Połącz ze zdalnym CAN"**

### 2.4 Weryfikacja połączenia

Po chwili powinieneś zobaczyć:

```
Klient: Połączono i autoryzowano
Odebrane ramki: 1,247
```

Ramki z PC-A teraz pojawiają się w:
- Zakładce **"Ruch CAN"** — tabela z ID, DLC, danymi, timestampem
- Zakładce **"Uczenie asocjacyjne"** — analiza korelacji, klastrowanie, anomalie
- Zakładce **"Dashboard CAN"** — jeśli wcześniej wczytano plik `.dbc`
- Zakładce **"Diagnostyka J1939"** — jeśli ramki mają 29-bitowe ID J1939

---

## Krok 3: Testowanie połączenia

### 3.1 Wygeneruj ruch testowy na PC-A

```bash
# Na PC-A, w terminalu:
cangen vcan0 -v -I 123 -L 8 -g 10
```

### 3.2 Obserwuj ramki na PC-B

W MagistralaCAN4 na PC-B, w zakładce "Ruch CAN", powinieneś zobaczyć napływające ramki o ID `0x123`.

### 3.3 Synchronizacja wsteczna

PC-A również widzi swoje własne ramki w zakładce "Ruch CAN". Serwer WSS streamuje każdą ramkę, która przechodzi przez `CanFrameModel` — zarówno lokalną, jak i tę z `cangen`.

---

## Rozwiązywanie problemów

### Serwer WSS nie uruchamia się

**Objaw:** błąd `"Nie można wygenerować certyfikatu TLS"`

**Rozwiązanie:** zainstaluj openssl:
```bash
sudo apt install openssl
```
Następnie ręcznie usuń stare pliki i spróbuj ponownie:
```bash
rm -rf ~/.magistrala_can4/
```

### Klient nie może się połączyć

**Objaw:** status "Rozłączono — ponawianie..."

**Sprawdź kolejno:**

1. **Firewall na PC-A** — czy port 9001 jest otwarty?
   ```bash
   # Na PC-A:
   sudo ufw status
   ```

2. **Adres IP** — czy PC-B pinguje PC-A?
   ```bash
   # Na PC-B:
   ping 192.168.1.10
   ```

3. **Certyfikat self-signed** — klient celowo ignoruje błędy walidacji certyfikatu (bo certyfikat jest self-signed). Jeśli widzisz błąd SSL, upewnij się że MagistralaCAN4 jest w wersji 2.1.0+.

4. **Token** — czy token na PC-B jest identyczny z tokenem na PC-A? Token musi być dokładnie taki sam (64 znaki, bez spacji, bez nowych linii).

### Błąd autoryzacji

**Objaw:** `"Autoryzacja odrzucona: Invalid token"`

**Rozwiązanie:** skopiuj token ponownie z PC-A (przycisk "Kopiuj" w panelu serwera). Upewnij się, że wklejasz całe 64 znaki.

### Ramki nie docierają

**Objaw:** połączenie jest, ale licznik "Odebrane ramki" stoi na 0

**Rozwiązanie:** upewnij się, że na PC-A działa sniffing (przycisk ▶ Start w głównym toolbarze). Serwer WSS streamuje tylko te ramki, które przechodzą przez model CAN — sniffing musi być aktywny.

---

## Bezpieczeństwo

### Co zapewnia ochrona

| Mechanizm | Działanie |
|-----------|-----------|
| **WSS (TLS 1.3)** | Szyfruje cały ruch między PC-A a PC-B. Osoba trzecia podsłuchująca ruch sieciowy widzi tylko zaszyfrowane dane. |
| **Token 256-bit** | 64-znakowy losowy ciąg hex. Bez niego serwer odrzuca połączenie po 5 sekundach. Przestrzeń kluczy: 2^256 ~ 10^77 kombinacji. |
| **Timeout auth** | Klient ma 5 sekund na poprawne uwierzytelnienie. Potem połączenie jest zamykane. |

### Dystrybucja tokena — najlepsze praktyki

1. **Wygeneruj token** (automatycznie przy pierwszym uruchomieniu serwera)
2. **Skopiuj** przyciskiem "Kopiuj" w UI
3. **Prześlij** bezpiecznym kanałem:
   - ✅ Signal / WhatsApp (end-to-end encrypted)
   - ✅ SSH / SCP
   - ✅ Pendrive (fizyczny transfer)
   - ❌ E-mail (chyba że PGP)
   - ❌ SMS
4. **Wklej** na PC-B w pole "Token"

### Regeneracja tokena

Aby wygenerować nowy token (np. po kompromitacji):

```bash
rm ~/.magistrala_can4/token
```

Przy następnym uruchomieniu serwera MagistralaCAN4 wygeneruje nowy token. Pamiętaj, że wszyscy klienci muszą dostać nowy token.

---

## Zaawansowane scenariusze

### Wiele klientów

Serwer WSS obsługuje wielu klientów jednocześnie. Każdy klient podaje ten sam token. Status serwera pokazuje liczbę podłączonych klientów:

```
Serwer: NASŁUCHUJE na porcie 9001 (klientów: 3)
```

### Tryb nie-szyfrowany (LAN)

Jeśli oba komputery są w tej samej sieci LAN i nie potrzebujesz szyfrowania, możesz skorzystać z trybu plaintext WebSocket. W kodzie aplikacji użyj `m_server->start(9000)` zamiast `startSecure(9001)`. Uwaga: w trybie plaintext token NIE jest wymagany.

### Niestandardowy port

Zmień port w polu "Port" przed kliknięciem "Uruchom serwer WSS". Na kliencie podaj URL z odpowiednim portem, np. `wss://192.168.1.10:4433`.

---

## Podsumowanie kroków (checklista)

- [ ] **PC-A**: Uruchom MagistralaCAN4
- [ ] **PC-A**: Zakładka "Zdalny CAN" → "Uruchom serwer WSS"
- [ ] **PC-A**: Skopiuj token
- [ ] **PC-A**: Otwórz port 9001 na firewallu
- [ ] **PC-A**: Sprawdź adres IP
- [ ] **PC-B**: Uruchom MagistralaCAN4
- [ ] **PC-B**: Zakładka "Zdalny CAN" → wpisz `wss://<IP-A>:9001`
- [ ] **PC-B**: Wklej token
- [ ] **PC-B**: Kliknij "Połącz ze zdalnym CAN"
- [ ] **PC-A**: Rozpocznij sniffing (▶ Start)
- [ ] **PC-B**: Obserwuj ramki w zakładce "Ruch CAN"

---

**Gotowe.** Dwa komputery połączone przez internet — ramki CAN płyną szyfrowanym kanałem, analityka działa zdalnie.

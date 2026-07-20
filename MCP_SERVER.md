# MagistralaCAN4 — Serwer MCP

**Sterowanie żywą sesją CAN przez asystentów AI.** Wbudowany serwer MCP (Model
Context Protocol) pozwala Claude Code, Codex CLI i CodeWhale (DeepSeek TUI)
sterować już działającą aplikacją MagistralaCAN4 — bez ręcznych `curl` do
REST API.

---

## Uruchomienie

Kliknij **"🤖 MCP Server"** w toolbarze. Serwer nasłuchuje na
`http://127.0.0.1:8790/mcp` (tylko localhost — nie jest wystawiony na sieć).

Transport: **Streamable HTTP** (MCP spec `2025-06-18`), tryb bezstanowy —
jeden endpoint `POST /mcp`, każda odpowiedź to pojedynczy obiekt JSON (bez
SSE, bez sesji).

---

## Konfiguracja klientów

### Claude Code

`.mcp.json` w katalogu projektu (lub globalnie `~/.claude.json`):

```json
{
  "mcpServers": {
    "magistrala": {
      "type": "http",
      "url": "http://127.0.0.1:8790/mcp"
    }
  }
}
```

Albo jedną komendą:
```bash
claude mcp add --transport http magistrala http://127.0.0.1:8790/mcp
```

### Codex CLI

`~/.codex/config.toml` (lub `.codex/config.toml` w katalogu projektu):

```toml
[mcp_servers.magistrala]
url = "http://127.0.0.1:8790/mcp"
```

### CodeWhale (DeepSeek TUI)

`~/.codewhale/mcp.json`:

```json
{
  "servers": {
    "magistrala": {
      "url": "http://127.0.0.1:8790/mcp"
    }
  }
}
```

---

## Dostępne narzędzia

| Narzędzie | Opis |
|---|---|
| `get_status` | Status na żywo: liczba ramek, fps, unikalne ID, obciążenie magistrali, liczba alertów, czy sniffing aktywny |
| `get_frames` | Ostatnie ramki CAN (`limit`, opcjonalny filtr `id`) |
| `get_ids` | Unikalne ID posortowane wg liczby wystąpień |
| `get_alerts` | Ostatnie alerty z CanAlertEngine |
| `send_frame` | Wysyła ramkę CAN (wymaga aktywnego sniffingu — patrz niżej) |
| `start_sniffing` / `stop_sniffing` | Uruchamia / zatrzymuje sniffing na wybranym interfejsie |
| `load_dbc_file` | Wczytuje plik DBC (Vector) |
| `load_arxml_file` | Wczytuje plik ARXML (AUTOSAR 4.x) |
| `list_dbc_messages` | Lista wiadomości i sygnałów z aktualnej bazy DBC/ARXML |
| `decode_signals` | Dekoduje sygnały DBC dla podanego ID + bajtów danych |
| `run_lua_snippet` | Uruchamia kod Lua w silniku skryptowym aplikacji |
| `list_sim_nodes` | Lista węzłów symulatora CAN (CanNodeSimulator) |
| `set_sim_node_enabled` | Włącza/wyłącza symulowany węzeł po nazwie |
| `load_sim_config` | Wczytuje konfigurację symulatora z pliku JSON |
| `load_replay_file` | Wczytuje nagranie `.mcan`/`.mcan.zst` do odtwarzacza |
| `replay_control` | Steruje odtwarzaniem: `play`/`pause`/`stop`/`seek`/`status` |

Pełne schematy parametrów (`inputSchema`) są dostępne przez `tools/list`.

---

## Ważne ograniczenia

- **`send_frame` wymaga aktywnego sniffingu.** Jeśli sterownik CAN nie jest
  otwarty, narzędzie zwraca błąd zamiast próbować wysłać ramkę — celowo,
  ponieważ próba zapisu przez nieotwarty sterownik uruchamia w GUI modalny
  dialog błędu (`QMessageBox::warning`), który zablokowałby cały wątek GUI
  (i serwer MCP) do czasu ręcznego kliknięcia OK. Użyj najpierw
  `start_sniffing` albo sprawdź `get_status.sniffingActive`.
- `start_sniffing` bez wybranego interfejsu w GUI (pusta lista interfejsów)
  może analogicznie pokazać modalny dialog "Wybierz interfejs CAN" — upewnij
  się, że interfejs CAN (np. `vcan0`, `can0`) jest widoczny w combo boxie
  przed wywołaniem tego narzędzia.
- Serwer nasłuchuje wyłącznie na `127.0.0.1` — nie jest dostępny z sieci.
- Brak uwierzytelniania — zakładamy zaufane środowisko lokalne (jak REST API
  na porcie 8080).

## Ręczny test przez curl

```bash
curl -s -X POST http://127.0.0.1:8790/mcp -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}'

curl -s -X POST http://127.0.0.1:8790/mcp -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"get_status","arguments":{}}}'
```

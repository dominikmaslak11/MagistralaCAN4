#!/usr/bin/env python3
"""
Test klienta WebSocket dla MagistralaCAN4 — Zdalny CAN.

Tryb WS (plaintext):
    python3 test_remote_can.py ws://127.0.0.1:9000

Tryb WSS (TLS, self-signed):
    python3 test_remote_can.py wss://127.0.0.1:9001 --token <64-char-hex>
"""
import asyncio, ssl, json, sys, argparse, time

try:
    import websockets
except ImportError:
    sys.exit("Brak biblioteki 'websockets'. Zainstaluj: pip install websockets")

async def run(url: str, token: str | None, limit: int) -> None:
    ssl_ctx = None
    if url.startswith("wss://"):
        ssl_ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        ssl_ctx.check_hostname = False
        ssl_ctx.verify_mode    = ssl.CERT_NONE   # self-signed

    print(f"Łączę z {url} …")
    async with websockets.connect(url, ssl=ssl_ctx) as ws:
        print("Połączono.")

        if url.startswith("wss://"):
            if not token:
                sys.exit("WSS wymaga tokena: --token <64-char-hex>")
            await ws.send(json.dumps({"type": "auth", "token": token}))
            resp = json.loads(await ws.recv())
            if resp.get("type") != "auth_ok":
                sys.exit(f"Autoryzacja nieudana: {resp}")
            print("Autoryzacja OK.")

        total = 0
        t0 = time.perf_counter()
        print(f"Odbieram ramki (limit={limit}, Ctrl+C aby przerwać) …\n")

        try:
            async for raw in ws:
                msg = json.loads(raw)
                if msg.get("type") != "frames":
                    print(f"  [inne] {msg}")
                    continue

                for f in msg["frames"]:
                    total += 1
                    data_hex = f["data"].upper()
                    # grupuj po 2 znaki
                    data_fmt = " ".join(data_hex[i:i+2] for i in range(0, len(data_hex), 2))
                    print(f"  #{total:4d}  ID=0x{f['id']:03X}  DLC={f['dlc']}  "
                          f"DATA=[{data_fmt}]  ts={f['timestamp']}")

                    if total >= limit:
                        raise asyncio.CancelledError

        except asyncio.CancelledError:
            pass
        except KeyboardInterrupt:
            pass

        elapsed = time.perf_counter() - t0
        fps = total / elapsed if elapsed > 0 else 0
        print(f"\nOdebrano {total} ramek w {elapsed:.1f}s ({fps:.1f} ramek/s)")

def main() -> None:
    p = argparse.ArgumentParser(description="MagistralaCAN4 Remote CAN test client")
    p.add_argument("url",             help="ws://host:port  lub  wss://host:port")
    p.add_argument("--token", "-t",   help="Token autoryzacji (tylko WSS)")
    p.add_argument("--limit", "-n",   type=int, default=200,
                   help="Maksymalna liczba ramek do odebrania (domyślnie 200)")
    args = p.parse_args()

    asyncio.run(run(args.url, args.token, args.limit))

if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
WebSocket client fixture for cross-language integration tests.

Connects to a WS server, receives frames, prints each received frame as:
    FRAME:<id_hex>:<dlc>:<data_hex>
then prints "RECEIVED:<total>" when done and exits.

Usage:
    python3 ws_client_fixture.py --url ws://127.0.0.1:19201 --expect 5

Exit codes:
    0 — received expected number of frames
    1 — timeout or protocol error
"""
import asyncio, json, sys, argparse

try:
    import websockets
except ImportError:
    sys.exit("pip install websockets")


async def run(url: str, expect: int) -> int:
    total = 0
    try:
        async with websockets.connect(url, open_timeout=5) as ws:
            print("CONNECTED", flush=True)
            while total < expect:
                try:
                    raw = await asyncio.wait_for(ws.recv(), timeout=5.0)
                except asyncio.TimeoutError:
                    print(f"TIMEOUT after {total} frames", file=sys.stderr, flush=True)
                    return 1

                msg = json.loads(raw)
                if msg.get("type") != "frames":
                    continue

                for f in msg["frames"]:
                    can_id   = f["id"]
                    dlc      = f["dlc"]
                    data_hex = f.get("data", "")
                    print(f"FRAME:{can_id:X}:{dlc}:{data_hex.upper()}", flush=True)
                    total += 1
                    if total >= expect:
                        break

    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr, flush=True)
        return 1

    print(f"RECEIVED:{total}", flush=True)
    return 0 if total >= expect else 1


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--url",    default="ws://127.0.0.1:19201")
    p.add_argument("--expect", type=int, default=5,
                   help="Number of frames expected")
    args = p.parse_args()

    rc = asyncio.run(run(args.url, args.expect))
    sys.exit(rc)


if __name__ == "__main__":
    main()

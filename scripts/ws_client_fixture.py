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


async def run(url: str, expect: int,
              send_id: int | None, send_data: str | None,
              wait_ack: bool) -> int:
    total = 0
    try:
        async with websockets.connect(url, open_timeout=5) as ws:
            print("CONNECTED", flush=True)

            # Opcjonalnie wyślij ramkę do serwera
            if send_id is not None:
                hex_data = (send_data or "").replace(" ", "")
                dlc = len(hex_data) // 2
                msg = json.dumps({
                    "type":     "send_frame",
                    "id":       send_id,
                    "extended": send_id > 0x7FF,
                    "rtr":      False,
                    "fd":       False,
                    "dlc":      dlc,
                    "timestamp": 0,
                    "data":     hex_data.lower(),
                })
                await ws.send(msg)
                print(f"SENT:0x{send_id:X}", flush=True)

            if expect == 0 and not wait_ack:
                return 0

            # Odbieraj ramki / ack
            deadline = asyncio.get_event_loop().time() + 5.0
            while total < expect or wait_ack:
                remaining = deadline - asyncio.get_event_loop().time()
                if remaining <= 0:
                    print(f"TIMEOUT after {total} frames", file=sys.stderr, flush=True)
                    return 1
                try:
                    raw = await asyncio.wait_for(ws.recv(), timeout=remaining)
                except asyncio.TimeoutError:
                    print(f"TIMEOUT after {total} frames", file=sys.stderr, flush=True)
                    return 1

                msg = json.loads(raw)
                t = msg.get("type")

                if t == "frame_ack":
                    print(f"ACK:0x{msg.get('id', 0):X}:{msg.get('status','?')}", flush=True)
                    if wait_ack:
                        wait_ack = False

                elif t == "frames":
                    for f in msg["frames"]:
                        can_id   = f["id"]
                        dlc      = f["dlc"]
                        data_hex = f.get("data", "")
                        print(f"FRAME:{can_id:X}:{dlc}:{data_hex.upper()}", flush=True)
                        total += 1
                        if total >= expect:
                            break

                if total >= expect and not wait_ack:
                    break

    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr, flush=True)
        return 1

    print(f"RECEIVED:{total}", flush=True)
    return 0 if total >= expect else 1


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--url",       default="ws://127.0.0.1:19201")
    p.add_argument("--expect",    type=int, default=5,
                   help="Number of frames to receive (0 = just send)")
    p.add_argument("--send-id",   type=lambda x: int(x, 0), default=None,
                   help="CAN ID to send to server (hex/dec), triggers send_frame")
    p.add_argument("--send-data", default=None,
                   help="Hex bytes to send, e.g. '02 01 0C'")
    p.add_argument("--wait-ack",  action="store_true",
                   help="Wait for frame_ack before exiting")
    args = p.parse_args()

    rc = asyncio.run(run(args.url, args.expect, args.send_id, args.send_data, args.wait_ack))
    sys.exit(rc)


if __name__ == "__main__":
    main()

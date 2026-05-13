#!/usr/bin/env python3
"""
WebSocket server fixture for cross-language integration tests.

Starts a WS server, prints "READY:<port>" to stdout when accepting connections,
then waits for one client to connect. When the client connects, broadcasts
the requested frames (in MagistralaCAN4 JSON batch format) and exits.

Usage:
    python3 ws_server_fixture.py --port 19200 --frames 5
    python3 ws_server_fixture.py --port 19201 --token abc123 --frames 3  (WSS not yet — use WS)

Exit codes:
    0 — frames sent successfully
    1 — error
"""
import asyncio, json, sys, argparse, signal

try:
    import websockets
    from websockets.server import serve
except ImportError:
    sys.exit("pip install websockets")


def make_batch(frame_specs: list[dict]) -> str:
    return json.dumps({
        "type": "frames",
        "count": len(frame_specs),
        "frames": frame_specs,
    })


async def run(port: int, frame_specs: list[dict]) -> int:
    sent = asyncio.Event()

    async def handler(ws):
        try:
            await ws.send(make_batch(frame_specs))
            sent.set()
        except Exception as e:
            print(f"FIXTURE_ERROR: {e}", file=sys.stderr, flush=True)

    async with serve(handler, "127.0.0.1", port) as server:
        actual_port = list(server.sockets)[0].getsockname()[1]
        # Signal readiness to the C++ test that started this process
        print(f"READY:{actual_port}", flush=True)
        await asyncio.wait_for(sent.wait(), timeout=10.0)

    print("DONE", flush=True)
    return 0


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--port", type=int, default=0,
                   help="Port to listen on (0 = OS picks a free port)")
    p.add_argument("--ids",  nargs="+", type=lambda x: int(x, 0),
                   default=[0x100, 0x200, 0x300, 0x7DF, 0x18DAF110],
                   help="CAN IDs to send (hex or dec)")
    args = p.parse_args()

    frames = []
    for i, can_id in enumerate(args.ids):
        dlc  = min(8, i + 1)
        data = bytes(range(i, i + dlc))
        frames.append({
            "id":        can_id,
            "extended":  can_id > 0x7FF,
            "rtr":       False,
            "fd":        False,
            "dlc":       dlc,
            "timestamp": 1_000_000 + i,
            "data":      data.hex(),
        })

    rc = asyncio.run(run(args.port, frames))
    sys.exit(rc)


if __name__ == "__main__":
    main()

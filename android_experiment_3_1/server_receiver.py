#!/usr/bin/env python3
# server_receiver.py - "serwer domowy" dla Eksperymentu 3.1: przyjmuje POST z
# aplikacji Android (calosc pliku CSV z wynikami) i zapisuje na dysku, z
# unikalna nazwa (znacznik czasu odbioru), zeby kolejne wyslania z telefonu
# nie nadpisywaly wczesniejszych.
#
# Uzycie: python3 server_receiver.py [--port 8765] [--out-dir ./wyniki_odebrane]
#
# W aplikacji Android wpisz jako "Adres serwera domowego": <IP_TEGO_KOMPUTERA>:8765
# (IP sprawdz np. przez `ip addr` - musi to byc adres w tej samej sieci co telefon).

import argparse
import datetime
import http.server
import pathlib
import socketserver


class UploadHandler(http.server.BaseHTTPRequestHandler):
    out_dir: pathlib.Path = pathlib.Path("wyniki_odebrane")

    def do_POST(self):
        if self.path != "/upload":
            self.send_response(404)
            self.end_headers()
            return

        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length)

        self.out_dir.mkdir(parents=True, exist_ok=True)
        timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        out_file = self.out_dir / f"eksperyment_3_1_wyniki_{timestamp}.csv"
        out_file.write_bytes(body)

        print(f"[ODEBRANO] {len(body)} bajtow -> {out_file} (z {self.client_address[0]})")

        self.send_response(200)
        self.end_headers()
        self.wfile.write(b"OK")

    def log_message(self, format, *args):
        pass  # wlasny, zwiezlejszy log powyzej zamiast domyslnego http.server


def main():
    parser = argparse.ArgumentParser(description="Prosty serwer odbioru wynikow Eksperymentu 3.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--out-dir", type=str, default="wyniki_odebrane")
    args = parser.parse_args()

    UploadHandler.out_dir = pathlib.Path(args.out_dir)

    with socketserver.TCPServer(("0.0.0.0", args.port), UploadHandler) as httpd:
        print(f"[START] Nasluch na porcie {args.port}, zapis do '{args.out_dir}/'")
        print("Zatrzymaj przez Ctrl+C.")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\n[STOP] Zatrzymano.")


if __name__ == "__main__":
    main()

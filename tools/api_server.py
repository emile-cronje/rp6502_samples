#!/usr/bin/env python3
"""
Minimal REST API server using only the Python standard library.
Endpoints:
- GET  /health              -> {"status": "ok"}
- GET  /items               -> list all items
- GET  /items/<id>          -> single item
- POST /items               -> create item {"name": str, "value": str}
- DELETE /items/<id>        -> remove item
"""
import argparse
import json
import threading
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Dict, List, Optional, Tuple


def parse_id(path: str) -> Tuple[Optional[int], List[str]]:
    """Split the path and return (id, segments)."""
    segments = [seg for seg in path.split("/") if seg]
    if len(segments) == 2 and segments[0] == "items":
        try:
            return int(segments[1]), segments
        except ValueError:
            return None, segments
    return None, segments


class ItemStore:
    """Thread-safe in-memory store for demo items."""

    def __init__(self) -> None:
        self._items: Dict[int, Dict[str, str]] = {}
        self._next_id = 1
        self._lock = threading.Lock()

    def list_items(self) -> List[Dict[str, str]]:
        with self._lock:
            return list(self._items.values())

    def get_item(self, item_id: int) -> Optional[Dict[str, str]]:
        with self._lock:
            return self._items.get(item_id)

    def create_item(self, name: str, value: str) -> Dict[str, str]:
        with self._lock:
            item_id = self._next_id
            self._next_id += 1
            item = {"id": item_id, "name": name, "value": value}
            self._items[item_id] = item
            return item

    def delete_item(self, item_id: int) -> bool:
        with self._lock:
            return self._items.pop(item_id, None) is not None


class ApiHandler(BaseHTTPRequestHandler):
    store = ItemStore()

    def _send_json(self, status: HTTPStatus, payload: Dict) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _read_json(self) -> Optional[Dict]:
        length_header = self.headers.get("Content-Length")
        if not length_header:
            return None
        length = int(length_header)
        raw = self.rfile.read(length)
        try:
            return json.loads(raw.decode("utf-8"))
        except json.JSONDecodeError:
            return None

    def do_GET(self) -> None:  # noqa: N802 (BaseHTTPRequestHandler naming)
        item_id, segments = parse_id(self.path)
        if self.path == "/health":
            self._send_json(HTTPStatus.OK, {"status": "ok"})
            return
        if self.path == "/items":
            self._send_json(HTTPStatus.OK, self.store.list_items())
            return
        if len(segments) == 2 and segments[0] == "items" and item_id is not None:
            item = self.store.get_item(item_id)
            if item:
                self._send_json(HTTPStatus.OK, item)
            else:
                self._send_json(HTTPStatus.NOT_FOUND, {"error": "not found"})
            return
        self._send_json(HTTPStatus.NOT_FOUND, {"error": "unknown endpoint"})

    def do_POST(self) -> None:  # noqa: N802
        if self.path != "/items":
            self._send_json(HTTPStatus.NOT_FOUND, {"error": "unknown endpoint"})
            return
        payload = self._read_json()
        if not payload or "name" not in payload or "value" not in payload:
            self._send_json(HTTPStatus.BAD_REQUEST, {"error": "expected JSON with name and value"})
            return
        item = self.store.create_item(str(payload["name"]), str(payload["value"]))
        self._send_json(HTTPStatus.CREATED, item)

    def do_DELETE(self) -> None:  # noqa: N802
        item_id, segments = parse_id(self.path)
        if len(segments) == 2 and segments[0] == "items" and item_id is not None:
            deleted = self.store.delete_item(item_id)
            if deleted:
                self._send_json(HTTPStatus.NO_CONTENT, {})
            else:
                self._send_json(HTTPStatus.NOT_FOUND, {"error": "not found"})
            return
        self._send_json(HTTPStatus.NOT_FOUND, {"error": "unknown endpoint"})

    def log_message(self, fmt: str, *args) -> None:
        # Keep server output compact.
        print(f"[{self.log_date_time_string()}] {self.client_address[0]} {self.command} {self.path} -> "
              f"{args[0] if args else ''}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Minimal REST API server")
    parser.add_argument("--host", default="127.0.0.1", help="Host/IP to bind (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=8000, help="Port to listen on (default: 8000)")
    args = parser.parse_args()

    server = ThreadingHTTPServer((args.host, args.port), ApiHandler)
    print(f"Serving on http://{args.host}:{args.port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down...")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()

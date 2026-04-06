"""
Lirin Bot - Web UI Server
Supports multiple clients with per-PID state files.
Usage: python bot_ui.py [port] [state_file] [cmd_file]
       python bot_ui.py              -> auto-detect from C:\\lirin_bot_state_*.json
       python bot_ui.py 5555 C:\\lirin_bot_state_12345.json C:\\lirin_bot_cmd_12345.json
"""

import http.server
import json
import os
import sys
import glob
import mimetypes

UI_DIR = os.path.dirname(os.path.abspath(__file__))

# Will be set per instance
STATE_FILE = None
CMD_FILE = None
PORT = 5555


def find_state_files():
    """Find all bot state files and return list of (pid, port, state_file, cmd_file)."""
    instances = []
    for f in glob.glob(r"C:\lirin_bot_state_*.json"):
        try:
            pid = f.split("_")[-1].replace(".json", "")
            with open(f, "r") as fh:
                data = json.load(fh)
            port = data.get("port", 5555)
            cmd = f.replace("_state_", "_cmd_")
            instances.append((pid, port, f, cmd))
        except:
            pass
    # Also check default format
    old = r"C:\lirin_bot_state.json"
    if os.path.isfile(old) and not any(i[2] == old for i in instances):
        try:
            with open(old, "r") as fh:
                data = json.load(fh)
            port = data.get("port", 5555)
        except:
            port = 5555
        instances.append(("?", port, old, r"C:\lirin_bot_cmd.json"))
    return instances


class BotAPIHandler(http.server.BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        pass

    def _send_json(self, data, status=200):
        try:
            body = json.dumps(data).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(body)
        except (ConnectionAbortedError, ConnectionResetError, BrokenPipeError):
            pass

    def _send_file(self, filepath, content_type):
        try:
            with open(filepath, "rb") as f:
                content = f.read()
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(content)))
            self.end_headers()
            self.wfile.write(content)
        except FileNotFoundError:
            self.send_error(404, "File not found")
        except (ConnectionAbortedError, ConnectionResetError, BrokenPipeError):
            pass

    def do_GET(self):
        if self.path == "/api/state":
            self._handle_get_state()
        elif self.path == "/" or self.path == "/index.html":
            self._send_file(os.path.join(UI_DIR, "index.html"), "text/html; charset=utf-8")
        else:
            safe_path = self.path.lstrip("/")
            filepath = os.path.join(UI_DIR, safe_path)
            if os.path.isfile(filepath):
                ctype, _ = mimetypes.guess_type(filepath)
                self._send_file(filepath, ctype or "application/octet-stream")
            else:
                self.send_error(404, "Not found")

    def do_POST(self):
        if self.path == "/api/start":
            self._handle_command("start")
        elif self.path == "/api/stop":
            self._handle_command("stop")
        elif self.path == "/api/range":
            length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(length).decode() if length else "3000"
            self._handle_range(body.strip())
        elif self.path == "/api/fish_capture":
            self._handle_command("fish_capture")
        elif self.path == "/api/fish_done":
            self._handle_command("fish_done")
        elif self.path == "/api/fish_watch":
            self._handle_command("fish_watch")
        elif self.path == "/api/fish_start":
            self._handle_command("fish_start")
        elif self.path == "/api/fish_stop":
            self._handle_command("fish_stop")
        else:
            self.send_error(404, "Not found")

    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def _handle_get_state(self):
        global STATE_FILE, CMD_FILE
        # Auto-detect state file if not found yet
        if not STATE_FILE or not os.path.isfile(STATE_FILE):
            instances = find_state_files()
            for pid, p, sf, cf in instances:
                if p == PORT:
                    STATE_FILE = sf
                    CMD_FILE = cf
                    break
        if not STATE_FILE or not os.path.isfile(STATE_FILE):
            self._send_json({
                "running": False, "kills": 0, "totalQueued": 0,
                "state": 0, "currentVID": 0, "playerX": 0, "playerY": 0,
                "pickup": False, "entities": [], "queue": [],
                "log": ["[ui] Waiting for bot..."]
            })
            return
        try:
            with open(STATE_FILE, "r", encoding="utf-8") as f:
                data = json.load(f)
            self._send_json(data)
        except (json.JSONDecodeError, IOError) as e:
            self._send_json({
                "running": False, "kills": 0, "totalQueued": 0,
                "state": 0, "currentVID": 0, "playerX": 0, "playerY": 0,
                "pickup": False, "entities": [], "queue": [],
                "log": [f"[ui] Error: {e}"]
            })

    def _handle_command(self, command):
        try:
            with open(CMD_FILE, "w", encoding="utf-8") as f:
                json.dump({"command": command}, f)
            self._send_json({"ok": True, "command": command})
        except IOError as e:
            self._send_json({"ok": False, "error": str(e)}, status=500)

    def _handle_range(self, value):
        try:
            with open(CMD_FILE, "w", encoding="utf-8") as f:
                json.dump({"range": float(value)}, f)
            self._send_json({"ok": True, "range": value})
        except (IOError, ValueError) as e:
            self._send_json({"ok": False, "error": str(e)}, status=500)


def main():
    global STATE_FILE, CMD_FILE, PORT

    if len(sys.argv) >= 4:
        PORT = int(sys.argv[1])
        STATE_FILE = sys.argv[2]
        CMD_FILE = sys.argv[3]
    elif len(sys.argv) >= 2:
        PORT = int(sys.argv[1])
        # Auto-detect state file for this port
        instances = find_state_files()
        for pid, p, sf, cf in instances:
            if p == PORT:
                STATE_FILE = sf
                CMD_FILE = cf
                break
    else:
        # Auto-detect: show available instances
        instances = find_state_files()
        if not instances:
            print("Bot instance bulunamadi. Oyunu ac ve F3'e bas...")
            wait_for_f3_selection([])
        elif len(instances) == 1:
            pid, PORT, STATE_FILE, CMD_FILE = instances[0]
            print(f"Auto-selected: PID {pid} port {PORT}")
        else:
            wait_for_f3_selection(instances)

    start_server()


def wait_for_f3_selection(instances):
    """Oyunda F3'e basilmasini bekle, o client'i otomatik sec."""
    global STATE_FILE, CMD_FILE, PORT
    import time

    select_file = r"C:\lirin_bot_selected.txt"

    print("Multiple bot instances found:")
    for pid, port, sf, cf in instances:
        print(f"  PID {pid} port {port}")
    print(f"\nOyunda F3'e bas - o client otomatik secilecek...")

    # Eski sinyal dosyasini temizle
    try:
        os.remove(select_file)
    except OSError:
        pass

    while True:
        time.sleep(0.3)
        # F3 sinyal dosyasini kontrol et
        if os.path.isfile(select_file):
            try:
                with open(select_file, "r") as f:
                    selected_pid = f.read().strip()
                os.remove(select_file)
                # PID ile eslestir
                for pid, port, sf, cf in instances:
                    if pid == selected_pid:
                        PORT = port
                        STATE_FILE = sf
                        CMD_FILE = cf
                        print(f"\nF3 ile secildi: PID {pid} port {port}")
                        return
                # PID bulunamadi, yeni instance olabilir - dosyalardan oku
                PORT = 5555 + (int(selected_pid) % 100)
                STATE_FILE = rf"C:\lirin_bot_state_{selected_pid}.json"
                CMD_FILE = rf"C:\lirin_bot_cmd_{selected_pid}.json"
                print(f"\nF3 ile secildi: PID {selected_pid} port {PORT}")
                return
            except Exception:
                pass

        # Yeni instance'lar gelebilir, listeyi guncelle
        new_instances = find_state_files()
        if len(new_instances) != len(instances):
            instances = new_instances
            print(f"  ({len(instances)} instance bulundu)")


class NoReuseHTTPServer(http.server.HTTPServer):
    allow_reuse_address = False

def start_server():
    global PORT
    # Port 0 ise veya dolu ise bos port bul
    server = None
    for p in range(5561, 5580):
        try:
            server = NoReuseHTTPServer(("0.0.0.0", p), BotAPIHandler)
            PORT = p
            break
        except OSError:
            continue
    if not server:
        print("[!] No free port found (5560-5580)")
        return
    print(f"\n=== Lirin Bot UI ===")
    print(f"http://localhost:{PORT}")
    print(f"State: {STATE_FILE}")
    print(f"Cmd:   {CMD_FILE}")
    print(f"Ctrl+C to stop.\n")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")
        server.server_close()


if __name__ == "__main__":
    main()

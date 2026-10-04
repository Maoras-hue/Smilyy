# SmileRAT

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
- **TCP C2 with length-prefixed framing** — 4-byte big-endian header + payload
- **AES-256-GCM encryption** — all traffic after handshake is encrypted
- **Multi-client C2** — server tracks sessions, per-client threads
- **Reconnect with jitter** — client retries on disconnect with random delay
- **Windows-native crypto** — uses BCrypt, no external crypto libraries

### Persistence
- **Registry `Run` key** — HKCU autorun on logon
- **Startup folder** — copy to `%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup`
- **Scheduled Task** — `schtasks /Create` (SYSTEM-level if admin, falls back to user-level)
- **Self-copy to `%APPDATA%`** — installs as `OneDriveSyncHelper.exe`
- **Mutex guard** — prevents duplicate instances

### Reconnaissance
- **System info** — OS version (via `RtlGetVersion`), CPU count, RAM
- **Process list / kill** — via `CreateToolhelp32Snapshot`
- **Network adapter info** — via `GetAdaptersInfo`
- **Network scanner** — ping sweep, port scan, TTL-based OS fingerprinting
- **File search** — recursive, pattern-matched
- **Password stealer**
  - Chrome / Edge / Brave / Opera — decrypted via DPAPI + AES-GCM (uses embedded SQLite)
  - WiFi credentials — via `netsh wlan show profile`
  - Windows Credential Manager — via `CredEnumerate`

### Remote Control
- **Shell command execution** — `cmd.exe` via `_popen`, output captured
- **File operations** — list, copy, move, delete, mkdir, rmdir
- **File download** — victim → operator, chunked, base64-encoded
- **File upload** — operator → victim, chunked, base64-encoded
- **Mouse control** — `SetCursorPos` + `SendInput` for clicks and scroll
- **Keyboard control** — `SendInput` for key press/release
- **Live remote desktop viewer** — real-time JPEG screen stream displayed in a Tkinter window on the operator side. Mouse and keyboard events from the viewer window are forwarded to the victim.

### Surveillance
- **Screenshot capture** — GDI BitBlt → BMP
- **Webcam capture** — Media Foundation, single JPEG frames at configurable quality
- **Microphone recording** — WASAPI, PCM WAV (16-bit, device default rate)
- **Keylogger** — `SetWindowsHookEx` low-level keyboard hook
- **Clipboard monitor** — reads clipboard on interval

### Evasion
- **AMSI patch** — patches `AmsiScanBuffer` in `amsi.dll` (affects the RAT process only)
- **ETW patch** — patches `EtwEventWrite` in `ntdll.dll` (affects the RAT process only)
- **Anti-debug** — `IsDebuggerPresent`, `CheckRemoteDebuggerPresent`
- **Sandbox detection** — RAM size, CPU cores, VM artifacts, analysis tool processes, username patterns, BIOS strings
- **Hook detection** — parses PE headers, walks IAT for inline hooks

### Crypter
- **AES-256 payload encryption** — via Windows CryptoAPI
- **Stub loader template** — decrypt-and-execute in memory. Stub ships with placeholder bytes; the `build_crypted.py` script must be run to embed a real payload.

---

## What Is Partially Implemented

- **Memory obfuscation** — AES-based `SecureString` / `SecureVar` templates exist but are not wired into the main flow
- **Proxy support** — IE proxy registry write works; the internal SOCKS/HTTP proxy server is a placeholder
- **DNS tunneling** — packet construction and Base64 encoding work, but it is not wired in as a C2 fallback
- **Firefox password extraction** — requires NSS libraries; currently returns no results
- **Windows Defender bypass** — the RAT attempts to disable Defender via registry and service control. On modern Windows with Tamper Protection enabled (default since Win10 20H1), these attempts are blocked by design. Works only when Tamper Protection is off.

---

## What Is Not Implemented

- USB device monitoring
- Real process hiding from Task Manager (requires kernel driver)
- Working Defender bypass on Windows with Tamper Protection enabled (requires a kernel driver)
- PNG screenshot output (current output is BMP)

---

## Project Structure

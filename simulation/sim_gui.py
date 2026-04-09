import os
import sys
import time
import random
import threading
import subprocess
import queue
import hashlib
import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox

PYTHON = sys.executable
UNBUFFERED = ["-u"] 

# ---------- helpers ----------
def generate_fake_file(path: str, size_bytes: int, seed: int):
    rng = random.Random(seed)
    with open(path, "wb") as f:
        f.write(bytes(rng.getrandbits(8) for _ in range(size_bytes)))

def sha256_of_file(path: str, chunk: int = 1024 * 1024) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(chunk)
            if not b:
                break
            h.update(b)
    return h.hexdigest()

def first_diff_offset(a_path: str, b_path: str, chunk: int = 64 * 1024) -> int | None:
    """Return byte offset of first difference, or None if identical."""
    with open(a_path, "rb") as fa, open(b_path, "rb") as fb:
        offset = 0
        while True:
            a = fa.read(chunk)
            b = fb.read(chunk)
            if not a and not b:
                return None
            if a != b:
                # Find exact position in this chunk
                m = min(len(a), len(b))
                for i in range(m):
                    if a[i] != b[i]:
                        return offset + i
                # One file ended or lengths differ within chunk
                return offset + m
            offset += len(a)

# ---------- subprocess log pipe ----------
class ProcPipe:
    def __init__(self, name: str, widget: scrolledtext.ScrolledText, status_cb=None):
        self.name = name
        self.widget = widget
        self.proc = None
        self.q = queue.Queue()
        self.status_cb = status_cb

    def start(self, cmd):
        self.stop()
        self._log(f"\n--- {self.name} START ---\n$ {' '.join(cmd)}\n")
        self.proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,              # line-buffered (paired with -u)
            universal_newlines=True,
        )
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        assert self.proc and self.proc.stdout
        for line in self.proc.stdout:
            self.q.put(line)
        self.q.put(None)

    def pump(self):
        while True:
            try:
                item = self.q.get_nowait()
            except queue.Empty:
                break

            if item is None:
                code = self.proc.poll() if self.proc else None
                self._log(f"--- {self.name} EXIT (code={code}) ---\n")
                if self.status_cb:
                    self.status_cb(f"{self.name} exited (code={code})")
                break

            self._log(item)

    def _log(self, text: str):
        ts = time.strftime("%H:%M:%S")
        line = f"[{ts}] {text}"

        start = self.widget.index("end-1c")
        self.widget.insert("end", line)
        end = self.widget.index("end-1c")

        u = line.upper()
        tag = None

        # ----- headers from the GUI itself -----
        if "--- RX START ---" in u or "--- TX START ---" in u or "--- RX EXIT" in u or "--- TX EXIT" in u:
            tag = "header"

        # ----- RX-side patterns -----
        elif "RX:" in u and "ACK DROPPED" in u:
            tag = "ack_drop"
        elif "RX:" in u and ("ACK(DUPLICATE)" in u or "DUPLICATE PACKET NUMBER" in u):
            tag = "duplicate"

        # ----- TX-side patterns -----
        elif "TX:" in u and " PCK " in u and "DROPPED" in u:
            tag = "data_drop"
        elif "TX:" in u and " TIMEOUT" in u:
            tag = "timeout"
        elif "FAILED AFTER" in u or "STOPPING EARLY" in u:
            tag = "fail"
        elif "DELIVERED SUCCESFULLY" in u:
            tag = "success"

        if tag:
            self.widget.tag_add(tag, start, end)

        self.widget.see("end")


    def stop(self):
        if self.proc and self.proc.poll() is None:
            try:
                self.proc.terminate()
            except Exception:
                pass
        self.proc = None

# ---------- UI ----------
class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Stop-and-Wait Simulator Control Panel")
        self.geometry("1150x760")

        self._theme_blue()
        self._vars()
        self._build_ui()

        self.rx = ProcPipe("RX", self.rx_log, status_cb=self._set_status)
        self.tx = ProcPipe("TX", self.tx_log, status_cb=self._set_status)

        self.after(50, self._pump)

    def _theme_blue(self):
        # light blue background + blue accents
        self.BG = "#EAF4FF"
        self.ACCENT = "#2B6CB0"
        self.ACCENT_LIGHT = "#CFE8FF"

        self.configure(bg=self.BG)
        style = ttk.Style()

        # Use a theme that supports ttk styling well on mac/windows/linux
        try:
            style.theme_use("clam")
        except Exception:
            pass

        style.configure("TFrame", background=self.BG)
        style.configure("TLabelframe", background=self.BG, bordercolor=self.ACCENT)
        style.configure("TLabelframe.Label", background=self.BG, foreground=self.ACCENT, font=("TkDefaultFont", 11, "bold"))

        style.configure("TLabel", background=self.BG, foreground="#0B1F33")
        style.configure("Header.TLabel", font=("TkDefaultFont", 14, "bold"), foreground=self.ACCENT)

        style.configure("TButton",
                        padding=(10, 6),
                        background=self.ACCENT_LIGHT,
                        foreground="#0B1F33")
        style.map("TButton",
                  background=[("active", "#B7DCFF"), ("pressed", "#A6D3FF")])

        style.configure("Accent.TButton",
                        padding=(10, 6),
                        background=self.ACCENT,
                        foreground="white")
        style.map("Accent.TButton",
                  background=[("active", "#225B96"), ("pressed", "#1D4E83")],
                  foreground=[("active", "white"), ("pressed", "white")])

        style.configure("TEntry", padding=(6, 4))
        style.configure("Status.TLabel", background=self.BG, foreground=self.ACCENT, font=("TkDefaultFont", 13, "bold"),)


    def _vars(self):
        # Core
        self.file_path = tk.StringVar(value="fake_data.bin")
        self.file_size = tk.IntVar(value=5000)
        self.seed = tk.IntVar(value=12)

        # TX knobs
        self.timeout_ms = tk.IntVar(value=1000)
        self.retries = tk.IntVar(value=5)
        self.loss_data = tk.DoubleVar(value=0.15)
        self.data_dmin = tk.IntVar(value=300)
        self.data_dmax = tk.IntVar(value=400)

        # RX knobs
        self.loss_ack = tk.DoubleVar(value=0.08)
        self.ack_dmin = tk.IntVar(value=25)
        self.ack_dmax = tk.IntVar(value=40)

        # Ports
        self.rx_port = tk.IntVar(value=9000)
        self.ack_port = tk.IntVar(value=9001)

        self.status_var = tk.StringVar(value="Ready.")

    def _build_ui(self):
        # Header
        header = ttk.Frame(self, padding=(12, 10))
        header.pack(fill="x")
        ttk.Label(header, text="Stop-and-Wait Simulation", style="Header.TLabel").pack(side="left")

        # Controls card
        controls = ttk.Labelframe(self, text="Simulation Controls", padding=12)
        controls.pack(fill="x", padx=12, pady=(0, 10))

        form = ttk.Frame(controls)
        form.pack(fill="x")

        # Helper to make compact label+entry pairs
        def add_field(parent, row, col, label, var, width=10):
            cell = ttk.Frame(parent)
            cell.grid(row=row, column=col, sticky="w", padx=(0, 18), pady=6)
            ttk.Label(cell, text=label).pack(side="left")
            ttk.Entry(cell, textvariable=var, width=width).pack(side="left", padx=(8, 0))
            return cell

        # Row 0
        add_field(form, 0, 0, "Input file", self.file_path, width=20)
        add_field(form, 0, 1, "Bytes", self.file_size, width=8)
        add_field(form, 0, 2, "Seed", self.seed, width=8)

        # Row 1 (ports)
        add_field(form, 1, 0, "RX Port", self.rx_port, width=8)
        add_field(form, 1, 1, "ACK Port", self.ack_port, width=8)

        # Row 2 (TX)
        add_field(form, 2, 3, "Timeout (ms)", self.timeout_ms, width=8)
        add_field(form, 2, 4, "Retries", self.retries, width=6)
        add_field(form, 2, 0, "Loss DATA", self.loss_data, width=8)
        add_field(form, 2, 1, "DATA delay min", self.data_dmin, width=6)
        add_field(form, 2, 2, "max", self.data_dmax, width=6)

        # Row 3 (RX)
        add_field(form, 3, 0, "Loss ACK", self.loss_ack, width=8)
        add_field(form, 3, 1, "ACK delay min", self.ack_dmin, width=6)
        add_field(form, 3, 2, "max", self.ack_dmax, width=6)

        # Buttons row
        btns = ttk.Frame(controls)
        btns.pack(fill="x", pady=(8, 0))

        ttk.Button(btns, text="Generate File", command=self.on_generate).pack(side="left")
        ttk.Button(btns, text="Run Simulation", style="Accent.TButton", command=self.on_run).pack(side="left", padx=10)
        ttk.Button(btns, text="Stop", command=self.on_stop).pack(side="left")
        ttk.Button(btns, text="Clear Logs", command=self.on_clear).pack(side="left", padx=10)
        ttk.Button(btns, text="Compare Files", command=self.on_compare).pack(side="right")
        ttk.Button(btns, text="Defaults", command=self.on_defaults).pack(side="left", padx=10)


        # Logs
        panes = ttk.PanedWindow(self, orient="horizontal")
        panes.pack(fill="both", expand=True, padx=12, pady=(0, 10))

        rx_frame = ttk.Labelframe(panes, text="Receiver Log (RX)", padding=8)
        tx_frame = ttk.Labelframe(panes, text="Transmitter Log (TX)", padding=8)
        panes.add(rx_frame, weight=1)
        panes.add(tx_frame, weight=1)

        self.rx_log = scrolledtext.ScrolledText(rx_frame, height=26)
        self.rx_log.pack(fill="both", expand=True)

        self.tx_log = scrolledtext.ScrolledText(tx_frame, height=26)
        self.tx_log.pack(fill="both", expand=True)

        def setup_log_tags(w: scrolledtext.ScrolledText):
            # Monospace helps readability
            w.configure(font=("Menlo", 9))

            # Drops
            w.tag_configure("ack_drop",  background="#FFD6E0", foreground="#7A0018")  # pink/red
            w.tag_configure("data_drop", background="#FFE3C2", foreground="#7A3E00")  # orange
            w.tag_configure("timeout",   background="#DCEEFF", foreground="#003A7A")  # light blue

            # Optional extras
            w.tag_configure("duplicate", background="#E9E2FF", foreground="#3B1E7A")  # purple-ish
            w.tag_configure("success",   foreground="#1AA152")                        # green text
            w.tag_configure("fail",      background="#FFCDD2", foreground="#7A0000")  # stronger red
            w.tag_configure("header",    foreground="#2B6CB0", font=("Menlo", 9, "bold"))

        setup_log_tags(self.rx_log)
        setup_log_tags(self.tx_log)


        # Status bar
        status = ttk.Frame(self, padding=(12, 6))
        status.pack(fill="x")
        ttk.Label(status, textvariable=self.status_var, style="Status.TLabel").pack(side="left")


    def _set_status(self, msg: str):
        self.status_var.set(msg)

    def on_generate(self):
        path = self.file_path.get().strip()
        n = int(self.file_size.get())
        seed = int(self.seed.get())

        if n < 0:
            messagebox.showerror("Invalid size", "File size must be >= 0")
            return

        generate_fake_file(path, n, seed)
        self._set_status(f"Generated {path} ({n} bytes) with seed={seed}")
        self.tx_log.insert("end", f"[{time.strftime('%H:%M:%S')}] Generated {path} ({n} bytes)\n")
        self.tx_log.see("end")

    
    def on_defaults(self):
        self.file_path.set("fake_data.bin")
        self.file_size.set(5000)
        self.seed.set(12)

        self.timeout_ms.set(1000)
        self.retries.set(5)
        self.loss_data.set(0.15)
        self.data_dmin.set(300)
        self.data_dmax.set(400)

        self.loss_ack.set(0.08)
        self.ack_dmin.set(25)
        self.ack_dmax.set(40)

        self.rx_port.set(9000)
        self.ack_port.set(9001)

        self._set_status("Defaults restored.")

    
    
    
    def rx_cmd(self):
        return [
            PYTHON, "-u", "rx.py",
            "--rx-port", str(self.rx_port.get()),
            "--tx-ack-port", str(self.ack_port.get()),
            "--seed", str(self.seed.get()),
            "--loss-ack", str(self.loss_ack.get()),
            "--ack-delay-min", str(self.ack_dmin.get()),
            "--ack-delay-max", str(self.ack_dmax.get()),
            "--out", "received.bin",
        ]

    def tx_cmd(self):
        return [
            PYTHON, "-u", "tx.py", self.file_path.get().strip(),
            "--rx-port", str(self.rx_port.get()),
            "--tx-ack-port", str(self.ack_port.get()),
            "--seed", str(self.seed.get()),
            "--loss-data", str(self.loss_data.get()),
            "--data-delay-min", str(self.data_dmin.get()),
            "--data-delay-max", str(self.data_dmax.get()),
            "--timeout-ms", str(self.timeout_ms.get()),
            "--retries", str(self.retries.get()),
        ]

    def on_run(self):
        inp = self.file_path.get().strip()
        if not os.path.exists(inp):
            if messagebox.askyesno("Missing input file", f"'{inp}' not found. Generate it now?"):
                self.on_generate()
            else:
                return

        self._set_status("Starting RX…")
        self.rx.start(self.rx_cmd())
        self.after(200, lambda: (self._set_status("Starting TX…"), self.tx.start(self.tx_cmd())))

    def on_stop(self):
        self.rx.stop()
        self.tx.stop()
        self._set_status("Stopped.")

    def on_clear(self):
        self.rx_log.delete("1.0", "end")
        self.tx_log.delete("1.0", "end")
        self._set_status("Logs cleared.")

    def on_compare(self):
        a = self.file_path.get().strip()
        b = "received.bin"

        if not os.path.exists(a):
            messagebox.showerror("Compare failed", f"Input file not found: {a}")
            return
        if not os.path.exists(b):
            messagebox.showerror("Compare failed", f"Received file not found: {b}\nRun a simulation first.")
            return

        try:
            size_a = os.path.getsize(a)
            size_b = os.path.getsize(b)
            hash_a = sha256_of_file(a)
            hash_b = sha256_of_file(b)
        except Exception as e:
            messagebox.showerror("Compare failed", str(e))
            return

        if size_a == size_b and hash_a == hash_b:
            self._set_status("✅ Files match perfectly.")
            messagebox.showinfo("Compare Files", f"✅ MATCH\n\n{a} == {b}\n\nSize: {size_a} bytes\nSHA-256: {hash_a}")
        else:
            off = first_diff_offset(a, b)
            self._set_status("❌ Files do not match.")
            msg = (
                f"❌ MISMATCH\n\n"
                f"{a} size={size_a}  sha256={hash_a[:16]}…\n"
                f"{b} size={size_b}  sha256={hash_b[:16]}…\n\n"
            )
            if off is None:
                msg += "Different hashes, but couldn't locate first differing byte (unexpected)."
            else:
                msg += f"First difference at byte offset: {off}"
            messagebox.showwarning("Compare Files", msg)

    def _pump(self):
        self.rx.pump()
        self.tx.pump()
        self.after(50, self._pump)

if __name__ == "__main__":
    App().mainloop()

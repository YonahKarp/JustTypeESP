"""Copy JustType documents over its CH340 USB serial connection.

Double-click Download Documents.cmd for the folder picker, or run:
python tools/download_documents.py --port COM6 --output C:/Backups
Requires pyserial (included in the ESP-IDF Python environment).
"""
import argparse
from datetime import datetime
from pathlib import Path, PurePosixPath
import secrets
import time
import zlib


class Receiver:
    def __init__(self, destination, progress=print):
        self.destination = Path(destination)
        self.progress = progress
        self.file = None
        self.target = None
        self.partial = None
        self.size = self.offset = self.crc = self.files = 0

    def accept(self, message):
        parts = message.split()
        if not parts:
            raise ValueError("Empty transfer frame")
        kind = parts[0]
        if kind == "BEGIN":
            if self.file:
                raise ValueError("Device restarted during transfer")
        elif kind == "ERROR":
            raise RuntimeError("Device could not export documents: " + " ".join(parts[1:]))
        elif kind == "FILE":
            if self.file or len(parts) != 3:
                raise ValueError("Unexpected file header")
            name = bytes.fromhex(parts[1]).decode("utf-8")
            rel = PurePosixPath(name)
            if rel.is_absolute() or not rel.parts or any(p in (".", "..") or ":" in p or "\\" in p for p in rel.parts):
                raise ValueError("Unsafe document path")
            self.target = self.destination.joinpath(*rel.parts)
            if not self.target.resolve().is_relative_to(self.destination.resolve()):
                raise ValueError("Document path escapes destination")
            self.target.parent.mkdir(parents=True, exist_ok=True)
            if self.target.exists():
                raise FileExistsError(self.target)
            self.partial = self.target.with_name(self.target.name + ".partial")
            self.file = self.partial.open("xb")
            self.size = int(parts[2])
            self.offset = self.crc = 0
            self.progress(f"Copying {name} ({self.size:,} bytes)")
        elif kind == "DATA":
            if not self.file or len(parts) != 3 or int(parts[1]) != self.offset:
                raise ValueError("Missing or out-of-order data")
            data = bytes.fromhex(parts[2])
            if self.offset + len(data) > self.size:
                raise ValueError("Too much data")
            self.file.write(data)
            self.crc = zlib.crc32(data, self.crc)
            self.offset += len(data)
        elif kind == "END":
            if not self.file or self.offset != self.size or self.crc != int(parts[1]):
                raise ValueError("Document size/checksum verification failed")
            self.file.close()
            self.file = None
            self.partial.rename(self.target)
            self.partial = None
            self.files += 1
        elif kind == "DONE":
            if self.file:
                raise ValueError("Incomplete document")
            return True
        else:
            raise ValueError("Unknown transfer frame")
        return False

    def close(self):
        if self.file:
            self.file.close()
            self.file = None
        if self.partial:
            self.partial.unlink(missing_ok=True)


def download(port_name, output, progress=print):
    import serial
    folder = Path(output) / ("JustType-" + datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + secrets.token_hex(2))
    folder.mkdir(parents=True)
    token = secrets.token_hex(8)
    prefix = ("@JT " + token + " ").encode()
    receiver = Receiver(folder, progress)
    try:
        port = serial.Serial(baudrate=115200, timeout=1, write_timeout=5)
        port.dtr = False
        port.rts = False
        port.port = port_name
        with port:
            started = False
            deadline = time.monotonic() + 30
            next_request = 0
            while True:
                now = time.monotonic()
                if now > deadline:
                    raise TimeoutError("Device stopped responding. Check USB and close serial monitors.")
                if not started and now >= next_request:
                    port.write(("JTEXPORT " + token + "\n").encode())
                    next_request = now + 3
                line = port.readline()
                index = line.find(prefix)
                if index < 0:
                    continue
                message = line[index + len(prefix):].decode("ascii").strip()
                if not started:
                    if message != "BEGIN":
                        continue
                    started = True
                deadline = time.monotonic() + 30
                if receiver.accept(message):
                    return folder, receiver.files
    finally:
        receiver.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.output:
        folder, count = download(args.port or "COM6", args.output)
        print(f"Verified {count} documents in {folder}")
        return
    import threading
    import queue
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk
    from serial.tools import list_ports
    root = tk.Tk()
    root.title("Download JustType Documents")
    root.geometry("520x220")
    ttk.Label(root, text="Copy text files from JustType to your computer.").pack(pady=12)
    ports = list(list_ports.comports())
    preferred = next((p.device for p in ports if p.vid == 0x1A86), ports[0].device if ports else "COM6")
    port_name = tk.StringVar(value=args.port or preferred)
    ttk.Combobox(root, textvariable=port_name, values=[p.device for p in ports]).pack()
    status = tk.StringVar(value="Connect JustType by USB, then choose a destination.")
    ttk.Label(root, textvariable=status, wraplength=490).pack(pady=12)
    events = queue.Queue()
    busy = False

    def start():
        nonlocal busy
        destination = filedialog.askdirectory(title="Choose where to save JustType documents")
        if not destination:
            return
        selected_port = port_name.get()
        busy = True
        button.config(state="disabled")
        status.set("Connecting... Please leave USB connected.")

        def worker():
            try:
                result = download(selected_port, destination, lambda text: events.put(("progress", text)))
                events.put(("done", result))
            except Exception as error:
                events.put(("error", str(error)))
        threading.Thread(target=worker, daemon=True).start()

    def update():
        nonlocal busy
        while not events.empty():
            kind, value = events.get()
            if kind == "progress":
                status.set(value)
            else:
                busy = False
                button.config(state="normal")
                if kind == "done":
                    folder, count = value
                    status.set(f"Copied and verified {count} documents.")
                    messagebox.showinfo("Download complete", f"Saved {count} documents to:\n{folder}")
                    import os
                    os.startfile(folder)
                else:
                    status.set("Download failed. Completed files remain in the backup folder.")
                    messagebox.showerror("Download failed", value)
        root.after(100, update)

    button = ttk.Button(root, text="Download documents…", command=start)
    button.pack()
    root.protocol("WM_DELETE_WINDOW", lambda: messagebox.showinfo("Transfer in progress", "Wait for the download to finish before closing.") if busy else root.destroy())
    update()
    root.mainloop()


if __name__ == "__main__":
    main()

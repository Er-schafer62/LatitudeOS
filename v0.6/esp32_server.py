#!/usr/bin/env python3
"""
esp32_server.py — Virtual ESP32 file server for MyOS

QEMU listens on TCP port 4444 (server,nowait).
This script connects TO QEMU as a client when you run it.

Usage:
  1. Start QEMU first:   build.bat run
  2. Then run server:    python esp32_server.py
     (or leave it running — it reconnects automatically)
  3. In MyOS shell:      get HELLO.TXT

Files are served from the 'server_files' folder next to this script.

Protocol:
  OS  -> Server:  "GET <filename>\n"
  Server -> OS:   uint32 LE file size  (0xFFFFFFFF = not found)
                  N bytes file data
"""

import socket
import struct
import os
import time

HOST       = '127.0.0.1'
PORT       = 4444
FILES_DIR  = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'server_files')
NOT_FOUND  = struct.pack('<I', 0xFFFFFFFF)

def ensure_files_dir():
    if not os.path.exists(FILES_DIR):
        os.makedirs(FILES_DIR)
        print(f"Created: {FILES_DIR}")
        with open(os.path.join(FILES_DIR, 'HELLO.TXT'), 'w') as f:
            f.write("Hello from the virtual ESP32 server!\r\n")
            f.write("This file was fetched over serial.\r\n")
        with open(os.path.join(FILES_DIR, 'INFO.TXT'), 'w') as f:
            f.write("MyOS Network Filesystem\r\n")
            f.write("Protocol: GET <filename> over UART 9600 8N1\r\n")
        print("Sample files created.")

def list_files():
    files = os.listdir(FILES_DIR)
    if files:
        print(f"  Serving: {', '.join(files)}")
    else:
        print(f"  No files in server_files\\ — add files there.")

def handle_connection(sock):
    print("[+] Connected to QEMU (MyOS serial port)")
    buf = b""
    sock.settimeout(1.0)

    try:
        while True:
            # Read until we have a newline
            try:
                chunk = sock.recv(256)
                if not chunk:
                    print("[-] QEMU disconnected.")
                    return
                buf += chunk
            except socket.timeout:
                continue

            while b'\n' in buf:
                line, buf = buf.split(b'\n', 1)
                line = line.strip().decode('ascii', errors='ignore')
                if not line:
                    continue
                print(f"[>] '{line}'")

                cmd = line.upper()

                # ── LIST ─────────────────────────────────────────────────────
                if cmd == 'LIST':
                    files = os.listdir(FILES_DIR)
                    listing = '\n'.join(files) + ('\n' if files else '')
                    data = listing.encode('ascii')
                    print(f"[<] Sending file list ({len(files)} files)")
                    sock.sendall(struct.pack('<I', len(data)))
                    sock.sendall(data)
                    continue


                # ── PUT ───────────────────────────────────────────────────────
                if cmd.startswith('PUT '):
                    filename = os.path.basename(line[4:].strip())
                    if not filename:
                        sock.sendall(b'E')
                        continue

                    # Read 4-byte file size
                    size_bytes = b""
                    while len(size_bytes) < 4:
                        size_bytes += sock.recv(4 - len(size_bytes))
                    file_size = struct.unpack('<I', size_bytes)[0]

                    # Read file data
                    data = b""
                    while len(data) < file_size:
                        chunk = sock.recv(min(4096, file_size - len(data)))
                        if not chunk:
                            break
                        data += chunk

                    if len(data) != file_size:
                        print(f"[!] PUT {filename}: expected {file_size} bytes, got {len(data)}")
                        sock.sendall(b'E')
                        continue

                    filepath = os.path.join(FILES_DIR, filename)
                    try:
                        with open(filepath, 'wb') as f:
                            f.write(data)
                        print(f"[<] Saved {filename} ({file_size} bytes)")
                        sock.sendall(b'K')
                    except Exception as e:
                        print(f"[!] Failed to save {filename}: {e}")
                        sock.sendall(b'E')
                    continue

                # ── GET ───────────────────────────────────────────────────────
                if not cmd.startswith('GET '):
                    continue

                filename = os.path.basename(line[4:].strip())
                filepath = os.path.join(FILES_DIR, filename)

                # Case-insensitive match
                if not os.path.exists(filepath):
                    for f in os.listdir(FILES_DIR):
                        if f.upper() == filename.upper():
                            filepath = os.path.join(FILES_DIR, f)
                            break

                if not os.path.exists(filepath):
                    print(f"[!] Not found: {filename}")
                    sock.sendall(NOT_FOUND)
                    continue

                with open(filepath, 'rb') as f:
                    data = f.read()

                print(f"[<] Sending {filename} ({len(data)} bytes)")
                sock.sendall(struct.pack('<I', len(data)))
                sock.sendall(data)
                print(f"[<] Done")

    except (ConnectionResetError, BrokenPipeError, OSError):
        print("[-] Connection lost.")

def main():
    ensure_files_dir()
    print(f"\n=== MyOS Virtual ESP32 File Server ===")
    print(f"Connecting to QEMU on {HOST}:{PORT}")
    list_files()
    print(f"\nStart QEMU first with:  build.bat run")
    print(f"Then in MyOS shell:     get HELLO.TXT")
    print(f"Press Ctrl+C to stop.\n")

    while True:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect((HOST, PORT))
            handle_connection(sock)
            sock.close()
        except ConnectionRefusedError:
            print("Waiting for QEMU... (start QEMU with 'build.bat run')")
            time.sleep(2)
        except KeyboardInterrupt:
            print("\nServer stopped.")
            break
        except Exception as e:
            print(f"Error: {e}")
            time.sleep(2)

if __name__ == '__main__':
    main()
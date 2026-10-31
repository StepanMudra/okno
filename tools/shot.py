"""Grabs screenshots from a running Azahar emulator over its RPC port (UDP 45987).

Usage: python3 tools/shot.py OUT_PREFIX [count] [interval_seconds]
Writes OUT_PREFIX_00.png, OUT_PREFIX_01.png, ...
"""
import random
import socket
import struct
import sys
import time

PORT = 45987
VERSION = 2
TAKE, READ = 5, 6
CHUNK = 32 * 1024


def request(sock, rtype, data, timeout=30):
    rid = random.getrandbits(32)
    sock.settimeout(timeout)
    sock.sendto(struct.pack("IIII", VERSION, rid, rtype, len(data)) + data, ("127.0.0.1", PORT))
    while True:
        raw = sock.recv(CHUNK + 16)
        ver, got_id, got_type, size = struct.unpack("IIII", raw[:16])
        if ver == VERSION and got_id == rid and got_type == rtype:
            return raw[16:]


def screenshot(sock):
    reply = request(sock, TAKE, struct.pack("II", 0, 0))
    result, w, h, size = struct.unpack("IIII", reply[:16])
    if result != 0:
        raise RuntimeError(f"screenshot failed: {result}")
    data = b""
    while len(data) < size:
        data += request(sock, READ, struct.pack("II", len(data), min(CHUNK, size - len(data))), timeout=5)
    return data


def main():
    prefix = sys.argv[1]
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    interval = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    for i in range(count):
        path = f"{prefix}_{i:02d}.png"
        with open(path, "wb") as f:
            f.write(screenshot(sock))
        print(path, flush=True)
        if i + 1 < count:
            time.sleep(interval)


if __name__ == "__main__":
    main()

"""Converts the game's test screenshots (PPM, written on 'P' in script.txt) to PNG.

Usage: python3 tools/ppm2png.py IN.ppm OUT.png
"""
import struct
import sys
import zlib


def main():
    data = open(sys.argv[1], "rb").read()
    parts = data.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    pixels = parts[3]
    raw = b"".join(b"\x00" + pixels[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    open(sys.argv[2], "wb").write(png)


if __name__ == "__main__":
    main()

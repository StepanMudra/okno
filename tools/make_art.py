"""Generates every sprite of OKNO as white/grey PNGs with alpha.

Colour comes at runtime from citro2d tinting (C2D_TintMult), so one shape
serves every hue. Shapes are rendered with 4x4 supersampling, no
dependencies beyond the standard library.

Usage: python3 tools/make_art.py   (writes gfx/*.png and gfx/sprites.t3s)
"""
import math
import struct
import zlib
from pathlib import Path

GFX = Path(__file__).resolve().parent.parent / "gfx"
SS = 4  # supersampling factor


def write_png(path, w, h, pixels):
    """pixels: list of rows, each a list of (r, g, b, a) floats 0..1."""
    raw = bytearray()
    for row in pixels:
        raw.append(0)
        for r, g, b, a in row:
            raw += bytes(max(0, min(255, round(c * 255))) for c in (r, g, b, a))

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    path.write_bytes(png)


def render(w, h, shader):
    """shader(u, v) -> (lum, alpha) or (r, g, b, a); u, v in pixel units, centre-origin."""
    rows = []
    for y in range(h):
        row = []
        for x in range(w):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(SS):
                for sx in range(SS):
                    u = x + (sx + 0.5) / SS - w / 2
                    v = y + (sy + 0.5) / SS - h / 2
                    px = shader(u, v)
                    if len(px) == 2:
                        px = (px[0], px[0], px[0], px[1])
                    a = px[3]
                    acc[0] += px[0] * a
                    acc[1] += px[1] * a
                    acc[2] += px[2] * a
                    acc[3] += a
            n = SS * SS
            a = acc[3] / n
            if a > 0:
                row.append((acc[0] / acc[3], acc[1] / acc[3], acc[2] / acc[3], a))
            else:
                row.append((1, 1, 1, 0))
        rows.append(row)
    return rows


def smooth(e0, e1, x):
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


# --- shapes -----------------------------------------------------------------

def glow(u, v, r=32):
    d = math.hypot(u, v) / r
    a = max(0.0, 1 - d) ** 2.4
    return (1.0, a)


def orb(u, v, r=15):
    d = math.hypot(u, v) / r
    a = 1 - smooth(0.82, 1.0, d)
    # light from the upper left: brighter rim highlight, darker lower right
    lum = 0.72 + 0.28 * (1 - d) + 0.18 * max(0.0, (-u - v) / (r * 1.4))
    spec = math.hypot(u + r * 0.35, v + r * 0.38) / r
    lum += 0.5 * (1 - smooth(0.0, 0.32, spec))
    return (min(1.0, lum), a)


def ring(u, v, r=14, w=2.2):
    d = math.hypot(u, v)
    a = 1 - smooth(w * 0.5, w * 0.5 + 1.0, abs(d - r))
    return (1.0, a)


def rounded_box(u, v, half, rad):
    qx = abs(u) - (half - rad)
    qy = abs(v) - (half - rad)
    out = math.hypot(max(qx, 0), max(qy, 0)) + min(max(qx, qy), 0) - rad
    return out  # signed distance, < 0 inside


def floor_tile(u, v):
    sd = rounded_box(u, v, 11.5, 4)
    a = 1 - smooth(-0.8, 0.6, sd)
    # soft inner bevel, faint centre
    edge = 1 - smooth(-3.0, -0.5, sd)
    lum = 0.55 + 0.45 * (1 - edge) + 0.15 * max(0.0, (-u - v) / 16)
    return (min(1.0, lum), a * (0.35 + 0.65 * (1 - edge * 0.0)))


def wall_tile(u, v):
    sd = rounded_box(u, v, 12, 3)
    a = 1 - smooth(-0.6, 0.6, sd)
    depth = smooth(-6, 0, sd)  # 0 centre .. 1 edge
    lum = 0.55 + 0.45 * depth * (0.6 + 0.4 * (1 if (u + v) < 0 else 0.4))
    return (lum, a)


def gate_tile(u, v):
    sd = rounded_box(u, v, 11, 5)
    a = 1 - smooth(-0.6, 0.6, sd)
    stripe = 0.5 + 0.5 * math.cos((u - v) * 0.9)
    lum = 0.45 + 0.35 * stripe * smooth(-11, -2, sd) + 0.2 * smooth(-3, 0, sd)
    return (lum, a)


def well(u, v):
    d = math.hypot(u, v)
    swirl = 0.5 + 0.5 * math.cos(math.atan2(v, u) * 3 + d * 0.6)
    a = (1 - smooth(9, 11, d)) * (0.35 + 0.65 * swirl * smooth(1, 6, d))
    return (1.0, a)


def portal(u, v, r=20):
    d = math.hypot(u, v)
    ang = math.atan2(v, u)
    arms = 0.5 + 0.5 * math.cos(ang * 5 - d * 0.35)
    a = (1 - smooth(r - 3, r, d)) * smooth(2, 10, d) * (0.25 + 0.75 * arms)
    return (1.0, a)


def spark(u, v, r=4):
    d = math.hypot(u, v) / r
    return (1.0, max(0.0, 1 - d) ** 1.6)


def hexcell(u, v, r=28):
    # hexagon outline, soft
    ang = math.atan2(v, u)
    k = math.pi / 3
    d = math.hypot(u, v) * math.cos((ang % k) - k / 2) / math.cos(k / 2)
    a = 1 - smooth(0.8, 2.2, abs(d - r))
    return (1.0, a * 0.9)


def star(u, v):
    d = math.hypot(u, v)
    cross = max(0.0, 1 - abs(u) / 0.9) * max(0.0, 1 - abs(v) / 6) + \
        max(0.0, 1 - abs(v) / 0.9) * max(0.0, 1 - abs(u) / 6)
    core = max(0.0, 1 - d / 2.5)
    return (1.0, min(1.0, cross * 0.8 + core))


def panel(w, h, rad):
    def sh(u, v):
        qx = abs(u) - (w / 2 - rad)
        qy = abs(v) - (h / 2 - rad)
        sd = math.hypot(max(qx, 0), max(qy, 0)) + min(max(qx, qy), 0) - rad
        a = 1 - smooth(-0.8, 0.6, sd)
        border = 1 - smooth(0.6, 1.8, abs(sd + 1.2))
        return (1.0, a * (0.55 + 0.45 * border))
    return sh


def icon(u, v):
    # 48x48 app icon: dark window with a glowing orb and a three-link trail
    sd = rounded_box(u, v, 23.5, 7)
    a = 1 - smooth(-0.7, 0.5, sd)
    bg = (0.04, 0.06, 0.13)
    col = list(bg)
    frame = 1 - smooth(0.4, 1.6, abs(sd + 3))
    col = [c + f * 0.55 for c, f in zip(col, (0.35 * frame, 0.55 * frame, 0.9 * frame))]
    for i, (cx, cy, rr, rgb) in enumerate([(-11, 9, 3.2, (0.95, 0.55, 0.75)),
                                           (-3, 5, 3.8, (0.55, 0.85, 0.95)),
                                           (6, 0, 7.5, (1.0, 0.95, 0.75))]):
        d = math.hypot(u - cx, v - cy)
        g = max(0.0, 1 - d / (rr * 3.2)) ** 2
        body = 1 - smooth(rr - 0.8, rr + 0.4, d)
        col = [c + g * 0.6 * k + body * k for c, k in zip(col, rgb)]
    return (min(1, col[0]), min(1, col[1]), min(1, col[2]), a)


SPRITES = [
    ("glow", 64, 64, glow),
    ("orb", 32, 32, orb),
    ("ring", 32, 32, ring),
    ("floor", 24, 24, floor_tile),
    ("wall", 24, 24, wall_tile),
    ("gate", 24, 24, gate_tile),
    ("well", 24, 24, well),
    ("portal", 48, 48, portal),
    ("spark", 8, 8, spark),
    ("hex", 64, 64, hexcell),
    ("star", 16, 16, star),
    ("panel", 64, 32, panel(64, 32, 9)),
]


def main():
    GFX.mkdir(exist_ok=True)
    names = []
    for name, w, h, fn in SPRITES:
        write_png(GFX / f"{name}.png", w, h, render(w, h, fn))
        names.append(f"{name}.png")
    (GFX / "sprites.t3s").write_text("--atlas -f rgba8888 -z auto\n" + "\n".join(names) + "\n")
    write_png(GFX.parent / "icon.png", 48, 48, render(48, 48, icon))
    print("wrote", len(names), "sprites + icon.png")


if __name__ == "__main__":
    main()

"""Parses tools/levels/*.txt, proves every room solvable and writes source/levels_data.h.

Rules (must match source/game.c exactly):
  #  wall          .  floor        (space) void, not walkable
  @  start         >  exit         *  well: stepping on it forgets the whole window
  a-z  word (token) lying at its home tile
  A-Z  gate; opens when the window satisfies its requirement, then stays open

  Stepping on a word appends it to the window (oldest ... newest).
  If the window is over capacity, the oldest word is forgotten and returns home.
  Gate "a b"   : every listed word is in the window.
  Gate "a>b>c" : the words are in the window in this order (not necessarily adjacent).
  Gate "-"     : the window must be empty.

Chapter file format (one file per chapter, rooms separated by a line '---'):
  chapter: Title of the chapter
  intro: text shown on the chapter card (may repeat)
  ---
  title: Room name
  cap: 3
  text: story line shown when the room starts (may repeat)
  word a: světlo
  gate A: a b
  map:
  ################
  #@....a.......>#
  ################

Usage: python3 tools/build_levels.py [--verbose]
"""
import sys
from collections import deque
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
W, H = 16, 10


class Room:
    def __init__(self, chapter, src):
        self.chapter = chapter
        self.src = src
        self.title = ""
        self.cap = 3
        self.text = []
        self.words = {}
        self.gates = {}
        self.rows = []


def parse_chapter(path):
    blocks = path.read_text(encoding="utf-8").split("\n---\n")
    head = {"chapter": "", "intro": []}
    for line in blocks[0].splitlines():
        if line.startswith("chapter:"):
            head["chapter"] = line.split(":", 1)[1].strip()
        elif line.startswith("intro:"):
            head["intro"].append(line.split(":", 1)[1].strip())
    rooms = []
    for i, block in enumerate(blocks[1:], 1):
        r = Room(head["chapter"], f"{path.name}#{i}")
        in_map = False
        for line in block.splitlines():
            if in_map:
                if line.strip() or r.rows:
                    r.rows.append(line.rstrip("\n"))
                continue
            if not line.strip() or line.startswith(";"):
                continue
            key, _, val = line.partition(":")
            val = val.strip()
            if key == "title":
                r.title = val
            elif key == "cap":
                r.cap = int(val)
            elif key == "text":
                r.text.append(val)
            elif key.startswith("word "):
                r.words[key[5:].strip()] = val
            elif key.startswith("gate "):
                r.gates[key[5:].strip()] = val
            elif key == "map":
                in_map = True
            else:
                raise SystemExit(f"{r.src}: unknown line {line!r}")
        while r.rows and not r.rows[-1].strip():
            r.rows.pop()
        rooms.append(r)
    return head, rooms


def gate_req(spec):
    spec = spec.strip()
    if spec == "-":
        return ("empty", ())
    if ">" in spec:
        return ("ordered", tuple(s.strip() for s in spec.split(">")))
    return ("all", tuple(spec.split()))


def satisfied(req, window):
    kind, ws = req
    if kind == "empty":
        return not window
    if kind == "all":
        return all(w in window for w in ws)
    i = 0
    for w in window:
        if i < len(ws) and w == ws[i]:
            i += 1
    return i == len(ws)


def validate(r):
    if len(r.rows) > H or any(len(row) > W for row in r.rows):
        raise SystemExit(f"{r.src}: map larger than {W}x{H}")
    # centre smaller maps
    used_w = max(len(row.rstrip()) for row in r.rows)
    dx, dy = (W - used_w) // 2, (H - len(r.rows)) // 2
    grid = [" " * W] * dy + [(" " * dx + row.rstrip()).ljust(W) for row in r.rows]
    grid += [" " * W] * (H - len(grid))
    r.grid = grid
    starts = [(x, y) for y in range(H) for x in range(W) if grid[y][x] == "@"]
    if len(starts) != 1:
        raise SystemExit(f"{r.src}: needs exactly one @")
    if not any(">" in row for row in grid):
        raise SystemExit(f"{r.src}: no exit >")
    for y in range(H):
        for x in range(W):
            c = grid[y][x]
            if c.islower() and c not in r.words:
                raise SystemExit(f"{r.src}: word {c} has no definition")
            if c.isupper() and c not in r.gates:
                raise SystemExit(f"{r.src}: gate {c} has no definition")
    for g, spec in r.gates.items():
        for w in gate_req(spec)[1]:
            if w not in r.words:
                raise SystemExit(f"{r.src}: gate {g} needs unknown word {w}")
    r.start = starts[0]


def solve(r):
    """BFS over (pos, window, open gates). Returns (moves, path) or None."""
    grid = r.grid
    homes = {grid[y][x]: (x, y) for y in range(H) for x in range(W) if grid[y][x].islower()}
    reqs = {g: gate_req(s) for g, s in r.gates.items()}
    start = (r.start, (), frozenset())
    prev = {start: None}
    q = deque([start])
    dirs = {"U": (0, -1), "D": (0, 1), "L": (-1, 0), "R": (1, 0)}
    while q:
        state = q.popleft()
        (x, y), win, opened = state
        for d, (ddx, ddy) in dirs.items():
            nx, ny = x + ddx, y + ddy
            if not (0 <= nx < W and 0 <= ny < H):
                continue
            c = grid[ny][nx]
            if c in "# ":
                continue
            nwin, nopen = win, opened
            if c.isupper() and c not in opened:
                if not satisfied(reqs[c], win):
                    continue
                nopen = opened | {c}
            if c == ">":
                path = [d]
                s = state
                while prev[s] is not None:
                    s, step = prev[s]
                    path.append(step)
                return len(path), "".join(reversed(path)), opened
            if c.islower() and c not in win:
                nwin = win + (c,)
                if len(nwin) > r.cap:
                    nwin = nwin[1:]
            elif c == "*":
                nwin = ()
            ns = ((nx, ny), nwin, nopen)
            if ns not in prev:
                prev[ns] = (state, d)
                q.append(ns)
    return None


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def main():
    verbose = "--verbose" in sys.argv
    files = sorted((ROOT / "tools" / "levels").glob("c*.txt"))
    chapters, rooms = [], []
    for f in files:
        head, rs = parse_chapter(f)
        chapters.append((head, len(rooms), len(rs)))
        for r in rs:
            validate(r)
            res = solve(r)
            if res is None:
                raise SystemExit(f"UNSOLVABLE: {r.src} ({r.title})")
            r.par, r.solution, r.used = res
            unused = set(r.gates) - set(r.used)
            note = f"  ⚠ gates not needed: {''.join(sorted(unused))}" if unused else ""
            print(f"{r.src:14} {r.title:28} par {r.par:3}{note}")
            if verbose:
                print("   ", r.solution)
        rooms.extend(rs)

    out = ["// Generated by tools/build_levels.py – do not edit.", "#pragma once", ""]
    out.append(f"#define LEVEL_COUNT {len(rooms)}")
    out.append(f"#define CHAPTER_COUNT {len(chapters)}")
    out.append("")
    out.append("static const ChapterDef CHAPTERS[CHAPTER_COUNT] = {")
    for head, first, n in chapters:
        out.append(f"  {{ {c_str(head['chapter'])}, {c_str(chr(10).join(head['intro']))}, {first}, {n} }},")
    out.append("};")
    out.append("")
    out.append("static const LevelDef LEVELS[LEVEL_COUNT] = {")
    for r in rooms:
        words = ", ".join(f"[{ord(k) - 97}] = {c_str(v)}" for k, v in sorted(r.words.items()))
        gates = ", ".join(f"[{ord(k) - 65}] = {c_str(v)}" for k, v in sorted(r.gates.items()))
        out.append("  {")
        out.append(f"    .title = {c_str(r.title)}, .cap = {r.cap}, .par = {r.par},")
        out.append(f"    .text = {c_str(chr(10).join(r.text))},")
        out.append(f"    .words = {{ {words} }},")
        out.append(f"    .gates = {{ {gates} }},")
        out.append("    .map = {")
        for row in r.grid:
            out.append(f"      {c_str(row)},")
        out.append("    },")
        out.append("  },")
    out.append("};")
    (ROOT / "source" / "levels_data.h").write_text("\n".join(out) + "\n", encoding="utf-8")
    print(f"{len(rooms)} rooms in {len(chapters)} chapters -> source/levels_data.h")


if __name__ == "__main__":
    main()

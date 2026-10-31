"""Parses tools/levels/*.txt, proves every room solvable and writes source/levels_data.h.

Rules (must match source/main.c exactly):
  #  wall          .  floor        (space) void, not walkable
  @  start         >  exit         *  well: forgets the whole window (notes stay)
  =  note stone: window not empty -> the newest word moves into the notes
                 (notes full -> the oldest note is released first);
                 window empty -> the oldest note is erased
  ^  heat plate: temperature becomes high      _  cold plate: temperature becomes low;
                 hallucinations held in the window or notes dissolve
  ~  mist: walkable only while the temperature is high
  a-z  word lying at its home tile      A-Z  gate; opens when satisfied, then stays open

  Words: "word a: ahoj" real, "word a: !ahoj" fake (planted), "word a: ~ahoj" hallucination
         (fake that exists only while hot). Fakes never count for a gate.
  Stepping on a word appends it to the window (oldest ... newest).
  If the window is over capacity, the oldest word is forgotten and returns home
  (words with no home tile, e.g. planted notes, just vanish).
  Gate "a b"   : every listed word is held (window or notes).
  Gate "a>b>c" : the words are in the window in this order (not necessarily adjacent).
  Gate "-"     : the window must be empty.
  Leading "!"  : truth gate, additionally nothing fake may be held. "!" alone = only that.

Chapter file format (one file per chapter, rooms separated by a line '---'):
  chapter: Title of the chapter
  intro: text shown on the chapter card (may repeat)
  ---
  title: Room name
  cap: 3
  notes: 2              (optional, capacity of the notes)
  start notes: a b      (optional, letters already in the notes)
  hot: 1                (optional, starting temperature)
  text: story line shown when the room starts (may repeat)
  word a: světlo
  gate A: a b
  map:
  ################
  #@....a.......>#
  ################

Usage: python3 tools/build_levels.py [--verbose] [--only FILE]
"""
import sys
from collections import deque
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
W, H = 16, 10
MAXCAP, MAXNOTES = 8, 4
TILES = set("#. @>*=^_~") | set("abcdefghijklmnopqrstuvwxyz") | set("ABCDEFGHIJKLMNOPQRSTUVWXYZ")


class Room:
    def __init__(self, chapter, src):
        self.chapter = chapter
        self.src = src
        self.title = ""
        self.cap = 3
        self.notes = 0
        self.start_notes = ""
        self.hot = 0
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
            elif key == "notes":
                r.notes = int(val)
            elif key == "start notes":
                r.start_notes = "".join(val.split())
            elif key == "hot":
                r.hot = int(val)
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
    """-> (kind, words, truth)"""
    spec = spec.strip()
    truth = spec.startswith("!")
    if truth:
        spec = spec[1:].strip()
    if spec == "-":
        return ("empty", (), truth)
    if ">" in spec:
        return ("ordered", tuple(s.strip() for s in spec.split(">")), truth)
    return ("all", tuple(spec.split()), truth)


def is_fake(r, w):
    return r.words[w][:1] in "!~"


def is_halluc(r, w):
    return r.words[w][:1] == "~"


def satisfied(r, req, window, notes):
    kind, ws, truth = req
    if truth and any(is_fake(r, w) for w in window + notes):
        return False
    if kind == "empty":
        return not window
    if kind == "all":
        return all(w in window or w in notes for w in ws)
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
    if not 1 <= r.cap <= MAXCAP or not 0 <= r.notes <= MAXNOTES:
        raise SystemExit(f"{r.src}: cap 1..{MAXCAP}, notes 0..{MAXNOTES}")
    starts = [(x, y) for y in range(H) for x in range(W) if grid[y][x] == "@"]
    if len(starts) != 1:
        raise SystemExit(f"{r.src}: needs exactly one @")
    if not any(">" in row for row in grid):
        raise SystemExit(f"{r.src}: no exit >")
    seen = set()
    for y in range(H):
        for x in range(W):
            c = grid[y][x]
            if c not in TILES:
                raise SystemExit(f"{r.src}: unknown tile {c!r}")
            if c.islower():
                if c in seen:
                    raise SystemExit(f"{r.src}: word {c} placed twice (each letter = one tile)")
                seen.add(c)
                if c not in r.words:
                    raise SystemExit(f"{r.src}: word {c} has no definition")
            if c.isupper() and c not in r.gates:
                raise SystemExit(f"{r.src}: gate {c} has no definition")
    for g, spec in r.gates.items():
        for w in gate_req(spec)[1]:
            if w not in r.words:
                raise SystemExit(f"{r.src}: gate {g} needs unknown word {w}")
            if is_fake(r, w):
                raise SystemExit(f"{r.src}: gate {g} requires fake word {w} (can never open)")
    for w in r.start_notes:
        if w not in r.words:
            raise SystemExit(f"{r.src}: start note {w} undefined")
    if len(r.start_notes) > max(r.notes, 0):
        raise SystemExit(f"{r.src}: more start notes than notes capacity")
    r.start = starts[0]


DIRS = {"U": (0, -1), "D": (0, 1), "L": (-1, 0), "R": (1, 0)}


def step(r, state, d):
    """Applies one move; returns (new_state, exited) or None when blocked."""
    (x, y), win, notes, hot, opened = state
    nx, ny = x + DIRS[d][0], y + DIRS[d][1]
    if not (0 <= nx < W and 0 <= ny < H):
        return None
    c = r.grid[ny][nx]
    if c in "# " or (c == "~" and not hot):
        return None
    if c.isupper() and c not in opened:
        if not satisfied(r, r.reqs[c], win, notes):
            return None
        opened = opened | {c}
    if c == ">":
        return (((nx, ny), win, notes, hot, opened), True)
    if c.islower() and c not in win and c not in notes and (not is_halluc(r, c) or hot):
        win = win + (c,)
        if len(win) > r.cap:
            win = win[1:]
    elif c == "*":
        win = ()
    elif c == "=" and r.notes > 0:
        if win:
            w = win[-1]
            win = win[:-1]
            if len(notes) == r.notes:
                notes = notes[1:]
            notes = notes + (w,)
        elif notes:
            notes = notes[1:]
    elif c == "^":
        hot = 1
    elif c == "_" and hot:
        hot = 0
        win = tuple(w for w in win if not is_halluc(r, w))
        notes = tuple(w for w in notes if not is_halluc(r, w))
    return (((nx, ny), win, notes, hot, opened), False)


def solve(r):
    """BFS. Returns (moves, path, gates used, reachable states) or None."""
    r.reqs = {g: gate_req(s) for g, s in r.gates.items()}
    start = (r.start, (), tuple(r.start_notes), r.hot, frozenset())
    prev = {start: None}
    q = deque([start])
    while q:
        state = q.popleft()
        for d in DIRS:
            res = step(r, state, d)
            if res is None:
                continue
            ns, exited = res
            if exited:
                path = [d]
                s = state
                while prev[s] is not None:
                    s, mv = prev[s]
                    path.append(mv)
                return len(path), "".join(reversed(path)), ns[4], len(prev)
            if ns not in prev:
                prev[ns] = (state, d)
                q.append(ns)
    return None


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def main():
    verbose = "--verbose" in sys.argv
    only = sys.argv[sys.argv.index("--only") + 1] if "--only" in sys.argv else None
    files = sorted((ROOT / "tools" / "levels").glob("c*.txt"))
    if only:
        files = [f for f in files if f.name == only or str(f).endswith(only)]
    chapters, rooms = [], []
    for f in files:
        head, rs = parse_chapter(f)
        chapters.append((head, len(rooms), len(rs)))
        for r in rs:
            validate(r)
            res = solve(r)
            if res is None:
                raise SystemExit(f"UNSOLVABLE: {r.src} ({r.title})")
            r.par, r.solution, r.used, r.states = res
            unused = set(r.gates) - set(r.used)
            note = f"  ⚠ gates not needed: {''.join(sorted(unused))}" if unused else ""
            print(f"{r.src:24} {r.title:26} par {r.par:3}  states {r.states:6}{note}")
            if verbose:
                print("   ", r.solution)
        rooms.extend(rs)
    if only:
        return

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
        out.append(f"    .notes = {r.notes}, .startNotes = {c_str(r.start_notes)}, .hot = {r.hot},")
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
    sols = ROOT / "tools" / "solutions.txt"
    sols.write_text("".join(f"{i} {r.solution}\n" for i, r in enumerate(rooms)))
    print(f"{len(rooms)} rooms in {len(chapters)} chapters -> source/levels_data.h")


if __name__ == "__main__":
    main()

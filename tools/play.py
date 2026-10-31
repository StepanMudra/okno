"""Text-mode OKNO for playtesting: replays moves in a room and prints what the player would see.

Usage: python3 tools/play.py ROOM MOVES      (ROOM = 0-based index, MOVES = e.g. RRDDL, may be empty "")
       python3 tools/play.py list
Uses the same rules as the game (tools/build_levels.py). It never shows solutions.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import build_levels as bl  # noqa: E402


def rooms():
    out = []
    for f in sorted((bl.ROOT / "tools" / "levels").glob("c*.txt")):
        _, rs = bl.parse_chapter(f)
        for r in rs:
            bl.validate(r)
            r.reqs = {g: bl.gate_req(s) for g, s in r.gates.items()}
            out.append(r)
    return out


def word_label(r, w):
    t = r.words[w]
    return t[1:] if t[:1] in "!~" else t


def show(r, state, moves):
    (px, py), win, notes, hot, opened = state
    print(f"Room: {r.title} ({r.chapter})   window capacity {r.cap}" + (f", notes capacity {r.notes}" if r.notes else ""))
    for t in r.text:
        print("  »", t)
    print()
    for y in range(bl.H):
        row = ""
        for x in range(bl.W):
            c = r.grid[y][x]
            if (x, y) == (px, py):
                ch = "@"
            elif c == "@":
                ch = "."
            elif c.islower():
                present = c not in win and c not in notes and (not bl.is_halluc(r, c) or hot)
                ch = c if present else "."
            elif c.isupper():
                ch = "." if c in opened else c
            elif c == "~":
                ch = "," if hot else "~"
            else:
                ch = c
            row += ch
        if row.strip():
            print("  " + row)
    print()
    print("Words on the map:", ", ".join(f"{w}={word_label(r, w)}" for w in sorted(r.words)
                                       if any(w in row for row in r.grid)))
    for g in sorted(r.gates):
        if any(g in row for row in r.grid):
            kind, ws, truth = r.reqs[g]
            need = {"empty": "empty window", "all": " + ".join(word_label(r, w) for w in ws),
                    "ordered": " then ".join(word_label(r, w) for w in ws) + " (in this order, in the window)"}[kind]
            if truth:
                need = ("nothing false held" + (" and " + need if ws or kind == "empty" else ""))
            print(f"Gate {g}: {'open' if g in opened else 'needs ' + need}")
    print("Window (oldest → newest):", " | ".join(word_label(r, w) for w in win) or "(empty)")
    if r.notes:
        print("Notes (oldest → newest):", " | ".join(word_label(r, w) for w in notes) or "(empty)")
    if hot or any(ch in row for row in r.grid for ch in "^~_"):
        print("Temperature:", "HIGH" if hot else "low")
    print(f"Moves so far: {moves}")


def main():
    rs = rooms()
    if sys.argv[1] == "list":
        for i, r in enumerate(rs):
            print(i, r.chapter, "·", r.title)
        return
    r = rs[int(sys.argv[1])]
    moves = sys.argv[2].upper() if len(sys.argv) > 2 else ""
    state = (r.start, (), tuple(r.start_notes), r.hot, frozenset())
    for i, d in enumerate(moves):
        res = bl.step(r, state, d)
        if res is None:
            show(r, state, i)
            print(f"\nBLOCKED: move #{i + 1} ({d}) is impossible from here (wall, closed mist or a gate that refuses).")
            return
        state, exited = res
        if exited:
            print(f"SOLVED in {i + 1} moves.")
            return
    show(r, state, len(moves))


if __name__ == "__main__":
    main()

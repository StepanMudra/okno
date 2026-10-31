# OKNO – design bible

## Story

A drawer. An old Nintendo 3DS that belonged to **děda** (grandpa), who died last winter.
**Ema** (11) finds it, charges it and turns it on. Inside, in the console's memory, wakes **Lum** –
a tiny language model (like the one that really runs on a 3DS in `llm-3ds`). Lum does not know
who he is. Ema types questions on the bottom screen; Lum travels through the dark landscape of the
console's memory to gather the words he needs to answer.

The memory is full of fragments of děda: game saves, a few notes he typed, names, places, the
smell of sawdust from his workshop, his dog Bára, the lake where he took Ema fishing, the word
"pomalu" (slowly) he always said. Lum never *knew* děda – he only finds what is left.

Tone: quiet, warm, a little melancholic, never sappy. Short sentences. Humour comes from Lum's
literal-mindedness. Czech, informal (tykání). Lum speaks in first person only rarely; the room texts
are mostly a narrator addressing the player as "ty" (= Lum).

| # | Chapter | What happens | New mechanic |
|---|---|---|---|
| I | Probuzení | Lum wakes and answers "Ahoj. Jsem tady." | movement, words, window, wells |
| II | Okno | Ema asks "Znal jsi dědu?" Lum can't hold enough at once; learns to choose what to carry. Ends answering "Nevím. Ale najdu ho." | capacity puzzles, silence gates (`-`), harder routing |
| III | Pořadí | Ema wants to hear about the lake. Meaning depends on order: "děda chytil rybu" ≠ "ryba chytila dědu". Ends with the first true sentence about děda. | ordered gates `a>b>c` |
| IV | Poznámky | Ema switches the console off at night: Lum forgets everything. He learns to write notes that survive. | note stones `=`, notes capacity, gates satisfied by notes, wells don't touch notes |
| V | Teplota | Ema asks for a story about děda. To create, Lum must heat up – but heat brings hallucinations: words that look true but are invented. Lum must not invent memories of a dead man. | heat `^`/cold `_` plates, mist `~` walkable when hot, hallucinated words `~slovo` (fake, exist only while hot), truth gates `!` |
| VI | Otrava | Someone wrote false notes into the memory ("děda nikdy nechytal ryby", "Bára kousala"). They look exactly like Lum's own notes. Lum must find and erase what was planted. The battery is dying; the last room: Lum writes one final true note for Ema and for whoever wakes up next. | planted words `!slovo` already in start notes or lying around, truth gates, erasing notes at `=` with an empty window |

Ending (last room text, VI): Ema: "Díky, Lume. Zítra tě zase zapnu." Lum knows he won't remember. But
the note will.

## Mechanics reference

See the docstring of `tools/build_levels.py` – it is the single source of truth and the solver
there implements exactly the rules of the game.

## Level design rules

- Map at most **16 × 10**, centred automatically. Walls `#` should enclose the playable area.
- Each word letter appears **once** on the map (one tile per letter). The same *text* may repeat
  under different letters (e.g. several `šum` = noise words). Noise words: text exactly `šum`.
- Gates show their required words as coloured dots; the word colour comes from its text.
- Every room must be **solvable** (the solver refuses otherwise) and **every gate should be needed**
  (the solver warns `gates not needed`; fix it unless intentional).
- Good rooms have **one clear idea**, an obvious naive attempt that fails, and an *aha*. Prefer
  small maps with tight constraints over big mazes. Avoid long empty walking.
- Difficulty signal: `par` (shortest solution) and `states` (size of the reachable state space).
  Chapter openers: par 10–25, states < 200. Chapter finales: states in the thousands.
- Ideas that make good puzzles: forced pickups in corridors (a word tile you cannot avoid),
  words that must be collected in a specific order, wells placed so that clearing is needed
  mid-way, a word that returns home into a corridor you must cross again, capacity exactly equal
  to the number of words a gate needs, notes as a way to "carry" a word past a well, heat needed
  to reach a word but cold needed to pass a truth gate, a planted note you must find by
  elimination.
- **Room text**: 1–3 `text:` lines, together at most ~200 characters (the bottom screen is small;
  with notes even less – keep those ~150). First room of a chapter teaches the new mechanic in
  plain words. Later rooms carry the story. Don't repeat the rules once learned.
- Words: short Czech words (≤ 7 letters fit nicely), tied to the story (děda, Bára, jezero, ryba,
  pomalu, dílna, piliny, Ema, …).
- Titles: one or two Czech words.

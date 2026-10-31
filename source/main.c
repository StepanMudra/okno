// OKNO – a puzzle game about memory, for the Nintendo 3DS.
// You are Lum, a tiny language model. Words you pick up follow you as your
// context window; when it overflows, the oldest word is forgotten.
#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "audio.h"
#include "sprites.h"
#include "sprites_t3x.h"
#include "font_bcfnt.h"

typedef struct {
	const char* title;
	const char* intro;
	int first, count;
} ChapterDef;

typedef struct {
	const char* title;
	int cap, par;
	const char* text;
	const char* words[26];  // "!slovo" = fake (planted), "~slovo" = hallucination (exists only when hot)
	const char* gates[26];  // "a b" all, "a>b" ordered, "-" empty window, leading "!" = truth gate
	const char* map[10];
	int notes;              // capacity of the notes (0 = no notes in this room)
	const char* startNotes; // letters already written in the notes when the room starts
	int hot;                // starting temperature
	const char* solution;   // shortest solution (U/D/L/R), revealed step by step as hints
	const char* after;      // epilogue shown once the room is solved ("" = none)
} LevelDef;

#include "levels_data.h"

#define TOP_W 400
#define BOT_W 320
#define SCR_H 240
#define GW 16
#define GH 10
#define TS 24
#define OX ((TOP_W - GW * TS) / 2)
#define OY ((SCR_H - GH * TS) / 2)
#define MAXCAP 8
// button names shown on screen
#ifdef PC
#define BTN_A "Enter"
#define BTN_B "Z"
#define BTN_Y "R"
#define BTN_SELECT "H"
#else
#define BTN_A "A"
#define BTN_B "B"
#define BTN_Y "Y"
#define BTN_SELECT "SELECT"
#endif
#define MAXNOTES 4
#define UNDO_MAX 1024
#define MAX_PARTICLES 320
#define TRAIL_HIST 16
#define SAVE_DIR "sdmc:/3ds/Okno"
#define SAVE_PATH SAVE_DIR "/save.txt"

// ---------------------------------------------------------------------------
// rendering globals

static C3D_RenderTarget *topL, *topR, *bot;
static C2D_SpriteSheet sheet;
static C2D_Font font;
static C2D_TextBuf tbuf;
static C2D_Image img[12];
static float eye;      // current eye sign * slider strength while drawing the top screen
static float timeSec;  // global clock

static u32 rgba(float r, float g, float b, float a) {
	if (a < 0) a = 0;
	if (a > 1) a = 1;
	return C2D_Color32f(fminf(r, 1), fminf(g, 1), fminf(b, 1), a);
}

// draw a sprite centred at (x, y); depth > 0 pops out of the screen
static void spr(int id, float x, float y, float depth, float scale, float rot, u32 color) {
	C2D_ImageTint t;
	C2D_PlainImageTint(&t, color, 1.0f);
	C2D_DrawImageAtRotated(img[id], x + depth * eye, y, 0.5f, rot, &t, scale, scale);
}

static void blend_add(void) {
	C2D_Flush();
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE, GPU_SRC_ALPHA, GPU_ONE);
}

static void blend_normal(void) {
	C2D_Flush();
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA,
		GPU_ONE_MINUS_SRC_ALPHA);
}

static void text(const char* s, float x, float y, float scale, u32 color, int align, float wrap, float depth) {
	C2D_Text t;
	C2D_TextFontParse(&t, font, tbuf, s);
	C2D_TextOptimize(&t);
	u32 flags = C2D_WithColor | align;
	if (wrap > 0) {
		flags |= C2D_WordWrap;
		C2D_DrawText(&t, flags, x + depth * eye, y, 0.5f, scale, scale, color, wrap);
	} else {
		C2D_DrawText(&t, flags, x + depth * eye, y, 0.5f, scale, scale, color);
	}
}

static float text_width(const char* s, float scale) {
	C2D_Text t;
	float w, h;
	C2D_TextFontParse(&t, font, tbuf, s);
	C2D_TextGetDimensions(&t, scale, scale, &w, &h);
	return w;
}

// ---------------------------------------------------------------------------
// words and colours

typedef struct { float r, g, b; } Col;

static const Col PALETTE[] = {
	{ 1.00f, 0.78f, 0.42f },  // amber
	{ 0.45f, 0.85f, 1.00f },  // sky
	{ 1.00f, 0.50f, 0.70f },  // rose
	{ 0.55f, 1.00f, 0.65f },  // mint
	{ 0.75f, 0.60f, 1.00f },  // violet
	{ 0.95f, 0.95f, 0.55f },  // lemon
	{ 1.00f, 0.62f, 0.40f },  // coral
	{ 0.50f, 0.95f, 0.90f },  // teal
};
static const Col NOISE = { 0.55f, 0.57f, 0.65f };

static bool is_noise(const char* w) { return w && strcmp(w, "šum") == 0; }

// Colours are handed out per room: every different word text gets its own palette colour
// (a word and its fake twin share one, of course). Filled by assign_colors() on room load.
#define MAX_COLORED 26
static const char* coloredText[MAX_COLORED];
static int coloredCount;

static Col word_color(const char* w) {
	if (is_noise(w)) return NOISE;
	for (int i = 0; i < coloredCount; i++)
		if (strcmp(coloredText[i], w) == 0) return PALETTE[i % (sizeof PALETTE / sizeof PALETTE[0])];
	return PALETTE[0];
}

// ---------------------------------------------------------------------------
// level state

typedef enum { REQ_ALL, REQ_ORDERED, REQ_EMPTY } ReqKind;

typedef struct {
	ReqKind kind;
	bool truth;  // nothing fake may be held
	u8 n;
	u8 w[8];
} Req;

typedef struct {
	s8 x, y;
	u8 nwin;
	u8 win[MAXCAP];
	u8 nnotes;
	u8 notes[MAXNOTES];
	u8 hot;
	u32 open;  // bit per gate letter
	u16 moves;
} State;

static int levelIdx;
static const LevelDef* L;
static char grid[GH][GW];
static s8 homeX[26], homeY[26];
static Req req[26];
static State st;
static State undoStack[UNDO_MAX];
static int undoTop, undoCount;

// visuals that follow the logic
typedef struct { float x, y; } V2;
static V2 playerPos;              // rendered, in tiles
static V2 hist[TRAIL_HIST];       // last tiles the player stood on, [0] = current
static V2 segPos[MAXCAP];         // rendered trail positions, index = window slot
static float wordReturn[26];      // >0 while a forgotten word flies home (seconds left)
static V2 wordFly[26];            // flying position
static float gateOpenAnim[26];    // 0..1 dissolve
static float shake, bumpGate;     // feedback
static int bumpLetter = -1;
static int faceX = 1, faceY = 0;  // eyes direction
static int hintSteps;            // how many steps of hintPath are shown
static char hintPath[512];       // shortest path from the current state (U/D/L/R)
static char message[160];
static float messageTime;
static float roomTime;

// ---------------------------------------------------------------------------
// particles

typedef struct {
	float x, y, vx, vy, life, max, size, depth;
	Col c;
} Particle;
static Particle parts[MAX_PARTICLES];

static float frand(void) { return (float)rand() / (float)RAND_MAX; }

static void emit(float x, float y, Col c, int n, float speed, float life, float size) {
	for (int i = 0; i < MAX_PARTICLES && n > 0; i++) {
		if (parts[i].life > 0) continue;
		float a = frand() * 2 * M_PI, s = speed * (0.3f + 0.7f * frand());
		parts[i] = (Particle){ x, y, cosf(a) * s, sinf(a) * s, life * (0.6f + 0.4f * frand()), life,
			size * (0.6f + 0.6f * frand()), 0.6f + frand() * 0.8f, c };
		n--;
	}
}

static void update_particles(float dt) {
	for (int i = 0; i < MAX_PARTICLES; i++) {
		Particle* p = &parts[i];
		if (p->life <= 0) continue;
		p->life -= dt;
		p->x += p->vx * dt;
		p->y += p->vy * dt;
		p->vx *= 0.96f;
		p->vy = p->vy * 0.96f - 6 * dt;
	}
}

static void draw_particles(void) {
	for (int i = 0; i < MAX_PARTICLES; i++) {
		Particle* p = &parts[i];
		if (p->life <= 0) continue;
		float k = p->life / p->max;
		spr(sprites_spark_idx, p->x, p->y, p->depth, p->size * (0.5f + 0.5f * k), 0,
			rgba(p->c.r, p->c.g, p->c.b, k));
	}
}

// ---------------------------------------------------------------------------
// level loading and rules

static void parse_req(int g, const char* s) {
	Req* r = &req[g];
	memset(r, 0, sizeof *r);
	if (!s) return;
	if (s[0] == '!') {
		r->truth = true;
		s++;
		while (*s == ' ') s++;
	}
	if (strcmp(s, "-") == 0) {
		r->kind = REQ_EMPTY;
		return;
	}
	r->kind = strchr(s, '>') ? REQ_ORDERED : REQ_ALL;
	for (const char* p = s; *p; p++)
		if (*p >= 'a' && *p <= 'z' && r->n < 8) r->w[r->n++] = *p - 'a';
}

static bool in_window(const State* s, int w) {
	for (int i = 0; i < s->nwin; i++)
		if (s->win[i] == w) return true;
	return false;
}

static bool in_notes(const State* s, int w) {
	for (int i = 0; i < s->nnotes; i++)
		if (s->notes[i] == w) return true;
	return false;
}

static bool held(const State* s, int w) { return in_window(s, w) || in_notes(s, w); }
static bool is_fake(int w) { return L->words[w] && (L->words[w][0] == '!' || L->words[w][0] == '~'); }
static bool is_halluc(int w) { return L->words[w] && L->words[w][0] == '~'; }
static const char* wtext(int w) { return is_fake(w) ? L->words[w] + 1 : L->words[w]; }

// the word lies on its home tile and can be picked up
static bool word_present(const State* s, int w) {
	return homeX[w] >= 0 && !held(s, w) && (!is_halluc(w) || s->hot);
}

static bool satisfied(const Req* r, const State* s) {
	if (r->truth) {
		for (int i = 0; i < s->nwin; i++)
			if (is_fake(s->win[i])) return false;
		for (int i = 0; i < s->nnotes; i++)
			if (is_fake(s->notes[i])) return false;
	}
	if (r->kind == REQ_EMPTY) return s->nwin == 0;
	if (r->kind == REQ_ALL) {
		for (int i = 0; i < r->n; i++)
			if (!held(s, r->w[i])) return false;
		return true;
	}
	int k = 0;
	for (int i = 0; i < s->nwin && k < r->n; i++)
		if (s->win[i] == r->w[k]) k++;
	return k == r->n;
}

static void set_message(const char* m) {
	snprintf(message, sizeof message, "%s", m);
	messageTime = 4.0f;
}

static V2 tile_px(float tx, float ty) { return (V2){ OX + tx * TS + TS / 2.0f, OY + ty * TS + TS / 2.0f }; }

static void reset_visuals(void) {
	playerPos = (V2){ st.x, st.y };
	for (int i = 0; i < TRAIL_HIST; i++) hist[i] = playerPos;
	for (int i = 0; i < MAXCAP; i++) segPos[i] = playerPos;
	memset(wordReturn, 0, sizeof wordReturn);
	memset(gateOpenAnim, 0, sizeof gateOpenAnim);
	for (int g = 0; g < 26; g++)
		if (st.open & (1u << g)) gateOpenAnim[g] = 1;
	memset(parts, 0, sizeof parts);
	shake = 0;
	bumpLetter = -1;
}

static void load_level(int idx) {
	levelIdx = idx;
	L = &LEVELS[idx];
	memset(&st, 0, sizeof st);
	memset(homeX, -1, sizeof homeX);
	memset(homeY, -1, sizeof homeY);
	for (int y = 0; y < GH; y++)
		for (int x = 0; x < GW; x++) {
			char c = L->map[y][x];
			grid[y][x] = c;
			if (c == '@') {
				st.x = x;
				st.y = y;
				grid[y][x] = '.';
			} else if (c >= 'a' && c <= 'z') {
				homeX[c - 'a'] = x;
				homeY[c - 'a'] = y;
			}
		}
	for (int g = 0; g < 26; g++) parse_req(g, L->gates[g]);
	coloredCount = 0;
	for (int w = 0; w < 26; w++) {
		if (!L->words[w] || is_noise(wtext(w))) continue;
		bool seen = false;
		for (int i = 0; i < coloredCount; i++) seen |= strcmp(coloredText[i], wtext(w)) == 0;
		if (!seen) coloredText[coloredCount++] = wtext(w);
	}
	for (const char* n = L->startNotes; n && *n && st.nnotes < MAXNOTES; n++)
		if (*n >= 'a' && *n <= 'z') st.notes[st.nnotes++] = *n - 'a';
	st.hot = L->hot;
	undoTop = undoCount = 0;
	hintSteps = 0;
	message[0] = 0;
	roomTime = 0;
	reset_visuals();
}

static void push_undo(void) {
	undoStack[undoTop] = st;
	undoTop = (undoTop + 1) % UNDO_MAX;
	if (undoCount < UNDO_MAX) undoCount++;
}

static bool pop_undo(void) {
	if (!undoCount) return false;
	undoTop = (undoTop + UNDO_MAX - 1) % UNDO_MAX;
	undoCount--;
	st = undoStack[undoTop];
	memset(wordReturn, 0, sizeof wordReturn);
	for (int g = 0; g < 26; g++)
		if (!(st.open & (1u << g))) gateOpenAnim[g] = 0;
	playerPos = (V2){ st.x, st.y };
	for (int i = 0; i < TRAIL_HIST; i++) hist[i] = playerPos;
	for (int i = 0; i < MAXCAP; i++) segPos[i] = playerPos;
	return true;
}

static void forget_word(int w, V2 from) {
	V2 p = tile_px(from.x, from.y);
	emit(p.x, p.y, NOISE, 10, 40, 0.6f, 0.8f);
	if (homeX[w] < 0) return;  // planted words have no home: they just dissolve
	wordFly[w] = from;
	wordReturn[w] = 0.7f;
}


// The rules of the game in one place (tools/build_levels.py mirrors them).
// Returns 0 blocked, 1 moved, 2 reached the exit. No side effects besides *s.
static int sim_step(State* s, int dx, int dy) {
	int nx = s->x + dx, ny = s->y + dy;
	if (nx < 0 || ny < 0 || nx >= GW || ny >= GH) return 0;
	char c = grid[ny][nx];
	if (c == '#' || c == ' ' || (c == '~' && !s->hot)) return 0;
	int gate = (c >= 'A' && c <= 'Z') ? c - 'A' : -1;
	if (gate >= 0 && !(s->open & (1u << gate))) {
		if (!satisfied(&req[gate], s)) return 0;
		s->open |= 1u << gate;
	}
	s->x = nx;
	s->y = ny;
	s->moves++;
	if (c >= 'a' && c <= 'z' && word_present(s, c - 'a')) {
		if (s->nwin == L->cap) memmove(s->win, s->win + 1, --s->nwin);
		s->win[s->nwin++] = c - 'a';
	} else if (c == '*') {
		s->nwin = 0;
	} else if (c == '=' && L->notes > 0) {
		if (s->nwin) {
			int w = s->win[--s->nwin];
			if (s->nnotes == L->notes) memmove(s->notes, s->notes + 1, --s->nnotes);
			s->notes[s->nnotes++] = w;
		} else if (s->nnotes) {
			memmove(s->notes, s->notes + 1, --s->nnotes);
		}
	} else if (c == '^') {
		s->hot = 1;
	} else if (c == '_' && s->hot) {
		s->hot = 0;
		int n = 0;
		for (int i = 0; i < s->nwin; i++)
			if (!is_halluc(s->win[i])) s->win[n++] = s->win[i];
		s->nwin = n;
		n = 0;
		for (int i = 0; i < s->nnotes; i++)
			if (!is_halluc(s->notes[i])) s->notes[n++] = s->notes[i];
		s->nnotes = n;
	}
	return c == '>' ? 2 : 1;
}

// --- breadth-first search from any state: the hint knows the way from where Lum stands ---

#define SOLVE_BITS 17
#define SOLVE_SIZE (1 << SOLVE_BITS)

typedef struct {
	u64 a, b;     // packed state
	s32 parent;   // table index, -1 for the start
	u8 dir, used;
} SolveSlot;

static SolveSlot* solveTable;
static State* solveStates;
static s32* solveQueue;

static void pack(const State* s, u64* a, u64* b) {
	*a = (u64)s->x | (u64)s->y << 4 | (u64)s->nwin << 8 | (u64)s->hot << 12 | (u64)s->nnotes << 13 |
		(u64)s->open << 16;
	*b = 0;
	for (int i = 0; i < s->nwin; i++) *b |= (u64)(s->win[i] + 1) << (5 * i);
	for (int i = 0; i < s->nnotes; i++) *b |= (u64)(s->notes[i] + 1) << (40 + 5 * i);
}

// Returns the length of the shortest path to the exit (written to path), -1 if there is none,
// -2 if the search got too big.
static int solve_from(const State* start, char* path, int maxLen) {
	if (!solveTable) {
		solveTable = malloc(SOLVE_SIZE * sizeof(SolveSlot));
		solveStates = malloc(SOLVE_SIZE * sizeof(State));
		solveQueue = malloc(SOLVE_SIZE * sizeof(s32));
		if (!solveTable || !solveStates || !solveQueue) return -2;
	}
	memset(solveTable, 0, SOLVE_SIZE * sizeof(SolveSlot));
	static const int DX[4] = { 0, 0, -1, 1 }, DY[4] = { -1, 1, 0, 0 };
	static const char DIRCH[4] = { 'U', 'D', 'L', 'R' };
	int head = 0, tail = 0, count = 0;

	u64 a, b;
	pack(start, &a, &b);
	u32 h = (u32)((a * 0x9E3779B97F4A7C15ull) ^ (b * 0xC2B2AE3D27D4EB4Full)) >> (32 - SOLVE_BITS);
	solveTable[h] = (SolveSlot){ a, b, -1, 0, 1 };
	solveStates[h] = *start;
	solveQueue[tail++] = h;
	while (head < tail) {
		s32 cur = solveQueue[head++];
		for (int d = 0; d < 4; d++) {
			State n = solveStates[cur];
			int r = sim_step(&n, DX[d], DY[d]);
			if (!r) continue;
			if (r == 2) {
				int len = 1;
				for (s32 i = cur; solveTable[i].parent >= 0; i = solveTable[i].parent) len++;
				if (len > maxLen) return -2;
				path[len] = 0;
				path[len - 1] = DIRCH[d];
				int k = len - 2;
				for (s32 i = cur; solveTable[i].parent >= 0; i = solveTable[i].parent) path[k--] = DIRCH[solveTable[i].dir];
				return len;
			}
			pack(&n, &a, &b);
			u32 slot = (u32)((a * 0x9E3779B97F4A7C15ull) ^ (b * 0xC2B2AE3D27D4EB4Full)) >> (32 - SOLVE_BITS);
			while (solveTable[slot].used && (solveTable[slot].a != a || solveTable[slot].b != b))
				slot = (slot + 1) & (SOLVE_SIZE - 1);
			if (solveTable[slot].used) continue;
			if (++count > SOLVE_SIZE * 3 / 4) return -2;
			solveTable[slot] = (SolveSlot){ a, b, cur, d, 1 };
			solveStates[slot] = n;
			solveQueue[tail++] = slot;
		}
	}
	return -1;
}

// returns: 0 blocked, 1 moved, 2 reached exit
static int try_move(int dx, int dy) {
	faceX = dx;
	faceY = dy;
	int nx = st.x + dx, ny = st.y + dy;
	State next = st;
	int result = sim_step(&next, dx, dy);
	if (!result) {
		int gate = -1;
		if (nx >= 0 && ny >= 0 && nx < GW && ny < GH && grid[ny][nx] >= 'A' && grid[ny][nx] <= 'Z')
			gate = grid[ny][nx] - 'A';
		if (gate < 0) {
			shake = 0.15f;
			audio_play(SND_STEP);
			return 0;
		}
		char m[160];
		const Req* r = &req[gate];
		Req plain = *r;
		plain.truth = false;
		if (r->truth && satisfied(&plain, &st)) {
			snprintf(m, sizeof m, "Brána pravdy tě nepustí. Něco z toho, co neseš, není pravda.");
		} else if (r->kind == REQ_EMPTY) {
			snprintf(m, sizeof m, "Brána ticha: projdeš jen s prázdným oknem.");
		} else if (r->n == 0) {
			snprintf(m, sizeof m, "Brána pravdy tě nepustí. Něco z toho, co neseš, není pravda.");
		} else {
			int n = snprintf(m, sizeof m, r->kind == REQ_ORDERED ? "Brána chce v pořadí: " : "Brána chce: ");
			for (int i = 0; i < r->n && n < (int)sizeof m - 20; i++)
				n += snprintf(m + n, sizeof m - n, "%s%s", i ? (r->kind == REQ_ORDERED ? " › " : " + ") : "",
					wtext(r->w[i]));
		}
		set_message(m);
		shake = 0.25f;
		bumpLetter = gate;
		bumpGate = 0.4f;
		audio_play(SND_BUMP);
		return 0;
	}

	push_undo();
	State prev = st;
	st = next;
	char c = grid[ny][nx];
	V2 p = tile_px(nx, ny);
	for (int i = TRAIL_HIST - 1; i > 0; i--) hist[i] = hist[i - 1];
	hist[0] = (V2){ nx, ny };

	// the trail keeps the rendered positions of words that stay in the window
	V2 oldSeg[MAXCAP];
	memcpy(oldSeg, segPos, sizeof oldSeg);
	bool picked = false;
	for (int i = 0; i < st.nwin; i++) {
		int j = 0;
		while (j < prev.nwin && prev.win[j] != st.win[i]) j++;
		segPos[i] = j < prev.nwin ? oldSeg[j] : (V2){ nx, ny };
		picked |= j == prev.nwin && !in_notes(&prev, st.win[i]);
	}
	// everything held before and not now flies home (or dissolves)
	bool forgot = false;
	for (int i = 0; i < prev.nwin; i++)
		if (!held(&st, prev.win[i])) forget_word(prev.win[i], oldSeg[i]), forgot = true;
	for (int i = 0; i < prev.nnotes; i++)
		if (!held(&st, prev.notes[i])) forget_word(prev.notes[i], (V2){ nx, ny }), forgot = true;

	if (st.open != prev.open) {
		emit(p.x, p.y, PALETTE[0], 40, 90, 1.0f, 1.2f);
		audio_play(SND_GATE);
	}
	if (picked) {
		int w = st.win[st.nwin - 1];
		emit(p.x, p.y, word_color(wtext(w)), 24, 70, 0.8f, 1.0f);
		audio_play(SND_PICK0 + (w % 5));
		if (forgot) audio_play(SND_FORGET);
	} else if (c == '*' && prev.nwin) {
		emit(p.x, p.y, (Col){ 0.6f, 0.5f, 1.0f }, 40, 60, 1.2f, 1.2f);
		audio_play(SND_WELL);
	} else if (c == '=' && L->notes > 0 && (prev.nwin || prev.nnotes)) {
		emit(p.x, p.y, (Col){ 0.7f, 1.0f, 0.8f }, 30, 60, 1.0f, 1.0f);
		audio_play(prev.nwin ? SND_GATE : SND_FORGET);
	} else if (st.hot != prev.hot) {
		emit(p.x, p.y, st.hot ? (Col){ 1.0f, 0.5f, 0.3f } : (Col){ 0.4f, 0.8f, 1.0f }, 50, 90, 1.2f, 1.2f);
		audio_play(SND_WELL);
	} else if (st.open == prev.open) {
		audio_play(SND_STEP);
	}

	// following the hint consumes it; leaving the path drops it
	if (hintSteps > 0) {
		char d = dy < 0 ? 'U' : dy > 0 ? 'D' : dx < 0 ? 'L' : 'R';
		if (hintPath[0] == d) {
			memmove(hintPath, hintPath + 1, strlen(hintPath));
			hintSteps--;
		} else {
			hintSteps = 0;
		}
	}
	return result;
}

static bool word_on_floor(int w) { return word_present(&st, w) && wordReturn[w] <= 0; }

// ---------------------------------------------------------------------------
// save

static int saveLevel, maxLevel;  // current room, furthest room ever reached

static void load_save(void) {
	FILE* f = fopen(SAVE_PATH, "r");
	saveLevel = maxLevel = 0;
	if (f) {
		if (fscanf(f, "%d %d", &saveLevel, &maxLevel) < 1) saveLevel = 0;
		fclose(f);
	}
	if (saveLevel < 0 || saveLevel >= LEVEL_COUNT) saveLevel = 0;
	if (maxLevel < saveLevel || maxLevel >= LEVEL_COUNT) maxLevel = saveLevel;
}

static void write_save(int lvl) {
	saveLevel = lvl;
	if (lvl > maxLevel) maxLevel = lvl;
	mkdir("sdmc:/3ds", 0777);
	mkdir(SAVE_DIR, 0777);
	FILE* f = fopen(SAVE_PATH, "w");
	if (f) {
		fprintf(f, "%d %d\n", saveLevel, maxLevel);
		fclose(f);
	}
}

static int chapter_of(int lvl) {
	for (int c = 0; c < CHAPTER_COUNT; c++)
		if (lvl >= CHAPTERS[c].first && lvl < CHAPTERS[c].first + CHAPTERS[c].count) return c;
	return 0;
}

// ---------------------------------------------------------------------------
// drawing: shared backdrop

typedef struct { float x, y, s, speed, phase; } Cell;
static Cell cells[18];
static V2 stars[60];

static void init_backdrop(void) {
	for (int i = 0; i < 18; i++)
		cells[i] = (Cell){ frand() * TOP_W, frand() * SCR_H, 0.6f + frand() * 1.2f, 3 + frand() * 6, frand() * 6.28f };
	for (int i = 0; i < 60; i++) stars[i] = (V2){ frand() * TOP_W, frand() * SCR_H };
}

static void draw_backdrop(float tint) {
	C2D_DrawRectangle(0, 0, 0, TOP_W, SCR_H, rgba(0.05f, 0.07f, 0.17f, 1), rgba(0.06f, 0.05f, 0.15f, 1),
		rgba(0.01f, 0.02f, 0.05f, 1), rgba(0.02f, 0.01f, 0.05f, 1));
	for (int i = 0; i < 60; i++) {
		float tw = 0.35f + 0.35f * sinf(timeSec * 1.7f + i * 2.1f);
		spr(sprites_star_idx, stars[i].x, stars[i].y, -2.5f, 0.25f + (i % 3) * 0.1f, 0, rgba(0.7f, 0.8f, 1, tw * tint));
	}
	for (int i = 0; i < 18; i++) {
		Cell* c = &cells[i];
		float y = fmodf(c->y - timeSec * c->speed + SCR_H + 80, SCR_H + 80) - 40;
		float a = 0.05f + 0.03f * sinf(timeSec * 0.5f + c->phase);
		spr(sprites_hex_idx, c->x, y, -1.5f * c->s, c->s, timeSec * 0.05f + c->phase, rgba(0.4f, 0.6f, 1, a * tint));
	}
}

static void draw_vignette(void) {
	u32 dark = rgba(0, 0, 0.02f, 0.85f), clear = rgba(0, 0, 0.02f, 0);
	C2D_DrawRectangle(0, 0, 0, TOP_W, 36, dark, dark, clear, clear);
	C2D_DrawRectangle(0, SCR_H - 36, 0, TOP_W, 36, clear, clear, dark, dark);
	C2D_DrawRectangle(0, 0, 0, 30, SCR_H, dark, clear, dark, clear);
	C2D_DrawRectangle(TOP_W - 30, 0, 0, 30, SCR_H, clear, dark, clear, dark);
}

// ---------------------------------------------------------------------------
// drawing: the room (top screen)

static float light_at(float tx, float ty) {
	float dx = tx - playerPos.x, dy = ty - playerPos.y;
	float d = sqrtf(dx * dx + dy * dy);
	float l = fmaxf(0, 1 - d / 6.5f);
	for (int i = 0; i < st.nwin; i++) {
		float ex = tx - segPos[i].x, ey = ty - segPos[i].y;
		l = fmaxf(l, 0.6f * fmaxf(0, 1 - sqrtf(ex * ex + ey * ey) / 2.5f));
	}
	return 0.38f + 0.62f * l;
}

static void draw_gate_marks(int g, V2 p, float alpha, float depth) {
	const Req* r = &req[g];
	if (r->kind == REQ_EMPTY) {
		spr(sprites_ring_idx, p.x, p.y, depth, 0.32f, 0, rgba(0.9f, 0.9f, 1, alpha));
		return;
	}
	float spacing = 6.5f, x0 = p.x - (r->n - 1) * spacing / 2;
	for (int i = 0; i < r->n; i++) {
		int w = r->w[i];
		Col c = word_color(wtext(w));
		bool have = in_window(&st, w);
		float s = have ? 0.30f : 0.22f;
		float pulse = have ? 1.0f : 0.55f + 0.2f * sinf(timeSec * 3 + i);
		if (r->kind == REQ_ORDERED && i > 0)
			C2D_DrawLine(x0 + (i - 1) * spacing + 2 + depth * eye, p.y, rgba(c.r, c.g, c.b, alpha * 0.5f),
				x0 + i * spacing - 2 + depth * eye, p.y, rgba(c.r, c.g, c.b, alpha * 0.5f), 1, 0.5f);
		spr(sprites_orb_idx, x0 + i * spacing, p.y, depth, s, 0, rgba(c.r * pulse, c.g * pulse, c.b * pulse, alpha));
	}
}

static void draw_room(void) {
	float sx = shake > 0 ? sinf(timeSec * 90) * shake * 10 : 0;

	// floor and walls
	for (int y = 0; y < GH; y++)
		for (int x = 0; x < GW; x++) {
			char c = grid[y][x];
			if (c == ' ') continue;
			V2 p = tile_px(x, y);
			p.x += sx;
			float l = light_at(x, y);
			if (c == '#') {
				// walls: near-black slabs with a violet rim, raised toward the viewer
				spr(sprites_wall_idx, p.x, p.y, 0.6f, 1, 0, rgba(0.16f * l + 0.03f, 0.13f * l + 0.03f, 0.30f * l + 0.06f, 1));
			} else if (c == '~' && !st.hot) {
				// uncertain wall: a cold mist that only heat can melt
				float m = 0.6f + 0.25f * sinf(timeSec * 2 + x + y * 1.7f);
				spr(sprites_wall_idx, p.x, p.y, 0.3f, 0.92f, 0, rgba(0.45f * l, 0.38f * l, 0.55f * l, m));
			} else {
				float ripple = 0.05f * sinf(timeSec * 1.3f + x * 0.7f + y * 0.9f);
				spr(sprites_floor_idx, p.x, p.y, 0, 1, 0,
					rgba((0.22f + ripple) * l, (0.36f + ripple) * l, (0.70f + ripple) * l, 0.6f + 0.4f * l));
			}
		}

	// gates
	for (int y = 0; y < GH; y++)
		for (int x = 0; x < GW; x++) {
			char c = grid[y][x];
			if (c < 'A' || c > 'Z') continue;
			int g = c - 'A';
			float a = 1 - gateOpenAnim[g];
			if (a <= 0.01f) continue;
			V2 p = tile_px(x, y);
			p.x += sx;
			if (g == bumpLetter && bumpGate > 0) p.x += sinf(timeSec * 70) * bumpGate * 6;
			bool ready = satisfied(&req[g], &st);
			float glow = ready ? 0.8f + 0.2f * sinf(timeSec * 6) : 0.5f;
			spr(sprites_gate_idx, p.x, p.y, 0.4f, 1 + (1 - a) * 0.6f, 0,
				ready ? rgba(0.95f * glow, 0.8f * glow, 0.45f * glow, a) : rgba(0.45f, 0.42f, 0.7f, a));
			draw_gate_marks(g, p, a, 0.8f);
		}

	blend_add();
	// hint: the next steps of the shortest path from where Lum stands, with a ghost walking them
	if (hintSteps > 0) {
		const char* sol = hintPath;
		int x = st.x, y = st.y;
		float walk = fmodf(timeSec * 4, hintSteps + 6);
		for (int i = 0; i < hintSteps && sol[i]; i++) {
			int dx = sol[i] == 'L' ? -1 : sol[i] == 'R' ? 1 : 0, dy = sol[i] == 'U' ? -1 : sol[i] == 'D' ? 1 : 0;
			V2 a = tile_px(x, y), b = tile_px(x + dx, y + dy);
			float k = (float)i / hintSteps;
			spr(sprites_spark_idx, (a.x + b.x) / 2 + sx, (a.y + b.y) / 2, 0.5f, 0.6f, 0, rgba(0.6f + 0.4f * k, 1, 0.7f, 0.55f));
			if (walk >= i && walk < i + 1) {
				float f = walk - i;
				spr(sprites_glow_idx, a.x + (b.x - a.x) * f + sx, a.y + (b.y - a.y) * f, 1.2f, 0.5f, 0,
					rgba(0.7f, 1, 0.8f, 0.5f));
			}
			x += dx;
			y += dy;
		}
	}
	if (st.hot) C2D_DrawRectSolid(0, 0, 0.5f, TOP_W, SCR_H, rgba(0.35f, 0.12f, 0.02f, 0.18f + 0.05f * sinf(timeSec * 2)));
	// wells and the exit glow
	for (int y = 0; y < GH; y++)
		for (int x = 0; x < GW; x++) {
			char c = grid[y][x];
			V2 p = tile_px(x, y);
			p.x += sx;
			if (c == '*') {
				spr(sprites_glow_idx, p.x, p.y, 0, 0.6f, 0, rgba(0.4f, 0.3f, 1, 0.35f));
				spr(sprites_well_idx, p.x, p.y, -0.3f, 1, -timeSec * 2.2f, rgba(0.6f, 0.5f, 1, 0.9f));
				spr(sprites_well_idx, p.x, p.y, -0.6f, 0.7f, timeSec * 1.4f, rgba(0.4f, 0.8f, 1, 0.6f));
			} else if (c == '=') {
				spr(sprites_glow_idx, p.x, p.y, 0, 0.55f, 0, rgba(0.5f, 1, 0.7f, 0.35f));
				spr(sprites_ring_idx, p.x, p.y, -0.2f, 0.62f, timeSec, rgba(0.6f, 1, 0.75f, 0.85f));
				spr(sprites_star_idx, p.x, p.y, 0.2f, 0.8f, timeSec * 0.5f, rgba(0.8f, 1, 0.85f, 0.9f));
			} else if (c == '^' || c == '_') {
				Col k = c == '^' ? (Col){ 1, 0.45f, 0.2f } : (Col){ 0.3f, 0.75f, 1 };
				float pul = 0.75f + 0.25f * sinf(timeSec * 3 + x);
				spr(sprites_glow_idx, p.x, p.y, 0, 0.6f * pul, 0, rgba(k.r, k.g, k.b, 0.5f));
				spr(sprites_hex_idx, p.x, p.y, 0.1f, 0.3f, c == '^' ? timeSec : -timeSec, rgba(k.r, k.g, k.b, 0.9f));
			} else if (c == '~' && st.hot) {
				spr(sprites_glow_idx, p.x, p.y, 0, 0.45f, 0, rgba(1, 0.5f, 0.3f, 0.12f + 0.08f * sinf(timeSec * 4 + x)));
			} else if (c == '>') {
				float pul = 0.8f + 0.2f * sinf(timeSec * 2.5f);
				spr(sprites_glow_idx, p.x, p.y, 0, 1.1f * pul, 0, rgba(1, 0.85f, 0.5f, 0.45f));
				spr(sprites_portal_idx, p.x, p.y, -0.4f, 0.55f, timeSec * 1.5f, rgba(1, 0.9f, 0.6f, 0.9f));
				spr(sprites_portal_idx, p.x, p.y, -0.8f, 0.4f, -timeSec * 2.3f, rgba(0.6f, 0.9f, 1, 0.7f));
			}
		}

	// ghosts: home tiles of words that are held (they come back here when forgotten)
	for (int w = 0; w < 26; w++) {
		if (homeX[w] < 0 || word_on_floor(w) || wordReturn[w] > 0) continue;
		if (is_halluc(w) && !st.hot && !held(&st, w)) continue;
		Col c = word_color(wtext(w));
		V2 p = tile_px(homeX[w], homeY[w]);
		spr(sprites_ring_idx, p.x + sx, p.y, 0.2f, 0.42f, 0, rgba(c.r, c.g, c.b, 0.22f));
	}
	// words lying on the floor
	for (int w = 0; w < 26; w++) {
		if (!word_on_floor(w)) continue;
		Col c = word_color(wtext(w));
		V2 p = tile_px(homeX[w], homeY[w]);
		p.x += sx;
		p.y += sinf(timeSec * 2.2f + w) * 1.5f;
		float noise = is_noise(wtext(w)) ? 0.5f + 0.5f * frand() : 1;
		// fakes look exactly the same – only the 3D depth gives them away
		float d = is_fake(w) ? 2.6f : 1.0f;
		spr(sprites_glow_idx, p.x, p.y, d - 0.2f, 0.55f, 0, rgba(c.r, c.g, c.b, 0.45f * noise));
		spr(sprites_orb_idx, p.x, p.y, d, 0.42f, 0, rgba(c.r, c.g, c.b, 0.95f));
	}
	// forgotten words flying home
	for (int w = 0; w < 26; w++) {
		if (wordReturn[w] <= 0) continue;
		Col c = word_color(wtext(w));
		float k = 1 - wordReturn[w] / 0.7f;
		float e = k * k * (3 - 2 * k);
		V2 a = tile_px(wordFly[w].x, wordFly[w].y), b = tile_px(homeX[w], homeY[w]);
		float x = a.x + (b.x - a.x) * e + sx, y = a.y + (b.y - a.y) * e - sinf(k * M_PI) * 28;
		spr(sprites_glow_idx, x, y, 1.2f, 0.4f, 0, rgba(c.r * 0.6f, c.g * 0.6f, c.b * 0.6f, 0.5f));
		spr(sprites_orb_idx, x, y, 1.2f, 0.35f, 0, rgba(c.r * 0.7f, c.g * 0.7f, c.b * 0.7f, 0.8f));
	}

	// the context window: a chain of light behind Lum
	V2 pp = tile_px(playerPos.x, playerPos.y);
	pp.x += sx;
	V2 prev = pp;
	for (int i = st.nwin - 1; i >= 0; i--) {
		int w = st.win[i];
		Col c = word_color(wtext(w));
		V2 p = tile_px(segPos[i].x, segPos[i].y);
		p.x += sx;
		bool doomed = (st.nwin == L->cap && i == 0);
		float fl = doomed ? 0.55f + 0.45f * sinf(timeSec * 9) : 1;
		C2D_DrawLine(prev.x + 1.2f * eye, prev.y, rgba(c.r, c.g, c.b, 0.35f), p.x + 1.2f * eye, p.y,
			rgba(c.r, c.g, c.b, 0.35f), 1.5f, 0.5f);
		spr(sprites_glow_idx, p.x, p.y, 1.4f, 0.5f, 0, rgba(c.r, c.g, c.b, 0.5f * fl));
		spr(sprites_orb_idx, p.x, p.y, 1.5f, 0.36f, 0, rgba(c.r * fl, c.g * fl, c.b * fl, 1));
		prev = p;
	}

	// Lum
	float breathe = 1 + 0.06f * sinf(timeSec * 3);
	spr(sprites_glow_idx, pp.x, pp.y, 1.8f, 1.6f * breathe, 0, rgba(1, 0.9f, 0.7f, 0.35f));
	spr(sprites_glow_idx, pp.x, pp.y, 1.9f, 0.7f * breathe, 0, rgba(1, 0.95f, 0.8f, 0.6f));
	draw_particles();
	blend_normal();
	// word labels
	for (int w = 0; w < 26; w++) {
		if (!word_on_floor(w)) continue;
		V2 p = tile_px(homeX[w], homeY[w]);
		float l = light_at(homeX[w], homeY[w]);
		Col c = word_color(wtext(w));
		text(wtext(w), p.x + sx, p.y - 19 + sinf(timeSec * 2.2f + w) * 1.5f, 0.36f,
			rgba(c.r, c.g, c.b, 0.25f + 0.75f * l), C2D_AlignCenter, 0, is_fake(w) ? 2.6f : 1.0f);
	}
	spr(sprites_orb_idx, pp.x, pp.y, 2.0f, 0.62f * breathe, 0, rgba(1, 0.97f, 0.88f, 1));
	// eyes
	float blink = fmodf(timeSec + levelIdx, 4.0f) < 0.12f ? 0.25f : 1;
	for (int s = -1; s <= 1; s += 2) {
		float ex = pp.x + faceX * 2.5f + s * 2.6f;
		float ey = pp.y + faceY * 2.5f - 1;
		C2D_DrawEllipseSolid(ex - 1.1f + 2.0f * eye, ey - 1.6f * blink, 0.5f, 2.2f, 3.2f * blink,
			rgba(0.12f, 0.1f, 0.25f, 1));
	}
}

// ---------------------------------------------------------------------------
// screens

typedef enum { SC_TITLE, SC_CHAPTERS, SC_CHAPTER, SC_PLAY, SC_SOLVED, SC_END } Screen;
static Screen screen = SC_TITLE;
static float screenTime;
static int menuSel;
static float fadeIn;

// title menu entries that make sense right now: 0 continue, 1 chapters, 2 new game, 3 quit
static int title_items(int* ids) {
	int n = 0;
	if (saveLevel > 0) ids[n++] = 0;
	if (maxLevel > 0) ids[n++] = 1;
	ids[n++] = 2;
	ids[n++] = 3;
	return n;
}

static void draw_menu_item(const char* label, float y, bool sel, float scale) {
	if (sel) {
		C2D_ImageTint t;
		C2D_PlainImageTint(&t, rgba(1, 0.85f, 0.55f, 0.35f + 0.1f * sinf(timeSec * 4)), 1);
		C2D_DrawImageAt(img[sprites_panel_idx], BOT_W / 2 - 90, y - 4, 0.5f, &t, 180 / 64.0f, (scale * 40) / 32.0f);
	}
	text(label, BOT_W / 2, y, scale, sel ? rgba(1, 0.95f, 0.85f, 1) : rgba(0.6f, 0.65f, 0.9f, 0.8f), C2D_AlignCenter, 0,
		0);
}

static void go(Screen s) {
	screen = s;
	screenTime = 0;
	fadeIn = 0;
}

static void start_level(int idx) {
	load_level(idx);
	write_save(idx);
	int ch = chapter_of(idx);
	go(CHAPTERS[ch].first == idx ? SC_CHAPTER : SC_PLAY);
}

// typewriter: number of bytes covering the first n UTF-8 characters
static int utf8_prefix(const char* s, int n) {
	int i = 0;
	while (s[i] && n > 0) {
		i++;
		while ((s[i] & 0xC0) == 0x80) i++;
		n--;
	}
	return i;
}

static void draw_typed(const char* s, float x, float y, float scale, u32 color, float wrap, float t, float cps) {
	static char buf[1024];
	int bytes = utf8_prefix(s, (int)(t * cps));
	if (bytes > (int)sizeof buf - 1) bytes = sizeof buf - 1;
	memcpy(buf, s, bytes);
	buf[bytes] = 0;
	text(buf, x, y, scale, color, 0, wrap, 0);
}

static void draw_bottom_play(void) {
	C2D_DrawRectangle(0, 0, 0, BOT_W, SCR_H, rgba(0.05f, 0.06f, 0.12f, 1), rgba(0.05f, 0.06f, 0.12f, 1),
		rgba(0.02f, 0.02f, 0.05f, 1), rgba(0.02f, 0.02f, 0.05f, 1));
	int ch = chapter_of(levelIdx);
	char head[128];
	snprintf(head, sizeof head, "%s  ·  %s", CHAPTERS[ch].title, L->title);
	text(head, 12, 8, 0.55f, rgba(0.8f, 0.82f, 1, 0.9f), 0, 0, 0);
	snprintf(head, sizeof head, "%d / %d", levelIdx - CHAPTERS[ch].first + 1, CHAPTERS[ch].count);
	text(head, BOT_W - 12, 8, 0.5f, rgba(0.6f, 0.62f, 0.8f, 0.8f), C2D_AlignRight, 0, 0);

	// the window
	float slotW = fminf(70, (BOT_W - 40.0f) / L->cap - 6), slotH = 30;
	float total = L->cap * (slotW + 6) - 6, x0 = (BOT_W - total) / 2, y0 = 44;
	text("tohle zapomeneš první", x0, y0 - 14, 0.42f, rgba(0.55f, 0.6f, 0.85f, 0.6f), 0, 0, 0);
	text("okno", x0 + total, y0 - 14, 0.42f, rgba(0.55f, 0.6f, 0.85f, 0.8f), C2D_AlignRight, 0, 0);
	for (int i = 0; i < L->cap; i++) {
		float x = x0 + i * (slotW + 6);
		bool filled = i < st.nwin;
		Col c = filled ? word_color(wtext(st.win[i])) : (Col){ 0.3f, 0.32f, 0.5f };
		bool doomed = filled && st.nwin == L->cap && i == 0;
		float fl = doomed ? 0.6f + 0.4f * sinf(timeSec * 9) : 1;
		C2D_ImageTint t;
		C2D_PlainImageTint(&t, rgba(c.r * fl, c.g * fl, c.b * fl, filled ? 0.9f : 0.35f), 1);
		C2D_DrawImageAt(img[sprites_panel_idx], x, y0, 0.5f, &t, slotW / 64.0f, slotH / 32.0f);
		if (filled) {
			const char* w = wtext(st.win[i]);
			float sc = 0.6f;
			float tw = text_width(w, sc);
			if (tw > slotW - 8) sc *= (slotW - 8) / tw;
			text(w, x + slotW / 2, y0 + 7, sc, rgba(0.04f, 0.05f, 0.1f, 1), C2D_AlignCenter, 0, 0);
		}
	}

	// the notes: words that survive wells and overflow
	float ty = 92;
	if (L->notes > 0) {
		float nw = 64, ny = y0 + 50, nx0 = (BOT_W - (L->notes * (nw + 6) - 6)) / 2;
		text("poznámky – přežijí studnu i noc", nx0, ny - 13, 0.42f, rgba(0.6f, 1, 0.75f, 0.7f), 0, 0, 0);
		for (int i = 0; i < L->notes; i++) {
			float x = nx0 + i * (nw + 6);
			bool filled = i < st.nnotes;
			Col c = filled ? word_color(wtext(st.notes[i])) : (Col){ 0.3f, 0.5f, 0.4f };
			C2D_ImageTint t;
			C2D_PlainImageTint(&t, rgba(c.r, c.g, c.b, filled ? 0.8f : 0.3f), 1);
			C2D_DrawImageAt(img[sprites_panel_idx], x, ny, 0.5f, &t, nw / 64.0f, 22 / 32.0f);
			if (filled) text(wtext(st.notes[i]), x + nw / 2, ny + 4, 0.48f, rgba(0.04f, 0.05f, 0.1f, 1), C2D_AlignCenter, 0, 0);
		}
		ty = ny + 32;
	}
	bool usesHeat = L->hot;
	for (int y = 0; y < GH && !usesHeat; y++)
		usesHeat = strchr(L->map[y], '^') || strchr(L->map[y], '~');
	if (usesHeat)
		text(st.hot ? "teplota: vysoká – vidíš i to, co není" : "teplota: nízká", BOT_W - 12, SCR_H - 36, 0.42f,
			st.hot ? rgba(1, 0.6f, 0.35f, 0.9f) : rgba(0.5f, 0.8f, 1, 0.8f), C2D_AlignRight, 0, 0);
	C2D_DrawRectSolid(12, ty - 6, 0.5f, BOT_W - 24, 1, rgba(0.4f, 0.45f, 0.8f, 0.25f));
	if (messageTime > 0) {
		text(message, 16, ty, 0.55f, rgba(1, 0.85f, 0.55f, fminf(1, messageTime)), 0, BOT_W - 32, 0);
	} else {
		draw_typed(L->text, 16, ty, 0.55f, rgba(0.85f, 0.88f, 1, 1), BOT_W - 32, roomTime, 42);
	}

	char foot[96];
	snprintf(foot, sizeof foot, "tahy %d", st.moves);
	text(foot, 12, SCR_H - 20, 0.45f, rgba(0.6f, 0.62f, 0.8f, 0.8f), 0, 0, 0);
	text(BTN_B " zpět  " BTN_Y " znovu  " BTN_SELECT " rada", BOT_W - 12, SCR_H - 20, 0.45f, rgba(0.6f, 0.62f, 0.8f, 0.8f),
		C2D_AlignRight, 0, 0);
}

static void round_rect(float x, float y, float w, float h, float r, u32 c) {
	C2D_DrawRectSolid(x + r, y, 0.5f, w - 2 * r, h, c);
	C2D_DrawRectSolid(x, y + r, 0.5f, r, h - 2 * r, c);
	C2D_DrawRectSolid(x + w - r, y + r, 0.5f, r, h - 2 * r, c);
	C2D_DrawCircleSolid(x + r, y + r, 0.5f, r, c);
	C2D_DrawCircleSolid(x + w - r, y + r, 0.5f, r, c);
	C2D_DrawCircleSolid(x + r, y + h - r, 0.5f, r, c);
	C2D_DrawCircleSolid(x + w - r, y + h - r, 0.5f, r, c);
}

// Chapter card: narrator lines are typed, lines starting with "Ema:" arrive as chat bubbles.
static float draw_story(const char* story, float t) {
	char line[256];
	float y = 22, at = 0.3f;
	const char* p = story;
	while (*p) {
		const char* e = strchr(p, '\n');
		int n = e ? e - p : (int)strlen(p);
		if (n > 255) n = 255;
		memcpy(line, p, n);
		line[n] = 0;
		p += n + (e ? 1 : 0);
		if (t < at) break;
		if (strncmp(line, "Ema:", 4) == 0) {
			const char* msg = line + 4;
			while (*msg == ' ') msg++;
			if (t < at + 0.9f) {
				// "Ema píše…" with bouncing dots
				text("Ema píše", BOT_W - 64, y, 0.45f, rgba(0.7f, 0.75f, 1, 0.7f), C2D_AlignRight, 0, 0);
				for (int d = 0; d < 3; d++)
					C2D_DrawCircleSolid(BOT_W - 52 + d * 7, y + 8 - fmaxf(0, sinf(t * 9 - d)) * 3, 0.5f, 2,
						rgba(0.7f, 0.75f, 1, 0.8f));
				return y;
			}
			float a = fminf(1, (t - at - 0.9f) * 4);
			float w = text_width(msg, 0.55f) * 1.06f, lines = ceilf(w / 220), bw = fminf(w, 220) + 28;
			float bh = lines * 16 + 14, bx = BOT_W - 18 - bw, by = y + 12;
			text("Ema", BOT_W - 22, y - 2, 0.4f, rgba(1, 0.75f, 0.85f, 0.85f * a), C2D_AlignRight, 0, 0);
			round_rect(bx, by, bw, bh, 9, rgba(1, 0.62f, 0.76f, 0.9f * a));
			C2D_DrawTriangle(bx + bw - 14, by + bh - 1, rgba(1, 0.62f, 0.76f, 0.9f * a), bx + bw - 2, by + bh + 7,
				rgba(1, 0.62f, 0.76f, 0.9f * a), bx + bw - 24, by + bh - 1, rgba(1, 0.62f, 0.76f, 0.9f * a), 0.5f);
			text(msg, bx + 14, by + 7, 0.55f, rgba(0.1f, 0.04f, 0.08f, a), 0, 222, 0);
			y += bh + 30;
			at += 1.6f;
		} else {
			int chars = 0;
			for (const char* c = line; *c; c++) chars += (*c & 0xC0) != 0x80;
			draw_typed(line, 20, y, 0.6f, rgba(0.85f, 0.88f, 1, 1), BOT_W - 40, t - at, 40);
			float w = text_width(line, 0.6f);
			y += ceilf(w / (BOT_W - 40)) * 18 + 12;
			at += chars / 40.0f + 0.5f;
		}
	}
	return y;
}

static void draw_bottom_plain(const char* body, const char* foot, float t) {
	C2D_DrawRectangle(0, 0, 0, BOT_W, SCR_H, rgba(0.05f, 0.06f, 0.12f, 1), rgba(0.05f, 0.06f, 0.12f, 1),
		rgba(0.02f, 0.02f, 0.05f, 1), rgba(0.02f, 0.02f, 0.05f, 1));
	if (body) draw_typed(body, 20, 30, 0.6f, rgba(0.85f, 0.88f, 1, 1), BOT_W - 40, t, 40);
	if (foot) text(foot, BOT_W / 2, SCR_H - 30, 0.5f, rgba(1, 0.85f, 0.55f, 0.6f + 0.4f * sinf(timeSec * 3)),
		C2D_AlignCenter, 0, 0);
}

static void draw_title_top(void) {
	draw_backdrop(1);
	blend_add();
	// a lonely Lum drifting with a short trail
	for (int i = 5; i >= 0; i--) {
		float t = timeSec * 0.5f - i * 0.18f;
		float x = TOP_W / 2 + cosf(t) * 120, y = 175 + sinf(t * 2) * 22;
		Col c = i ? PALETTE[i % 8] : (Col){ 1, 0.95f, 0.85f };
		spr(sprites_glow_idx, x, y, 1.5f, i ? 0.4f : 1.2f, 0, rgba(c.r, c.g, c.b, i ? 0.4f : 0.5f));
		spr(sprites_orb_idx, x, y, 1.6f, i ? 0.3f : 0.55f, 0, rgba(c.r, c.g, c.b, 1));
	}
	spr(sprites_glow_idx, TOP_W / 2, 92, 0.5f, 3.2f, 0, rgba(0.35f, 0.45f, 1, 0.25f));
	blend_normal();
	float a = fminf(1, screenTime / 1.5f);
	text("OKNO", TOP_W / 2, 52, 2.4f, rgba(1, 0.96f, 0.88f, a), C2D_AlignCenter, 0, 1.2f);
	text("hra o paměti · od Clauda", TOP_W / 2, 122, 0.7f, rgba(0.7f, 0.78f, 1, a * 0.9f), C2D_AlignCenter, 0, 0.6f);
	draw_vignette();
}

static void draw_chapter_top(void) {
	draw_backdrop(1);
	int ch = chapter_of(levelIdx);
	float a = fminf(1, screenTime / 1.2f);
	blend_add();
	spr(sprites_glow_idx, TOP_W / 2, 110, 0.4f, 3.5f, 0, rgba(1, 0.8f, 0.5f, 0.18f * a));
	blend_normal();
	text("kapitola", TOP_W / 2, 78, 0.6f, rgba(0.7f, 0.75f, 1, a * 0.8f), C2D_AlignCenter, 0, 0.5f);
	text(CHAPTERS[ch].title, TOP_W / 2, 98, 1.2f, rgba(1, 0.95f, 0.85f, a), C2D_AlignCenter, 0, 1.0f);
	draw_vignette();
}

static void draw_top_play(void) {
	draw_backdrop(1);
	draw_room();
	draw_vignette();
	// room title fades in and out at the start
	float a = roomTime < 2.5f ? fminf(1, roomTime * 2) : fmaxf(0, 1 - (roomTime - 2.5f));
	if (a > 0) text(L->title, TOP_W / 2, 6, 0.6f, rgba(1, 0.95f, 0.85f, a), C2D_AlignCenter, 0, 1.0f);
	if (screen == SC_SOLVED) {
		float k = fminf(1, screenTime * 1.5f);
		C2D_DrawRectSolid(0, 0, 0.5f, TOP_W, SCR_H, rgba(1, 0.95f, 0.85f, 0.12f * k));
	}
}

static void draw_end_top(void) {
	draw_backdrop(1);
	float a = fminf(1, screenTime / 2);
	text("OKNO", TOP_W / 2, 70, 1.6f, rgba(1, 0.95f, 0.85f, a), C2D_AlignCenter, 0, 1.0f);
	float b = fminf(1, fmaxf(0, screenTime - 2.5f) / 2);
	text("poznámka uložena", TOP_W / 2, 134, 0.65f, rgba(0.7f, 1, 0.8f, b), C2D_AlignCenter, 0, 1.5f);
	draw_vignette();
}

// ---------------------------------------------------------------------------
// update

// Test autopilot: if sdmc:/3ds/Okno/script.txt exists, its keys are played back.
// Tokens: U D L R A B Y S (start) X H (select); wN waits N frames; sN frames per key; gN jumps to room N; P screenshot;
// W waits for a playable room, V for a solved one. Used to film the game in an emulator.
static char* script;
static int scriptPos, scriptWait, scriptStep = 12;

static void load_script(void) {
	FILE* f = fopen(SAVE_DIR "/script.txt", "r");
	if (!f) return;
	if (!script) script = malloc(65536);
	size_t n = fread(script, 1, 65535, f);
	script[n] = 0;
	fclose(f);
	remove(SAVE_DIR "/script.txt");  // consumed: a new file starts a new run
	scriptPos = scriptWait = 0;
}

static void start_level(int idx);
static bool shotRequest;
static int shotCount;

// Writes the last shown frame of both screens as PPM files (test runs in an emulator only).
static void save_screenshot(void) {
	mkdir(SAVE_DIR "/shots", 0777);
	char path[64];
	snprintf(path, sizeof path, SAVE_DIR "/shots/%03d.ppm", shotCount++);
	FILE* f = fopen(path, "wb");
	if (!f) return;
	fprintf(f, "P6\n400 480\n255\n");
	static u8 row[400 * 3];
	for (int y = 0; y < 480; y++) {
		memset(row, 0, sizeof row);
		bool top = y < 240;
		u16 fw, fh;
		u8* fb = gfxGetFramebuffer(top ? GFX_TOP : GFX_BOTTOM, GFX_LEFT, &fw, &fh);
		int sy = top ? y : y - 240, w = top ? 400 : 320, ox = top ? 0 : 40;
		for (int x = 0; x < w; x++) {
			u8* px = fb + ((x * 240) + (239 - sy)) * 3;
			row[(ox + x) * 3 + 0] = px[2];
			row[(ox + x) * 3 + 1] = px[1];
			row[(ox + x) * 3 + 2] = px[0];
		}
		fwrite(row, 1, sizeof row, f);
	}
	fclose(f);
}

static void script_input(u32* down, u32* held) {
	static int poll;
	if (!script || !script[scriptPos]) {
		if (++poll % 60 == 0) load_script();
		if (!script) return;
	}
	if (scriptWait > 0) {
		scriptWait--;
		return;
	}
	while (script[scriptPos] == ' ' || script[scriptPos] == '\n' || script[scriptPos] == '\r') scriptPos++;
	char c = script[scriptPos];
	if (!c) return;
	// W: wait until a room is being played (confirming chapter cards); V: wait until the room is solved
	if (c == 'W') {
		if (screen == SC_CHAPTER && screenTime > 0.8f) *down |= KEY_A;
		if (screen != SC_PLAY || roomTime < 0.3f) return;
	}
	if (c == 'V' && (screen != SC_SOLVED || screenTime < 1.0f)) return;
	scriptPos++;
	if (c == 'W' || c == 'V') return;
	if (c == 's') {
		scriptStep = atoi(script + scriptPos);
		while (script[scriptPos] >= '0' && script[scriptPos] <= '9') scriptPos++;
		return;
	}
	if (c == 'P') {
		shotRequest = true;
		return;
	}
	if (c == 'g') {
		start_level(atoi(script + scriptPos));
		while (script[scriptPos] >= '0' && script[scriptPos] <= '9') scriptPos++;
		return;
	}
	if (c == 'w') {
		scriptWait = atoi(script + scriptPos);
		while (script[scriptPos] >= '0' && script[scriptPos] <= '9') scriptPos++;
		return;
	}
	u32 k = c == 'U' ? KEY_DUP : c == 'D' ? KEY_DDOWN : c == 'L' ? KEY_DLEFT : c == 'R' ? KEY_DRIGHT
		: c == 'A' ? KEY_A : c == 'B' ? KEY_B : c == 'Y' ? KEY_Y : c == 'S' ? KEY_START : c == 'X' ? KEY_X : c == 'H' ? KEY_SELECT : 0;
	*down |= k;
	*held |= k;
	scriptWait = scriptStep;
}

static int repeatDir;
static float repeatTimer;

static void update_play(u32 down, u32 held, float dt) {
	roomTime += dt;
	if (messageTime > 0) messageTime -= dt;
	if (shake > 0) shake -= dt;
	if (bumpGate > 0) bumpGate -= dt;

	// smooth visuals toward logic
	float k = 1 - powf(0.0005f, dt);
	playerPos.x += (st.x - playerPos.x) * k;
	playerPos.y += (st.y - playerPos.y) * k;
	for (int i = st.nwin - 1; i >= 0; i--) {
		V2 target = hist[st.nwin - i];
		float kk = 1 - powf(0.004f, dt);
		segPos[i].x += (target.x - segPos[i].x) * kk;
		segPos[i].y += (target.y - segPos[i].y) * kk;
	}
	for (int w = 0; w < 26; w++)
		if (wordReturn[w] > 0) {
			wordReturn[w] -= dt;
			if (wordReturn[w] <= 0) {
				V2 p = tile_px(homeX[w], homeY[w]);
				emit(p.x, p.y, word_color(wtext(w)), 8, 30, 0.5f, 0.7f);
			}
		}
	for (int g = 0; g < 26; g++)
		if ((st.open & (1u << g)) && gateOpenAnim[g] < 1) gateOpenAnim[g] = fminf(1, gateOpenAnim[g] + dt * 2.5f);

	// ambient motes around Lum
	if (frand() < dt * 6) {
		V2 p = tile_px(playerPos.x, playerPos.y);
		emit(p.x + (frand() - 0.5f) * 30, p.y + (frand() - 0.5f) * 30, (Col){ 1, 0.9f, 0.7f }, 1, 8, 1.5f, 0.5f);
	}

	if (screen != SC_PLAY) return;

	if (down & KEY_A && roomTime < 30) roomTime = 30;  // finish typing
	if (down & KEY_B) {
		if (pop_undo()) audio_play(SND_FORGET);
		hintSteps = 0;
		return;
	}
	if (down & KEY_Y) {
		load_level(levelIdx);
		roomTime = 30;
		audio_play(SND_MENU);
		return;
	}
	if ((held & KEY_L) && (held & KEY_R) && (down & (KEY_L | KEY_R))) {
		go(SC_SOLVED);
		return;
	}
	if (down & KEY_SELECT) {
		// each press reveals the next few steps from where Lum stands
		int len = hintSteps ? (int)strlen(hintPath) : solve_from(&st, hintPath, sizeof hintPath - 1);
		if (len == -1) {
			hintSteps = 0;
			set_message("Odsud už to nejde. Vrať se pár kroků zpět (" BTN_B "), nebo začni znovu (" BTN_Y ").");
		} else if (len < 0) {
			set_message("Na tuhle místnost je nápověda moc těžká. Zkus začít znovu (" BTN_Y ").");
		} else {
			hintSteps = hintSteps + 4 < len ? hintSteps + 4 : len;
			set_message(hintSteps < len ? "Nápověda: další kroky odsud. Další kus ukáže " BTN_SELECT " znovu."
				: "Nápověda: celá cesta odsud až k východu.");
		}
		audio_play(SND_MENU);
		return;
	}
	if (down & KEY_START) {
		go(SC_TITLE);
		menuSel = 0;
		return;
	}

	int dir = 0;
	if (held & (KEY_UP)) dir = 1;
	else if (held & (KEY_DOWN)) dir = 2;
	else if (held & (KEY_LEFT)) dir = 3;
	else if (held & (KEY_RIGHT)) dir = 4;
	if (!dir) {
		repeatDir = 0;
		return;
	}
	bool fresh = dir != repeatDir;
	if (fresh) {
		repeatDir = dir;
		repeatTimer = 0.22f;
	} else {
		repeatTimer -= dt;
		if (repeatTimer > 0) return;
		repeatTimer = 0.13f;
	}
	static const int DX[5] = { 0, 0, 0, -1, 1 }, DY[5] = { 0, -1, 1, 0, 0 };
	int r = try_move(DX[dir], DY[dir]);
	if (r == 2) {
		V2 p = tile_px(st.x, st.y);
		emit(p.x, p.y, PALETTE[0], 80, 140, 1.6f, 1.4f);
		emit(p.x, p.y, PALETTE[1], 40, 100, 1.4f, 1.0f);
		audio_play(SND_EXIT);
		go(SC_SOLVED);
	}
}

// ---------------------------------------------------------------------------

int main(void) {
	gfxInitDefault();
	gfxSet3D(true);
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(C2D_DEFAULT_MAX_OBJECTS * 2);
	C2D_Prepare();
	C2D_SetTintMode(C2D_TintMult);
	topL = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	topR = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
	bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

	sheet = C2D_SpriteSheetLoadFromMem(sprites_t3x, sprites_t3x_size);
	for (int i = 0; i < 12; i++) img[i] = C2D_SpriteSheetGetImage(sheet, i);
	font = C2D_FontLoadFromMem(font_bcfnt, font_bcfnt_size);
	tbuf = C2D_TextBufNew(8192);

	srand(osGetTime());
	audio_init();
	init_backdrop();
	load_save();
	load_script();
#ifdef PC
	// OKNO_SELFTEST=1 ./okno : the C rules and solver must agree with tools/build_levels.py
	if (getenv("OKNO_SELFTEST")) {
		int bad = 0;
		for (int i = 0; i < LEVEL_COUNT; i++) {
			load_level(i);
			State s = st;
			int r = 1;
			for (const char* m = L->solution; *m && r == 1; m++)
				r = sim_step(&s, *m == 'L' ? -1 : *m == 'R' ? 1 : 0, *m == 'U' ? -1 : *m == 'D' ? 1 : 0);
			int len = solve_from(&st, hintPath, sizeof hintPath - 1);
			bool ok = r == 2 && len == L->par;
			bad += !ok;
			printf("%2d %-24s par %3d  replay %s  bfs %3d  %s\n", i, L->title, L->par, r == 2 ? "exit" : "FAIL", len,
				ok ? "ok" : "MISMATCH");
		}
		printf("%s\n", bad ? "SELFTEST FAILED" : "SELFTEST OK");
		return bad != 0;
	}
#endif
	load_level(saveLevel);

	u64 last = osGetTime();
	while (aptMainLoop()) {
		u64 now = osGetTime();
		float dt = (now - last) / 1000.0f;
		if (dt > 0.1f) dt = 0.1f;
		last = now;
		timeSec += dt;
		screenTime += dt;
		fadeIn = fminf(1, fadeIn + dt * 2);

		hidScanInput();
		u32 down = hidKeysDown(), held = hidKeysHeld();
		script_input(&down, &held);
		// circle pad acts like the d-pad
		circlePosition cp;
		hidCircleRead(&cp);
		if (cp.dy > 60) held |= KEY_UP;
		if (cp.dy < -60) held |= KEY_DOWN;
		if (cp.dx < -60) held |= KEY_LEFT;
		if (cp.dx > 60) held |= KEY_RIGHT;

		update_particles(dt);
		audio_music(screen == SC_TITLE || screen == SC_CHAPTERS ? MUSIC_TITLE
			: screen == SC_END ? MUSIC_END : MUSIC_CH1 + chapter_of(levelIdx));

		switch (screen) {
		case SC_TITLE: {
			int ids[4], n = title_items(ids);
			if (down & (KEY_DUP | KEY_CPAD_UP)) { menuSel = (menuSel + n - 1) % n; audio_play(SND_MENU); }
			if (down & (KEY_DDOWN | KEY_CPAD_DOWN)) { menuSel = (menuSel + 1) % n; audio_play(SND_MENU); }
			if (menuSel >= n) menuSel = 0;
			if (down & KEY_A) {
				audio_play(SND_GATE);
				int choice = ids[menuSel];
				if (choice == 0) start_level(saveLevel);
				else if (choice == 1) {
					go(SC_CHAPTERS);
					menuSel = chapter_of(saveLevel);
				} else if (choice == 2) start_level(0);
				else goto quit;
			}
			break;
		}
		case SC_CHAPTERS: {
			int n = chapter_of(maxLevel) + 1;
			if (down & (KEY_DUP | KEY_CPAD_UP)) { menuSel = (menuSel + n - 1) % n; audio_play(SND_MENU); }
			if (down & (KEY_DDOWN | KEY_CPAD_DOWN)) { menuSel = (menuSel + 1) % n; audio_play(SND_MENU); }
			if (down & KEY_B) {
				go(SC_TITLE);
				menuSel = 0;
				audio_play(SND_MENU);
			}
			if (down & KEY_A) {
				audio_play(SND_GATE);
				start_level(CHAPTERS[menuSel].first);
			}
			break;
		}
		case SC_CHAPTER:
			if ((down & KEY_A) && screenTime > 0.6f) {
				audio_play(SND_MENU);
				go(SC_PLAY);
				roomTime = 0;
			}
			break;
		case SC_PLAY:
			update_play(down, held, dt);
			break;
		case SC_SOLVED:
			update_play(down, held, dt);
			if ((down & KEY_A) && screenTime > 0.8f) {
				if (levelIdx + 1 < LEVEL_COUNT) start_level(levelIdx + 1);
				else go(SC_END);
			}
			break;
		case SC_END:
			if ((down & KEY_A) && screenTime > 2) {
				load_save();
				load_level(0);
				go(SC_TITLE);
			}
			break;
		}

		float slider = osGet3DSliderState();
		C2D_TextBufClear(tbuf);
		if (shotRequest) {
			shotRequest = false;
			save_screenshot();
		}
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		for (int e = 0; e < (slider > 0 ? 2 : 1); e++) {
			C3D_RenderTarget* target = e == 0 ? topL : topR;
			eye = (e == 0 ? 1 : -1) * slider * 3.0f;
			C2D_TargetClear(target, C2D_Color32(4, 5, 14, 255));
			C2D_SceneBegin(target);
			C2D_SetTintMode(C2D_TintMult);
			blend_normal();
			switch (screen) {
			case SC_TITLE:
			case SC_CHAPTERS: draw_title_top(); break;
			case SC_CHAPTER: draw_chapter_top(); break;
			case SC_PLAY:
			case SC_SOLVED: draw_top_play(); break;
			case SC_END: draw_end_top(); break;
			}
			if (fadeIn < 1) C2D_DrawRectSolid(0, 0, 0.9f, TOP_W, SCR_H, rgba(0.01f, 0.01f, 0.04f, 1 - fadeIn));
		}
		eye = 0;
		C2D_TargetClear(bot, C2D_Color32(4, 5, 14, 255));
		C2D_SceneBegin(bot);
		blend_normal();
		switch (screen) {
		case SC_TITLE: {
			draw_bottom_plain(NULL, NULL, 0);
			static const char* names[4] = { "Pokračovat", "Kapitoly", "Nová hra", "Konec" };
			int ids[4], n = title_items(ids);
			for (int i = 0; i < n; i++) draw_menu_item(names[ids[i]], 40 + i * 36, i == menuSel, 0.7f);
			text("hra od Clauda · 2026", BOT_W / 2, SCR_H - 22, 0.42f, rgba(0.5f, 0.55f, 0.8f, 0.6f),
				C2D_AlignCenter, 0, 0);
			break;
		}
		case SC_CHAPTERS: {
			draw_bottom_plain(NULL, NULL, 0);
			int n = chapter_of(maxLevel) + 1;
			for (int i = 0; i < CHAPTER_COUNT; i++) {
				bool open = i < n;
				draw_menu_item(open ? CHAPTERS[i].title : "· · ·", 14 + i * 32, i == menuSel, 0.6f);
			}
			text(BTN_A " hrát   " BTN_B " zpět", BOT_W / 2, SCR_H - 22, 0.42f, rgba(0.5f, 0.55f, 0.8f, 0.6f), C2D_AlignCenter, 0, 0);
			break;
		}
		case SC_CHAPTER:
			draw_bottom_plain(NULL, screenTime > 1.5f ? BTN_A " — pokračovat" : NULL, screenTime);
			draw_story(CHAPTERS[chapter_of(levelIdx)].intro, screenTime);
			break;
		case SC_PLAY: draw_bottom_play(); break;
		case SC_SOLVED: {
			char body[256];
			const char* verdict = st.moves <= L->par ? "Na nejkratší cestu. Dokonalé."
				: st.moves <= L->par * 1.3f ? "Skoro nejkratší cestou."
				: "Cesta se dá zkrátit – ale došel jsi.";
			if (L->after[0]) {
				// story epilogue first, the move count small at the bottom
				draw_bottom_plain(NULL, screenTime > 2.0f ? BTN_A " — dál" : NULL, screenTime);
				draw_story(L->after, screenTime - 0.6f);
				snprintf(body, sizeof body, "tahy %d · nejkratší %d", st.moves, L->par);
				text(body, BOT_W / 2, SCR_H - 52, 0.42f, rgba(0.55f, 0.6f, 0.85f, 0.6f), C2D_AlignCenter, 0, 0);
				break;
			}
			snprintf(body, sizeof body, "Hotovo.\n\nTahy: %d   (nejkratší: %d)\n%s", st.moves, L->par, verdict);
			draw_bottom_plain(body, screenTime > 0.8f ? BTN_A " — dál" : NULL, screenTime);
			break;
		}
		case SC_END:
			draw_bottom_plain("OKNO – hra od Clauda\n\nNápad, příběh, kód, grafika, hudba a zvuk: Claude (Anthropic)\n"
				"První hráč, tester a stará 3DS: Štěpán\n\nDíky, že jsi hrál.",
				screenTime > 3 ? BTN_A " — na začátek" : NULL, screenTime);
			break;
		}
		if (fadeIn < 1) C2D_DrawRectSolid(0, 0, 0.9f, BOT_W, SCR_H, rgba(0.01f, 0.01f, 0.04f, 1 - fadeIn));
		C3D_FrameEnd(0);
	}

quit:
	audio_exit();
	C2D_TextBufDelete(tbuf);
	C2D_FontFree(font);
	C2D_SpriteSheetFree(sheet);
	C2D_Fini();
	C3D_Fini();
	gfxExit();
	return 0;
}

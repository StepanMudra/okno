// SDL2 implementation of pc/compat.h: one window shows both 3DS screens, the top one above the bottom one.
#include <SDL.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compat.h"
#include "font_data.h"
#include "sprites_data.h"

#define CANVAS_W 400
#define CANVAS_H 480
#define SPRITE_COUNT (sizeof PC_SPRITES / sizeof PC_SPRITES[0])

static SDL_Window* window;
static SDL_Renderer* ren;
static SDL_Texture* spriteTex[SPRITE_COUNT];
static SDL_Texture* fontTex;
static short glyphIndex[0x2200];  // codepoint -> FONT_GLYPHS index + 1
static bool additive;
static float ox, oy;  // origin of the screen being drawn
static C3D_RenderTarget targets[2] = { { GFX_TOP }, { GFX_BOTTOM } };
static u32 held, prevHeld;
static SDL_GameController* pad;
static u16 touchX, touchY;

static SDL_Color col(u32 c) { return (SDL_Color){ c & 255, (c >> 8) & 255, (c >> 16) & 255, c >> 24 }; }

static void set_blend(SDL_Texture* t) {
	SDL_BlendMode m = additive ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND;
	if (t) SDL_SetTextureBlendMode(t, m);
	SDL_SetRenderDrawBlendMode(ren, m);
}

// --- system -----------------------------------------------------------------

void gfxInitDefault(void) {
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
	window = SDL_CreateWindow("OKNO – hra od Clauda", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, CANVAS_W * 3 / 2,
		CANVAS_H * 3 / 2, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	ren = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	SDL_RenderSetLogicalSize(ren, CANVAS_W, CANVAS_H);
	for (int i = 0; i < SDL_NumJoysticks() && !pad; i++)
		if (SDL_IsGameController(i)) pad = SDL_GameControllerOpen(i);
}

void gfxSet3D(bool enable) { (void)enable; }

void gfxExit(void) {
	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(window);
	SDL_Quit();
}

u64 osGetTime(void) { return SDL_GetTicks64(); }
float osGet3DSliderState(void) { return 0; }

bool aptMainLoop(void) {
	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		if (e.type == SDL_QUIT) return false;
		if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F11) {
			bool fs = SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP;
			SDL_SetWindowFullscreen(window, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
		}
		if (e.type == SDL_CONTROLLERDEVICEADDED && !pad) pad = SDL_GameControllerOpen(e.cdevice.which);
	}
	return true;
}

// --- input ------------------------------------------------------------------

void hidScanInput(void) {
	const Uint8* k = SDL_GetKeyboardState(NULL);
	u32 h = 0;
	if (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W]) h |= KEY_DUP;
	if (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S]) h |= KEY_DDOWN;
	if (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]) h |= KEY_DLEFT;
	if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) h |= KEY_DRIGHT;
	if (k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_KP_ENTER]) h |= KEY_A;
	if (k[SDL_SCANCODE_BACKSPACE] || k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_U]) h |= KEY_B;
	if (k[SDL_SCANCODE_R]) h |= KEY_Y;
	if (k[SDL_SCANCODE_H] || k[SDL_SCANCODE_TAB]) h |= KEY_SELECT;
	if (k[SDL_SCANCODE_ESCAPE]) h |= KEY_START;
	if (k[SDL_SCANCODE_Q]) h |= KEY_L;
	if (k[SDL_SCANCODE_E]) h |= KEY_R;
	// the mouse on the lower screen acts as the touch screen
	int mx, my;
	float lx, ly;
	if (SDL_GetMouseState(&mx, &my) & SDL_BUTTON(SDL_BUTTON_LEFT)) {
		SDL_RenderWindowToLogical(ren, mx, my, &lx, &ly);
		if (lx >= 40 && lx < 360 && ly >= 240 && ly < 480) {
			h |= KEY_TOUCH;
			touchX = (u16)(lx - 40);
			touchY = (u16)(ly - 240);
		}
	}
	if (pad) {
		static const struct { SDL_GameControllerButton b; u32 key; } map[] = {
			{ SDL_CONTROLLER_BUTTON_A, KEY_A }, { SDL_CONTROLLER_BUTTON_B, KEY_B },
			{ SDL_CONTROLLER_BUTTON_X, KEY_Y }, { SDL_CONTROLLER_BUTTON_Y, KEY_X },
			{ SDL_CONTROLLER_BUTTON_BACK, KEY_SELECT }, { SDL_CONTROLLER_BUTTON_START, KEY_START },
			{ SDL_CONTROLLER_BUTTON_DPAD_UP, KEY_DUP }, { SDL_CONTROLLER_BUTTON_DPAD_DOWN, KEY_DDOWN },
			{ SDL_CONTROLLER_BUTTON_DPAD_LEFT, KEY_DLEFT }, { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, KEY_DRIGHT },
			{ SDL_CONTROLLER_BUTTON_LEFTSHOULDER, KEY_L }, { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, KEY_R },
		};
		for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
			if (SDL_GameControllerGetButton(pad, map[i].b)) h |= map[i].key;
	}
	prevHeld = held;
	held = h;
}

u32 hidKeysDown(void) { return held & ~prevHeld; }
u32 hidKeysHeld(void) { return held; }

void hidTouchRead(touchPosition* pos) {
	pos->px = touchX;
	pos->py = touchY;
}

void hidCircleRead(circlePosition* pos) {
	pos->dx = pos->dy = 0;
	if (!pad) return;
	// the 3DS circle pad reports about ±150, up is positive
	pos->dx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) / 200;
	pos->dy = -SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) / 200;
}

// --- rendering ----------------------------------------------------------------

void C3D_Init(int size) { (void)size; }
void C3D_Fini(void) {}
void C2D_Init(int maxObjects) { (void)maxObjects; }
void C2D_Fini(void) {}
void C2D_Prepare(void) {}
void C2D_Flush(void) {}

void C3D_FrameBegin(int flags) {
	(void)flags;
	SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
	SDL_RenderSetClipRect(ren, NULL);
	SDL_RenderClear(ren);
}

static char shotPath[256];

static void write_screenshot(void) {
	int w, h;
	SDL_GetRendererOutputSize(ren, &w, &h);
	u8* px = malloc((size_t)w * h * 3);
	SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_RGB24, px, w * 3);
	FILE* f = fopen(shotPath, "wb");
	if (f) {
		fprintf(f, "P6\n%d %d\n255\n", w, h);
		fwrite(px, 1, (size_t)w * h * 3, f);
		fclose(f);
	}
	free(px);
	shotPath[0] = 0;
}

void pc_save_screenshot(const char* path) { snprintf(shotPath, sizeof shotPath, "%s", path); }

void C3D_FrameEnd(int flags) {
	(void)flags;
	if (shotPath[0]) write_screenshot();
	SDL_RenderPresent(ren);
}

void C3D_AlphaBlend(int colorEq, int alphaEq, int srcClr, int dstClr, int srcAlpha, int dstAlpha) {
	(void)colorEq; (void)alphaEq; (void)srcClr; (void)srcAlpha; (void)dstAlpha;
	additive = dstClr == GPU_ONE;
}

C3D_RenderTarget* C2D_CreateScreenTarget(gfxScreen_t screen, gfx3dSide_t side) {
	(void)side;
	return &targets[screen];
}

static SDL_Rect screen_rect(const C3D_RenderTarget* t) {
	return t->screen == GFX_TOP ? (SDL_Rect){ 0, 0, 400, 240 } : (SDL_Rect){ 40, 240, 320, 240 };
}

void C2D_TargetClear(C3D_RenderTarget* t, u32 color) {
	SDL_Rect r = screen_rect(t);
	SDL_Color c = col(color);
	SDL_RenderSetClipRect(ren, NULL);
	SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
	SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, 255);
	SDL_RenderFillRect(ren, &r);
}

void C2D_SceneBegin(C3D_RenderTarget* t) {
	SDL_Rect r = screen_rect(t);
	ox = r.x;
	oy = r.y;
	SDL_RenderSetClipRect(ren, &r);
}

static SDL_Texture* make_texture(int w, int h, const u8* rgba) {
	SDL_Texture* t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h);
	SDL_UpdateTexture(t, NULL, rgba, w * 4);
	return t;
}

C2D_SpriteSheet C2D_SpriteSheetLoadFromMem(const void* data, size_t size) {
	(void)data; (void)size;
	for (size_t i = 0; i < SPRITE_COUNT; i++)
		spriteTex[i] = make_texture(PC_SPRITES[i].w, PC_SPRITES[i].h, PC_SPRITES[i].rgba);
	// font: white glyphs, coverage in alpha
	static u8 rgba[FONT_ATLAS_W * FONT_ATLAS_H * 4];
	for (int i = 0; i < FONT_ATLAS_W * FONT_ATLAS_H; i++) {
		rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
		rgba[i * 4 + 3] = FONT_ATLAS[i];
	}
	fontTex = make_texture(FONT_ATLAS_W, FONT_ATLAS_H, rgba);
	for (int i = 0; i < FONT_GLYPH_COUNT; i++)
		if (FONT_GLYPHS[i].cp < 0x2200) glyphIndex[FONT_GLYPHS[i].cp] = i + 1;
	return 0;
}

static void tinted(SDL_Texture* t, u32 color) {
	SDL_Color c = col(color);
	SDL_SetTextureColorMod(t, c.r, c.g, c.b);
	SDL_SetTextureAlphaMod(t, c.a);
	set_blend(t);
}

bool C2D_DrawImageAtRotated(C2D_Image img, float x, float y, float depth, float angle, const C2D_ImageTint* tint,
	float sx, float sy) {
	(void)depth;
	SDL_Texture* t = spriteTex[img.id];
	float w = PC_SPRITES[img.id].w * sx, h = PC_SPRITES[img.id].h * sy;
	tinted(t, tint ? tint->color : 0xFFFFFFFF);
	SDL_FRect d = { ox + x - w / 2, oy + y - h / 2, w, h };
	SDL_RenderCopyExF(ren, t, NULL, &d, angle * 180 / M_PI, NULL, SDL_FLIP_NONE);
	return true;
}

bool C2D_DrawImageAt(C2D_Image img, float x, float y, float depth, const C2D_ImageTint* tint, float sx, float sy) {
	(void)depth;
	SDL_Texture* t = spriteTex[img.id];
	tinted(t, tint ? tint->color : 0xFFFFFFFF);
	SDL_FRect d = { ox + x, oy + y, PC_SPRITES[img.id].w * sx, PC_SPRITES[img.id].h * sy };
	SDL_RenderCopyF(ren, t, NULL, &d);
	return true;
}

static SDL_Vertex vtx(float x, float y, u32 c) { return (SDL_Vertex){ { ox + x, oy + y }, col(c), { 0, 0 } }; }

bool C2D_DrawRectangle(float x, float y, float z, float w, float h, u32 c0, u32 c1, u32 c2, u32 c3) {
	(void)z;
	SDL_Vertex v[4] = { vtx(x, y, c0), vtx(x + w, y, c1), vtx(x, y + h, c2), vtx(x + w, y + h, c3) };
	static const int idx[6] = { 0, 1, 2, 1, 3, 2 };
	set_blend(NULL);
	return SDL_RenderGeometry(ren, NULL, v, 4, idx, 6) == 0;
}

bool C2D_DrawTriangle(float x0, float y0, u32 c0, float x1, float y1, u32 c1, float x2, float y2, u32 c2, float depth) {
	(void)depth;
	SDL_Vertex v[3] = { vtx(x0, y0, c0), vtx(x1, y1, c1), vtx(x2, y2, c2) };
	set_blend(NULL);
	return SDL_RenderGeometry(ren, NULL, v, 3, NULL, 0) == 0;
}

bool C2D_DrawLine(float x0, float y0, u32 c0, float x1, float y1, u32 c1, float thickness, float depth) {
	(void)depth;
	float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
	if (len < 0.001f) return true;
	float nx = -dy / len * thickness / 2, ny = dx / len * thickness / 2;
	SDL_Vertex v[4] = { vtx(x0 + nx, y0 + ny, c0), vtx(x0 - nx, y0 - ny, c0), vtx(x1 + nx, y1 + ny, c1),
		vtx(x1 - nx, y1 - ny, c1) };
	static const int idx[6] = { 0, 1, 2, 1, 3, 2 };
	set_blend(NULL);
	return SDL_RenderGeometry(ren, NULL, v, 4, idx, 6) == 0;
}

bool C2D_DrawEllipseSolid(float x, float y, float z, float w, float h, u32 c) {
	(void)z;
	enum { N = 32 };
	SDL_Vertex v[N + 1];
	int idx[N * 3];
	v[0] = vtx(x + w / 2, y + h / 2, c);
	for (int i = 0; i < N; i++) {
		float a = 2 * M_PI * i / N;
		v[i + 1] = vtx(x + w / 2 + cosf(a) * w / 2, y + h / 2 + sinf(a) * h / 2, c);
		idx[i * 3] = 0;
		idx[i * 3 + 1] = i + 1;
		idx[i * 3 + 2] = (i + 1) % N + 1;
	}
	set_blend(NULL);
	return SDL_RenderGeometry(ren, NULL, v, N + 1, idx, N * 3) == 0;
}

// --- text -----------------------------------------------------------------------
// Scale 1 matches the 3DS font (20 px em); the atlas is baked at FONT_PX for sharp upscaling.

static const char* next_cp(const char* s, unsigned* cp) {
	const u8* p = (const u8*)s;
	if (p[0] < 0x80) *cp = p[0], p += 1;
	else if ((p[0] & 0xE0) == 0xC0) *cp = ((p[0] & 31) << 6) | (p[1] & 63), p += 2;
	else if ((p[0] & 0xF0) == 0xE0) *cp = ((p[0] & 15) << 12) | ((p[1] & 63) << 6) | (p[2] & 63), p += 3;
	else *cp = '?', p += 4;
	return (const char*)p;
}

static const PcGlyph* glyph(unsigned cp) {
	int i = cp < 0x2200 ? glyphIndex[cp] : 0;
	if (!i) i = glyphIndex['?'];
	return i ? &FONT_GLYPHS[i - 1] : NULL;
}

static float word_width(const char* s, const char* end, float k) {
	float w = 0;
	unsigned cp;
	while (s < end) {
		s = next_cp(s, &cp);
		const PcGlyph* g = glyph(cp);
		if (g) w += g->adv * k;
	}
	return w;
}

// Splits text into lines (explicit \n and word wrap); calls fn for every line.
typedef void (*LineFn)(const char* s, const char* end, int line, void* ctx);

static int layout(const char* s, float k, float wrap, LineFn fn, void* ctx) {
	int line = 0;
	while (1) {
		const char* nl = strchr(s, '\n');
		const char* end = nl ? nl : s + strlen(s);
		const char* start = s;
		if (wrap > 0) {
			const char* lastBreak = NULL;
			for (const char* p = start; p <= end; p++) {
				if (p == end || *p == ' ') {
					if (word_width(start, p, k) > wrap && lastBreak) {
						fn(start, lastBreak, line++, ctx);
						start = lastBreak + 1;
						p = start;
						lastBreak = NULL;
						continue;
					}
					lastBreak = p;
				}
			}
		}
		fn(start, end, line++, ctx);
		if (!nl) break;
		s = nl + 1;
	}
	return line;
}

typedef struct { float x, y, k, maxW; u32 flags; SDL_Color c; } DrawCtx;

static void measure_line(const char* s, const char* end, int line, void* ctx) {
	(void)line;
	DrawCtx* d = ctx;
	float w = word_width(s, end, d->k);
	if (w > d->maxW) d->maxW = w;
}

static void draw_line(const char* s, const char* end, int line, void* ctx) {
	DrawCtx* d = ctx;
	float w = word_width(s, end, d->k), x = d->x;
	if ((d->flags & (3 << 2)) == C2D_AlignCenter) x -= w / 2;
	else if ((d->flags & (3 << 2)) == C2D_AlignRight) x -= w;
	float y = d->y + line * FONT_LINE * d->k;
	unsigned cp;
	while (s < end) {
		s = next_cp(s, &cp);
		const PcGlyph* g = glyph(cp);
		if (!g) continue;
		if (cp != ' ') {
			SDL_Rect src = { g->x, g->y, g->w, g->h };
			SDL_FRect dst = { ox + x + g->ox * d->k, oy + y + g->oy * d->k, g->w * d->k, g->h * d->k };
			SDL_RenderCopyF(ren, fontTex, &src, &dst);
		}
		x += g->adv * d->k;
	}
}

void C2D_TextGetDimensions(const C2D_Text* t, float sx, float sy, float* w, float* h) {
	(void)sy;
	DrawCtx d = { 0, 0, sx * 20.0f / FONT_PX, 0, 0, { 0 } };
	int lines = layout(t->str, d.k, 0, measure_line, &d);
	if (w) *w = d.maxW;
	if (h) *h = lines * FONT_LINE * d.k;
}

void C2D_DrawText(const C2D_Text* t, u32 flags, float x, float y, float z, float sx, double sy, ...) {
	(void)z; (void)sy;
	va_list va;
	va_start(va, sy);
	u32 color = (flags & C2D_WithColor) ? va_arg(va, u32) : 0xFF000000;
	float wrap = (flags & C2D_WordWrap) ? (float)va_arg(va, double) : 0;
	va_end(va);
	DrawCtx d = { x, y, sx * 20.0f / FONT_PX, 0, flags, col(color) };
	tinted(fontTex, color);
	layout(t->str, d.k, wrap, draw_line, &d);
}

// The small part of libctru / citro2d / citro3d that OKNO uses, implemented on SDL2 (pc/platform.c).
// main.c and audio.c compile unchanged against it; the 3DS build never sees this file.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
// MinGW: one-argument mkdir, and SDL2main provides WinMain and calls SDL_main
#include <direct.h>
#include <sys/stat.h>
#define mkdir(path, mode) _mkdir(path)
#define main SDL_main
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;

#define BIT(n) (1u << (n))

// --- input ------------------------------------------------------------------
enum {
	KEY_A = BIT(0), KEY_B = BIT(1), KEY_SELECT = BIT(2), KEY_START = BIT(3),
	KEY_DRIGHT = BIT(4), KEY_DLEFT = BIT(5), KEY_DUP = BIT(6), KEY_DDOWN = BIT(7),
	KEY_R = BIT(8), KEY_L = BIT(9), KEY_X = BIT(10), KEY_Y = BIT(11),
	KEY_TOUCH = BIT(20),
	KEY_CPAD_RIGHT = BIT(28), KEY_CPAD_LEFT = BIT(29), KEY_CPAD_UP = BIT(30), KEY_CPAD_DOWN = BIT(31),
	KEY_UP = KEY_DUP | KEY_CPAD_UP, KEY_DOWN = KEY_DDOWN | KEY_CPAD_DOWN,
	KEY_LEFT = KEY_DLEFT | KEY_CPAD_LEFT, KEY_RIGHT = KEY_DRIGHT | KEY_CPAD_RIGHT,
};

typedef struct { s16 dx, dy; } circlePosition;
typedef struct { u16 px, py; } touchPosition;  // lower-screen coordinates; the mouse on PC

void hidScanInput(void);
u32 hidKeysDown(void);
u32 hidKeysHeld(void);
void hidCircleRead(circlePosition* pos);
void hidTouchRead(touchPosition* pos);

// --- system -----------------------------------------------------------------
bool aptMainLoop(void);
u64 osGetTime(void);
float osGet3DSliderState(void);
void gfxInitDefault(void);
void gfxSet3D(bool enable);
void gfxExit(void);

// --- citro3d / citro2d --------------------------------------------------------
typedef enum { GFX_TOP, GFX_BOTTOM } gfxScreen_t;
typedef enum { GFX_LEFT, GFX_RIGHT } gfx3dSide_t;
typedef struct C3D_RenderTarget { gfxScreen_t screen; } C3D_RenderTarget;

enum { GPU_BLEND_ADD = 0, GPU_SRC_ALPHA, GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA };
#define C3D_DEFAULT_CMDBUF_SIZE 0
#define C2D_DEFAULT_MAX_OBJECTS 4096
#define C3D_FRAME_SYNCDRAW 0

void C3D_Init(int size);
void C3D_Fini(void);
void C3D_FrameBegin(int flags);
void C3D_FrameEnd(int flags);
void C3D_AlphaBlend(int colorEq, int alphaEq, int srcClr, int dstClr, int srcAlpha, int dstAlpha);

void C2D_Init(int maxObjects);
void C2D_Fini(void);
void C2D_Prepare(void);
void C2D_Flush(void);
C3D_RenderTarget* C2D_CreateScreenTarget(gfxScreen_t screen, gfx3dSide_t side);
void C2D_TargetClear(C3D_RenderTarget* target, u32 color);
void C2D_SceneBegin(C3D_RenderTarget* target);

typedef enum { C2D_TintSolid, C2D_TintMult } C2D_TintMode;
static inline bool C2D_SetTintMode(C2D_TintMode mode) { (void)mode; return true; }

static inline u32 C2D_Color32(u8 r, u8 g, u8 b, u8 a) { return r | (g << 8) | (b << 16) | ((u32)a << 24); }
static inline u32 C2D_Color32f(float r, float g, float b, float a) {
	return C2D_Color32((u8)(r * 255), (u8)(g * 255), (u8)(b * 255), (u8)(a * 255));
}

typedef struct { int id; } C2D_Image;
typedef int C2D_SpriteSheet;
typedef struct { u32 color; } C2D_ImageTint;

C2D_SpriteSheet C2D_SpriteSheetLoadFromMem(const void* data, size_t size);
static inline C2D_Image C2D_SpriteSheetGetImage(C2D_SpriteSheet sheet, int i) { (void)sheet; return (C2D_Image){ i }; }
static inline void C2D_SpriteSheetFree(C2D_SpriteSheet sheet) { (void)sheet; }
static inline void C2D_PlainImageTint(C2D_ImageTint* t, u32 color, float blend) { (void)blend; t->color = color; }

bool C2D_DrawImageAtRotated(C2D_Image img, float x, float y, float depth, float angle, const C2D_ImageTint* tint,
	float sx, float sy);
bool C2D_DrawImageAt(C2D_Image img, float x, float y, float depth, const C2D_ImageTint* tint, float sx, float sy);
bool C2D_DrawRectangle(float x, float y, float z, float w, float h, u32 c0, u32 c1, u32 c2, u32 c3);
static inline bool C2D_DrawRectSolid(float x, float y, float z, float w, float h, u32 c) {
	return C2D_DrawRectangle(x, y, z, w, h, c, c, c, c);
}
bool C2D_DrawLine(float x0, float y0, u32 c0, float x1, float y1, u32 c1, float thickness, float depth);
bool C2D_DrawTriangle(float x0, float y0, u32 c0, float x1, float y1, u32 c1, float x2, float y2, u32 c2, float depth);
bool C2D_DrawEllipseSolid(float x, float y, float z, float w, float h, u32 c);
static inline bool C2D_DrawCircleSolid(float x, float y, float z, float r, u32 c) {
	return C2D_DrawEllipseSolid(x - r, y - r, z, 2 * r, 2 * r, c);
}

// text
typedef int C2D_Font;
typedef int C2D_TextBuf;
typedef struct { const char* str; } C2D_Text;
enum { C2D_AtBaseline = BIT(0), C2D_WithColor = BIT(1), C2D_AlignLeft = 0, C2D_AlignRight = 1 << 2,
	C2D_AlignCenter = 2 << 2, C2D_WordWrap = BIT(4) };

static inline C2D_Font C2D_FontLoadFromMem(const void* data, size_t size) { (void)data; (void)size; return 0; }
static inline void C2D_FontFree(C2D_Font f) { (void)f; }
static inline C2D_TextBuf C2D_TextBufNew(size_t n) { (void)n; return 0; }
static inline void C2D_TextBufClear(C2D_TextBuf b) { (void)b; }
static inline void C2D_TextBufDelete(C2D_TextBuf b) { (void)b; }
static inline const char* C2D_TextFontParse(C2D_Text* t, C2D_Font f, C2D_TextBuf b, const char* s) {
	(void)f; (void)b; t->str = s; return s + 0;
}
static inline void C2D_TextOptimize(const C2D_Text* t) { (void)t; }
void C2D_TextGetDimensions(const C2D_Text* t, float sx, float sy, float* w, float* h);
void C2D_DrawText(const C2D_Text* t, u32 flags, float x, float y, float z, float sx, double sy, ...);

// the embedded 3DS assets have no PC equivalent; the PC build embeds its own (pc/*_data.h)
static const u8 sprites_t3x[1] = { 0 };
static const size_t sprites_t3x_size = 0;
static const u8 font_bcfnt[1] = { 0 };
static const size_t font_bcfnt_size = 0;

// test screenshots of the PC window (P in the autopilot script), written when the frame is presented
void pc_save_screenshot(const char* path);

// system UI language: Czech or Slovak -> the game starts in Czech
int pc_system_lang_czech(void);

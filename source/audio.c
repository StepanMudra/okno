// Every sound in OKNO is synthesised at start-up: no samples, no files.
// On the 3DS it needs the DSP firmware (sdmc:/3ds/dspfirm.cdc); without it the game is silent.
// The PC build (-DPC) plays the same sounds through SDL2.
#ifdef PC
#include <SDL.h>
#include <stdlib.h>
#include "../pc/compat.h"
#define linearAlloc malloc
#define linearFree free
#else
#include <3ds.h>
#endif
#include <math.h>
#include <string.h>
#include "audio.h"

#define RATE 22050
#define MUSIC_SECONDS 16
#define SFX_CHANNELS 6

typedef struct {
	s16* data;
	u32 n;
} Sound;

static bool ok;
static Sound sounds[SND_COUNT];
static Sound music;
static int musicTheme = -1;
static int nextChannel;
#ifdef PC
// channel 0 = music (looping), 1.. = sound effects
typedef struct { const s16* data; u32 n, pos; bool loop; float vol; } Voice;
static Voice voices[1 + SFX_CHANNELS];
static SDL_AudioDeviceID device;
#else
static ndspWaveBuf musicBuf;
static ndspWaveBuf sfxBuf[SFX_CHANNELS];
#endif

static float clampf(float v) { return v > 1 ? 1 : v < -1 ? -1 : v; }

static Sound alloc_sound(float seconds) {
	Sound s;
	s.n = (u32)(seconds * RATE);
	s.data = linearAlloc(s.n * sizeof(s16));
	memset(s.data, 0, s.n * sizeof(s16));
	return s;
}

// A soft bell: fundamental plus two inharmonic partials, exponential decay.
static void add_bell(Sound* s, float start, float freq, float amp, float decay) {
	u32 i0 = (u32)(start * RATE);
	for (u32 i = i0; i < s->n; i++) {
		float t = (float)(i - i0) / RATE;
		float env = expf(-t / decay) * (1 - expf(-t * 400));
		float v = sinf(2 * M_PI * freq * t) + 0.35f * sinf(2 * M_PI * freq * 2.76f * t) * expf(-t * 6)
			+ 0.15f * sinf(2 * M_PI * freq * 5.4f * t) * expf(-t * 12);
		float out = s->data[i] / 32767.0f + v * env * amp;
		s->data[i] = (s16)(clampf(out) * 32767);
	}
}

static void make_sounds(void) {
	// pentatonic pick-up chimes (A minor pentatonic around A5)
	static const float penta[5] = { 880.0f, 1046.5f, 1174.7f, 1318.5f, 1568.0f };
	for (int k = 0; k < 5; k++) {
		sounds[SND_PICK0 + k] = alloc_sound(0.9f);
		add_bell(&sounds[SND_PICK0 + k], 0, penta[k], 0.28f, 0.22f);
		add_bell(&sounds[SND_PICK0 + k], 0.06f, penta[k] * 2, 0.08f, 0.15f);
	}

	// forgetting: a falling, breathy glide
	Sound f = alloc_sound(0.6f);
	float ph = 0;
	for (u32 i = 0; i < f.n; i++) {
		float t = (float)i / RATE;
		float freq = 660 * powf(0.35f, t / 0.6f);
		ph += 2 * M_PI * freq / RATE;
		float env = sinf(M_PI * t / 0.6f) * 0.22f;
		f.data[i] = (s16)(clampf(sinf(ph) * env + sinf(ph * 1.5f) * env * 0.3f) * 32767);
	}
	sounds[SND_FORGET] = f;

	// gate opening: a warm two-step chord
	Sound g = alloc_sound(1.6f);
	add_bell(&g, 0.00f, 440.0f, 0.18f, 0.6f);
	add_bell(&g, 0.00f, 554.4f, 0.14f, 0.6f);
	add_bell(&g, 0.12f, 659.3f, 0.16f, 0.7f);
	add_bell(&g, 0.24f, 880.0f, 0.12f, 0.8f);
	sounds[SND_GATE] = g;

	// bump into a closed gate or wall: low soft thud
	Sound b = alloc_sound(0.18f);
	for (u32 i = 0; i < b.n; i++) {
		float t = (float)i / RATE;
		b.data[i] = (s16)(sinf(2 * M_PI * 90 * t) * expf(-t * 30) * 0.35f * 32767);
	}
	sounds[SND_BUMP] = b;

	// footstep: a tiny glassy tick
	Sound st = alloc_sound(0.08f);
	add_bell(&st, 0, 2093.0f, 0.05f, 0.02f);
	sounds[SND_STEP] = st;

	// well: a deep swirl
	Sound w = alloc_sound(1.0f);
	ph = 0;
	for (u32 i = 0; i < w.n; i++) {
		float t = (float)i / RATE;
		float freq = 220 + 80 * sinf(t * 18);
		ph += 2 * M_PI * freq / RATE;
		w.data[i] = (s16)(sinf(ph) * expf(-t * 3) * 0.25f * 32767);
	}
	sounds[SND_WELL] = w;

	// room solved: rising arpeggio
	Sound e = alloc_sound(2.2f);
	static const float arp[6] = { 523.3f, 659.3f, 784.0f, 1046.5f, 1318.5f, 1568.0f };
	for (int k = 0; k < 6; k++) add_bell(&e, k * 0.09f, arp[k], 0.16f, 0.7f);
	sounds[SND_EXIT] = e;

	// menu move / confirm
	Sound m = alloc_sound(0.25f);
	add_bell(&m, 0, 1318.5f, 0.12f, 0.08f);
	sounds[SND_MENU] = m;
}

#ifdef PC
static void mix_callback(void* user, Uint8* stream, int len) {
	(void)user;
	s16* out = (s16*)stream;
	for (int i = 0; i < len / 2; i++) {
		float m = 0;
		for (int v = 0; v <= SFX_CHANNELS; v++) {
			Voice* c = &voices[v];
			if (!c->data) continue;
			m += c->data[c->pos] / 32768.0f * c->vol;
			if (++c->pos >= c->n) {
				c->pos = 0;
				if (!c->loop) c->data = NULL;
			}
		}
		out[i] = (s16)(clampf(m) * 32767);
	}
}

static void start_voice(int ch, const Sound* snd, bool loop, float vol) {
	SDL_LockAudioDevice(device);
	voices[ch] = (Voice){ snd->data, snd->n, 0, loop, vol };
	SDL_UnlockAudioDevice(device);
}

void audio_init(void) {
	SDL_AudioSpec want = { .freq = RATE, .format = AUDIO_S16SYS, .channels = 1, .samples = 1024,
		.callback = mix_callback };
	device = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
	ok = device != 0;
	if (!ok) return;
	make_sounds();
	SDL_PauseAudioDevice(device, 0);
}
#else
static void setup_channel(int ch, float volume) {
	ndspChnReset(ch);
	ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
	ndspChnSetRate(ch, RATE);
	ndspChnSetFormat(ch, NDSP_FORMAT_MONO_PCM16);
	float mix[12] = { 0 };
	mix[0] = mix[1] = volume;
	ndspChnSetMix(ch, mix);
}

void audio_init(void) {
	ok = R_SUCCEEDED(ndspInit());
	if (!ok) return;
	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	make_sounds();
	for (int i = 0; i < SND_COUNT; i++) DSP_FlushDataCache(sounds[i].data, sounds[i].n * sizeof(s16));
	setup_channel(0, 0.9f);

	for (int c = 0; c < SFX_CHANNELS; c++) setup_channel(1 + c, 1.0f);
}
#endif

// ---------------------------------------------------------------------------
// music: one 16-second loop per theme, four chords of a slow pad plus sparse bells.
// Every frequency is a multiple of 1/MUSIC_SECONDS, so the loop has no seam.

typedef struct {
	u8 chords[4][4];  // MIDI notes
	float bright;     // amount of 2nd/3rd harmonic in the pad
	float detune;     // chorus between the two oscillators of a voice
	float bells;      // probability of a bell on each of the 32 slots
	int bellOctave;   // semitones added to chord tones for bells
	u32 seed;
} Theme;

static const Theme THEMES[MUSIC_COUNT] = {
	// title
	{ { { 57, 60, 64, 69 }, { 53, 57, 60, 65 }, { 48, 55, 60, 64 }, { 43, 55, 59, 62 } }, 0.20f, 0.003f, 0.22f, 24, 7 },
	// I Probuzení: open, hesitant
	{ { { 57, 59, 64, 69 }, { 53, 60, 64, 69 }, { 48, 55, 62, 64 }, { 52, 55, 59, 62 } }, 0.10f, 0.002f, 0.14f, 24, 11 },
	// II Okno
	{ { { 50, 57, 62, 65 }, { 46, 58, 62, 65 }, { 53, 57, 60, 65 }, { 48, 55, 60, 64 } }, 0.25f, 0.003f, 0.28f, 24, 23 },
	// III Pořadí: the lake, rippling
	{ { { 48, 55, 60, 64 }, { 45, 57, 60, 64 }, { 41, 57, 60, 65 }, { 43, 55, 59, 62 } }, 0.18f, 0.004f, 0.45f, 24, 5 },
	// IV Poznámky: night, music box
	{ { { 40, 55, 59, 64 }, { 48, 55, 60, 64 }, { 43, 55, 59, 62 }, { 50, 54, 57, 62 } }, 0.05f, 0.002f, 0.34f, 36, 17 },
	// V Teplota: warm, bright
	{ { { 50, 57, 62, 66 }, { 50, 56, 59, 64 }, { 47, 54, 59, 62 }, { 45, 57, 61, 64 } }, 0.45f, 0.005f, 0.30f, 24, 29 },
	// VI Otrava: minor, uneasy chorus
	{ { { 48, 55, 60, 63 }, { 44, 56, 60, 63 }, { 41, 56, 60, 65 }, { 43, 55, 59, 62 } }, 0.30f, 0.009f, 0.20f, 24, 31 },
	// ending
	{ { { 41, 57, 60, 65 }, { 48, 55, 60, 64 }, { 50, 57, 62, 65 }, { 46, 58, 62, 65 } }, 0.20f, 0.003f, 0.40f, 24, 3 },
};

#define TABLE 4096
static float wave[TABLE];

static u32 phase_inc(float freq) {
	// quantise to whole cycles per loop
	float f = roundf(freq * MUSIC_SECONDS) / MUSIC_SECONDS;
	return (u32)(f / RATE * 4294967296.0);
}

static float midi_hz(int m) { return 440.0f * powf(2.0f, (m - 69) / 12.0f); }

static void render_theme(const Theme* th, s16* out, u32 n) {
	for (int i = 0; i < TABLE; i++) {
		float a = 2 * M_PI * i / TABLE;
		wave[i] = (sinf(a) + th->bright * (0.5f * sinf(2 * a) + 0.25f * sinf(3 * a))) / (1 + th->bright * 0.75f);
	}
	float* mix = linearAlloc(n * sizeof(float));
	memset(mix, 0, n * sizeof(float));
	u32 seg = n / 4, fade = RATE / 2;

	// pad: each chord sounds in its quarter, cross-fading into the neighbours (cyclically)
	for (int c = 0; c < 4; c++) {
		for (int v = 0; v < 4; v++) {
			float f = midi_hz(th->chords[c][v]);
			u32 inc1 = phase_inc(f), inc2 = phase_inc(f * (1 + th->detune));
			float amp = (v == 0 ? 0.06f : 0.035f);
			for (u32 k = 0; k < seg + 2 * fade; k++) {
				u32 i = (c * seg + n - fade + k) % n;  // start fade before the quarter
				u32 p1 = inc1 * i, p2 = inc2 * i;      // phase from absolute position: seamless
				float env = k < 2 * fade ? (float)k / (2 * fade) : k > seg ? (float)(seg + 2 * fade - k) / (2 * fade) : 1;
				env = env * env * (3 - 2 * env);
				mix[i] += amp * env * (wave[p1 >> 20] + wave[p2 >> 20]);
			}
		}
	}

	// bells on a grid of 32 slots, notes taken from the current chord
	u32 rng = th->seed * 2654435761u + 1;
	for (int slot = 0; slot < 32; slot++) {
		rng = rng * 1664525u + 1013904223u;
		if ((rng >> 8) % 1000 >= th->bells * 1000) continue;
		int chord = slot / 8;
		int note = th->chords[chord][1 + (rng >> 20) % 3] + th->bellOctave;
		float f = midi_hz(note), vel = 0.05f + 0.04f * ((rng >> 12) % 100) / 100.0f;
		u32 inc = phase_inc(f), inc2 = phase_inc(f * 2.76f);
		u32 start = slot * (n / 32), len = (u32)(RATE * 2.5f);
		for (u32 k = 0; k < len; k++) {
			u32 i = (start + k) % n;
			float t = (float)k / RATE;
			float env = expf(-t * 1.6f) * (1 - expf(-t * 300));
			mix[i] += vel * env * (wave[(inc * i) >> 20] + 0.3f * expf(-t * 5) * wave[(inc2 * i) >> 20]);
		}
	}
	for (u32 i = 0; i < n; i++) out[i] = (s16)(clampf(mix[i]) * 32767);
	linearFree(mix);
}

void audio_music(int theme) {
	if (!ok || theme == musicTheme || theme < 0 || theme >= MUSIC_COUNT) return;
	musicTheme = theme;
#ifdef PC
	SDL_LockAudioDevice(device);
	voices[0].data = NULL;
	SDL_UnlockAudioDevice(device);
	if (!music.data) music = alloc_sound(MUSIC_SECONDS);
	render_theme(&THEMES[theme], music.data, music.n);
	start_voice(0, &music, true, 0.9f);
#else
	ndspChnWaveBufClear(0);
	while (ndspChnIsPlaying(0)) svcSleepThread(1000000);
	if (!music.data) music = alloc_sound(MUSIC_SECONDS);
	render_theme(&THEMES[theme], music.data, music.n);
	DSP_FlushDataCache(music.data, music.n * sizeof(s16));
	memset(&musicBuf, 0, sizeof musicBuf);
	musicBuf.data_vaddr = music.data;
	musicBuf.nsamples = music.n;
	musicBuf.looping = true;
	ndspChnWaveBufAdd(0, &musicBuf);
#endif
}

void audio_play(int id) {
	if (!ok || id < 0 || id >= SND_COUNT) return;
	int ch = 1 + nextChannel;
	nextChannel = (nextChannel + 1) % SFX_CHANNELS;
#ifdef PC
	start_voice(ch, &sounds[id], false, 1.0f);
#else
	ndspChnWaveBufClear(ch);
	ndspWaveBuf* wb = &sfxBuf[ch - 1];
	memset(wb, 0, sizeof *wb);
	wb->data_vaddr = sounds[id].data;
	wb->nsamples = sounds[id].n;
	ndspChnWaveBufAdd(ch, wb);
#endif
}

void audio_exit(void) {
	if (!ok) return;
#ifdef PC
	SDL_CloseAudioDevice(device);
#else
	ndspExit();
#endif
	for (int i = 0; i < SND_COUNT; i++) linearFree(sounds[i].data);
	if (music.data) linearFree(music.data);
}

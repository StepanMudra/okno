// Every sound in OKNO is synthesised at start-up: no samples, no files.
// Needs the DSP firmware (sdmc:/3ds/dspfirm.cdc); without it the game is silent.
#include <3ds.h>
#include <math.h>
#include <string.h>
#include "audio.h"

#define RATE 22050
#define AMBIENT_SECONDS 8
#define SFX_CHANNELS 6

typedef struct {
	s16* data;
	u32 n;
} Sound;

static bool ok;
static Sound sounds[SND_COUNT];
static Sound ambient;
static ndspWaveBuf ambientBuf;
static ndspWaveBuf sfxBuf[SFX_CHANNELS];
static int nextChannel;

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

	// ambient pad: frequencies are multiples of 1/AMBIENT_SECONDS so the loop is seamless
	ambient = alloc_sound(AMBIENT_SECONDS);
	static const float pad[5] = { 110.0f, 164.75f, 220.0f, 261.625f, 329.625f };
	static const float padAmp[5] = { 0.10f, 0.06f, 0.05f, 0.035f, 0.025f };
	for (u32 i = 0; i < ambient.n; i++) {
		float t = (float)i / RATE;
		float v = 0;
		for (int k = 0; k < 5; k++) {
			float lfo = 0.55f + 0.45f * sinf(2 * M_PI * t / AMBIENT_SECONDS * (k + 1) + k);
			v += sinf(2 * M_PI * pad[k] * t) * padAmp[k] * lfo;
		}
		ambient.data[i] = (s16)(clampf(v) * 32767);
	}
}

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
	DSP_FlushDataCache(ambient.data, ambient.n * sizeof(s16));

	setup_channel(0, 0.9f);
	memset(&ambientBuf, 0, sizeof ambientBuf);
	ambientBuf.data_vaddr = ambient.data;
	ambientBuf.nsamples = ambient.n;
	ambientBuf.looping = true;
	ndspChnWaveBufAdd(0, &ambientBuf);

	for (int c = 0; c < SFX_CHANNELS; c++) setup_channel(1 + c, 1.0f);
}

void audio_play(int id) {
	if (!ok || id < 0 || id >= SND_COUNT) return;
	int ch = 1 + nextChannel;
	nextChannel = (nextChannel + 1) % SFX_CHANNELS;
	ndspChnWaveBufClear(ch);
	ndspWaveBuf* wb = &sfxBuf[ch - 1];
	memset(wb, 0, sizeof *wb);
	wb->data_vaddr = sounds[id].data;
	wb->nsamples = sounds[id].n;
	ndspChnWaveBufAdd(ch, wb);
}

void audio_exit(void) {
	if (!ok) return;
	ndspExit();
	for (int i = 0; i < SND_COUNT; i++) linearFree(sounds[i].data);
	linearFree(ambient.data);
}

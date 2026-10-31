#pragma once

enum {
	SND_PICK0, SND_PICK1, SND_PICK2, SND_PICK3, SND_PICK4,
	SND_FORGET, SND_GATE, SND_BUMP, SND_STEP, SND_WELL, SND_EXIT, SND_MENU,
	SND_COUNT
};

void audio_init(void);
void audio_play(int id);
void audio_exit(void);

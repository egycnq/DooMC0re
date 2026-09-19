#ifndef DG_MUSIC_H
#define DG_MUSIC_H

int  DG_Music_Init(const unsigned char *genmidi, int genmidi_len, int rate);

int  DG_Music_Play(const unsigned char *mus, int mus_len, int looping);

void DG_Music_Stop(void);
void DG_Music_SetVolume(int volume);
int  DG_Music_IsPlaying(void);

void DG_Music_Render(short *buf, int nframes);

unsigned int DG_Music_NoteMilliHz(int note, int detune32);

#endif

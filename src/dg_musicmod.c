#include "doomtype.h"
#include "i_sound.h"
#include "w_wad.h"
#include "z_zone.h"
#include "deh_str.h"

#include "dg_music.h"

#define DG_MUSIC_RATE 48000        /* must match core.h SAMPLE_RATE */
#define MAX_SONGS     4

typedef struct
{
    const unsigned char *data;
    int len;
} dg_song_t;

static dg_song_t songs[MAX_SONGS];
static int       next_song;
static boolean   music_ok;

static boolean I_DG_InitMusic(void)
{
    int lumpnum;
    const unsigned char *genmidi;
    int len;

    music_ok = false;

    lumpnum = W_CheckNumForName("GENMIDI");
    if (lumpnum < 0)
    {
        printf("DG_music: no GENMIDI lump -- music disabled\n");
        return false;
    }

    genmidi = W_CacheLumpNum(lumpnum, PU_STATIC);
    len = W_LumpLength(lumpnum);

    if (!DG_Music_Init(genmidi, len, DG_MUSIC_RATE))
    {
        printf("DG_music: DG_Music_Init failed (GENMIDI %d bytes)\n", len);
        W_ReleaseLumpNum(lumpnum);
        return false;
    }

    W_ReleaseLumpNum(lumpnum);

    music_ok = true;
    next_song = 0;
    printf("DG_music: OPL init ok, %d instruments, %dHz\n", 175, DG_MUSIC_RATE);
    return true;
}

static void I_DG_ShutdownMusic(void)
{
    DG_Music_Stop();
    music_ok = false;
}

static int dg_music_volume = 127;

static void I_DG_SetMusicVolume(int volume)
{
    dg_music_volume = volume;
    DG_Music_SetVolume(volume);
}

static void I_DG_PauseSong(void)
{
    DG_Music_SetVolume(0);
}

static void I_DG_ResumeSong(void)
{
    DG_Music_SetVolume(dg_music_volume);
}

static void *I_DG_RegisterSong(void *data, int len)
{
    dg_song_t *song;

    if (!music_ok || data == NULL || len < 16)
    {
        return NULL;
    }

    song = &songs[next_song & (MAX_SONGS - 1)];
    next_song++;
    song->data = (const unsigned char *) data;
    song->len  = len;
    return song;
}

static void I_DG_UnRegisterSong(void *handle)
{
    dg_song_t *song = (dg_song_t *) handle;

    if (song != NULL && song->data != NULL)
    {
        DG_Music_Stop();
        song->data = NULL;
        song->len = 0;
    }
}

static void I_DG_PlaySong(void *handle, boolean looping)
{
    dg_song_t *song = (dg_song_t *) handle;

    if (!music_ok || song == NULL || song->data == NULL)
    {
        return;
    }
    if (!DG_Music_Play(song->data, song->len, looping ? 1 : 0))
    {
        printf("DG_music: DG_Music_Play rejected a %d byte lump\n", song->len);
    }
}

static void I_DG_StopSong(void)
{
    DG_Music_Stop();
}

static boolean I_DG_MusicIsPlaying(void)
{
    return DG_Music_IsPlaying() ? true : false;
}

static void I_DG_PollMusic(void)
{
}

static snddevice_t music_dg_devices[] =
{
    SNDDEVICE_SB,               /* snd_musicdevice defaults to this */
    SNDDEVICE_ADLIB,
    SNDDEVICE_PAS,
    SNDDEVICE_GUS,
    SNDDEVICE_WAVEBLASTER,
    SNDDEVICE_SOUNDCANVAS,
    SNDDEVICE_GENMIDI,
    SNDDEVICE_AWE32,
};

music_module_t DG_music_module =
{
    music_dg_devices,
    arrlen(music_dg_devices),
    I_DG_InitMusic,
    I_DG_ShutdownMusic,
    I_DG_SetMusicVolume,
    I_DG_PauseSong,
    I_DG_ResumeSong,
    I_DG_RegisterSong,
    I_DG_UnRegisterSong,
    I_DG_PlaySong,
    I_DG_StopSong,
    I_DG_MusicIsPlaying,
    I_DG_PollMusic,
};

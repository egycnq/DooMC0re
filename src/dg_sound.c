#include <stdlib.h>

#include "doomtype.h"
#include "doomgeneric.h"
#include "i_sound.h"
#include "i_timer.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"
#include "deh_str.h"

#define DG_RATE 48000
#define DG_DEFAULT_BUF_FRAMES 256

extern int plt_audio_grain(void);
extern int plt_audio_thread_start(void *(*entry)(void *));
static int dg_buf_frames = DG_DEFAULT_BUF_FRAMES;

/* the dt clamp; at 20ms a slow frame starved the device */
#define DG_MAX_UPDATE_FRAMES  ((DG_RATE * 60) / 1000)

#define DG_MAX_CHANNELS 16

#define DG_RING_FRAMES  8192
#define DG_RING_MASK    (DG_RING_FRAMES - 1)
#define DG_MAX_BUF_FRAMES 2048

#define DG_SUBMIT_IDLE_SLEEP_MS 2
#define DG_START_LOG_LIMIT      5
#define DG_QUEUE_MIN_UNSET      (1 << 30)

extern int plt_audio_out(void *buf);

extern void DG_Music_Render(short *buf, int nframes);
extern int  DG_Music_IsPlaying(void);

typedef struct
{
    short *data;
    int    len;
} dg_sfx_t;

typedef struct
{
    dg_sfx_t *sfx;
    int       pos;
    int       lvol;
    int       rvol;
    boolean   active;
} dg_chan_t;

static int audio_epoch_ms;
static int64_t produced_frames;
static int queue_min_frames = DG_QUEUE_MIN_UNSET;

int DG_AudioQueueMinFrames(void)
{
    int min_frames = queue_min_frames;
    queue_min_frames = DG_QUEUE_MIN_UNSET;
    return min_frames == DG_QUEUE_MIN_UNSET ? 0 : min_frames;
}

#define DG_TARGET_QUEUE (dg_buf_frames * 2)

static dg_chan_t channels[DG_MAX_CHANNELS];

static short ring[DG_RING_FRAMES * 2];
static volatile unsigned ring_w;
static volatile unsigned ring_r;

static short mix_buf[DG_MAX_UPDATE_FRAMES * 2];

static short music_buf[DG_MAX_UPDATE_FRAMES + 8];

static boolean audio_threaded = false;

static unsigned ring_fill(void)
{
    return ring_w - ring_r;
}

static void ring_push(const short *src, int frames)
{
    unsigned write_pos = ring_w;
    int i;

    for (i = 0; i < frames; ++i)
    {
        unsigned idx = (write_pos + i) & DG_RING_MASK;
        ring[idx * 2]     = src[i * 2];
        ring[idx * 2 + 1] = src[i * 2 + 1];
    }

    __asm__ volatile ("" ::: "memory");
    ring_w = write_pos + frames;
}

static boolean ring_pop(short *dst, int frames)
{
    unsigned read_pos = ring_r;
    int i;

    if (ring_fill() < (unsigned) frames)
    {
        return false;
    }

    for (i = 0; i < frames; ++i)
    {
        unsigned idx = (read_pos + i) & DG_RING_MASK;
        dst[i * 2]     = ring[idx * 2];
        dst[i * 2 + 1] = ring[idx * 2 + 1];
    }

    __asm__ volatile ("" ::: "memory");
    ring_r = read_pos + frames;
    return true;
}

/* sceAudioOutOutput blocks until the device has room, so submitting runs on its
   own thread to overlap that block with rendering. */
static void *dg_audio_thread(void *arg)
{
    static short submit[DG_MAX_BUF_FRAMES * 2];

    (void) arg;

    for (;;)
    {
        if (ring_pop(submit, dg_buf_frames))
        {
            if (!plt_audio_out(submit))
            {
                break;
            }
        }
        else
        {
            DG_SleepMs(DG_SUBMIT_IDLE_SLEEP_MS);
        }
    }
    return 0;
}

static boolean sound_initialised = false;
static boolean sfx_prefix_enabled = true;
static int     dg_start_logs = 0;
static int     dg_submit_fail_logged = 0;

static boolean DecodeSfx(sfxinfo_t *sfxinfo)
{
    byte      *lump;
    dg_sfx_t  *sfx;
    short     *decoded;
    unsigned   lump_len;
    unsigned   length;
    int        sample_rate;
    int        out_samples;
    int        step;
    int        i;

    if (sfxinfo->lumpnum < 0)
    {
        return false;
    }

    lump     = W_CacheLumpNum(sfxinfo->lumpnum, PU_STATIC);
    lump_len = W_LumpLength(sfxinfo->lumpnum);

    if (lump_len < 8 || lump[0] != 0x03 || lump[1] != 0x00)
    {
        W_ReleaseLumpNum(sfxinfo->lumpnum);
        return false;
    }

    sample_rate = (lump[3] << 8) | lump[2];
    length      = ((unsigned)lump[7] << 24) | ((unsigned)lump[6] << 16)
                | ((unsigned)lump[5] << 8)  |  (unsigned)lump[4];

    if (length > lump_len - 8 || length <= 48 || sample_rate <= 0)
    {
        W_ReleaseLumpNum(sfxinfo->lumpnum);
        return false;
    }

    lump   += 16;
    length -= 32;

    out_samples = (int)(((unsigned long long)length * DG_RATE)
                        / (unsigned)sample_rate);
    if (out_samples <= 0)
    {
        W_ReleaseLumpNum(sfxinfo->lumpnum);
        return false;
    }

    sfx     = malloc(sizeof(dg_sfx_t));
    decoded = malloc((unsigned)out_samples * sizeof(short));

    if (sfx == NULL || decoded == NULL)
    {
        printf("DG_sound: out of heap decoding lump %d (%d samples)\n",
               sfxinfo->lumpnum, out_samples);
        W_ReleaseLumpNum(sfxinfo->lumpnum);
        return false;
    }

    step = ((int)length << 8) / out_samples;
    {
        unsigned src_pos = 0;
        for (i = 0; i < out_samples; ++i)
        {
            unsigned src_index = src_pos >> 8;
            int sample;

            if (src_index >= length)
            {
                src_index = length - 1;
            }

            sample = (lump[8 + src_index] | (lump[8 + src_index] << 8)) - 32768;
            decoded[i] = (short) sample;

            src_pos += (unsigned) step;
        }
    }

    sfx->data = decoded;
    sfx->len  = out_samples;

    sfxinfo->driver_data = sfx;

    W_ReleaseLumpNum(sfxinfo->lumpnum);

    return true;
}

static dg_sfx_t *GetSfx(sfxinfo_t *sfxinfo)
{
    if (sfxinfo == NULL)
    {
        return NULL;
    }

    if (sfxinfo->driver_data == NULL && !DecodeSfx(sfxinfo))
    {
        return NULL;
    }

    return (dg_sfx_t *) sfxinfo->driver_data;
}

/* vol is 0..127, sep is 32..224 with 128 centred (s_sound.c) */
static void SetPan(dg_chan_t *chan, int vol, int sep)
{
    if (vol < 0)   vol = 0;
    if (vol > 127) vol = 127;
    if (sep < 0)   sep = 0;
    if (sep > 254) sep = 254;

    chan->lvol = ((254 - sep) * vol) / 127;
    chan->rvol = ((      sep) * vol) / 127;
}

static boolean I_DG_InitSound(boolean use_sfx_prefix)
{
    int i;

    sfx_prefix_enabled = use_sfx_prefix;

    for (i = 0; i < DG_MAX_CHANNELS; ++i)
    {
        channels[i].active = false;
        channels[i].sfx    = NULL;
    }

    ring_w = ring_r = 0;

    dg_buf_frames = plt_audio_grain();
    if (dg_buf_frames <= 0 || dg_buf_frames > DG_MAX_BUF_FRAMES)
    {
        dg_buf_frames = DG_DEFAULT_BUF_FRAMES;
    }

    audio_threaded = (plt_audio_thread_start(dg_audio_thread) == 0);
    printf("DG_sound: submit thread %s\n",
           audio_threaded ? "started" : "UNAVAILABLE, submitting inline");

    sound_initialised = true;

    printf("DG_sound: init ok, %dHz, %d-frame buffers, target %d, prefix=%d\n",
           DG_RATE, dg_buf_frames, DG_TARGET_QUEUE, (int) use_sfx_prefix);

    return true;
}

static void I_DG_ShutdownSound(void)
{
    int i;

    for (i = 0; i < DG_MAX_CHANNELS; ++i)
    {
        channels[i].active = false;
    }

    ring_r = ring_w;
    sound_initialised = false;
}

static int I_DG_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char namebuf[9];

    if (sfx->link != NULL)
    {
        sfx = sfx->link;
    }

    if (sfx_prefix_enabled)
    {
        M_snprintf(namebuf, sizeof(namebuf), "ds%s", DEH_String(sfx->name));
    }
    else
    {
        M_snprintf(namebuf, sizeof(namebuf), "%s", DEH_String(sfx->name));
    }

    return W_CheckNumForName(namebuf);
}

static void I_DG_UpdateSoundParams(int handle, int vol, int sep)
{
    if (!sound_initialised || handle < 0 || handle >= DG_MAX_CHANNELS)
    {
        return;
    }

    SetPan(&channels[handle], vol, sep);
}

static int I_DG_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep)
{
    dg_sfx_t *sfx;

    if (!sound_initialised || channel < 0 || channel >= DG_MAX_CHANNELS)
    {
        return -1;
    }

    sfx = GetSfx(sfxinfo);
    if (sfx == NULL)
    {
        if (sfxinfo != NULL && sfxinfo->lumpnum >= 0)
        {
            printf("DG_sound: decode FAILED for lump %d (%s)\n",
                   sfxinfo->lumpnum, sfxinfo->name);
        }
        return -1;
    }

    if (dg_start_logs < DG_START_LOG_LIMIT)
    {
        ++dg_start_logs;
        printf("DG_sound: start ch=%d len=%d vol=%d sep=%d -> l=%d r=%d\n",
               channel, sfx->len, vol, sep,
               ((254 - sep) * vol) / 127, (sep * vol) / 127);
    }

    channels[channel].sfx    = sfx;
    channels[channel].pos    = 0;
    channels[channel].active = true;
    SetPan(&channels[channel], vol, sep);

    return channel;
}

static void I_DG_StopSound(int handle)
{
    if (handle < 0 || handle >= DG_MAX_CHANNELS)
    {
        return;
    }

    channels[handle].active = false;
}

static boolean I_DG_SoundIsPlaying(int handle)
{
    if (handle < 0 || handle >= DG_MAX_CHANNELS)
    {
        return false;
    }

    return channels[handle].active;
}

static void I_DG_UpdateSound(void)
{
    int want_frames;
    int i;
    int chan_index;

    if (!sound_initialised)
    {
        return;
    }

    {
        int now_ms = I_GetTimeMS();
        int64_t consumed_frames, queued_frames;

        if (audio_epoch_ms == 0)
        {
            audio_epoch_ms = now_ms;
            produced_frames = 0;
        }

        consumed_frames = ((int64_t)(now_ms - audio_epoch_ms) * DG_RATE) / 1000;
        queued_frames   = produced_frames - consumed_frames;

        if (queued_frames < -(DG_RATE / 4) || queued_frames > DG_RATE)
        {
            audio_epoch_ms  = now_ms;
            produced_frames = 0;
            queued_frames   = 0;
        }

        if (queued_frames < queue_min_frames)
        {
            queue_min_frames = (int)queued_frames;
        }

        want_frames = (int)(DG_TARGET_QUEUE - queued_frames);
        if (want_frames < 0)
        {
            want_frames = 0;
        }
    }

    if (want_frames > DG_MAX_UPDATE_FRAMES)
    {
        want_frames = DG_MAX_UPDATE_FRAMES;
    }

    if ((int) ring_fill() + want_frames > DG_RING_FRAMES)
    {
        want_frames = DG_RING_FRAMES - (int) ring_fill();
    }
    if (want_frames < 0)
    {
        want_frames = 0;
    }

    for (i = 0; i < want_frames; ++i)
    {
        int left  = 0;
        int right = 0;

        for (chan_index = 0; chan_index < DG_MAX_CHANNELS; ++chan_index)
        {
            dg_chan_t *chan = &channels[chan_index];
            int sample;

            if (!chan->active)
            {
                continue;
            }

            if (chan->pos >= chan->sfx->len)
            {
                chan->active = false;
                continue;
            }

            sample = chan->sfx->data[chan->pos++];

            left  += (sample * chan->lvol) >> 8;
            right += (sample * chan->rvol) >> 8;
        }

        if (left  >  32767) left  =  32767;
        if (left  < -32768) left  = -32768;
        if (right >  32767) right =  32767;
        if (right < -32768) right = -32768;

        mix_buf[i * 2]     = (short) left;
        mix_buf[i * 2 + 1] = (short) right;
    }

    if (DG_Music_IsPlaying())
    {
        for (i = 0; i < want_frames; ++i)
        {
            music_buf[i] = 0;
        }
        DG_Music_Render(music_buf, want_frames);

        for (i = 0; i < want_frames; ++i)
        {
            int left  = mix_buf[i * 2]     + music_buf[i];
            int right = mix_buf[i * 2 + 1] + music_buf[i];

            if (left  >  32767) left  =  32767;
            if (left  < -32768) left  = -32768;
            if (right >  32767) right =  32767;
            if (right < -32768) right = -32768;

            mix_buf[i * 2]     = (short) left;
            mix_buf[i * 2 + 1] = (short) right;
        }
    }

    ring_push(mix_buf, want_frames);
    produced_frames += want_frames;

    if (!audio_threaded)
    {
        static short submit[DG_MAX_BUF_FRAMES * 2];

        while (ring_pop(submit, dg_buf_frames))
        {
            if (!plt_audio_out(submit))
            {
                if (dg_submit_fail_logged == 0)
                {
                    dg_submit_fail_logged = 1;
                    printf("DG_sound: plt_audio_out FAILED (no audio handle)\n");
                }
                ring_r = ring_w;
                return;
            }
        }
    }
}

static void I_DG_CacheSounds(sfxinfo_t *sounds, int num_sounds)
{
    (void) sounds;
    (void) num_sounds;
}

static snddevice_t sound_dg_devices[] =
{
    SNDDEVICE_SB,
    SNDDEVICE_PAS,
    SNDDEVICE_GUS,
    SNDDEVICE_WAVEBLASTER,
    SNDDEVICE_SOUNDCANVAS,
    SNDDEVICE_AWE32,
};

sound_module_t DG_sound_module =
{
    sound_dg_devices,
    arrlen(sound_dg_devices),
    I_DG_InitSound,
    I_DG_ShutdownSound,
    I_DG_GetSfxLumpNum,
    I_DG_UpdateSound,
    I_DG_UpdateSoundParams,
    I_DG_StartSound,
    I_DG_StopSound,
    I_DG_SoundIsPlaying,
    I_DG_CacheSounds,
};

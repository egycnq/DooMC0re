#include "dg_music.h"
#include "dg_opl.h"
#include "dg_opl_tables.h"

#define MUS_TICKRATE    140
#define MAX_MUS_CHANS   16
#define PERC_CHANNEL    15
#define GENMIDI_MELODIC 128
#define NUM_INSTRUMENTS 175

#define DEFAULT_OUT_RATE 48000

#define GENMIDI_FLAG_FIXED   0x0001
#define GENMIDI_FLAG_DOUBLE  0x0004

#define GENMIDI_HEADER_LEN 8
#define GENMIDI_INSTR_LEN  36
#define GENMIDI_NAME_LEN   32

#define GENMIDI_PERC_FIRST_NOTE 35
#define GENMIDI_PERC_LAST_NOTE  81
#define PERC_BASE_NOTE          60
#define MIDI_PERC_CHANNEL       9

#define MUS_EV_RELEASE_NOTE 0
#define MUS_EV_PLAY_NOTE    1
#define MUS_EV_PITCH_BEND   2
#define MUS_EV_SYSTEM       3
#define MUS_EV_CONTROLLER   4
#define MUS_EV_END_MEASURE  5
#define MUS_EV_SCORE_END    6
#define MUS_EV_UNUSED       7

#define MUS_SYS_ALL_SOUNDS_OFF 10
#define MUS_SYS_ALL_NOTES_OFF  11
#define MUS_SYS_RESET_CONTROL  14

#define MUS_CTRL_INSTRUMENT 0
#define MUS_CTRL_VOLUME     3
#define MUS_CTRL_PAN        4

typedef struct
{
    unsigned char tremolo;
    unsigned char attack;
    unsigned char sustain;
    unsigned char waveform;
    unsigned char scale;
    unsigned char level;
} gm_op_t;

typedef struct
{
    gm_op_t       mod;
    unsigned char feedback;
    gm_op_t       car;
    short         base_note_offset;
} gm_voice_t;

typedef struct
{
    unsigned short flags;
    unsigned char  fine_tuning;
    unsigned char  fixed_note;
    gm_voice_t     voices[2];
} gm_instr_t;

static gm_instr_t gm_bank[NUM_INSTRUMENTS];
static int        gm_loaded = 0;

typedef struct
{
    int instrument;
    int volume_base;
    int volume;
    int last_velocity;
    int pan;
    int bend;
} mus_chan_t;

typedef struct
{
    int active;
    int mus_chan;
    int note;
    int play_note;
    int detune32;
    int note_volume;
    int second_voice;
    const gm_voice_t *voice;
    unsigned int age;
} opl_voice_t;

static mus_chan_t  mus_chans[MAX_MUS_CHANS];
static opl_voice_t voices[OPL_NUM_CHANNELS];

static const unsigned char *score;
static int          score_len;
static int          score_pos;
static int          playing;
static int          loop_song;
static int          music_volume = 127;
static unsigned int voice_clock;

unsigned int dgm_notes_started, dgm_notes_dropped;
unsigned int dgm_effective_volume_sum, dgm_effective_volume_min = 999,
             dgm_effective_volume_max;
unsigned int dgm_notes_pitch_clamped;
unsigned int dgm_bends, dgm_bends_offcentre;
unsigned long long dgm_active_voice_samples, dgm_total_samples;

static int out_rate = DEFAULT_OUT_RATE;
static int samples_per_tick_q16;

static long long tick_accum_q16;

static void ParseOp(const unsigned char *data, gm_op_t *op)
{
    op->tremolo  = data[0];
    op->attack   = data[1];
    op->sustain  = data[2];
    op->waveform = data[3];
    op->scale    = data[4];
    op->level    = data[5];
}

static void ParseVoice(const unsigned char *data, gm_voice_t *voice)
{
    ParseOp(data, &voice->mod);
    voice->feedback = data[6];
    ParseOp(data + 7, &voice->car);
    voice->base_note_offset = (short) (data[14] | (data[15] << 8));
}

int DG_Music_Init(const unsigned char *genmidi, int genmidi_len, int rate)
{
    int i;
    int expected_len = GENMIDI_HEADER_LEN
                     + NUM_INSTRUMENTS * GENMIDI_INSTR_LEN
                     + NUM_INSTRUMENTS * GENMIDI_NAME_LEN;

    gm_loaded = 0;

    if (genmidi == 0 || genmidi_len < expected_len)
    {
        return 0;
    }
    if (genmidi[0] != '#' || genmidi[1] != 'O' || genmidi[2] != 'P'
        || genmidi[3] != 'L' || genmidi[4] != '_' || genmidi[5] != 'I'
        || genmidi[6] != 'I' || genmidi[7] != '#')
    {
        return 0;
    }

    for (i = 0; i < NUM_INSTRUMENTS; ++i)
    {
        const unsigned char *entry =
            genmidi + GENMIDI_HEADER_LEN + i * GENMIDI_INSTR_LEN;

        gm_bank[i].flags       = (unsigned short) (entry[0] | (entry[1] << 8));
        gm_bank[i].fine_tuning = entry[2];
        gm_bank[i].fixed_note  = entry[3];
        ParseVoice(entry + 4,  &gm_bank[i].voices[0]);
        ParseVoice(entry + 20, &gm_bank[i].voices[1]);
    }

    out_rate = (rate > 0) ? rate : DEFAULT_OUT_RATE;
    samples_per_tick_q16 = (int) (((long long) out_rate << 16) / MUS_TICKRATE);

    OPL_Init(out_rate);
    /* Reg 0x08 bit 6 is NTS, the keyscale-rate note select; DMX sets it. */
    OPL_WriteReg(0x08, 0x40);
    gm_loaded = 1;
    playing = 0;
    return 1;
}

/* One octave above concert pitch (note 69 -> 880 Hz) per DMX's register */
#define SEMITONE_REF_OCTAVE 9

static const unsigned int semitone_milli[12] =
{
    8372018u, 8869844u, 9397273u, 9956063u, 10548082u, 11175303u,
    11839820u, 12543854u, 13289750u, 14080000u, 14917240u, 15804266u
};

/* Q16 fractional pitch: entry r = round(65536 * 2**(r/384)),
   r in 1/32 semitones. */
static const unsigned int detune_q16[32] =
{
     65536u,  65654u,  65773u,  65892u,  66011u,  66130u,  66250u,  66369u,
     66489u,  66609u,  66730u,  66850u,  66971u,  67092u,  67213u,  67335u,
     67456u,  67578u,  67700u,  67823u,  67945u,  68068u,  68191u,  68314u,
     68438u,  68561u,  68685u,  68809u,  68933u,  69058u,  69183u,  69308u
};

unsigned int DG_Music_NoteMilliHz(int note, int detune32)
{
    unsigned int milli_hz;
    int octave, semitones, detune_frac;

    semitones   = detune32 >> 5;
    detune_frac = detune32 - (semitones << 5);
    note += semitones;

    if (note < 0)   note = 0;
    if (note > 127) note = 127;

    octave = note / 12;
    milli_hz = semitone_milli[note % 12];

    if (octave < SEMITONE_REF_OCTAVE)
    {
        milli_hz >>= (SEMITONE_REF_OCTAVE - octave);
    }
    else if (octave > SEMITONE_REF_OCTAVE)
    {
        milli_hz <<= (octave - SEMITONE_REF_OCTAVE);
    }

    if (detune_frac != 0)
    {
        milli_hz = (unsigned int) (((unsigned long long) milli_hz
                                    * detune_q16[detune_frac]) >> 16);
    }
    return milli_hz;
}

static const int op_reg_offsets[OPL_NUM_CHANNELS] =
{
    0x000, 0x001, 0x002, 0x008, 0x009, 0x00A, 0x010, 0x011, 0x012,
    0x100, 0x101, 0x102, 0x108, 0x109, 0x10A, 0x110, 0x111, 0x112
};

static const unsigned char opl_volume_map[128] =
{
      0,   1,   3,   5,   6,   8,  10,  11,
     13,  14,  16,  17,  19,  20,  22,  23,
     25,  26,  27,  29,  30,  32,  33,  34,
     36,  37,  39,  41,  43,  45,  47,  49,
     50,  52,  54,  55,  57,  59,  60,  61,
     63,  64,  66,  67,  68,  69,  71,  72,
     73,  74,  75,  76,  77,  79,  80,  81,
     82,  83,  84,  84,  85,  86,  87,  88,
     89,  90,  91,  92,  92,  93,  94,  95,
     96,  96,  97,  98,  99,  99, 100, 101,
    101, 102, 103, 103, 104, 105, 105, 106,
    107, 107, 108, 109, 109, 110, 110, 111,
    112, 112, 113, 113, 114, 114, 115, 115,
    116, 117, 117, 118, 118, 119, 119, 120,
    120, 121, 121, 122, 122, 123, 123, 123,
    124, 124, 125, 125, 126, 126, 127, 127
};

static int VoiceCarTL(int note_volume, int channel_volume)
{
    unsigned int midi_volume, scaled_volume;

    if (note_volume < 0)      note_volume = 0;
    if (note_volume > 127)    note_volume = 127;
    if (channel_volume < 0)   channel_volume = 0;
    if (channel_volume > 127) channel_volume = 127;

    midi_volume = 2u * (opl_volume_map[channel_volume] + 1u);
    scaled_volume = ((unsigned int) opl_volume_map[note_volume]
                     * midi_volume) >> 9;
    return 0x3F - (int) scaled_volume;
}

static void ApplyVoiceLevels(int slot)
{
    const gm_voice_t *voice = voices[slot].voice;
    int reg_offset, car_tl;

    if (voice == 0)
    {
        return;
    }
    reg_offset = op_reg_offsets[slot];
    car_tl = VoiceCarTL(voices[slot].note_volume,
                        mus_chans[voices[slot].mus_chan].volume);

    OPL_WriteReg((unsigned) (reg_offset + 0x40 + 3),
                 (unsigned) (car_tl | (voice->car.scale & 0xC0)));

    if ((voice->feedback & 1) && (voice->mod.level & 0x3F) != 0x3F)
    {
        int mod_tl = voice->mod.level & 0x3F;
        if (mod_tl < car_tl)
        {
            mod_tl = car_tl;
        }
        OPL_WriteReg((unsigned) (reg_offset + 0x40),
                     (unsigned) (mod_tl | (voice->mod.scale & 0xC0)));
    }
}

static void ProgramVoice(int slot, const gm_voice_t *voice)
{
    int reg_offset = op_reg_offsets[slot];
    int mod_op = reg_offset;
    int car_op = reg_offset + 3;
    int additive = voice->feedback & 1;

    OPL_WriteReg(0x20 + mod_op, voice->mod.tremolo);
    OPL_WriteReg(0x60 + mod_op, voice->mod.attack);
    OPL_WriteReg(0x80 + mod_op, voice->mod.sustain);
    OPL_WriteReg(0xE0 + mod_op, voice->mod.waveform);
    OPL_WriteReg(0x40 + mod_op,
                 additive ? (unsigned) (0x3F | (voice->mod.scale & 0xC0))
                          : (unsigned) ((voice->mod.level & 0x3F)
                                        | (voice->mod.scale & 0xC0)));

    OPL_WriteReg(0x20 + car_op, voice->car.tremolo);
    OPL_WriteReg(0x60 + car_op, voice->car.attack);
    OPL_WriteReg(0x80 + car_op, voice->car.sustain);
    OPL_WriteReg(0xE0 + car_op, voice->car.waveform);
    OPL_WriteReg(0x40 + car_op, (unsigned) (0x3F | (voice->car.scale & 0xC0)));

    OPL_WriteReg((reg_offset & 0x100) + 0xC0 + (slot % OPL_CHANNELS_PER_BANK),
                 voice->feedback);
}

static void RelevelChannel(int mus_chan)
{
    int i;

    for (i = 0; i < OPL_NUM_CHANNELS; ++i)
    {
        if (voices[i].active && voices[i].mus_chan == mus_chan)
        {
            ApplyVoiceLevels(i);
        }
    }
}

static void RepitchChannel(int mus_chan)
{
    int i;

    for (i = 0; i < OPL_NUM_CHANNELS; ++i)
    {
        unsigned int milli_hz;
        int block = 0, fnum;

        if (!voices[i].active || voices[i].mus_chan != mus_chan)
        {
            continue;
        }

        milli_hz = DG_Music_NoteMilliHz(voices[i].play_note,
                                        voices[i].detune32
                                            + mus_chans[mus_chan].bend);
        fnum = OPL_FreqToFnum((int) milli_hz, &block);
        OPL_SetFreq(i, block, fnum);
    }
}

static int MidiChanOf(int mus_chan)
{
    if (mus_chan == PERC_CHANNEL) return MIDI_PERC_CHANNEL;
    return (mus_chan >= MIDI_PERC_CHANNEL) ? mus_chan + 1 : mus_chan;
}

static int FindFreeVoice(void)
{
    int i;

    for (i = 0; i < OPL_NUM_CHANNELS; ++i)
    {
        if (!voices[i].active)
        {
            return i;
        }
    }
    return -1;
}

static void ReplaceExistingVoice(void)
{
    int order[OPL_NUM_CHANNELS];
    int n = 0, i, j, victim;

    for (i = 0; i < OPL_NUM_CHANNELS; ++i)
    {
        if (voices[i].active)
        {
            for (j = n; j > 0 && voices[order[j - 1]].age > voices[i].age; --j)
            {
                order[j] = order[j - 1];
            }
            order[j] = i;
            n++;
        }
    }
    if (n == 0)
    {
        return;
    }

    victim = 0;
    for (i = 0; i < n; ++i)
    {
        if (voices[order[i]].second_voice != 0
         || MidiChanOf(voices[order[i]].mus_chan)
            >= MidiChanOf(voices[order[victim]].mus_chan))
        {
            victim = i;
        }
    }

    OPL_NoteOff(order[victim]);
    voices[order[victim]].active = 0;
}

static void StartNote(int mus_chan, int note, int volume)
{
    const gm_instr_t *instr;
    const gm_voice_t *voice;
    int instr_index, voice_index, num_voices;
    int base_note = note;

    if (!gm_loaded)
    {
        return;
    }

    if (mus_chan == PERC_CHANNEL)
    {
        if (note < GENMIDI_PERC_FIRST_NOTE || note > GENMIDI_PERC_LAST_NOTE)
        {
            return;
        }
        instr_index = GENMIDI_MELODIC + (note - GENMIDI_PERC_FIRST_NOTE);
        base_note = PERC_BASE_NOTE;
    }
    else
    {
        instr_index = mus_chans[mus_chan].instrument;
        if (instr_index < 0 || instr_index >= GENMIDI_MELODIC)
        {
            instr_index = (instr_index < 0) ? 0 : GENMIDI_MELODIC - 1;
        }
    }

    instr = &gm_bank[instr_index];

    dgm_notes_started++;
    {
        unsigned int effective_volume =
            (unsigned) (0x3F - VoiceCarTL(volume,
                                          mus_chans[mus_chan].volume)) * 2u;
        dgm_effective_volume_sum += effective_volume;
        if (effective_volume < dgm_effective_volume_min)
        {
            dgm_effective_volume_min = effective_volume;
        }
        if (effective_volume > dgm_effective_volume_max)
        {
            dgm_effective_volume_max = effective_volume;
        }
    }

    num_voices = (instr->flags & GENMIDI_FLAG_DOUBLE) ? 2 : 1;

    if (FindFreeVoice() < 0)
    {
        ReplaceExistingVoice();
    }

    for (voice_index = 0; voice_index < num_voices; ++voice_index)
    {
        int slot = FindFreeVoice();
        int play_note = base_note;
        int detune32 = 0;
        unsigned int milli_hz;
        int block = 0, fnum;

        if (slot < 0)
        {
            dgm_notes_dropped++;
            break;
        }

        voice = &instr->voices[voice_index];

        if (instr->flags & GENMIDI_FLAG_FIXED)
        {
            play_note = instr->fixed_note;
        }
        else
        {
            play_note += voice->base_note_offset;
        }
        if (play_note < 0)   play_note = 0;
        if (play_note > 127) play_note = 127;

        if (voice_index == 1)
        {
            detune32 = ((int) instr->fine_tuning / 2) - 64;
        }

        milli_hz = DG_Music_NoteMilliHz(play_note,
                                        detune32 + mus_chans[mus_chan].bend);
        fnum = OPL_FreqToFnum((int) milli_hz, &block);
        if (block == 7 && fnum == 1023) dgm_notes_pitch_clamped++;

        ProgramVoice(slot, voice);

        voices[slot].active      = 1;
        voices[slot].mus_chan    = mus_chan;
        voices[slot].note        = note;
        voices[slot].play_note   = play_note;
        voices[slot].detune32    = detune32;
        voices[slot].note_volume = volume;
        voices[slot].second_voice      = voice_index;
        voices[slot].voice       = voice;
        voices[slot].age         = voice_clock++;

        ApplyVoiceLevels(slot);
        OPL_NoteOn(slot, block, fnum);
    }
}

static void StopNote(int mus_chan, int note)
{
    int i;
    for (i = 0; i < OPL_NUM_CHANNELS; ++i)
    {
        if (voices[i].active && voices[i].mus_chan == mus_chan
            && voices[i].note == note)
        {
            OPL_NoteOff(i);
            voices[i].active = 0;
        }
    }
}

static void AllNotesOff(void)
{
    int i;
    for (i = 0; i < OPL_NUM_CHANNELS; ++i)
    {
        OPL_NoteOff(i);
        voices[i].active = 0;
    }
}

int DG_Music_Play(const unsigned char *mus, int mus_len, int looping)
{
    int i;
    unsigned int score_start, declared_score_len;

    if (!gm_loaded || mus == 0 || mus_len < 16)
    {
        return 0;
    }
    if (mus[0] != 'M' || mus[1] != 'U' || mus[2] != 'S' || mus[3] != 0x1A)
    {
        return 0;
    }

    declared_score_len = (unsigned) (mus[4] | (mus[5] << 8));
    score_start        = (unsigned) (mus[6] | (mus[7] << 8));

    if (score_start >= (unsigned) mus_len)
    {
        return 0;
    }

    score     = mus + score_start;
    score_len = (int) declared_score_len;
    if (score_start + declared_score_len > (unsigned) mus_len)
    {
        score_len = mus_len - (int) score_start;
    }
    score_pos = 0;

    for (i = 0; i < MAX_MUS_CHANS; ++i)
    {
        mus_chans[i].instrument = 0;
        mus_chans[i].volume_base = 127;
        mus_chans[i].volume = music_volume;
        mus_chans[i].last_velocity = 127;
        mus_chans[i].pan = 64;
        mus_chans[i].bend = 0;
    }

    AllNotesOff();
    voice_clock = 0;
    tick_accum_q16 = 0;
    loop_song = looping;
    playing = 1;
    return 1;
}

void DG_Music_Stop(void)
{
    playing = 0;
    AllNotesOff();
}

void DG_Music_SetVolume(int volume)
{
    int i;

    if (volume < 0)   volume = 0;
    if (volume > 127) volume = 127;
    music_volume = volume;

    for (i = 0; i < MAX_MUS_CHANS; ++i)
    {
        mus_chans[i].volume = (mus_chans[i].volume_base > music_volume)
                            ? music_volume : mus_chans[i].volume_base;
        RelevelChannel(i);
    }
}

int DG_Music_IsPlaying(void)
{
    return playing;
}

static int RunTick(void)
{
    for (;;)
    {
        unsigned char descriptor;
        int last_in_tick, event_type, mus_chan;

        if (score_pos >= score_len)
        {
            return -1;
        }

        descriptor   = score[score_pos++];
        last_in_tick = descriptor & 0x80;
        event_type   = (descriptor >> 4) & 0x07;
        mus_chan     = descriptor & 0x0F;

        switch (event_type)
        {
        case MUS_EV_RELEASE_NOTE:
            if (score_pos >= score_len) return -1;
            StopNote(mus_chan, score[score_pos++] & 0x7F);
            break;

        case MUS_EV_PLAY_NOTE:
        {
            int note, volume;
            if (score_pos >= score_len) return -1;
            note = score[score_pos++];
            volume = -1;
            if (note & 0x80)
            {
                if (score_pos >= score_len) return -1;
                volume = score[score_pos++] & 0x7F;
            }
            note &= 0x7F;
            if (volume < 0)
            {
                volume = mus_chans[mus_chan].last_velocity;
            }
            else
            {
                mus_chans[mus_chan].last_velocity = volume;
            }
            StartNote(mus_chan, note, volume);
            break;
        }

        case MUS_EV_PITCH_BEND:
        {
            int wheel;
            if (score_pos >= score_len) return -1;
            wheel = score[score_pos++];
            mus_chans[mus_chan].bend = (wheel >> 1) - 64;
            dgm_bends++;
            if (mus_chans[mus_chan].bend != 0) dgm_bends_offcentre++;
            RepitchChannel(mus_chan);
            break;
        }

        case MUS_EV_SYSTEM:
        {
            int sys_event;
            if (score_pos >= score_len) return -1;
            sys_event = score[score_pos++] & 0x7F;
            if (sys_event == MUS_SYS_ALL_SOUNDS_OFF
                || sys_event == MUS_SYS_ALL_NOTES_OFF)
            {
                int i;
                for (i = 0; i < OPL_NUM_CHANNELS; ++i)
                {
                    if (voices[i].active && voices[i].mus_chan == mus_chan)
                    {
                        OPL_NoteOff(i);
                        voices[i].active = 0;
                    }
                }
            }
            else if (sys_event == MUS_SYS_RESET_CONTROL)
            {
                mus_chans[mus_chan].bend = 0;
                RepitchChannel(mus_chan);
            }
            break;
        }

        case MUS_EV_CONTROLLER:
        {
            int controller, value;
            if (score_pos + 1 >= score_len) return -1;
            controller = score[score_pos++] & 0x7F;
            value      = score[score_pos++] & 0x7F;
            if (controller == MUS_CTRL_INSTRUMENT)
            {
                mus_chans[mus_chan].instrument = value;
            }
            else if (controller == MUS_CTRL_VOLUME)
            {
                mus_chans[mus_chan].volume_base = value;
                mus_chans[mus_chan].volume =
                    (value > music_volume) ? music_volume : value;
                RelevelChannel(mus_chan);
            }
            else if (controller == MUS_CTRL_PAN)
            {
                mus_chans[mus_chan].pan = value;
            }
            break;
        }

        case MUS_EV_END_MEASURE:
            break;

        case MUS_EV_SCORE_END:
            return -1;

        /* Type 7 is unused but still carries a payload byte; not skipping
           it desyncs the stream. */
        case MUS_EV_UNUSED:
            if (score_pos >= score_len) return -1;
            score_pos++;
            break;

        default:
            break;
        }

        if (last_in_tick)
        {
            int delay = 0;
            for (;;)
            {
                unsigned char delay_byte;
                if (score_pos >= score_len) return -1;
                delay_byte = score[score_pos++];
                delay = (delay << 7) | (delay_byte & 0x7F);
                if (!(delay_byte & 0x80)) break;
            }
            return delay;
        }
    }
}

void DG_Music_Render(short *buf, int nframes)
{
    if (!playing || !gm_loaded)
    {
        return;
    }

    while (nframes > 0)
    {
        int chunk;

        if (tick_accum_q16 <= 0)
        {
            int delay = RunTick();

            if (delay < 0)
            {
                if (loop_song)
                {
                    int i;

                    score_pos = 0;
                    AllNotesOff();
                    for (i = 0; i < MAX_MUS_CHANS; ++i)
                    {
                        mus_chans[i].instrument = 0;
                        mus_chans[i].volume_base = 127;
                        mus_chans[i].volume = music_volume;
                        mus_chans[i].last_velocity = 127;
                        mus_chans[i].pan = 64;
                        mus_chans[i].bend = 0;
                    }
                    delay = 0;
                }
                else
                {
                    playing = 0;
                    AllNotesOff();
                    return;
                }
            }
            tick_accum_q16 += (long long) delay * samples_per_tick_q16;
            if (delay == 0)
            {
                continue;
            }
        }

        chunk = (int) (tick_accum_q16 >> 16);
        if (chunk <= 0)
        {
            chunk = 1;
        }
        if (chunk > nframes)
        {
            chunk = nframes;
        }

        {
            int i, active_voices = 0;

            for (i = 0; i < OPL_NUM_CHANNELS; ++i)
            {
                if (voices[i].active)
                {
                    active_voices++;
                }
            }
            dgm_active_voice_samples +=
                (unsigned long long) active_voices * chunk;
            dgm_total_samples += chunk;
        }
        OPL_Generate(buf, chunk);

        buf += chunk;
        nframes -= chunk;
        tick_accum_q16 -= (long long) chunk << 16;
    }
}

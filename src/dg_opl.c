#include "dg_opl.h"
#include "dg_opl_tables.h"

#define PHASE_BITS      32
#define SINE_ENTRIES    1024
#define PHASE_TO_INDEX  (PHASE_BITS - 10)

#define ENV_MAX         511

#define FNUM_SCALE_BITS 20
#define FNUM_LIMIT      1024
#define NUM_BLOCKS      8

#define TWO_PI_Q16      411775LL

enum { EG_OFF = 0, EG_ATTACK, EG_DECAY, EG_SUSTAIN, EG_RELEASE };

typedef struct
{
    unsigned char am_vib_eg_ksr_mult;
    unsigned char ksl_tl;
    unsigned char ar_dr;
    unsigned char sl_rr;
    unsigned char waveform;

    unsigned int  phase;
    unsigned int  phase_inc;
    unsigned int  eg_counter;
    int           eg_state;
    int           env;
    int           tl_att;
    int           ksl_att;
    int           out;
    int           prev_out;
} opl_op_t;

typedef struct
{
    opl_op_t op[2];
    unsigned int fnum;
    unsigned int block;
    unsigned char fb_con;
    int keyon;
} opl_chan_t;

static opl_chan_t chans[OPL_NUM_CHANNELS];
static int        out_rate = 48000;

static unsigned int eg_inc_per_sample[64];

static int lp_alpha_q16;
static int lp_z1, lp_z2;

static unsigned int trem_acc_q24, trem_inc_q24;
static unsigned int vib_acc_q24,  vib_inc_q24;
static int trem_pos;
static int trem_att;
static int vib_pos;
static int trem_depth_shift = 4;
static int vib_depth_shift = 1;
static int note_select;

static int VibOffset(const opl_chan_t *chan)
{
    int range = (int) ((chan->fnum >> 7) & 7);

    if (!(vib_pos & 3))
    {
        return 0;
    }
    if (vib_pos & 1)
    {
        range >>= 1;
    }
    range >>= vib_depth_shift;
    return (vib_pos & 4) ? -range : range;
}

/* YM3812 operator register offsets are irregular; within each group of eight:
     0x00-0x02 mod | 0x03-0x05 car   channels 0-2   (0x06,0x07 unused)
     0x08-0x0A mod | 0x0B-0x0D car   channels 3-5   (0x0E,0x0F unused)
     0x10-0x12 mod | 0x13-0x15 car   channels 6-8 */
static const signed char reg_chan[0x16] =
{
     0,  1,  2,  0,  1,  2, -1, -1,
     3,  4,  5,  3,  4,  5, -1, -1,
     6,  7,  8,  6,  7,  8
};

static const signed char reg_slot[0x16] =
{
     0,  0,  0,  1,  1,  1, -1, -1,
     0,  0,  0,  1,  1,  1, -1, -1,
     0,  0,  0,  1,  1,  1
};

static int reg_to_op(unsigned int reg, int *chan_out, int *slot_out)
{
    unsigned int idx = reg & 0x1F;
    int bank_base = (reg & 0x100) ? OPL_CHANNELS_PER_BANK : 0;

    if (idx >= 0x16 || reg_chan[idx] < 0)
    {
        return 0;
    }
    *chan_out = reg_chan[idx] + bank_base;
    *slot_out = reg_slot[idx];
    return 1;
}

static int reg_to_chan(unsigned int reg, unsigned int base_reg)
{
    int channel = (int) ((reg & 0xFF) - base_reg);
    if (channel < 0 || channel >= OPL_CHANNELS_PER_BANK)
    {
        return -1;
    }
    return channel + ((reg & 0x100) ? OPL_CHANNELS_PER_BANK : 0);
}

int OPL_FnumToFreqMilli(int block, int fnum)
{
    unsigned long long freq_scaled =
        (unsigned long long) fnum * OPL_NATIVE_RATE * 1000ULL;
    return (int) (freq_scaled >> (FNUM_SCALE_BITS - block));
}

int OPL_FreqToFnum(int freq_millihz, int *block_out)
{
    int block;

    for (block = 0; block < NUM_BLOCKS; ++block)
    {
        unsigned long long fnum =
            ((unsigned long long) freq_millihz << (FNUM_SCALE_BITS - block))
            / ((unsigned long long) OPL_NATIVE_RATE * 1000ULL);
        if (fnum < FNUM_LIMIT)
        {
            if (block_out != 0)
            {
                *block_out = block;
            }
            return (int) fnum;
        }
    }
    if (block_out != 0)
    {
        *block_out = NUM_BLOCKS - 1;
    }
    return FNUM_LIMIT - 1;
}

static void UpdatePhaseInc(opl_chan_t *chan, opl_op_t *op)
{
    unsigned long long freq_milli;
    unsigned long long phase_inc;
    int mult_x2;
    int fnum = (int) chan->fnum;

    if (op->am_vib_eg_ksr_mult & 0x40)
    {
        fnum += VibOffset(chan);
    }

    freq_milli =
        (unsigned long long) OPL_FnumToFreqMilli((int) chan->block, fnum);
    mult_x2 = opl_mult_x2[op->am_vib_eg_ksr_mult & 0x0F];

    phase_inc = (freq_milli * mult_x2) / 2ULL;
    phase_inc = (phase_inc << PHASE_BITS)
                / ((unsigned long long) out_rate * 1000ULL);

    op->phase_inc = (unsigned int) phase_inc;
}

static void UpdateAttenuation(opl_chan_t *chan, opl_op_t *op)
{
    int ksl = (op->ksl_tl >> 6) & 3;
    int tl  = op->ksl_tl & 0x3F;

    op->tl_att = tl * 4;

    if (ksl == 0)
    {
        op->ksl_att = 0;
    }
    else
    {
        /* KSL is not monotonic on OPL2: field 1 gives 3.0 dB/oct and field 2
           gives 1.5 dB/oct, so those two are swapped here. */
        static const int ksl_shift[4] = { 0, 1, 2, 0 };
        int attenuation = opl_ksl[(chan->fnum >> 6) & 0x0F]
                          - 8 * (7 - (int) chan->block);
        if (attenuation < 0)
        {
            attenuation = 0;
        }
        op->ksl_att = (attenuation >> ksl_shift[ksl]) * 4;
    }
}

static int EffectiveRate(opl_chan_t *chan, opl_op_t *op, int rate_field)
{
    int rate;

    if (rate_field == 0)
    {
        return 0;
    }

    {
        int key_scale = ((int) chan->block << 1)
                        | (int) ((chan->fnum >> (9 - note_select)) & 1);
        int rate_offset =
            (op->am_vib_eg_ksr_mult & 0x10) ? key_scale : (key_scale >> 2);
        rate = rate_field * 4 + rate_offset;
    }

    if (rate > 63)
    {
        rate = 63;
    }
    return rate;
}

void OPL_SetLowpass(int cutoff_hz)
{
    lp_z1 = 0;
    lp_z2 = 0;

    if (cutoff_hz <= 0 || cutoff_hz * 2 >= out_rate)
    {
        lp_alpha_q16 = 0;
        return;
    }
    {
        long long omega     = ((long long) cutoff_hz * TWO_PI_Q16) / out_rate;
        long long omega2    = (omega * omega) >> 16;
        long long omega3    = (omega2 * omega) >> 16;
        long long exp_omega = 65536LL + omega + omega2 / 2 + omega3 / 6;
        lp_alpha_q16 = (int) (65536LL - ((65536LL << 16) / exp_omega));
    }
}

void OPL_Init(int rate)
{
    int i, j;

    out_rate = (rate > 0) ? rate : 48000;

    for (i = 0; i < 64; ++i)
    {
        unsigned long long scaled =
            ((unsigned long long) opl_eg_inc[i] * (unsigned) OPL_EG_TABLE_RATE)
            / (unsigned) out_rate;
        eg_inc_per_sample[i] = (scaled < 1ULL) ? 1u : (unsigned int) scaled;
    }

    OPL_SetLowpass(OPL_LOWPASS_CUTOFF_HZ);

    {
        unsigned long long trem_denom = 64ULL * (unsigned) out_rate;
        unsigned long long vib_denom  = 1024ULL * (unsigned) out_rate;
        trem_inc_q24 = (unsigned int)
            ((((unsigned long long) OPL_NATIVE_RATE << 24) + trem_denom / 2)
             / trem_denom);
        vib_inc_q24 = (unsigned int)
            ((((unsigned long long) OPL_NATIVE_RATE << 24) + vib_denom / 2)
             / vib_denom);
    }
    trem_acc_q24 = 0;
    vib_acc_q24 = 0;
    trem_pos = 0;
    trem_att = 0;
    vib_pos = 0;
    trem_depth_shift = 4;
    vib_depth_shift = 1;
    note_select = 0;

    for (i = 0; i < OPL_NUM_CHANNELS; ++i)
    {
        chans[i].fnum = 0;
        chans[i].block = 0;
        chans[i].fb_con = 0;
        chans[i].keyon = 0;
        for (j = 0; j < 2; ++j)
        {
            opl_op_t *op = &chans[i].op[j];
            op->am_vib_eg_ksr_mult = 0;
            op->ksl_tl = 0;
            op->ar_dr = 0;
            op->sl_rr = 0;
            op->waveform = 0;
            op->phase = 0;
            op->phase_inc = 0;
            op->eg_counter = (unsigned int) ENV_MAX << 16;
            op->eg_state = EG_OFF;
            op->env = ENV_MAX;
            op->tl_att = 0;
            op->ksl_att = 0;
            op->out = 0;
            op->prev_out = 0;
        }
    }
}

void OPL_WriteReg(unsigned int reg, unsigned int value)
{
    int channel, slot;

    value &= 0xFF;

    if ((reg & 0xFF) >= 0x20 && (reg & 0xFF) <= 0x35)
    {
        if (reg_to_op(reg, &channel, &slot))
        {
            chans[channel].op[slot].am_vib_eg_ksr_mult = (unsigned char) value;
            UpdatePhaseInc(&chans[channel], &chans[channel].op[slot]);
        }
    }
    else if ((reg & 0xFF) >= 0x40 && (reg & 0xFF) <= 0x55)
    {
        if (reg_to_op(reg, &channel, &slot))
        {
            chans[channel].op[slot].ksl_tl = (unsigned char) value;
            UpdateAttenuation(&chans[channel], &chans[channel].op[slot]);
        }
    }
    else if ((reg & 0xFF) >= 0x60 && (reg & 0xFF) <= 0x75)
    {
        if (reg_to_op(reg, &channel, &slot))
        {
            chans[channel].op[slot].ar_dr = (unsigned char) value;
        }
    }
    else if ((reg & 0xFF) >= 0x80 && (reg & 0xFF) <= 0x95)
    {
        if (reg_to_op(reg, &channel, &slot))
        {
            chans[channel].op[slot].sl_rr = (unsigned char) value;
        }
    }
    else if ((reg & 0xFF) >= 0xE0 && (reg & 0xFF) <= 0xF5)
    {
        if (reg_to_op(reg, &channel, &slot))
        {
            chans[channel].op[slot].waveform = (unsigned char) (value & 0x03);
        }
    }
    else if ((reg & 0xFF) >= 0xA0 && (reg & 0xFF) <= 0xA8)
    {
        channel = reg_to_chan(reg, 0xA0);
        if (channel < 0) return;
        chans[channel].fnum = (chans[channel].fnum & 0x300) | value;
        UpdatePhaseInc(&chans[channel], &chans[channel].op[0]);
        UpdatePhaseInc(&chans[channel], &chans[channel].op[1]);
        UpdateAttenuation(&chans[channel], &chans[channel].op[0]);
        UpdateAttenuation(&chans[channel], &chans[channel].op[1]);
    }
    else if ((reg & 0xFF) >= 0xB0 && (reg & 0xFF) <= 0xB8)
    {
        int keyon;

        channel = reg_to_chan(reg, 0xB0);
        if (channel < 0) return;
        chans[channel].fnum = (chans[channel].fnum & 0xFF)
                              | ((value & 0x03) << 8);
        chans[channel].block = (value >> 2) & 0x07;
        keyon = (value >> 5) & 1;

        UpdatePhaseInc(&chans[channel], &chans[channel].op[0]);
        UpdatePhaseInc(&chans[channel], &chans[channel].op[1]);
        UpdateAttenuation(&chans[channel], &chans[channel].op[0]);
        UpdateAttenuation(&chans[channel], &chans[channel].op[1]);

        if (keyon && !chans[channel].keyon)
        {
            int j;
            for (j = 0; j < 2; ++j)
            {
                opl_op_t *op = &chans[channel].op[j];
                op->phase = 0;
                op->eg_state = EG_ATTACK;
                op->eg_counter = (unsigned int) ENV_MAX << 16;
                op->env = ENV_MAX;
            }
        }
        else if (!keyon && chans[channel].keyon)
        {
            chans[channel].op[0].eg_state = EG_RELEASE;
            chans[channel].op[1].eg_state = EG_RELEASE;
        }
        chans[channel].keyon = keyon;
    }
    else if ((reg & 0xFF) >= 0xC0 && (reg & 0xFF) <= 0xC8)
    {
        channel = reg_to_chan(reg, 0xC0);
        if (channel < 0) return;
        chans[channel].fb_con = (unsigned char) value;
    }
    else if ((reg & 0xFF) == 0xBD)
    {
        trem_depth_shift = (value & 0x80) ? 2 : 4;
        vib_depth_shift = (value & 0x40) ? 0 : 1;
        trem_att = ((trem_pos < 105) ? trem_pos : 210 - trem_pos)
                   >> trem_depth_shift;
        for (channel = 0; channel < OPL_NUM_CHANNELS; ++channel)
        {
            if (chans[channel].op[0].am_vib_eg_ksr_mult & 0x40)
            {
                UpdatePhaseInc(&chans[channel], &chans[channel].op[0]);
            }
            if (chans[channel].op[1].am_vib_eg_ksr_mult & 0x40)
            {
                UpdatePhaseInc(&chans[channel], &chans[channel].op[1]);
            }
        }
    }
    else if (reg == 0x08)
    {
        note_select = (int) ((value >> 6) & 1);
    }
}

void OPL_NoteOn(int channel, int block, int fnum)
{
    if (channel < 0 || channel >= OPL_NUM_CHANNELS)
    {
        return;
    }
    {
        unsigned int bank = (channel >= OPL_CHANNELS_PER_BANK) ? 0x100u : 0u;
        unsigned int bank_channel =
            (unsigned int) (channel % OPL_CHANNELS_PER_BANK);
        OPL_WriteReg(bank + 0xA0 + bank_channel, (unsigned int) fnum & 0xFF);
        OPL_WriteReg(bank + 0xB0 + bank_channel,
                     0x20 | (((unsigned int) block & 7) << 2)
                          | (((unsigned int) fnum >> 8) & 3));
    }
}

void OPL_SetFreq(int channel, int block, int fnum)
{
    if (channel < 0 || channel >= OPL_NUM_CHANNELS)
    {
        return;
    }
    {
        unsigned int bank = (channel >= OPL_CHANNELS_PER_BANK) ? 0x100u : 0u;
        unsigned int bank_channel =
            (unsigned int) (channel % OPL_CHANNELS_PER_BANK);
        OPL_WriteReg(bank + 0xA0 + bank_channel, (unsigned int) fnum & 0xFF);
        OPL_WriteReg(bank + 0xB0 + bank_channel,
                     (chans[channel].keyon ? 0x20u : 0u)
                     | (((unsigned int) block & 7) << 2)
                     | (((unsigned int) fnum >> 8) & 3));
    }
}

void OPL_NoteOff(int channel)
{
    if (channel < 0 || channel >= OPL_NUM_CHANNELS)
    {
        return;
    }
    {
        unsigned int bank = (channel >= OPL_CHANNELS_PER_BANK) ? 0x100u : 0u;
        unsigned int bank_channel =
            (unsigned int) (channel % OPL_CHANNELS_PER_BANK);
        OPL_WriteReg(bank + 0xB0 + bank_channel,
                     (((unsigned int) chans[channel].block & 7) << 2)
                     | ((chans[channel].fnum >> 8) & 3));
    }
}

static int ExpLookup(int total_att)
{
    int mantissa;

    if (total_att < 0)
    {
        total_att = 0;
    }
    if (total_att > 0x1FFF)
    {
        return 0;
    }
    mantissa = (opl_exp[(total_att & 0xFF) ^ 0xFF] | 0x400);
    return mantissa >> (total_att >> 8);
}

static int OperatorOutput(opl_op_t *op, unsigned int phase_mod)
{
    unsigned int phase_index;
    unsigned int quarter;
    unsigned int idx;
    int logsin;
    int total_att;
    int output;
    int negate;

    if (op->eg_state == EG_OFF)
    {
        return 0;
    }

    phase_index = (op->phase + phase_mod) >> PHASE_TO_INDEX;
    phase_index &= (SINE_ENTRIES - 1);

    quarter = (phase_index >> 8) & 3;
    idx = phase_index & 0xFF;
    negate = 0;

    switch (op->waveform)
    {
    case 0:
        if (quarter & 1) idx = 0xFF - idx;
        if (quarter & 2) negate = 1;
        break;
    case 1:
        if (quarter & 2) return 0;
        if (quarter & 1) idx = 0xFF - idx;
        break;
    case 2:
        if (quarter & 1) idx = 0xFF - idx;
        break;
    case 3:
        if (quarter & 1) return 0;
        break;
    }

    logsin = opl_logsin[idx];
    total_att = logsin + ((op->env + op->tl_att + op->ksl_att
                           + ((op->am_vib_eg_ksr_mult & 0x80) ? trem_att : 0))
                          << 3);

    output = ExpLookup(total_att);
    if (negate)
    {
        output = -output;
    }
    return output;
}

static void AdvanceEnvelope(opl_chan_t *chan, opl_op_t *op)
{
    int rate;
    int sl;

    switch (op->eg_state)
    {
    case EG_ATTACK:
        rate = EffectiveRate(chan, op, (op->ar_dr >> 4) & 0x0F);
        if (rate == 0)
        {
            break;
        }
        {
            unsigned long long attack_step =
                ((unsigned long long) (op->eg_counter + 65536u)
                 * (unsigned long long) eg_inc_per_sample[rate]) >> 19;
            if (attack_step == 0)
            {
                attack_step = 1;
            }
            if ((unsigned long long) op->eg_counter <= attack_step)
            {
                op->eg_counter = 0;
                op->eg_state = EG_DECAY;
            }
            else
            {
                op->eg_counter -= (unsigned int) attack_step;
            }
        }
        break;

    case EG_DECAY:
        rate = EffectiveRate(chan, op, op->ar_dr & 0x0F);
        sl = ((op->sl_rr >> 4) & 0x0F) * 16;
        if (rate != 0)
        {
            op->eg_counter += eg_inc_per_sample[rate];
        }
        if ((int) (op->eg_counter >> 16) >= sl)
        {
            op->eg_counter = (unsigned int) sl << 16;
            op->eg_state = EG_SUSTAIN;
        }
        break;

    case EG_SUSTAIN:
        if (!(op->am_vib_eg_ksr_mult & 0x20))
        {
            rate = EffectiveRate(chan, op, op->sl_rr & 0x0F);
            if (rate != 0)
            {
                op->eg_counter += eg_inc_per_sample[rate];
            }
        }
        break;

    case EG_RELEASE:
        rate = EffectiveRate(chan, op, op->sl_rr & 0x0F);
        if (rate != 0)
        {
            op->eg_counter += eg_inc_per_sample[rate];
        }
        break;

    default:
        return;
    }

    if ((int) (op->eg_counter >> 16) >= ENV_MAX)
    {
        op->eg_counter = (unsigned int) ENV_MAX << 16;
        if (op->eg_state == EG_RELEASE)
        {
            op->eg_state = EG_OFF;
        }
    }
    op->env = (int) (op->eg_counter >> 16);
}

void OPL_Generate(short *buf, int nsamples)
{
    int i, channel;

    for (i = 0; i < nsamples; ++i)
    {
        int mix = 0;

        trem_acc_q24 += trem_inc_q24;
        while (trem_acc_q24 >= (1u << 24))
        {
            trem_acc_q24 -= (1u << 24);
            trem_pos = (trem_pos + 1) % 210;
            trem_att = ((trem_pos < 105) ? trem_pos : 210 - trem_pos)
                       >> trem_depth_shift;
        }
        vib_acc_q24 += vib_inc_q24;
        while (vib_acc_q24 >= (1u << 24))
        {
            vib_acc_q24 -= (1u << 24);
            vib_pos = (vib_pos + 1) & 7;
            for (channel = 0; channel < OPL_NUM_CHANNELS; ++channel)
            {
                if (chans[channel].op[0].am_vib_eg_ksr_mult & 0x40)
                {
                    UpdatePhaseInc(&chans[channel], &chans[channel].op[0]);
                }
                if (chans[channel].op[1].am_vib_eg_ksr_mult & 0x40)
                {
                    UpdatePhaseInc(&chans[channel], &chans[channel].op[1]);
                }
            }
        }

        for (channel = 0; channel < OPL_NUM_CHANNELS; ++channel)
        {
            opl_chan_t *chan = &chans[channel];
            opl_op_t *modulator = &chan->op[0];
            opl_op_t *carrier = &chan->op[1];
            int feedback = (chan->fb_con >> 1) & 7;
            unsigned int phase_mod = 0;
            int modulator_out, carrier_out;

            if (modulator->eg_state == EG_OFF && carrier->eg_state == EG_OFF)
            {
                continue;
            }

            if (feedback != 0)
            {
                int feedback_avg = (modulator->out + modulator->prev_out) >> 1;
                phase_mod = ((unsigned int) feedback_avg) << (15 + feedback);
            }

            modulator_out = OperatorOutput(modulator, phase_mod);
            modulator->prev_out = modulator->out;
            modulator->out = modulator_out;

            if (chan->fb_con & 1)
            {
                carrier_out = OperatorOutput(carrier, 0);
                mix += modulator_out + carrier_out;
            }
            else
            {
                carrier_out =
                    OperatorOutput(carrier,
                                   ((unsigned int) modulator_out) << 23);
                mix += carrier_out;
            }
            carrier->out = carrier_out;

            modulator->phase += modulator->phase_inc;
            carrier->phase += carrier->phase_inc;

            AdvanceEnvelope(chan, modulator);
            AdvanceEnvelope(chan, carrier);
        }

        mix = (mix * OPL_OUTPUT_GAIN_Q2) >> 2;

        if (lp_alpha_q16 > 0)
        {
            if (mix >  32767) mix =  32767;
            if (mix < -32768) mix = -32768;
            lp_z1 += (int) ((((long long) mix << 16) - lp_z1)
                            * lp_alpha_q16 >> 16);
            lp_z2 += (int) (((long long) lp_z1 - lp_z2)
                            * lp_alpha_q16 >> 16);
            mix = lp_z2 >> 16;
        }

        if (mix > 32767)  mix = 32767;
        if (mix < -32768) mix = -32768;

        buf[i] = (short) (buf[i] + mix > 32767 ? 32767
                        : (buf[i] + mix < -32768 ? -32768 : buf[i] + mix));
    }
}

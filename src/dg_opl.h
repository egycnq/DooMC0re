#ifndef DG_OPL_H
#define DG_OPL_H

#define OPL_NUM_CHANNELS      18
#define OPL_NUM_OPERATORS     36
#define OPL_CHANNELS_PER_BANK 9

#define OPL_NATIVE_RATE       49716

#define OPL_LOWPASS_CUTOFF_HZ 0

#define OPL_OUTPUT_GAIN_Q2    11

void OPL_Init(int rate);

void OPL_SetLowpass(int cutoff_hz);

void OPL_WriteReg(unsigned int reg, unsigned int value);

void OPL_Generate(short *buf, int nsamples);

void OPL_NoteOn(int channel, int block, int fnum);

void OPL_SetFreq(int channel, int block, int fnum);
void OPL_NoteOff(int channel);

int  OPL_FreqToFnum(int freq_millihz, int *block_out);
int  OPL_FnumToFreqMilli(int block, int fnum);

#endif

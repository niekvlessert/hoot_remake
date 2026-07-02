/***************************************************************************

  sn76496.c

  Routines to emulate the Texas Instruments SN76489 / SN76496 programmable
  tone /noise generator. Also known as (or at least compatible with) TMS9919.

  Noise emulation is not accurate due to lack of documentation. The noise
  generator uses a shift register with a XOR-feedback network, but the exact
  layout is unknown. It can be set for either period or white noise; again,
  the details are unknown.

***************************************************************************/

// またまた MAME からパクリ :-)
// Tue Oct 24 23:46 JST 2000 (Fu-.)
#include "StdAfx.h"
#include "ssConfig.h"
#include "ssSoundDriverManager.h"
#include "ssSoundStream.h"
#include "sound/ssSN76496.h"


#define MAX_OUTPUT 0x7fff
#define STEP 0x10000

/* Formulas for noise generator */
/* bit0 = output */

/* noise feedback for white noise mode */
#define FB_WNOISE 0x12000	/* bit15.d(16bits) = bit0(out) ^ bit2 */
//#define FB_WNOISE 0x14000	/* bit15.d(16bits) = bit0(out) ^ bit1 */
//#define FB_WNOISE 0x28000	/* bit16.d(17bits) = bit0(out) ^ bit2 (same to AY-3-8910) */
//#define FB_WNOISE 0x50000	/* bit17.d(18bits) = bit0(out) ^ bit2 */

/* noise feedback for periodic noise mode */
/* it is correct maybe (it was in the Megadrive sound manual) */
//#define FB_PNOISE 0x10000	/* 16bit rorate */
#define FB_PNOISE 0x08000   /* JH 981127 - fixes Do Run Run */

/* noise generator start preset (for periodic noise) */
#define NG_PRESET 0x0f35


ssSN76496::ssSN76496()
{
}

ssSN76496::~ssSN76496()
{
}


void ssSN76496::Write(BYTE _data)
{
	const BYTE data = _data;

	if (data & 0x80)
	{
		int r = (data & 0x70) >> 4;
		int c = r/2;
		int reg;

		sn.LastRegister = r;
		reg = (sn.Register[r] & 0x3f0) | (data & 0x0f);
		sn.Register[r] = reg;
		switch (r)
		{
			case 0:	/* tone 0 : frequency */
			case 2:	/* tone 1 : frequency */
			case 4:	/* tone 2 : frequency */
			{
				int freq = sn.Register[r];

				sn.Period[c] = sn.UpdateStep * freq;
				if (sn.Period[c] == 0) sn.Period[c] = sn.UpdateStep;
				if (r == 4)
				{
					/* update noise shift frequency */
					if ((sn.Register[6] & 0x03) == 0x03)
						sn.Period[3] = 2 * sn.Period[2];
				}

				// set Freq.
				info[c].keycode = m_kctable[freq * 4];
				break;

			}
			case 1:	/* tone 0 : volume */
			case 3:	/* tone 1 : volume */
			case 5:	/* tone 2 : volume */
			case 7:	/* noise  : volume */
			{
				int vol = sn.VolTable[data & 0x0f];
				int newvol = m_voltbl[data & 0x0f];

				sn.Volume[c] = vol;

				// set volume
				info[c].volume = newvol;
				if (newvol) {
					info[c].keyon = 0xff;		// keyon
				} else {
					info[c].keyon = 0x00;		// keyoff
				}
				break;
			}
			case 6:	/* noise  : frequency, mode */
				{
					int n = sn.Register[6];
					sn.NoiseFB = (n & 4) ? FB_WNOISE : FB_PNOISE;
					n &= 3;
					/* N/512,N/1024,N/2048,Tone #3 output */
					sn.Period[3] = (n == 3) ? 2 * sn.Period[2] : (sn.UpdateStep << (5+n));

					/* reset noise shifter */
					sn.RNG = NG_PRESET;
					sn.Output[3] = sn.RNG & 1;
				}
				break;
		}
	}
	else
	{
		// $00-$7f
		int r = sn.LastRegister;
		int c = r/2;

		switch (r)
		{
			case 0:	/* tone 0 : frequency */
			case 2:	/* tone 1 : frequency */
			case 4:	/* tone 2 : frequency */
			{
				int freq = (sn.Register[r] & 0x0f) | ((data & 0x3f) << 4);

				info[c].keycode = m_kctable[freq * 4];
				sn.Register[r] = freq;
				sn.Period[c] = sn.UpdateStep * freq;
				if (sn.Period[c] == 0) sn.Period[c] = sn.UpdateStep;
				if (r == 4)
				{
					/* update noise shift frequency */
					if ((sn.Register[6] & 0x03) == 0x03)
						sn.Period[3] = 2 * sn.Period[2];
				}
				break;
			}
		}
	}
}

void ssSN76496::Update(SHORT **_buffer, DWORD _count)
{
	int i;
	int length = _count;

	int *mix = (int *)_buffer[0];

	memset(mix, 0, _count * sizeof(int) * 2);

	/* If the volume is 0, increase the counter */
	for (i = 0;i < 4;i++)
	{
		if (sn.Volume[i] == 0)
		{
			/* note that I do count += length, NOT count = length + 1. You might think */
			/* it's the same since the volume is 0, but doing the latter could cause */
			/* interferencies when the program is rapidly modulating the volume. */
			if (sn.Count[i] <= length*STEP) sn.Count[i] += length*STEP;
		}
	}

	while (length > 0)
	{
		int vol[4];
		unsigned int out;
		int left;


		/* vol[] keeps track of how long each square wave stays */
		/* in the 1 position during the sample period. */
		vol[0] = vol[1] = vol[2] = vol[3] = 0;

		for (i = 0;i < 3;i++)
		{
			if (sn.Output[i]) vol[i] += sn.Count[i];
			sn.Count[i] -= STEP;
			/* Period[i] is the half period of the square wave. Here, in each */
			/* loop I add Period[i] twice, so that at the end of the loop the */
			/* square wave is in the same status (0 or 1) it was at the start. */
			/* vol[i] is also incremented by Period[i], since the wave has been 1 */
			/* exactly half of the time, regardless of the initial position. */
			/* If we exit the loop in the middle, Output[i] has to be inverted */
			/* and vol[i] incremented only if the exit status of the square */
			/* wave is 1. */
			while (sn.Count[i] <= 0)
			{
				sn.Count[i] += sn.Period[i];
				if (sn.Count[i] > 0)
				{
					sn.Output[i] ^= 1;
					if (sn.Output[i]) vol[i] += sn.Period[i];
					break;
				}
				sn.Count[i] += sn.Period[i];
				vol[i] += sn.Period[i];
			}
			if (sn.Output[i]) vol[i] -= sn.Count[i];
		}

		left = STEP;
		do
		{
			int nextevent;

			if (sn.Count[3] < left) nextevent = sn.Count[3];
			else nextevent = left;

			if (sn.Output[3]) vol[3] += sn.Count[3];
			sn.Count[3] -= nextevent;
			if (sn.Count[3] <= 0)
			{
				if (sn.RNG & 1) sn.RNG ^= sn.NoiseFB;
				sn.RNG >>= 1;
				sn.Output[3] = sn.RNG & 1;
				sn.Count[3] += sn.Period[3];
				if (sn.Output[3]) vol[3] += sn.Period[3];
			}
			if (sn.Output[3]) vol[3] -= sn.Count[3];

			left -= nextevent;
		} while (left > 0);

		//out = vol[0] * sn.Volume[0] + vol[1] * sn.Volume[1] +
		//		vol[2] * sn.Volume[2] + vol[3] * sn.Volume[3];
		out = 0;
		const DWORD mask = m_mask;
		if (!(mask & 1))
			out += vol[0] * sn.Volume[0];
		if (!(mask & 2))
			out += vol[1] * sn.Volume[1];
		if (!(mask & 4))
			out += vol[2] * sn.Volume[2];
		if (!(mask & 8))
			out += vol[3] * sn.Volume[3];

		if (out > MAX_OUTPUT * STEP) out = MAX_OUTPUT * STEP;
		*(mix++) += out / STEP;
		*(mix++) += out / STEP;
		length--;
	}
}


int ssSN76496::GetBufferCount(void) const
{
	return 1;
}

WORD ssSN76496::GetBufferFlag(int _b) const
{
//	return ssSoundStream::F_MONO;
	return ssSoundStream::F_STEREO_LONG;		// F_MONO だとブチブチゆーのでこっち
}

int ssSN76496::GetTrackCount(void) const
{
	return 4;
}

ssTrackInfo *ssSN76496::GetInfo(int _track)
{
	return &info[_track];

	return NULL;
}

int ssSN76496::GetRegs(BYTE *_buffer, int _count, int _offset)
{
	const int size = sizeof(sn.Register);
	int count;

	if (_offset >= size) {
		return 0;
	}

	if (_offset + _count >= size) {
		count = size - _offset;
	} else {
		count = _count;
	}

	memcpy(_buffer, sn.Register, count);
	return _count;
}


void ssSN76496::set_clock(int clock)
{
	/* the base clock for the tone generators is the chip clock divided by 16; */
	/* for the noise generator, it is clock / 256. */
	/* Here we calculate the number of steps which happen during one sample */
	/* at the given sample rate. No. of events = sample rate / (clock/16). */
	/* STEP is a multiplier used to turn the fraction into a fixed point */
	/* number. */
	sn.UpdateStep = ((double)STEP * sn.SampleRate * 16) / clock;
}


void ssSN76496::set_gain(int gain)
{
	int i;
	double out;


	gain &= 0xff;

	/* increase max output basing on gain (0.2 dB per step) */
	out = MAX_OUTPUT / 3;
	while (gain-- > 0)
		out *= 1.023292992;	/* = (10 ^ (0.2/20)) */

	/* build volume table (2dB per step) */
	for (i = 0;i < 15;i++)
	{
		/* limit volume to avoid clipping */
		if (out > MAX_OUTPUT / 3) sn.VolTable[i] = MAX_OUTPUT / 3;
		else sn.VolTable[i] = out;

		out /= 1.258925412;	/* = 10 ^ (2/20) = 2dB */
	}
	sn.VolTable[15] = 0;
}

//
// 初期化
//
void ssSN76496::init_sub(int clock, int sample_rate)
{
	int i;

	sn.SampleRate = sample_rate;
	set_clock(clock);

	for (i = 0;i < 4;i++) {
		sn.Volume[i] = 0;
	}

	sn.LastRegister = 0;
	for (i = 0;i < 8;i+=2)
	{
		sn.Register[i] = 0;
		sn.Register[i + 1] = 0x0f;	/* volume = 0 */
	}

	for (i = 0;i < 4;i++)
	{
		sn.Output[i] = 0;
		sn.Period[i] = sn.Count[i] = sn.UpdateStep;
	}
	sn.RNG = NG_PRESET;
	sn.Output[3] = sn.RNG & 1;
}


bool ssSN76496::Initialize(int _clock)
{
	ssSoundDriverManager *manager = ssSoundDriverManager::Instance();
	const ssConfig &config = manager->GetConfig();
	sample_rate = config.sampling_rate;
//	int *clktbl = (int *)clocktbl[0];
	const int clk = _clock;
	int i;

	init_sub(clk, sample_rate);
	set_gain(0);

	for (i = 0; i < 3; i++) {
		InitTrackInfo(&info[i]);
		sprintf(info[i].name, "SN76496 #%d", i);
	}
	InitTrackInfo(&info[3]);
	sprintf(info[3].name, "SN76496 NOISE", i);

	// KC テーブル作成
	m_kctable[0] = 127;
	for (i = 1; i < 0x1000; i++) {
		m_kctable[i] = Hz2Kc((double)clk / ((double)i * 8.0));
	}

	// ボリュームテーブル作成
	for (i = 0; i < 16; i++ ) {
		m_voltbl[15-i] = i * 8;
	}

	SetMask(0);

	return true;
}

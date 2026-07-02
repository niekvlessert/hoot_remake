#include "StdAfx.h"
#include "ssConfig.h"
#include "ssSoundDriverManager.h"
#include "ssSoundStream.h"
#include "sound/ssADPCM.h"

#define INCR_SHIFT 12
#define NO_INTERPOLATE

int ssADPCM::index_shift[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
//int ssADPCM::diff_lookup[49*16];
//unsigned int ssADPCM::volume_table[16];

BYTE ssADPCM::release_data[] = {
	0x80,0x80,0x80,0x80,0x80,
	0x80,0x80,0x80,0x80,0x80,
	0x80,0x80,0x80,0x80,0x80,
	0x80,0x80,0x80,0x80,0x80,

	0x80,0x80,0x80,0x80,0x80,
	0x80,0x80,0x80,0x80,0x80,
};


ssADPCM::ssADPCM(int _num, VolMode _vm)
{
	compute_tables(_vm);

	m_numvoice = _num;
	m_voice = new ADPCM[m_numvoice];

	m_bitorder = 4;
	m_noise_reduction = false;

	for (int ch = 0; ch < m_numvoice; ch++) {
		m_voice[ch].playing = false;
		m_voice[ch].release = false;

		m_voice[ch].data = NULL;
		m_voice[ch].sample = 0;
		m_voice[ch].signal = 0;
		m_voice[ch].step = 0;
		m_voice[ch].length = 0;
		m_voice[ch].volume = 0;
		m_voice[ch].count = 0;
		m_voice[ch].incr = 0;

		m_voice[ch].voll = 1;
		m_voice[ch].volr = 1;

		m_voice[ch].mode = MODE_ADPCM;
	}
}


ssADPCM::~ssADPCM()
{
	delete[] m_voice;
}


void ssADPCM::Initialize(void)
{
	ssSoundDriverManager *manager = ssSoundDriverManager::Instance();
	const ssConfig &config = manager->GetConfig();

	m_rate = config.sampling_rate;

	SetMask(0);
}


void ssADPCM::compute_tables(VolMode _vm)
{
	/* nibble to bit map */
	static int nbl2bit[16][4] =
	{
		{ 1, 0, 0, 0}, { 1, 0, 0, 1}, { 1, 0, 1, 0}, { 1, 0, 1, 1},
		{ 1, 1, 0, 0}, { 1, 1, 0, 1}, { 1, 1, 1, 0}, { 1, 1, 1, 1},
		{-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 1, 0}, {-1, 0, 1, 1},
		{-1, 1, 0, 0}, {-1, 1, 0, 1}, {-1, 1, 1, 0}, {-1, 1, 1, 1}
	};

	int step, nib;

	/* loop over all possible steps */
	for (step = 0; step <= 48; step++)
	{
		/* compute the step value */
		int stepval = floor(16.0 * pow(11.0 / 10.0, (double)step));

		/* loop over all nibbles and compute the difference */
		for (nib = 0; nib < 16; nib++)
		{
			diff_lookup[step*16 + nib] = nbl2bit[nib][0] *
				(stepval   * nbl2bit[nib][1] +
				 stepval/2 * nbl2bit[nib][2] +
				 stepval/4 * nbl2bit[nib][3] +
				 stepval/8);
		}
	}

	if (_vm == MODE_X68K_PCM8) {
		double out;

		out = 256.0;
		for (step = 9; step < 16; step++) {
			/* 2dB per step */
			out *= pow(10., 2./20.);
			volume_table[step] = (UINT32)out;
		}
		out = 256.0;
		for (step = 8; step >= 0; step--) {
			volume_table[step] = (UINT32)out;
			/* 2dB per step */
			out /= pow(10., 2./20.);
		}
	} else {
		/* generate the OKI6295 volume table */
		for (step = 0; step < 16; step++)
		{
			double out = 256.0;
			int vol = step;

			/* 3dB per step */
			while (vol-- > 0)
				out /= 1.412537545;	/* = 10 ^ (3/20) = 3dB */
			volume_table[step] = (UINT32)out;
		}
	}
}


void ssADPCM::Fetch(int _ch)
{
	ADPCM *voice = &m_voice[_ch];
	int val;
	int sample = voice->sample;
	int signal = voice->signal;
	int step = voice->step;

	voice->prev_signal = signal;

	if (sample/2 >= voice->length) {
		if (voice->playing) {
			voice->playing = false;
			if (m_noise_reduction) {
				voice->release = true;
			} else {
				voice->step = 0;
				voice->signal = 0;
			}

			voice->data = release_data;
			voice->sample = 0;
			voice->length = 26;
		} else if (voice->release) {
			voice->release = false;
			voice->step = 0;
			voice->signal = 0;
		}
		return;
	}

	//val = voice->data[sample/2] >> (((sample&1)<<2)^4);
	val = voice->data[sample/2] >> (((sample&1)<<2) ^ m_bitorder);
	sample++;

	signal += diff_lookup[voice->step*16 + (val&15)];
	if (signal > 2047) {
		signal = 2047;
	} else if (signal < -2048) {
		signal = -2048;
	}

	step += index_shift[val & 7];
	if (step > 48) {
		step = 48;
	} else if (step < 0) {
		step = 0;
	}

	voice->sample = sample;
	voice->signal = signal;
	voice->step = step;
}


void ssADPCM::Update(SHORT **_buffer, DWORD _count)
{
	int *buff = (int *)(_buffer[0]);
	memset(buff, 0, _count * sizeof(int) * 2);

	for (int ch = 0; ch < m_numvoice; ch++) {
		ADPCM *voice = &m_voice[ch];

		const int incr = voice->incr;
		const int volume = voice->volume;
		const int voll = voice->voll;
		const int volr = voice->volr;
		const bool mute = (m_mask & (1<<ch)) != 0;

		int *p = buff;

		if (voice->mode == MODE_ADPCM) {
			int prev_signal = voice->prev_signal;
			int signal = voice->signal;
			int count = voice->count;

			for (int i = 0; i < _count; i++) {
				if (voice->playing || voice->release) {
					while (count >= (1<<INCR_SHIFT)) {
						Fetch(ch);
						prev_signal = voice->prev_signal;
						signal = voice->signal;

						count -= (1<<INCR_SHIFT);
					}

#ifdef NO_INTERPOLATE
					int dat = prev_signal * volume / 16;
#else
					int dat =
						((prev_signal * count) +
						 (signal * ((1<<INCR_SHIFT) - count))) * volume / (16<<INCR_SHIFT);
#endif

					if (!mute) {
						*p++ += dat * voll;
						*p++ += dat * volr;
					}

					count += incr;
				}
			}
			voice->count = count;
		} else if (voice->mode == MODE_8BITPCM) {
			BYTE *data = voice->data;
			int length = voice->length;

			for (int i = 0; i < _count; i++) {
				if (voice->playing) {
					int sample = voice->sample;

					int pos = (sample >> INCR_SHIFT) * 2;
					int dat = (short)((data[pos]<<8) + data[pos+1]);

					if (!mute) {
						*p++ += dat * voll;
						*p++ += dat * volr;
					}

					sample += incr;
					if ((sample >> INCR_SHIFT) >= length) {
						voice->playing = false;
					}
				}
			}
		} else if (voice->mode == MODE_16BITPCM) {
			BYTE *data = voice->data;
			int length = voice->length;

			for (int i = 0; i < _count; i++) {
				if (voice->playing) {
					int sample = voice->sample;

					int pos = (sample >> INCR_SHIFT);
					int dat = (short)(data[pos]<<8);

					if (!mute) {
						*p++ += dat * voll;
						*p++ += dat * volr;
					}

					sample += incr;
					if ((sample >> INCR_SHIFT) >= length) {
						voice->playing = false;
					}
				}
			}
		}
	}
}


int ssADPCM::GetBufferCount(void) const
{
	return 1;
}


WORD ssADPCM::GetBufferFlag(int _b) const
{
	return ssSoundStream::F_STEREO_LONG;
}


void ssADPCM::SetMask(DWORD _mask)
{
	ssSoundChip::SetMask(_mask);
}


DWORD ssADPCM::GetMask(void)
{
	return ssSoundChip::GetMask();
}


void ssADPCM::Play(int _ch, void *_adr, int _length)
{
	m_voice[_ch].playing = true;
	m_voice[_ch].release = false;
	m_voice[_ch].data = (BYTE *)_adr;
	m_voice[_ch].length = _length;

	m_voice[_ch].sample = 0;
	m_voice[_ch].step = 0;

	m_voice[_ch].count = 0;

	m_voice[_ch].signal = 0;
	m_voice[_ch].prev_signal = 0;

	if (m_voice[_ch].mode == MODE_ADPCM) {
		Fetch(_ch);
	}
}


void ssADPCM::Stop(int _ch)
{
	m_voice[_ch].playing = false;
	m_voice[_ch].sample = 0;

	if (m_noise_reduction) {
		m_voice[_ch].release = true;
		m_voice[_ch].data = release_data;
		m_voice[_ch].length = 26;
	} else {
		m_voice[_ch].release = false;
		m_voice[_ch].step = 0;
		m_voice[_ch].signal = 0;
	}
}


void ssADPCM::SetVol(int _ch, int _vol)
{
	m_voice[_ch].volume = volume_table[_vol];
}


void ssADPCM::SetFreq(int _ch, int _freq)
{
	m_voice[_ch].incr = (_freq << INCR_SHIFT) / m_rate;
}


void ssADPCM::SetPan(int _ch, int _pan)
{
	m_voice[_ch].voll = (_pan & 1) ? 1 : 0;
	m_voice[_ch].volr = (_pan & 2) ? 1 : 0;
}


void ssADPCM::SetMode(int _ch, Mode _mode)
{
	m_voice[_ch].mode = _mode;
}


void ssADPCM::SetLSBFirst(bool _b)
{
	if (_b) {
		m_bitorder = 0;
	} else {
		m_bitorder = 4;
	}
}


void ssADPCM::SetNoiseReduction(bool _b)
{
	m_noise_reduction = _b;
}


bool ssADPCM::IsPlaying(int _ch) const
{
	return m_voice[_ch].playing;
}

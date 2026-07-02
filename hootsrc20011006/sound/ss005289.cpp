#include "StdAfx.h"
#include "sound/ss005289.h"
#include "ssConfig.h"
#include "ssSoundDriverManager.h"
#include "ssSoundStream.h"

#define FREQBASEBITS	16

ss005289::ss005289()
{
}

ss005289::~ss005289()
{
}

bool ss005289::Initialize(int _baseclock, BYTE *_wave0, BYTE *_wave1)
{
	int i;

	ssSoundDriverManager *manager = ssSoundDriverManager::Instance();
	const ssConfig &config = manager->GetConfig();
	int sampling_rate = config.sampling_rate;

	m_sample_rate = sampling_rate;
	m_baseclock = _baseclock;
	m_wave[0] = _wave0;
	m_wave[1] = _wave1;

	for (i = 0; i < 2; i++) {
		m_ch[i].frequency = 0;
		m_ch[i].counter = 0;
		m_ch[i].volume = 0;
		m_ch[i].wave = m_wave[i];

		InitTrackInfo(&info[i]);

		sprintf(info[i].name, "005289 #%d", i);
	}

	m_kctable[0] = 127;
	for (i = 1; i < 0x1000; i++) {
		m_kctable[i] = Hz2Kc((double)m_baseclock / ((double)i * 8.0));
	}

	SetMask(0);

	return true;
}

void ss005289::WriteCtrl(int _ch, BYTE _data)
{
	m_ch[_ch].volume = _data & 15;
	m_ch[_ch].wave = &m_wave[_ch][32 * (_data >> 5)];

	if (m_ch[_ch].volume) {
		info[_ch].volume = m_ch[_ch].volume * 8;
		info[_ch].keyon = 0xff;
	} else {
		info[_ch].volume = 0;
		info[_ch].keyon = 0;
	}
}

void ss005289::WritePitch(int _ch, WORD _data)
{
	m_ch[_ch].latch = _data;
}

void ss005289::WriteLatch(int _ch, BYTE _data)
{
	m_ch[_ch].frequency = m_ch[_ch].latch;
	info[_ch].keycode = m_kctable[m_ch[_ch].frequency];
}


int ss005289::GetBufferCount(void) const
{
	return 1;
}

WORD ss005289::GetBufferFlag(int _b) const
{
	return ssSoundStream::F_STEREO_LONG;
}

int ss005289::GetTrackCount(void) const
{
	return 2;
}

ssTrackInfo *ss005289::GetInfo(int _track)
{
	if (_track < 2) {
		return &info[_track];
	}

	return NULL;
}


int ss005289::GetRegs(BYTE *_buffer, int _count, int _offset)
{
	return 0;
}


void ss005289::Update(SHORT **_buffer, DWORD _count)
{
	int *mixer_buffer = (int *)_buffer[0];
	DWORD length = _count;

	k005289_sound_channel *voice = m_ch;
	int *mix;
	int i,v,f;

	memset(_buffer[0], 0, sizeof(int) * 2 * _count);

	for (int ch = 0; ch < 2; ch++) {
		v=voice[ch].volume;
		f=voice[ch].frequency;
		if (v && f)
		{
			const bool mute = (m_mask & (1<<ch)) != 0;
			const unsigned char *w = voice[ch].wave;
			int c = voice[ch].counter;

			int incr = (int)((((float)m_baseclock / (float)(f * 16))*(float)(1<<FREQBASEBITS))
						  / (float)(m_sample_rate / 32));
			mix = mixer_buffer;

			/* add our contribution */
			for (i = 0; i < length; i++)
			{
				int offs;

				c += incr;
				if (!mute) {
					offs = (c >> 16) & 0x1f;
					int d = ((w[offs] & 0x0f) - 8) * v * 16;
					*mix++ += d;
					*mix++ += d;
				}
			}

			/* update the counter for this voice */
			voice[ch].counter = c;
		}
	}
}

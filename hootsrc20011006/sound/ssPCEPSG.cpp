#include "StdAfx.h"
#include "sound/ssPCEPSG.h"
#include "ssConfig.h"
#include "ssSoundDriverManager.h"
#include "ssSoundStream.h"

ssPCEPSG::ssPCEPSG()
{
	memset(m_reg0, 0, sizeof(m_reg0));
	memset(m_reg, 0, sizeof(m_reg));
}

ssPCEPSG::~ssPCEPSG()
{
}

bool ssPCEPSG::Initialize(int _clock)
{
	int i;
	ssSoundDriverManager *manager = ssSoundDriverManager::Instance();
	const ssConfig &config = manager->GetConfig();
	int sampling_rate = config.sampling_rate;

	m_sampling_rate = sampling_rate;
	m_freqbase = (double)_clock * (double)(1<<INCR_SHIFT) / (double)m_sampling_rate;

	m_voll = 0;
	m_volr = 0;

	for (i = 0; i < MAX_CH; i++) {
		m_ch[i].freq = 0;
		m_ch[i].vol = 0;
		m_ch[i].voll = 0;
		m_ch[i].volr = 0;

		memset(m_ch[i].wave, 0x10, sizeof(m_ch[0].wave));
		m_ch[i].wave_ptr = 0;

		m_ch[i].mode = 0;
		m_ch[i].offset = 0;

		m_ch[i].noise = 0;
		m_ch[i].noise_freq = 0;
		m_ch[i].noise_state = 0;
		m_ch[i].noise_counter = 0;
		m_ch[i].noise_seed = 1;

		InitTrackInfo(&info[i]);

		sprintf(info[i].name, "PSG(HuC6280) #%d", i);
	}

	m_kctable[0] = 127;
	for (i = 1; i < 0x1000; i++) {
		m_kctable[i] = Hz2Kc((double)_clock / ((double)i * 16.0));
	}

	for (i = 0; i < 32/2; i++) {
		m_lfofreqtbl[ 0 + i] = 0;
		m_lfofreqtbl[ 0 + 31-i] = -0;
		m_lfofreqtbl[32 + i] = i;
		m_lfofreqtbl[32 + 31-i] = -i;
		m_lfofreqtbl[64 + i] = i * 16;
		m_lfofreqtbl[64 + 31-i] = -16;
		m_lfofreqtbl[96 + i] = i * 256;
		m_lfofreqtbl[96 + 31-i] = -256;
	}

	double out = (double)0x8000;
	for (i = 0; i < 32 - 1; i++) {
		m_voltbl[i] = out + 0.5;
		out /= pow(10., 1.5 / 20.);
	}
	m_voltbl[31] = 0;

	SetMask(0);

	return true;
}

void ssPCEPSG::Write(int _adr, BYTE _data)
{
	static const BYTE pantbl[31] = {
		1,1,1,1,1,1,1,1,1,2,2,3,3,4,
		4,
		4,5,5,6,6,7,7,7,7,7,7,7,7,7
	};
	const int adr = _adr & 0xff;

	if (adr > 9) return;

	m_reg0[adr] = _data;

	if (adr == 0 || adr == 1 || adr == 8 || adr == 9) {
		// Voice independent
		m_reg[adr] = _data;

		switch (adr) {
		case 0:
			// Voice Select
			m_ch_sel = _data & 0x07;
			break;
		case 1:
			// Main Volume
			m_voll = 30 - (_data >> 4) * 2;
			m_volr = 30 - (_data & 15) * 2;
			break;
		case 8:
			// LFO Frequency
			m_lfofreq = _data;
			break;
		case 9:
			// LFO Control
			m_lfoctl = _data;
			m_lfooffset = 0;
			break;
		}
		return;
	}

	// Voice dependent
	const int channel = m_ch_sel;
	m_reg[channel * 16 + adr] = _data;
	struct channel *ch = &m_ch[channel];

	switch (adr) {
	case 2:
		// Frequency (low)
	case 3:
		// Frequency (high)
		{
			ch->freq = ((m_reg[channel*16+3]&0x0f)<<8) + (m_reg[channel*16+2]);
			info[channel].keycode = m_kctable[ch->freq];
		}
		break;
	case 4:
		// Channel on/dda/volume
		ch->mode = _data & 0xc0;
		ch->vol = 31 - _data & 0x1f;
		if ((ch->mode & 0x80) == 0) {
			ch->wave_ptr = 0;
			ch->offset = 0;
		}
		break;
	case 5:
		// Pan volume
		{
			int l, r;
			l = ch->voll = 30 - (_data >> 4) * 2;
			r = ch->volr = 30 - (_data & 0x0f) * 2;

			int p = ((l - r) + 31) / 2;
			info[channel].pan = pantbl[p];
#if 0
			info[channel].volume = l > r ? l<<2 : r<<2;
			if (l == 0 && r == 0) {
				info[channel].keyon &= 0x80;
			} else {
				info[channel].keyon = 0xff;
			}
#endif
		}
		break;
	case 6:
		// Wave data
		ch->wave[ch->wave_ptr] = (char)(_data & 0x1f) - 0x10;
		m_reg[0x10*MAX_CH + 0x20*channel + ch->wave_ptr] = _data;
		if ((ch->mode & 0x40) == 0) {
			ch->wave_ptr++;
			ch->wave_ptr &= 0x1f;
		}
		break;
	case 7:
		// Noise
		if (channel >= 4) {
			// channel 4,5 only
			ch->noise = _data & 0x80;
			ch->noise_freq = _data & 0x1f;
		}
		break;
	}
}

BYTE ssPCEPSG::Read(int _adr)
{
	if (_adr <= 9) {
		return m_reg0[_adr];
	}
	return 0xff;
}

int ssPCEPSG::GetBufferCount(void) const
{
	return 1;
}

WORD ssPCEPSG::GetBufferFlag(int _b) const
{
	return ssSoundStream::F_STEREO;
}

int ssPCEPSG::GetTrackCount(void) const
{
	return MAX_CH;
}

ssTrackInfo *ssPCEPSG::GetInfo(int _track)
{
	if (_track < MAX_CH) {
		return &info[_track];
	}

	return NULL;
}


int ssPCEPSG::GetRegs(BYTE *_buffer, int _count, int _offset)
{
	const int size = sizeof(m_reg);
	int count;

	if (_offset >= size) {
		return 0;
	}

	if (_offset + _count >= size) {
		count = size - _offset;
	} else {
		count = _count;
	}

	memcpy(_buffer, m_reg + _offset, count);

	return count;
}


void ssPCEPSG::Update(SHORT **_buffer, DWORD _count)
{
	memset(_buffer[0], 0, sizeof(short) * 2 * _count);

	for (int i = 0; i < MAX_CH; i++) {
		struct channel *ch = &m_ch[i];
		const bool mute = (m_mask & (1<<i)) != 0;

		if ((ch->mode & 0x80) == 0) {
			info[i].volume = 0;
			info[i].keyon &= 0x80;
			continue;
		}

		if (i == 1 && (m_lfoctl & 0x80)) {
			info[i].volume = 0;
			info[i].keyon &= 0x80;
			continue;
		}

		int vl = m_voll + ch->vol + ch->voll;
		int vr = m_volr + ch->vol + ch->volr;
		if (vl > 31) vl = 31;
		if (vr > 31) vr = 31;

		if (vl == 31 && vr == 31) {
			info[i].volume = 0;
			info[i].keyon = 0x80;
		} else {
			info[i].volume = 31*4 - (vl < vr ? vl * 4 : vr * 4);
			info[i].keyon = 0xff;
		}

		vl = m_voltbl[vl];
		vr = m_voltbl[vr];
		short *buffer = _buffer[0];

		if (ch->noise == 0) {
			// tone
			if ((ch->mode & 0x40) == 0) {
				// normal
				int incr;
				if (ch->freq) {
					incr = m_freqbase / ch->freq;
				} else {
					incr = 0;
				}
				int offset = ch->offset;

				if (i != 0 || (m_lfoctl & 0x80) == 0 || (m_lfoctl & 0x03) == 0) {
					for (DWORD count = _count; count != 0; count--) {
						const int d = ch->wave[(offset>>INCR_SHIFT) & 0x1f];

						if (!mute) {
							*buffer++ += d * vl / 64;
							*buffer++ += d * vr / 64;
						}

						offset += incr;
					}
				} else {
					// LFO
					int lfooffset = m_lfooffset;
					int *lfofreqtbl = &m_lfofreqtbl[32 * (m_lfoctl & 3)];
					int lfoincr;
					if (m_ch[1].freq && m_lfofreq) {
						lfoincr = m_freqbase / (m_ch[1].freq * m_lfofreq);
					} else {
						lfoincr = 0;
					}
					const int chfreq = ch->freq;

					for (DWORD count = _count; count != 0; count--) {
						const int d = ch->wave[(offset>>INCR_SHIFT) & 0x1f];

						if (!mute) {
							*buffer++ += d * vl / 64;
							*buffer++ += d * vr / 64;
						}

						const int lfofreq = lfofreqtbl[m_ch[1].wave[(lfooffset>>INCR_SHIFT)&0x1f]];
						const int freq = chfreq + lfofreq;
						if (freq) {
							incr = m_freqbase / freq;
						} else {
							incr = 0;
						}
						offset += incr;
						lfooffset += lfoincr;
					}

					m_lfooffset = lfooffset;
				}

				ch->offset = offset;
			} else {
				// dda
				const int d = ch->wave[0];
				const int dl = d * vl / 64;
				const int dr = d * vr / 64;

				for (DWORD count = _count; count != 0; count--) {
					if (!mute) {
						*buffer++ += dl;
						*buffer++ += dr;
					}
				}
			}
		} else {
			// noise
			int delta;
			if (ch->noise_freq) {
				delta = m_freqbase / (ch->noise_freq * 16);
			} else {
				delta = 0;
			}
			int c = ch->noise_counter;

			for (DWORD count = _count; count != 0; count--) {
				int d;
				int cnt;

				if (ch->noise_state) {
					d = 0x0f;
				} else {
					d = -0x0f;
				}

				if (!mute) {
					*buffer++ += d * vl / 64;
					*buffer++ += d * vr / 64;
				}

				c += delta;
				cnt = c >> INCR_SHIFT;
				c &= (1<<INCR_SHIFT) - 1;
				for (; cnt > 0; cnt--) {
					if ((ch->noise_seed + 1) & 2) ch->noise_state ^= 1;
					if (ch->noise_seed & 1) ch->noise_seed ^= 0x28000;
					ch->noise_seed >>= 1;
				}
			}
			ch->noise_counter = c;
		}
	}
}


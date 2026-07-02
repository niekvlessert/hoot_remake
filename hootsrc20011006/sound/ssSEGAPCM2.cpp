#include "StdAfx.h"
#include "sound/ssSEGAPCM2.h"
#include "ssConfig.h"
#include "ssSoundDriverManager.h"
#include "ssSoundStream.h"

// 315-5560

#define SIZE_ADD 0x00

const int ssSEGAPCM2::m_chtbl[32] = {
	 0, 1, 2, 3, 4, 5, 6, -1,
	 7, 8, 9,10,11,12,13, -1,
	14,15,16,17,18,19,20, -1,
	21,22,23,24,25,26,27, -1,
};

const int ssSEGAPCM2::pantbl_stereo[16] = {
	0,
	1,1,2,2,2,3,3,
	4,
	5,5,6,6,6,7,7,
};

const int ssSEGAPCM2::pantbl_mono[16] = {
	0,
	4,4,4,4,4,4,4,
	4,
	4,4,4,4,4,4,4,
};

ssSEGAPCM2::ssSEGAPCM2(int _type)
{
	m_type = _type;

	m_ch = 0;
	m_reg = 0;
	m_bankL = 0;
	m_bankR = 0;

	memset(m_regs, 0, sizeof(m_regs));
}

bool ssSEGAPCM2::Initialize(int _rate, BYTE *_pcmdata, int _banksize)
{
	int i;
	ssSoundDriverManager *manager = ssSoundDriverManager::Instance();
	const ssConfig &config = manager->GetConfig();
	const int sampling_rate = config.sampling_rate;

	m_pcmdata = _pcmdata;
	m_banksize = _banksize;
	m_bankL = 0x100000 / m_banksize;
	m_bankR = 0x100000 / m_banksize;

	for (i = 0; i < MAX_PCM; i++) {
		InitTrackInfo(&info[i]);
		sprintf(info[i].name, "SEGA 315-5560 #%d", i);
	}

	for (i = 0; i < MAX_PCM; i++) {
		m_voi[i].play = 0;
		m_voi[i].start = 0;
		m_voi[i].size = 0;
		m_voi[i].incr = 0;
		m_voi[i].pos = 0;
		m_voi[i].volume = 0;
		m_voi[i].pan = 0;
		m_voi[i].loop = 0;

		m_voi[i].attackrate = 0;
		m_voi[i].attacklevel = 0;
		m_voi[i].decayrate = 0;
		m_voi[i].sustainlevel = 0;
		m_voi[i].releaserate = 0;

		m_voi[i].envphase = ENV_NONE;
		m_voi[i].envincr = 0;
		m_voi[i].envlevel = 0;
		m_voi[i].envgoal = 0;
	}

	for (i = 0; i < 0x1000; i++) {
		m_pitchtbl[i] = (double)_rate / (double)sampling_rate
			* (1. + (double)i / 4096.) * (double)(1<<INCR_SHIFT);
	}

	double out = 256.0;
	for (i = 0; i < 127; i++) {
		m_voltbl[i] = out;
		out /= pow(10., 0.4 / 20.);
	}
	m_voltbl[127] = 0;

	switch (m_type) {
	case TYPE_STEREO:
		for (i = 0; i < 16; i++) {
			m_pantbl[i] = 128. * sqrt((double)i) / sqrt(8.);
			m_infopantbl[i] = pantbl_stereo[i];
		}
		break;
	case TYPE_MONO:
		for (i = 0; i < 16; i++) {
			m_pantbl[i] = 128;
			m_infopantbl[i] = pantbl_mono[i];
		}
		break;
	}

	double ar = 0.0005;
	for (i = 0; i < 16; i++) {
		m_ar_table[i] = 1. / (ar * sampling_rate);
		ar *= 2.;
	}

	double dr = 0.0015;
	for (i = 0; i < 15; i++) {
		m_dr_table[15 - i] = 1. / (dr * sampling_rate);
		dr *= 2.;
	}
	m_dr_table[0] = 0.;

	double rr = 0.0007;
	for (i = 0; i < 16; i++) {
		m_rr_table[15-i] = 1. / (rr * sampling_rate);
		rr *= 2.;
	}

	SetMask(0);

	return true;
}

ssSEGAPCM2::~ssSEGAPCM2()
{
}

void ssSEGAPCM2::SetChannel(BYTE _data)
{
	int ch = m_chtbl[_data & 0x1f];
	if (ch != -1) {
		m_ch = ch;
	}
}

void ssSEGAPCM2::SetReg(BYTE _data)
{
	m_reg = _data;
}

void ssSEGAPCM2::SetData(BYTE _data)
{
	const int b = m_ch * 16;
	m_regs[b + m_reg] = _data;

	switch (m_reg) {
	case 0x00:
		m_voi[m_ch].pan = (char)_data / 16;
		info[m_ch].pan = m_infopantbl[m_voi[m_ch].pan + 8];
		break;
	case 0x01:
		break;
	case 0x02:
	case 0x03:
		{
			int pitch = ((m_regs[b + 3]<<8) + m_regs[b + 2]) & 0xfff;
			int oct = (((char)m_regs[b + 3]) >> 4) - 1;
			info[m_ch].keycode = oct * 12 + (ssTrackInfo::OCT4_A + 0.5 + 12 * (log(1. + pitch/4096.) / log(2.)));
			if (oct >= 0) {
				pitch = m_pitchtbl[pitch] * (1 << oct);
			} else {
				pitch = m_pitchtbl[pitch] / (1 << -oct);
			}
			m_voi[m_ch].incr = pitch;
		}
		break;
	case 0x04:
		if (_data & 0x80) {
			// key on
			voice *const voi = &m_voi[m_ch];
			voi->play = 1;
			voi->pos = 0;
			info[m_ch].keyon = 0xff;

			// START ADDRESS
			//BYTE *hd = &m_pcmdata[_data * 12];
			BYTE *hd = &m_pcmdata[m_regs[b+1] * 12];
			int start = (hd[0]<<16) + (hd[1]<<8) + hd[2];
			int size = 0xffff - ((hd[5]<<8) + hd[6]) + SIZE_ADD;
			int loop = ((hd[3]<<8) + hd[4]) + SIZE_ADD;
			if (start >= 0x100000) {
				switch (m_type) {
				case TYPE_STEREO:
					start = (start & 0xfffff) + m_banksize * m_bankL;
					loop = (loop & 0xfffff) + m_banksize * m_bankL;
					break;
				case TYPE_MONO:
					if (m_voi[m_ch].pan < 0) {
						start = (start & 0xfffff) + m_banksize * m_bankL;
						loop = (loop & 0xfffff) + m_banksize * m_bankL;
					} else {
						start = (start & 0xfffff) + m_banksize * m_bankR;
						loop = (loop & 0xfffff) + m_banksize * m_bankR;
					}
					break;
				}
			}
			voi->start = start;
			voi->loop = loop << INCR_SHIFT;
			voi->size = size << INCR_SHIFT;

			voi->attackrate = hd[8] & 15;
			voi->attacklevel = (hd[8]>>4) & 15;

			voi->decayrate = hd[9] & 15;
			voi->sustainlevel = (hd[9]>>4) & 15;

			voi->releaserate = hd[10] & 15;

			voi->envphase = ENV_ATTACK;
			//voi->envlevel = 0;
			voi->envgoal = (voi->attacklevel << ENV_SHIFT) / 15;
			if (voi->envlevel < voi->envgoal) voi->envlevel = voi->envgoal;
			voi->envincr = (voi->envgoal - voi->envlevel) * m_ar_table[voi->attackrate];
#if 0
			for (int xx=0;xx<5;xx++)
				m_regs[b+11+xx] = hd[7+xx];
			if (size > loop) {
				m_regs[b+10] = 0xff;
			} else {
				m_regs[b+10] = 0x00;
			}
#endif
		} else {
			// key off
			voice *const voi = &m_voi[m_ch];
			info[m_ch].keyon &= 0x80;

			voi->envphase = ENV_RELEASE;
			voi->envgoal = 0;
			voi->envincr = -voi->envlevel * m_rr_table[voi->releaserate];
		}
		break;
	case 0x05:
		m_voi[m_ch].volume = m_voltbl[_data >> 1];
		info[m_ch].volume = 127 - (_data>>1);
		break;
	}
}

void ssSEGAPCM2::SetBank(BYTE _data)
{
	switch (m_type) {
	case TYPE_STEREO:
		m_bankL = _data;
		break;
	case TYPE_MONO:
		m_bankL = (_data>>3) & 7;
		m_bankR = _data & 7;
		break;
	}
}

BYTE ssSEGAPCM2::ReadStatus(void)
{
	return 0;
}

int ssSEGAPCM2::GetBufferCount(void) const
{
	return 1;
}

WORD ssSEGAPCM2::GetBufferFlag(int _b) const
{
	return ssSoundStream::F_STEREO_LONG;
}

int ssSEGAPCM2::GetTrackCount(void) const
{
	return MAX_PCM;
}

ssTrackInfo *ssSEGAPCM2::GetInfo(int _track)
{
	if (_track < MAX_PCM) {
		return &info[_track];
	}

	return NULL;
}


int ssSEGAPCM2::GetRegs(BYTE *_buffer, int _count, int _offset)
{
	const int size = sizeof(m_regs);
	int count;

	if (_offset >= size) {
		return 0;
	}

	if (_offset + _count >= size) {
		count = size - _offset;
	} else {
		count = _count;
	}

	memcpy(_buffer, m_regs + _offset, count);

	return count;
}


void ssSEGAPCM2::Update(SHORT **_buffer, DWORD _count)
{
	int *const buffer = (int *)_buffer[0];
	const DWORD count = _count;

	memset(buffer, 0, sizeof(int) * 2 * count);

	int ch;

	const BYTE *const pcmdata = m_pcmdata;

	for (ch = 0; ch < MAX_PCM; ch++) {
		voice *voi = &m_voi[ch];
		if (voi->play) {
			const bool mute = (m_mask & (1<<ch)) != 0;
			const int start = voi->start;
			const int size = voi->size;
			const int incr = voi->incr;
			const int pan = voi->pan;
			int lv, rv;
			if (pan == -8) {
				lv = rv = 0;
			} else {
				lv = m_pantbl[-pan + 7];
				rv = m_pantbl[ pan + 7];
			}
			const int lvol = voi->volume * lv;
			const int rvol = voi->volume * rv;
			int *buff = buffer;
			int pos = voi->pos;

			int envphase = voi->envphase;
			int envincr = voi->envincr;
			int envlevel = voi->envlevel;
			int envgoal = voi->envgoal;

			for (int p = 0; p < count; p++) {

				envlevel += envincr;
				switch (envphase) {
				case ENV_NONE:
					break;
				case ENV_ATTACK:
					if (envlevel >= envgoal) {
						envlevel = envgoal;
						envphase = ENV_DECAY;
						envgoal = (voi->sustainlevel << ENV_SHIFT) / 15;
						envincr = (envgoal - envlevel) * m_dr_table[voi->decayrate];

						if (envincr == 0) {
							envphase = ENV_NONE;
						}
					}
					break;
				case ENV_DECAY:
					if (envincr < 0) {
						if (envlevel <= envgoal) {
							envlevel = envgoal;
							envphase = ENV_NONE;
							envincr = 0;
						}
					} else {
						if (envlevel >= envgoal) {
							envlevel = envgoal;
							envphase = ENV_NONE;
							envincr = 0;
						}
					}
					break;
				case ENV_RELEASE:
					if (envlevel <= 0) {
						envlevel = 0;
						envphase = ENV_NONE;
						envincr = 0;
						voi->play = 0;
					}
					break;
				}
				if (voi->play == 0) break;

				if (pos >= size) {
					const int loop = voi->loop;
					if (size > loop) {
						pos = loop + (pos - size);
					} else {
						info[ch].keyon &= 0x80;
						voi->play = 0;
					}
				}

				if (!mute) {
					char d = pcmdata[start + (pos >> INCR_SHIFT)];

					int l = d * lvol * (envlevel>>ENV_SHIFT2) / (1<<(ENV_SHIFT-ENV_SHIFT2+8));
					int r = d * rvol * (envlevel>>ENV_SHIFT2) / (1<<(ENV_SHIFT-ENV_SHIFT2+8));
					*buff++ += l;
					*buff++ += r;
				}

				pos += incr;
				//pos += (1<<INCR_SHIFT);
			}
			voi->envphase = envphase;
			voi->envincr = envincr;
			voi->envlevel = envlevel;
			voi->envgoal = envgoal;

			voi->pos = pos;
		}
	}
}

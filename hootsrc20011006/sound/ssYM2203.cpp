#include "StdAfx.h"
#include "sound/ssYM2203.h"
#include "sound/ssAY8910.h"
#include "ssConfig.h"
#include "ssSoundDriverManager.h"
#include "ssSoundStream.h"

// dummy for MAME
#define YM2203_NUMBUF 1
#include "m88/opna.h"

#include <math.h>

const DWORD ssYM2203::MAX_YM2203 = 2;
DWORD ssYM2203::m_bitmap = 0;

static ssYM2203 *mapYM2203[ssYM2203::MAX_YM2203];

static FM::OPN *instYM2203[ssYM2203::MAX_YM2203];

ssYM2203::ssYM2203(int _idTimerA, int _idTimerB)
{
	m_ChipNo = -1;
	InitTimer(_idTimerA, _idTimerB);

	m_SSG = new ssAY8910(ssAY8910::TYPE_YM2203_M88);

	m_pris = 0xff;

	memset(reg, 0, 256);
}

ssYM2203::~ssYM2203()
{
	delete instYM2203[m_ChipNo];
	instYM2203[m_ChipNo] = NULL;

	delete m_SSG;

	if (m_ChipNo != -1) {
		m_bitmap &= ~(1 << m_ChipNo);
		mapYM2203[m_ChipNo] = NULL;
	}
}

void ssYM2203::calc_notetable(int _pris, int _ssg_pris)
{
	m_SSG->SetClock(m_baseclock / _ssg_pris);

	if (m_pris == _pris) {
		return;
	}
	m_pris = _pris;
	for (int oct = 0; oct < 8; oct++) {
		for (int fnum = 0; fnum < 2048; fnum++) {
			const int kci = oct*2048 + fnum;
			const int n = _pris;
			const double freq = (double)fnum * pow(2.0, (double)(oct - 1)) * (double)m_baseclock / (12.0 * n * pow(2.0, 20));
			int kco = (int)(12.0 * log(freq/440.0) / log(2.0) + 57.0 + 0.5);
			if (kco < 0) {
				kco = 0;
			} else if (kco > 255) {
				kco = 255;
			}
			notetable[kci] = kco;
		}
	}
}

bool ssYM2203::Initialize(int _baseclock)
{
	bool ret = false;

	int num;
	ssSoundDriverManager *manager = ssSoundDriverManager::Instance();
	const ssConfig &config = manager->GetConfig();
	int sampling_rate = config.sampling_rate;

	m_baseclock = _baseclock;

	SetClock(72. / m_baseclock);

	for (num = 0; num < MAX_YM2203; num++) {
		if ((m_bitmap & (1 << num)) == 0) {
			m_ChipNo = num;
			m_bitmap |= 1 << m_ChipNo;
			mapYM2203[m_ChipNo] = this;

			instYM2203[m_ChipNo] = new FM::OPN;
			instYM2203[m_ChipNo]->Init(_baseclock, config.sampling_rate,
									   config.ym2203_interpolation > 0, NULL);
			instYM2203[m_ChipNo]->SetVolumeFM(config.ym2203_volume_fm);
			instYM2203[m_ChipNo]->SetVolumePSG(config.ym2203_volume_psg);
			instYM2203[m_ChipNo]->Reset();

			m_SSG->Initialize(_baseclock);

			ret = true;
			break;
		}
	}

	for (int t = 0; t < 3; t++) {
		InitTrackInfo(&info[t]);
		sprintf(info[t].name, "YM-2203 FM#%d", t);
	}

	calc_notetable(6, 4);

	SetMask(0);

	return ret;
}

void ssYM2203::TimerAOver(void)
{
}

void ssYM2203::TimerBOver(void)
{
}

void ssYM2203::Write(int _adr, BYTE _data)
{
	if ((_adr & 1) == 0) {
		m_reg = _data;
		if (m_reg == 0x2d) {
			TRACE("OPN: Pris 1/6\n");
			calc_notetable(6, 4);
			instYM2203[m_ChipNo]->SetReg(m_reg, 0);
		} else if (m_reg == 0x2e) {
			TRACE("OPN: Pris 1/3\n");
			calc_notetable(3, 2);
			instYM2203[m_ChipNo]->SetReg(m_reg, 0);
		} else if (m_reg == 0x2f) {
			TRACE("OPN: Pris 1/2\n");
			calc_notetable(2, 1);
			instYM2203[m_ChipNo]->SetReg(m_reg, 0);
		}
	} else {
		reg[m_reg] = _data;
		if (m_reg == 0x24 || m_reg == 0x25) {
			SetTimerA(m_reg, _data);
		} else if (m_reg == 0x26) {
			SetTimerB(_data);
		} else if (m_reg == 0x27) {
			SetTimerControl(_data);
		} else if (m_reg == 0x28) {
			const BYTE ch = _data & 3;
			if (ch != 3) {
				if (_data & 0xf0) {
					info[ch].keyon = 0xff;
				} else {
					info[ch].keyon &= 0x80;
				}
			}
		} else if (m_reg < 16) {
			m_SSG->WriteInfo(0, m_reg);
			m_SSG->WriteInfo(1, _data);
		} else {
			const BYTE ch = m_reg & 3;
			if (ch != 3) {
				switch (m_reg & 0xfc) {
				case 0xa0:	// F-Num1 : A0-A2
					{
						const int kc = ((reg[m_reg+4]<<8) + _data) & 0x3fff;
						info[ch].keycode = notetable[kc];
					}
					break;
				case 0x4c:	// TL(4): 4C-4E 
					{
						info[ch].volume = 127 - (_data & 0x7f);
					}
					break;
				}
			}
		}

	}

	if (_adr & 1) {
		instYM2203[m_ChipNo]->SetReg(m_reg, _data);
	}
}

BYTE ssYM2203::Read(int _adr)
{
	switch (_adr & 1) {
	case 0:
		//return instYM2203[m_ChipNo]->ReadStatus();
		return GetStatus();
	case 1:
		return instYM2203[m_ChipNo]->GetReg(m_reg);
	}
	return 0xff;
}

int ssYM2203::GetBufferCount(void) const
{
	return 1;
}

WORD ssYM2203::GetBufferFlag(int _b) const
{
	return ssSoundStream::F_STEREO_LONG;
}

void ssYM2203::SetVolume(int _b, int _p, short _vol)
{
	if (_b >= YM2203_NUMBUF) {
		m_SSG->SetVolume(_b - YM2203_NUMBUF, _p, _vol);
	} else {
		ssSoundChip::SetVolume(0, _p, _vol);
	}
}

short ssYM2203::GetVolume(int _b, int _p) const
{
	if (_b >= YM2203_NUMBUF) {
		return m_SSG->GetVolume(_b - YM2203_NUMBUF, _p);
	} else {
		return ssSoundChip::GetVolume(0, _p);
	}
}

int ssYM2203::GetTrackCount(void) const
{
	return 3 + m_SSG->GetTrackCount();
}

ssTrackInfo *ssYM2203::GetInfo(int _track)
{
	if (_track < 3) {
		return &info[_track];
	}

	return m_SSG->GetInfo(_track - 3);
}

int ssYM2203::GetRegs(BYTE *_buffer, int _count, int _offset)
{
	int count;

	if (_offset >= 256) {
		return 0;
	}

	if (_offset + _count >= 256) {
		count = 256 - _offset;
	} else {
		count = _count;
	}

	memcpy(_buffer, reg + _offset, count);

	return count;
}

void ssYM2203::Update(SHORT **_buffer, DWORD _count)
{
	int *buff = (int *)(_buffer[0]);
	memset(buff, 0, _count * sizeof(int) * 2);
	instYM2203[m_ChipNo]->Mix(buff, _count);
}

void ssYM2203::SetMask(DWORD _mask)
{
	ssSoundChip::SetMask(_mask);
	DWORD mask = 0;
	mask |= (m_mask    & 0x007);
	mask |= (m_mask<<3 & 0x1c0);
	instYM2203[m_ChipNo]->SetChannelMask(mask);
}

DWORD ssYM2203::GetMask(void)
{
	return ssSoundChip::GetMask();
}


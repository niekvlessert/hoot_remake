#include "StdAfx.h"
#include "sound/ssYM2151.h"
#include "ssConfig.h"
#include "ssSoundDriverManager.h"
#include "ssSoundStream.h"


#include "m88/opm.h"

#include <math.h>

const DWORD ssYM2151::MAX_YM2151 = 2;
DWORD ssYM2151::m_bitmap = 0;

static ssYM2151 *mapYM2151[ssYM2151::MAX_YM2151];

static BYTE notetable[(8*12)*64];
static const kcconv[16] = {
	 1,  2,  3,  3,
	 4,  5,  6,  6,
	 7,  8,  9,  9,
	10, 11, 12, 12
};

static FM::OPM *instYM2151[ssYM2151::MAX_YM2151];

ssYM2151::ssYM2151(int _idTimerA, int _idTimerB)
{
	m_ChipNo = -1;
	InitTimer(_idTimerA, _idTimerB);

	memset(reg, 0, 256);
}

ssYM2151::~ssYM2151()
{
	if (m_ChipNo != -1) 
	{
		delete instYM2151[m_ChipNo];
		instYM2151[m_ChipNo] = NULL;
		m_bitmap &= ~(1 << m_ChipNo);
		mapYM2151[m_ChipNo] = NULL;
	}
}

bool ssYM2151::Initialize(int _baseclock)
{
	bool ret = false;

	int num;
	ssSoundDriverManager *manager = ssSoundDriverManager::Instance();
	const ssConfig &config = manager->GetConfig();
	int sampling_rate = config.sampling_rate;

	SetClock(64. / _baseclock);

	for (num = 0; num < MAX_YM2151; num++) {
		if ((m_bitmap & (1 << num)) == 0) {
			m_ChipNo = num;
			m_bitmap |= 1 << m_ChipNo;
			mapYM2151[m_ChipNo] = this;

			instYM2151[m_ChipNo] = new FM::OPM;
			instYM2151[m_ChipNo]->Init(_baseclock, config.sampling_rate,
									   config.ym2151_interpolation > 0);
			instYM2151[m_ChipNo]->SetVolume(config.ym2151_volume);
			instYM2151[m_ChipNo]->Reset();
			//instYM2151[m_ChipNo]->SetReg(0x12, 0xf0);
			//instYM2151[m_ChipNo]->SetReg(0x14, 0x3f);

			ret = true;
			break;
		}
	}


	for (int t = 0; t < 8; t++) {
		InitTrackInfo(&info[t]);
		sprintf(info[t].name, "YM-2151 #%d", t);
	}

	for (int oct = 0; oct < 8; oct++) {
		for (int note = 0; note < 12; note++) {
			for (int kf = 0; kf < 64; kf++) {
				const int kci = (oct*12 + note) * 64 + kf;
				const double freq = 440.0 * pow(2.0, (double)(kci - 57*64) / (12.0*64.0)) * (double)_baseclock / 3579545.0;
				int kco = (int)(12.0 * log(freq/440.0) / log(2.0) + 57.0 + 0.5);
				if (kco < 0) {
					kco = 0;
				} else if (kco > 127) {
					kco = 127;
				}
				notetable[kci] = kco;
			}
		}
	}

	SetMask(0);

	return ret;
}

void ssYM2151::TimerAOver(void)
{
}

void ssYM2151::TimerBOver(void)
{
}

void ssYM2151::Write(int _adr, BYTE _data)
{
	if ((_adr & 1) == 0) {
		m_reg = _data;
	} else {
		reg[m_reg] = _data;

		if (m_reg == 0x08) {
			const BYTE ch = _data & 7;
			if (_data & 0x78) {
				info[ch].keyon = 0xff;
			} else {
				info[ch].keyon &= 0x80;
			}
		} else if (m_reg == 0x10 || m_reg == 0x11) {
			SetTimerA(m_reg, _data);
		} else if (m_reg == 0x12) {
			SetTimerB(_data);
		} else if (m_reg == 0x14) {
			SetTimerControl(_data);
		} else {
			const BYTE ch = m_reg & 7;
			switch (m_reg & 0xf8) {
			case 0x20:
				{
					switch (_data & 0xc0) {
					case 0xc0:
						info[ch].pan = ssTrackInfo::PAN_CENTER;
						break;
					case 0x80:
						info[ch].pan = ssTrackInfo::PAN_RIGHT;
						break;
					case 0x40:
						info[ch].pan = ssTrackInfo::PAN_LEFT;
						break;
					case 0x00:
						info[ch].pan = ssTrackInfo::PAN_OFF;
						break;
					}
				}
				break;
			case 0x28:	// KC: 28-2f
			case 0x30:	// KF: 30-3f
				{
					const BYTE o = (reg[0x28+ch] >> 4) & 7;
					const BYTE n = kcconv[reg[0x28+ch] & 15];
					const BYTE kf = reg[0x30+ch] >> 2;
					const int kc = (o*12 + n) * 64 + kf;
					info[ch].keycode = notetable[kc];
				}
				break;
			case 0x78:	// TL(4): 78-7f
				{
					info[ch].volume = 127 - (_data & 0x7f);
				}
				break;
			}
		}
	}

	if (_adr & 1) 
	{
		instYM2151[m_ChipNo]->SetReg(m_reg, _data);
	}
}

BYTE ssYM2151::Read(int _adr)
{
	switch (_adr & 1) {
	case 0:
		return 0;
	case 1:
		//return instYM2151[m_ChipNo]->ReadStatus();
		return GetStatus();
	}
	return 0xff;
}

int ssYM2151::GetBufferCount(void) const
{
	return 1;
}

WORD ssYM2151::GetBufferFlag(int _b) const
{
	return ssSoundStream::F_STEREO_LONG;
}

int ssYM2151::GetTrackCount(void) const
{
	return 8;
}

ssTrackInfo *ssYM2151::GetInfo(int _track)
{
	if (_track < 8) {
		return &info[_track];
	}

	return NULL;
}

int ssYM2151::GetRegs(BYTE *_buffer, int _count, int _offset)
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

void ssYM2151::Update(SHORT **_buffer, DWORD _count)
{
	int *buff = (int *)(_buffer[0]);
	memset(buff, 0, _count * sizeof(int) * 2);
	instYM2151[m_ChipNo]->Mix(buff, _count);
}

void ssYM2151::SetMask(DWORD _mask)
{
	ssSoundChip::SetMask(_mask);
	instYM2151[m_ChipNo]->SetChannelMask(m_mask);
}

DWORD ssYM2151::GetMask(void)
{
	return ssSoundChip::GetMask();
}


#include "StdAfx.h"
#include "sound/ssYM2608.h"
#include "sound/ssAY8910.h"
#include "ssConfig.h"
#include "ssSoundDriverManager.h"
#include "ssSoundStream.h"
#include "ssUnZip.h"

// dummy for MAME
#define YM2608_NUMBUF 2
#include "m88/opna.h"

#include <math.h>

const DWORD ssYM2608::MAX_YM2608 = 1;
DWORD ssYM2608::m_bitmap = 0;

static ssYM2608 *mapYM2608[ssYM2608::MAX_YM2608];


static FM::OPNA *instYM2608[ssYM2608::MAX_YM2608];

ssYM2608::ssYM2608(int _idTimerA, int _idTimerB)
{
	m_ChipNo = -1;
	InitTimer(_idTimerA, _idTimerB);

	m_SSG = new ssAY8910(ssAY8910::TYPE_YM2608_M88);

	m_pris = 0xff;

	memset(reg, 0, 512);
}

ssYM2608::~ssYM2608()
{
	delete instYM2608[m_ChipNo];
	instYM2608[m_ChipNo] = NULL;

	delete m_SSG;

	if (m_ChipNo != -1) {
		m_bitmap &= ~(1 << m_ChipNo);
		mapYM2608[m_ChipNo] = NULL;

	}
}

void ssYM2608::calc_notetable(int _pris, int _ssg_pris)
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
			const double freq = (double)fnum * pow(2.0, (double)(oct - 1)) * (double)m_baseclock / (24.0 * n * pow(2.0, 20));
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

bool ssYM2608::Initialize(int _baseclock,
						  void *_pcmrom, int _pcmsize)
{
	bool ret = false;

	int num;
	ssSoundDriverManager *manager = ssSoundDriverManager::Instance();
	const ssConfig &config = manager->GetConfig();
	int sampling_rate = config.sampling_rate;

	m_baseclock = _baseclock;

	SetClock(72. / (m_baseclock / 2));

	for (num = 0; num < MAX_YM2608; num++) {
		if ((m_bitmap & (1 << num)) == 0) {
			m_ChipNo = num;
			m_bitmap |= 1 << m_ChipNo;
			mapYM2608[m_ChipNo] = this;
			instYM2608[m_ChipNo] = new FM::OPNA;
			instYM2608[m_ChipNo]->Init(_baseclock, config.sampling_rate,
									   config.ym2608_interpolation > 0);
			instYM2608[m_ChipNo]->SetVolumeFM(config.ym2608_volume_fm);
			instYM2608[m_ChipNo]->SetVolumePSG(config.ym2608_volume_psg);
			instYM2608[m_ChipNo]->SetVolumeRhythmTotal(config.ym2608_volume_rhythmt);
			instYM2608[m_ChipNo]->SetVolumeRhythm(0, config.ym2608_volume_rhythm0);
			instYM2608[m_ChipNo]->SetVolumeRhythm(1, config.ym2608_volume_rhythm1);
			instYM2608[m_ChipNo]->SetVolumeRhythm(2, config.ym2608_volume_rhythm2);
			instYM2608[m_ChipNo]->SetVolumeRhythm(3, config.ym2608_volume_rhythm3);
			instYM2608[m_ChipNo]->SetVolumeRhythm(4, config.ym2608_volume_rhythm4);
			instYM2608[m_ChipNo]->SetVolumeRhythm(5, config.ym2608_volume_rhythm5);
			if (_pcmrom && _pcmsize) {
				BYTE *adpcm = instYM2608[m_ChipNo]->GetADPCMBuffer();
				memcpy(adpcm, _pcmrom, _pcmsize);
			}
			instYM2608[m_ChipNo]->Reset();

			m_SSG->Initialize(_baseclock);

			ret =  true;
			break;
		}
	}

	int t;
	for (t = 0; t < 6; t++) {
		InitTrackInfo(&info[t]);
		sprintf(info[t].name, "YM-2608 FM#%d", t);
	}

	for (t = 0; t < 1; t++) {
		InitTrackInfo(&info[INFO_ADPCM + t]);
		sprintf(info[INFO_ADPCM + t].name, "YM-2608 ADPCM#%d", t);
	}

	for (t = 0; t < 6; t++) {
		InitTrackInfo(&info[INFO_RHYTHM + t]);
		sprintf(info[INFO_RHYTHM + t].name, "YM-2608 RHYTHM#%d", t);
	}

	calc_notetable(6, 4);

	SetMask(0);

	return ret;
}


void ssYM2608::TimerAOver(void)
{
}

void ssYM2608::TimerBOver(void)
{
}

void ssYM2608::write_reg(BYTE _reg, BYTE _data, BYTE _c)
{
	BYTE ch = _reg & 3;
	if (ch != 3) {
		ch += _c*3;
		switch (_reg & 0xfc) {
		case 0x4c:	// TL(4): 4C-4E 
			{
				info[ch].volume = 127 - (_data & 0x7f);
			}
			break;
		case 0xa0:	// F-Num1 : A0-A2
			{
				const int kc = ((reg[_reg+4 + 0x100*_c]<<8) + _data) & 0x3fff;
				info[ch].keycode = notetable[kc];
			}
			break;
		case 0xb4:	// PAN : B4-B6
			{
				switch (_data & 0xc0) {
				case 0xc0:
					info[ch].pan = ssTrackInfo::PAN_CENTER;
					break;
				case 0x80:
					info[ch].pan = ssTrackInfo::PAN_LEFT;
					break;
				case 0x40:
					info[ch].pan = ssTrackInfo::PAN_RIGHT;
					break;
				case 0x00:
					info[ch].pan = ssTrackInfo::PAN_OFF;
					break;
				}
			}
			break;
		}
	}
}

void ssYM2608::Write(int _adr, BYTE _data)
{
	switch (_adr & 3) {
	case 0x00:
		m_reg1 = _data;
		if (m_reg1 == 0x2d) {
			TRACE("OPN: Pris 1/6\n");
			calc_notetable(6, 4);
			instYM2608[m_ChipNo]->SetReg(m_reg1, 0);
		} else if (m_reg1 == 0x2e) {
			TRACE("OPN: Pris 1/3\n");
			calc_notetable(3, 2);
			instYM2608[m_ChipNo]->SetReg(m_reg1, 0);
		} else if (m_reg1 == 0x2f) {
			TRACE("OPN: Pris 1/2\n");
			calc_notetable(2, 1);
			instYM2608[m_ChipNo]->SetReg(m_reg1, 0);
		}
		break;
	case 0x01:
		reg[m_reg1] = _data;
		if (m_reg1 == 0x24 || m_reg1 == 0x25) {
			SetTimerA(m_reg1, _data);
		} else if (m_reg1 == 0x26) {
			SetTimerB(_data);
		} else if (m_reg1 == 0x27) {
			SetTimerControl(_data);
		} else if (m_reg1 == 0x28) {
			BYTE ch = _data & 3;
			if (ch != 3) {
				if (_data & 0x04) {
					ch += 3;
				}
				if (_data & 0xf0) {
					info[ch].keyon = 0xff;
				} else {
					info[ch].keyon &= 0x80;
				}
			}
		} else if (m_reg1 < 16) {
			m_SSG->WriteInfo(0, m_reg1);
			m_SSG->WriteInfo(1, _data);
		} else if (m_reg1 == 0x10) {
			if (_data & 0x80) {
				if (_data & 0x01) info[INFO_RHYTHM + 0].keyon &= 0x80;
				if (_data & 0x02) info[INFO_RHYTHM + 1].keyon &= 0x80;
				if (_data & 0x04) info[INFO_RHYTHM + 2].keyon &= 0x80;
				if (_data & 0x08) info[INFO_RHYTHM + 3].keyon &= 0x80;
				if (_data & 0x10) info[INFO_RHYTHM + 4].keyon &= 0x80;
				if (_data & 0x20) info[INFO_RHYTHM + 5].keyon &= 0x80;
			} else {
				if (_data & 0x01) info[INFO_RHYTHM + 0].keyon = 0xff;
				if (_data & 0x02) info[INFO_RHYTHM + 1].keyon = 0xff;
				if (_data & 0x04) info[INFO_RHYTHM + 2].keyon = 0xff;
				if (_data & 0x08) info[INFO_RHYTHM + 3].keyon = 0xff;
				if (_data & 0x10) info[INFO_RHYTHM + 4].keyon = 0xff;
				if (_data & 0x20) info[INFO_RHYTHM + 5].keyon = 0xff;
			}
		} else if (m_reg1 >= 0x18 && m_reg1 <= 0x1d) {
			int t = m_reg1 - 0x18;
			const int ch = INFO_RHYTHM + t;
			info[ch].volume = (_data & 0x1f) * 4;
			switch (_data & 0xc0) {
			case 0xc0:
				info[ch].pan = ssTrackInfo::PAN_CENTER;
				break;
			case 0x80:
				info[ch].pan = ssTrackInfo::PAN_LEFT;
				break;
			case 0x40:
				info[ch].pan = ssTrackInfo::PAN_RIGHT;
				break;
			case 0x00:
				info[ch].pan = ssTrackInfo::PAN_OFF;
				break;
			}
		} else {
			write_reg(m_reg1, _data, 0);
		}
		break;
	case 0x02:
		m_reg2 = _data;
		break;
	case 0x03:
		reg[m_reg2 + 0x100] = _data;
		if (m_reg2 == 0x00) {
			if (_data & 0x80) {
				info[INFO_ADPCM].keyon = 0xff;
			} else {
				info[INFO_ADPCM].keyon = 0x00;
			}
		} else if (m_reg2 == 0x01) {
			const int ch = INFO_ADPCM;
			switch (_data & 0xc0) {
			case 0xc0:
				info[ch].pan = ssTrackInfo::PAN_CENTER;
				break;
			case 0x80:
				info[ch].pan = ssTrackInfo::PAN_LEFT;
				break;
			case 0x40:
				info[ch].pan = ssTrackInfo::PAN_RIGHT;
				break;
			case 0x00:
				info[ch].pan = ssTrackInfo::PAN_OFF;
				break;
			}
		} else if (m_reg2 == 0x02 || m_reg2 == 0x03) {
			reg[0x100 + m_reg2] = _data;
		} else if (m_reg2 == 0x09 || m_reg1 == 0x0a) {
			reg[0x100 + m_reg2] = _data;
			const int delta = (reg[0x10a] << 8) + reg[0x109];
			info[INFO_ADPCM].keycode = Hz2Kc(440.0 * (double)delta / (double)0x556a * m_baseclock / 8000000.0);
		} else if (m_reg2 == 0x0b) {
			info[INFO_ADPCM].volume = _data >> 1;
		} else {
			write_reg(m_reg2, _data, 1);
		}
		break;
	}

	switch (_adr & 3) {
	case 1:
		instYM2608[m_ChipNo]->SetReg(m_reg1, _data);
		break;
	case 3:
		instYM2608[m_ChipNo]->SetReg(m_reg2 + 0x100, _data);
		break;
	}
}

BYTE ssYM2608::Read(int _adr)
{
	switch (_adr & 3) {
	case 0:
		//return instYM2608[m_ChipNo]->ReadStatus();
		return GetStatus();
	case 1:
		return instYM2608[m_ChipNo]->GetReg(m_reg1);
	case 2:
		return instYM2608[m_ChipNo]->ReadStatusEx();
	case 3:
		return instYM2608[m_ChipNo]->GetReg(m_reg2);
	}
	return 0xff;
}

int ssYM2608::GetBufferCount(void) const
{
	return 1;
}

WORD ssYM2608::GetBufferFlag(int _b) const
{
	return ssSoundStream::F_STEREO_LONG;
}

void ssYM2608::SetVolume(int _b, int _p, short _vol)
{
	if (_b >= YM2608_NUMBUF) {
		m_SSG->SetVolume(_b - YM2608_NUMBUF, _p, _vol);
	} else {
		ssSoundChip::SetVolume(0, _p, _vol);
	}
}

// 各デバイス向けボリューム設定
void ssYM2608::SetVolumeDev(int _dev, short _vol)
{
	int chip = 0;			// m_ChipNo

	switch(_dev) {
	// FM device
	case 0:
		instYM2608[chip]->SetVolumeFM(_vol);
		break;
	// SSG device
	case 1:
		instYM2608[chip]->SetVolumePSG(_vol);
		break;
	// RHYTHM device
	case 2:
		instYM2608[chip]->SetVolumeRhythmTotal(_vol);
		break;
	default:
		break;
	}
}

short ssYM2608::GetVolume(int _b, int _p) const
{
	if (_b >= YM2608_NUMBUF) {
		return m_SSG->GetVolume(_b - YM2608_NUMBUF, _p);
	} else {
		return ssSoundChip::GetVolume(0, _p);
	}
}

int ssYM2608::GetTrackCount(void) const
{
	return 6 + 1 + 6 + m_SSG->GetTrackCount();
}

ssTrackInfo *ssYM2608::GetInfo(int _track)
{
	if (_track < 6) {
		return &info[_track];
	} if (_track >= 9 && _track < 16) {
		return &info[_track - 9 + 6];
	}

	return m_SSG->GetInfo(_track - 6);
}

int ssYM2608::GetRegs(BYTE *_buffer, int _count, int _offset)
{
	int count;

	if (_offset >= 512) {
		return 0;
	}

	if (_offset + _count >= 512) {
		count = 512 - _offset;
	} else {
		count = _count;
	}

	memcpy(_buffer, reg + _offset, count);

	return count;
}


void ssYM2608::Update(SHORT **_buffer, DWORD _count)
{
	int *buff = (int *)(_buffer[0]);
	memset(buff, 0, _count * sizeof(int) * 2);
	instYM2608[m_ChipNo]->Mix(buff, _count);

	BYTE s = Read(2);
	if (s & 0x04) info[INFO_ADPCM].keyon = 0x00;
}

void ssYM2608::SetMask(DWORD _mask)
{
	ssSoundChip::SetMask(_mask);
	instYM2608[m_ChipNo]->SetChannelMask(m_mask);
}

DWORD ssYM2608::GetMask(void)
{
	return ssSoundChip::GetMask();
}

#include "StdAfx.h"
#include "sound/ssYM2413.h"
#include "ssConfig.h"
#include "ssSoundDriverManager.h"
#include "ssSoundStream.h"


#include "emu2413/emu2413.h"

ssYM2413::ssYM2413()
{
	m_opll = NULL;
	m_reg0e = 0;

	memset(reg, 0, sizeof(reg));
}

ssYM2413::~ssYM2413()
{
	OPLL_delete(m_opll);
}

bool ssYM2413::Initialize(int _baseclock)
{
	ssSoundDriverManager *manager = ssSoundDriverManager::Instance();
	const ssConfig &config = manager->GetConfig();
	int sampling_rate = config.sampling_rate;

	OPLL_init(_baseclock, sampling_rate);
	m_opll = OPLL_new();
	OPLL_reset(m_opll);
	OPLL_reset_patch(m_opll, 0);

	int t;
	for (t = 0; t < 9; t++) {
		InitTrackInfo(&info[t]);
		sprintf(info[t].name, "YM-2413 FM#%d", t);
	}

	for (t = 0; t < 5; t++) {
		InitTrackInfo(&info[9+t]);
		sprintf(info[9+t].name, "YM-2413 RHYTHM#%d", t);
	}

	for (int oct = 0; oct < 8; oct++) {
		for (int fnum = 0; fnum < 512; fnum++) {
			const int kci = oct*512 + fnum;
			const double freq = (double)fnum * pow(2.0, (double)(oct - 1)) * (double)_baseclock / 72. / pow(2., 18.);
			notetable[kci] = Hz2Kc(freq);
		}
	}

	SetMask(0);

	return true;
}

void ssYM2413::Write(int _adr, BYTE _data)
{
	const BYTE data = _data;

	if ((_adr & 1) == 0) {
		m_reg = data;
	} else {
		reg[m_reg] = data;
		OPLL_writeReg(m_opll, m_reg, data);

		const int ch = m_reg & 0x0f;
		switch (m_reg) {
		case 0x0e:
			if (data & 0x20) {
				int c;
				for (c = 0; c < 3; c++) {
					// FM keyoff
					info[6+c].keyon &= 0x80;
				}
				BYTE tmp = m_reg0e ^ data;
				for (c = 0; c < 5; c++) {
					if (tmp & (1<<c)) {
						if (data & (1<<c)) {
							info[9+c].keyon = 0xff;
						} else {
							info[9+c].keyon &= 0x80;
						}
					}
				}
			} else {
				for (int c = 0; c < 5; c++) {
					// RHYTHM key off
					info[9+c].keyon &= 0x80;
				}
			}
			m_reg0e = data;
			UpdateMask();
			break;
		case 0x20:
		case 0x21:
		case 0x22:
		case 0x23:
		case 0x24:
		case 0x25:
		case 0x26:
		case 0x27:
		case 0x28:
			if (ch < 6 || (reg[0x0e] & 0x20) == 0) {
				if (data & 0x10) {
					info[ch].keyon = 0xff;
				} else {
					info[ch].keyon &= 0x80;
				}
			}
			// through
		case 0x10:
		case 0x11:
		case 0x12:
		case 0x13:
		case 0x14:
		case 0x15:
		case 0x16:
		case 0x17:
		case 0x18:
			{
				const int fnum = reg[0x10+ch] + ((reg[0x20+ch]&0x0f)<<8);
				info[ch].keycode = notetable[fnum];
			}
			break;
		case 0x30:
		case 0x31:
		case 0x32:
		case 0x33:
		case 0x34:
		case 0x35:
			info[ch].volume = (15 - (data&0x0f)) * 8;
			break;
		case 0x36:
			info[6].volume = (15 - (data&0x0f)) * 8;
			info[9+4].volume = (15 - (data&0x0f)) * 8;
			break;
		case 0x37:
			info[7].volume = (15 - (data&0x0f)) * 8;
			info[9+3].volume = (15 - (data&0x0f)) * 8;
			info[9+0].volume = (15 - (data>>4)) * 8;
			break;
		case 0x38:
			info[8].volume = (15 - (data&0x0f)) * 8;
			info[9+2].volume = (15 - (data&0x0f)) * 8;
			info[9+1].volume = (15 - (data>>4)) * 8;
			break;
		}

	}
}

BYTE ssYM2413::Read(int _adr)
{
	return 0xff;
}

int ssYM2413::GetBufferCount(void) const
{
	return 1;
}

WORD ssYM2413::GetBufferFlag(int _b) const
{
	return ssSoundStream::F_MONO;
}

int ssYM2413::GetTrackCount(void) const
{
	return 14;
}

ssTrackInfo *ssYM2413::GetInfo(int _track)
{
	if (_track < 14) {
		return &info[_track];
	}

	return NULL;
}

int ssYM2413::GetRegs(BYTE *_buffer, int _count, int _offset)
{
	int count;
	const int size = sizeof(reg);

	if (_offset >= size) {
		return 0;
	}

	if (_offset + _count >= size) {
		count = 256 - _offset;
	} else {
		count = _count;
	}

	memcpy(_buffer, reg + _offset, count);

	return count;
}

void ssYM2413::Update(SHORT **_buffer, DWORD _count)
{
	short *buff = _buffer[0];
	for (int i = 0; i < _count; i++) {
		*buff++ = OPLL_calc(m_opll);
	}
}

void ssYM2413::SetMask(DWORD _mask)
{
	ssSoundChip::SetMask(_mask);
	OPLL_setMask(m_opll, m_mask);
}

DWORD ssYM2413::GetMask(void)
{
	return ssSoundChip::GetMask();
}

void ssYM2413::UpdateMask(void)
{
#if 0
	for (int i = 0; i < 6; i++) {
		m_opll->mask[i] = (m_mask & (1 <<  i)) ? 1 : 0;
	}
	if (reg[0x0e] & 0x20) {
		// RHYTHM
		m_opll->mask[6] = (m_mask & (1 << 13)) ? 1 : 0;
		m_opll->mask[7] = (m_mask & (3 << 11)) ? 1 : 0;
		m_opll->mask[8] = (m_mask & (3 <<  9)) ? 1 : 0;
	} else {
		// FM
		m_opll->mask[6] = (m_mask & (1 <<  6)) ? 1 : 0;
		m_opll->mask[7] = (m_mask & (1 <<  7)) ? 1 : 0;
		m_opll->mask[8] = (m_mask & (1 <<  8)) ? 1 : 0;
	}
#endif
}

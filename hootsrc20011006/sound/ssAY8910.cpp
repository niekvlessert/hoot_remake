#include "StdAfx.h"
#include "sound/ssAY8910.h"
#include "ssConfig.h"
#include "ssSoundDriverManager.h"
#include "ssSoundStream.h"

// USE_M88_ENGINE が定義されている場合はcisc版OPNAエンジン使用。
// そうでない場合は、Tatsuyuki版OPNAエンジン使用。
#define USE_M88_ENGINE

#ifdef USE_M88_ENGINE
#define MAX_8910 5
#include "m88/psg.h"
#else // USE_M88_ENGINE
extern "C" {
#include "mame/sound/driver.h"
#include "mame/sound/ay8910.h"
}
#endif // USE_M88_ENGINE

const DWORD ssAY8910::MAX_AY8910 = MAX_8910;
DWORD ssAY8910::m_bitmap = 0;

static ssAY8910 *mapAY8910[ssAY8910::MAX_AY8910];


#ifdef USE_M88_ENGINE

static PSG *instAY8910[ssAY8910::MAX_AY8910];

#endif // USE_M88_ENGINE


ssAY8910::ssAY8910(int _type)
{
	m_type = _type;
	m_ChipNo = -1;
	m_baseclock = -1;

	m_reg = 0x00;

	PAwrite = NULL;
	PAread = NULL;
	PBwrite = NULL;
	PBread = NULL;

	memset(reg, 0, 16);
}

ssAY8910::~ssAY8910()
{
	if (m_ChipNo != -1) {
		m_bitmap &= ~(1 << m_ChipNo);
		mapAY8910[m_ChipNo] = NULL;
	}
}

bool ssAY8910::Initialize(int _baseclock)
{
	bool ret = false;

	int num;
	ssSoundDriverManager *manager = ssSoundDriverManager::Instance();
	const ssConfig &config = manager->GetConfig();
	int sampling_rate = config.sampling_rate;

	for (num = 0; num < MAX_AY8910; num++) {
		if ((m_bitmap & (1 << num)) == 0) {
			m_ChipNo = num;
			m_bitmap |= 1 << m_ChipNo;
			mapAY8910[m_ChipNo] = this;
			if (!(m_type & TYPE_M88)) {
#ifdef USE_M88_ENGINE
				instAY8910[m_ChipNo] = new PSG;
				instAY8910[m_ChipNo]->SetClock(_baseclock / 2, config.sampling_rate);
#else
				AY8910_init(m_ChipNo,
							_baseclock, sampling_rate, 16,
							NULL, NULL, NULL,NULL);
				//AY8910_set_gain(m_ChipNo, 0);
#endif // USE_M88_ENGINE
			}

			SetClock(_baseclock);
			ret = true;
			break;
		}
	}

	for (int i = 0; i < 3; i++) {
		InitTrackInfo(&info[i]);
		switch (m_type) {
		case TYPE_YM2203:
		case TYPE_YM2203_M88:
			sprintf(info[i].name, "YM-2203 SSG#%d", i);
			break;
		case TYPE_YM2608:
		case TYPE_YM2608_M88:
			sprintf(info[i].name, "YM-2608 SSG#%d", i);
			break;
		case TYPE_YM2610:
		case TYPE_YM2610_M88:
			sprintf(info[i].name, "YM-2610 SSG#%d", i);
			break;
		default:
			sprintf(info[i].name, "AY-3-8910 #%d", i);
			SetMask(0);
			break;
		}
	}

	return ret;
}

void ssAY8910::Write(int _adr, BYTE _data)
{
	if (!(m_type & TYPE_M88)) {
#ifdef USE_M88_ENGINE
		if (_adr & 1) {
			instAY8910[m_ChipNo]->SetReg(m_reg, _data);
		}
#else // USE_M88_ENGINE
		AY8910Write(m_ChipNo, _adr, _data);
#endif // USE_M88_ENGINE
	}

	WriteInfo(_adr, _data);
}

void ssAY8910::WriteInfo(int _adr, BYTE _data)
{
	if ((_adr & 1) == 0) {
		m_reg = _data;
	} else {
		reg[m_reg] = _data;

		switch (m_reg) {
		case 0x00:
		case 0x01:
		case 0x02:
		case 0x03:
		case 0x04:
		case 0x05:
			{
				int ch = m_reg / 2;
				info[ch].keycode = m_kctable[((reg[ch*2+1]<<8) + reg[ch*2]) & 0xfff];
			}
			break;
		case 0x07:
			{
				if ((_data & 0x09) == 0x09) info[0].keyon = 0x00;
				if ((_data & 0x12) == 0x12) info[1].keyon = 0x00;
				if ((_data & 0x24) == 0x24) info[2].keyon = 0x00;
			}
			break;
		case 0x08:
		case 0x09:
		case 0x0a:
			{
				const int ch = m_reg - 8;
				if ((reg[7] & (9<<ch)) != (9<<ch)) {
					if (reg[7] & (1<<ch)) {
						//info[ch].keycode = m_kctable[reg[6] & 0x1f];
						info[ch].keycode = 12*3 + 31 - (reg[6] & 0x1f);
					}
					if (_data & 0x10) {
						info[ch].volume = 127;
						info[ch].keyon = 0xff;
					} else if (_data & 0x0f) {
						info[ch].volume = (_data&0x0f) * 8;
						info[ch].keyon = 0xff;
					} else {
						info[ch].volume = 0;
						info[ch].keyon = 0x00;
					}
				}
			}
			break;
		case 0x0e:
			if (PAwrite) {
				PAwrite(_data);
			}
			break;
		case 0x0f:
			if (PBwrite) {
				PBwrite(_data);
			}
			break;
		}
	}
}

BYTE ssAY8910::Read(void)
{
	if (m_reg == 0x0e) {
		if (PAread) {
			return PAread();
		} else {
			return reg[m_reg];
		}
	} else if (m_reg == 0x0b) {
		if (PBread) {
			return PBread();
		} else {
			return reg[m_reg];
		}
	}
	if (!(m_type & TYPE_M88)) {
#ifdef USE_M88_ENGINE
		return reg[m_reg];
#else // USE_M88_ENGINE
		return AY8910Read(m_ChipNo);
#endif // USE_M88_ENGINE
	} else {
		return 0x00;
	}
}

void ssAY8910::SetClock(int _baseclock)
{
	if (m_baseclock == _baseclock) {
		return;
	}

	m_baseclock = _baseclock;
	if (!(m_type & TYPE_M88)) {
#ifdef USE_M88_ENGINE
		// instAY8910[m_ChipNo]->SetClock(m_baseclock, rate);
#else // USE_M88_ENGINE
		AY8910_set_clock(m_ChipNo, m_baseclock);
#endif // USE_M88_ENGINE
	}

	m_kctable[0] = 127;
	for (int i = 1; i < 0x1000; i++) {
		m_kctable[i] = Hz2Kc((double)m_baseclock / ((double)i * 8.0));
	}
}

void ssAY8910::SetPortWriteHandler(int _port, void (*_handler)(BYTE _data))
{
	switch (_port) {
	case PORT_A:
		PAwrite = _handler;
		break;
	case PORT_B:
		PBwrite = _handler;
		break;
	}
}

void ssAY8910::SetPortReadHandler(int _port, BYTE (*_handler)())
{
	switch (_port) {
	case PORT_A:
		PAread = _handler;
		break;
	case PORT_B:
		PBread = _handler;
		break;
	}
}


int ssAY8910::GetBufferCount(void) const
{
#ifdef USE_M88_ENGINE
	return 1;
#else // USE_M88_ENGINE
	//return AY8910_NUMBUF;
	return 3;
#endif // USE_M88_ENGINE
}

WORD ssAY8910::GetBufferFlag(int _b) const
{
#ifdef USE_M88_ENGINE
	return ssSoundStream::F_STEREO_LONG;
#else // USE_M88_ENGINE
	return ssSoundStream::F_MONO;
#endif // USE_M88_ENGINE
}

int ssAY8910::GetTrackCount(void) const
{
	return 3;
}

ssTrackInfo *ssAY8910::GetInfo(int _track)
{
	if (_track < 3) {
		return &info[_track];
	}

	return NULL;
}


int ssAY8910::GetRegs(BYTE *_buffer, int _count, int _offset)
{
	int count;

	if (_offset >= 16) {
		return 0;
	}

	if (_offset + _count >= 16) {
		count = 16 - _offset;
	} else {
		count = _count;
	}

	memcpy(_buffer, reg + _offset, count);

	return count;
}


void ssAY8910::Update(SHORT **_buffer, DWORD _count)
{
	if (!(m_type & TYPE_M88)) {
#ifdef USE_M88_ENGINE
		int *buff = (int *)(_buffer[0]);
		memset(buff, 0, _count * sizeof(int) * 2);
		instAY8910[m_ChipNo]->Mix(buff, _count);
#else // USE_M88_ENGINE
		AY8910Update(m_ChipNo, _buffer, _count);
#endif // USE_M88_ENGINE
	}
}

void ssAY8910::SetMask(DWORD _mask)
{
	ssSoundChip::SetMask(_mask);
	instAY8910[m_ChipNo]->SetChannelMask(m_mask);
}

DWORD ssAY8910::GetMask(void)
{
	return ssSoundChip::GetMask();
}

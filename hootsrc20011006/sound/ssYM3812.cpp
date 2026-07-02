//	Fu-. 2001-01-20 Translated for 'hoot'
#include "StdAfx.h"
#include "sound/ssYM3812.h"
#include "ssConfig.h"
#include "ssSoundDriverManager.h"
#include "ssSoundStream.h"

extern "C" {
#include "mame/sound/driver.h"
#include "mame/sound/fmopl.h"
}

const DWORD ssYM3812::MAX_YM3812 = 2;
DWORD ssYM3812::m_bitmap = 0;

static ssYM3812 *mapYM3812[ssYM3812::MAX_YM3812];

static void TimerHandler(int c, double stepTime)
{
	for (int i = 0; i < ssYM3812::MAX_YM3812; i++) {
		if (mapYM3812[i]) {
			mapYM3812[i]->TimerHandler(c, stepTime);
		}
	}
}

static void IRQHandler(int n, int irq)
{
	for (int i = 0; i < ssYM3812::MAX_YM3812; i++) {
		if (mapYM3812[i]) {
			mapYM3812[i]->IRQHandler(irq);
		}
	}
}

ssYM3812::ssYM3812(int _idTimerA, int _idTimerB)
{
	m_ChipNo = -1;
	m_TimerB = new ssTimer(_idTimerB);
	m_TimerB->SetInterval(1/320);

	m_TimerA = new ssTimer(_idTimerA);
	m_TimerA->SetInterval(1/80);

	memset(reg, 0, sizeof(reg));
}

ssYM3812::~ssYM3812()
{
	delete m_TimerA;
	delete m_TimerB;

	if (m_ChipNo != -1) {
		m_bitmap &= ~(1 << m_ChipNo);
		mapYM3812[m_ChipNo] = NULL;
		OPLDestroy(F3812);
	}
}

void ssYM3812::calc_notetable()
{
	for (int oct = 0; oct < 8; oct++) {
		for (int fnum = 0; fnum < 1024; fnum++) {
			const int kci = oct*1024 + fnum;
			const double freq = ((double)fnum * (double)m_baseclock / 72.0 * pow(2.0, (double)(oct - 1)) / pow(2.0, 19));
			int kco = (int)(12.0 * log(freq/440.0) / log(2.0) + 57 + 0.5);
			if (kco < 0) {
				kco = 0;
			} else if (kco > 127) {
				kco = 127;
			}
			notetable[kci] = kco;
		}
	}
}

bool ssYM3812::Initialize(int _baseclock)
{
	bool ret = false;

	int num;
	ssSoundDriverManager *manager = ssSoundDriverManager::Instance();
	const ssConfig &config = manager->GetConfig();
	int sampling_rate = config.sampling_rate;

	m_baseclock = _baseclock;

	if (m_bitmap == 0) {
		/* emulator create */
		F3812 = OPLCreate(OPL_TYPE_YM3812, m_baseclock, sampling_rate);
		if (!F3812) {
			return false;
		}
	}

	for (num = 0; num < MAX_YM3812; num++) {
		if ((m_bitmap & (1 << num)) == 0) {
			m_ChipNo = num;
			m_bitmap |= 1 << m_ChipNo;
			mapYM3812[m_ChipNo] = this;

			ret = true;
			break;
		}
	}

	OPLResetChip(F3812);

	OPLSetTimerHandler(F3812, ::TimerHandler, 0);
	OPLSetIRQHandler(F3812, ::IRQHandler, 0);

	for (int t = 0; t < 9; t++) {
		InitTrackInfo(&info[t]);
		sprintf(info[t].name, "YM-3812 #%d", t);
	}

	calc_notetable();

	return ret;
}

void ssYM3812::TimerHandler(int _c, double _step)
{
	if (_c == 0) {
		// Timer A
		if (_step == 0) {
			m_TimerA->Active(false);
		} else {
			m_TimerA->Active(true);
			m_TimerA->SetInterval(_step);
		}
	} else {
		// Timer B
		if (_step == 0) {
			m_TimerB->Active(false);
		} else {
			m_TimerB->Active(true);
			m_TimerB->SetInterval(_step);
		}
	}
}

void ssYM3812::IRQHandler(int _irq)
{
}

void ssYM3812::TimerAOver(void)
{
	OPLTimerOver(F3812, 0);
}

void ssYM3812::TimerBOver(void)
{
	OPLTimerOver(F3812, 1);
}

void ssYM3812::Write(int _adr, BYTE _data)
{
	static const int chtbl[32]= {
		-1,-1,-1, 0, 1, 2,-1,-1
		-1,-1,-1, 3, 4, 5,-1,-1,
		-1,-1,-1, 6, 7, 8,-1,-1
	};

	OPLWrite(F3812, _adr, _data);

	if ((_adr & 1) == 0) {
		m_reg = _data;
	} else {
		reg[m_reg] = _data;

		// F-Number HIGH / key on
		if (m_reg >= 0xb0 && m_reg <= 0xb8) {
			const BYTE ch = m_reg & 0x0f;
			const int kc = ((_data << 8) + reg[m_reg - 0x10]) & 0x1fff;
			info[ch].keycode = notetable[kc];

			// key on / key off
			if (_data & 0x20) {
				info[ch].keyon = 0xff;
			} else {
				info[ch].keyon &= 0x80;
			}
		// TL (2)
		} else if (m_reg >= 0x40 && m_reg <= 0x55) {
			const BYTE ch = chtbl[m_reg - 0x40];
			if (ch != -1) {
				info[ch].volume = 127 - (_data & 0x3f) * 2;
			}
		}
	}
}

BYTE ssYM3812::Read(int _adr)
{
	return OPLRead(F3812, _adr);
}

int ssYM3812::GetBufferCount(void) const
{
	return 1;
}

WORD ssYM3812::GetBufferFlag(int _b) const
{
	return ssSoundStream::F_MONO;
}

int ssYM3812::GetTrackCount(void) const
{
	return 9;
}

ssTrackInfo *ssYM3812::GetInfo(int _track)
{
	return &info[_track];
}

int ssYM3812::GetRegs(BYTE *_buffer, int _count, int _offset)
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

/* update handler */
void ssYM3812::Update(SHORT **_buffer, DWORD _count)
{
	short *buffer = _buffer[0];

	YM3812UpdateOne(F3812, buffer, _count);
}

void ssYM3812::SetMask(DWORD _mask)
{
}

DWORD ssYM3812::GetMask(void)
{
	return 0;
}

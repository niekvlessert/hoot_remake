//
// MSM6258V エンジン
//
// Fri Nov 24 15:40 JST 2000 (Fu-.)
//
#include "StdAfx.h"
#include "ssMSM6258V.h"
#include "ssConfig.h"
#include "ssSoundDriverManager.h"
#include "ssSoundStream.h"


ssMSM6258V::ssMSM6258V()
: ssADPCM(1)
{
	m_addr = NULL;
	m_size = 0;
}

ssMSM6258V::~ssMSM6258V()
{
}

bool ssMSM6258V::Initialize(int _basefreq)
{
	ssADPCM::Initialize();
	SetLSBFirst(true);
	SetNoiseReduction(true);

	m_basefreq = _basefreq;
	m_addr = NULL;
	m_size = 0;

	InitTrackInfo(&info);
	sprintf(info.name, "MSM6258V #%d", 0);
	SetFreq(0, _basefreq);
	SetVol(0, 0);

	m_ppi = 0x08;

	return true;
}

// サイズの設定
void ssMSM6258V::SetAdpcmSize(int _size)
{
	m_size = _size;
}

// アドレスの設定
void ssMSM6258V::SetAdpcmAddr(void *_adr)
{
	m_addr = _adr;
}

// 再生停止・開始ステータスのセット
void ssMSM6258V::SetStat(BYTE _data)
{
	if (_data == 0x88) {
		// 再生開始
		if (m_addr != NULL) {
			Play(0, m_addr, m_size);

			info.keyon = 0xff;
			info.keycode = ssTrackInfo::OCT4_A;
			info.volume = 127;
			switch(m_ppi & 0x03) {
			// CENTER
			case 0x00:
				info.pan = ssTrackInfo::PAN_CENTER;
				break;
			// RIGHT
			case 0x01:
				info.pan = ssTrackInfo::PAN_LEFT;
				break;
			// LEFT
			case 0x02:
				info.pan = ssTrackInfo::PAN_RIGHT;
				break;
			// MASK(OFF)
			case 0x03:
				info.pan = ssTrackInfo::PAN_OFF;
				break;
			// ???
			default:
				break;
			}
		}
	} else {
		// 再生停止
		Stop(0);
	}
}

BYTE ssMSM6258V::Read(int _adr)
{
	BYTE d = 0;

	if (IsPlaying(0)) d |= 0x08;

	return d;
}

BYTE ssMSM6258V::GetPanAndRate()
{
	return m_ppi;
}

// 再生レートとパンの変更
void ssMSM6258V::SetPanAndRate(BYTE _value)
{
	m_ppi = _value;
	int rate = (m_ppi >> 2) & 0x03;
	SetPan(0, (((m_ppi & 0x01)<<1) | ((m_ppi & 0x02) >> 1)) ^ 3);

	switch(rate) {
	case 0x00:
		// 7.8KHz
		SetFreq(0, 7800);
		break;
	case 0x01:
		// 10.4KHz
		SetFreq(0, 10400);
		break;
	case 0x02:
		// 15.6KHz
		SetFreq(0, 15600);
		break;
	default:
		// ???
		break;
	}
}

int ssMSM6258V::GetTrackCount(void) const
{
	return 1;
}

ssTrackInfo *ssMSM6258V::GetInfo(int _track)
{
	return &info;
}

void ssMSM6258V::Update(SHORT **_buffer, DWORD _count)
{
	ssADPCM::Update(_buffer, _count);

	if (!IsPlaying(0)) {
		info.keyon = 0x00;
	}
}

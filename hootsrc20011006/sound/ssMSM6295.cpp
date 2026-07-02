#include "StdAfx.h"
#include "sound/ssMSM6295.h"
#include "ssConfig.h"
#include "ssSoundDriverManager.h"
#include "ssSoundStream.h"

static BYTE CH_tbl[] = {
	0xff, 0x03, 0x02, 0xff, 0x01, 0xff, 0xff, 0xff,
	0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

ssMSM6295::ssMSM6295()
: ssADPCM(4)
{
	int i;

	m_adr_mask = 0x3ffff;
	for (i = 0; i < 128; i++) {
		m_bank[i] = 0;
	}
}

bool ssMSM6295::Initialize(int _basefreq, BYTE *_pcmdata)
{
	ssADPCM::Initialize();

	m_basefreq = _basefreq;
	m_pcmdata = _pcmdata;

	m_state = STATE_INI;

	for (int ch = 0; ch < 4; ch++) {
		InitTrackInfo(&info[ch]);
		sprintf(info[ch].name, "MSM6295 #%d", ch);

		SetFreq(ch, m_basefreq);
	}

	return true;
}

ssMSM6295::~ssMSM6295()
{
}

void ssMSM6295::Write(int _adr, BYTE _data)
{
	if (m_state == STATE_INI) {
		TRACE("MSM6295(0) : %02x\n", _data);
		if (_data & 0x80) {
			m_state = STATE_KEYON;
			m_key = _data & 0x7f;
		} else {
			if (_data & 0x40) Stop(0);
			if (_data & 0x20) Stop(1);
			if (_data & 0x10) Stop(2);
			if (_data & 0x08) Stop(3);
		}
	} else {
		TRACE("MSM6295(1) : %02x\n", _data);
		m_state = STATE_INI;
		int ch = CH_tbl[_data >> 4];

		if (ch == 0xff) return;

		BYTE *p = m_pcmdata + m_key * 8 + m_bank[m_key];
		int start_point = (((p[0]<<16) + (p[1]<<8) + p[2]) & m_adr_mask) + m_bank[m_key];
		int end_point = (((p[3]<<16) + (p[4]<<8) + p[5]) & m_adr_mask) + m_bank[m_key];
		int vol = _data & 0x0f;

		if (start_point > end_point) {
			Stop(ch);
			info[ch].keyon = 0x00;
		} else {
			SetVol(ch, vol);
			Play(ch, &m_pcmdata[start_point], end_point - start_point);
			info[ch].keyon = 0xff;
			info[ch].keycode = m_key;
			info[ch].volume = (15-vol) * 8;
		}
	}
}

BYTE ssMSM6295::Read(int _adr)
{
	BYTE d = 0;
	if (IsPlaying(0)) d |= 0x08;
	if (IsPlaying(1)) d |= 0x04;
	if (IsPlaying(2)) d |= 0x02;
	if (IsPlaying(3)) d |= 0x01;
	return d;
}

void ssMSM6295::SetBank(int _key, int _bank)
{
	m_bank[_key] = _bank;
}

void ssMSM6295::SetBankRegion(int _key1, int _key2, int _bank)
{
	for (int k = _key1; k <= _key2; k++) {
		m_bank[k] = _bank;
	}
}

void ssMSM6295::SetAdrMask(int _mask)
{
	m_adr_mask = _mask;
}


int ssMSM6295::GetTrackCount(void) const
{
	return 4;
}

ssTrackInfo *ssMSM6295::GetInfo(int _track)
{
	if (_track < 4) {
		return &info[_track];
	}

	return NULL;
}

void ssMSM6295::Update(SHORT **_buffer, DWORD _count)
{
	ssADPCM::Update(_buffer, _count);

	for (int ch = 0; ch < 4; ch++) {
		if (!IsPlaying(ch)) {
			info[ch].keyon = 0x00;
		}
	}
}


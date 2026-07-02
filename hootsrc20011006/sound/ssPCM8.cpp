#include "StdAfx.h"
#include "sound/ssPCM8.h"
#include "ssConfig.h"
#include "ssSoundDriverManager.h"
#include "ssSoundStream.h"

ssPCM8::ssPCM8()
: ssADPCM(8, ssADPCM::MODE_X68K_PCM8)
{
	memset(m_reg, 0, sizeof(m_reg));
}

bool ssPCM8::Initialize(void)
{
	ssADPCM::Initialize();
	SetLSBFirst(true);
	//SetNoiseReduction(true);

	for (int ch = 0; ch < 8; ch++) {
		InitTrackInfo(&info[ch]);
		sprintf(info[ch].name, "PCM8 #%d", ch);

		SetFreq(ch, 15600);
		SetVol(ch, 8);
	}

	return true;
}

ssPCM8::~ssPCM8()
{
}

int ssPCM8::GetTrackCount(void) const
{
	return 8;
}

ssTrackInfo *ssPCM8::GetInfo(int _track)
{
	if (_track < 8) {
		return &info[_track];
	}

	return NULL;
}

int ssPCM8::GetRegs(BYTE *_buffer, int _count, int _offset)
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


void ssPCM8::Update(SHORT **_buffer, DWORD _count)
{
	ssADPCM::Update(_buffer, _count);

	for (int ch = 0; ch < 8; ch++) {
		if (!IsPlaying(ch)) {
			info[ch].keyon &= 0x80;
		}
	}
}


void ssPCM8::Play(int _ch, void *_adr, int _length)
{
	ssADPCM::Play(_ch, _adr, _length);

	if (_length != 0) {
		info[_ch].keycode = ssTrackInfo::OCT4_A;
		info[_ch].keyon = 0xff;
	}
}

void ssPCM8::Play(int _ch, void *_adr, int _offset, int _length)
{
	BYTE *adr = (BYTE *)_adr;
	const int b = _ch * 16;

	m_reg[b+0] = _offset >> 24;
	m_reg[b+1] = _offset >> 16;
	m_reg[b+2] = _offset >> 8;
	m_reg[b+3] = _offset;
	m_reg[b+4] = _length >> 24;
	m_reg[b+5] = _length >> 16;
	m_reg[b+6] = _length >> 8;
	m_reg[b+7] = _length;

	Play(_ch, adr + _offset, _length);
}

void ssPCM8::Stop(int _ch)
{
	ssADPCM::Stop(_ch);
	info[_ch].keyon &= 0x80;
}

void ssPCM8::SetVol(int _ch, int _vol)
{
	const int b = _ch * 16;
	m_reg[b+12] = _vol;

	ssADPCM::SetVol(_ch, _vol);
	info[_ch].volume = _vol * 8;
}

void ssPCM8::SetFreq(int _ch, int _freq)
{
	const int b = _ch * 16;

	m_reg[b+8] = _freq >> 24;
	m_reg[b+9] = _freq >> 16;
	m_reg[b+10] = _freq >> 8;
	m_reg[b+11] = _freq;
	ssADPCM::SetFreq(_ch, _freq);
}

void ssPCM8::SetPan(int _ch, int _pan)
{
	static const pantbl[4] = {
		ssTrackInfo::PAN_OFF,
		ssTrackInfo::PAN_LEFT,
		ssTrackInfo::PAN_RIGHT,
		ssTrackInfo::PAN_CENTER
	};

	const int b = _ch * 16;
	m_reg[b+13] = _pan;

	ssADPCM::SetPan(_ch, _pan);
	info[_ch].pan = pantbl[_pan];
}

void ssPCM8::SetMode(int _ch, ssADPCM::Mode _mode)
{
	const int b = _ch * 16;
	m_reg[b+14] = _mode;

	ssADPCM::SetMode(_ch, _mode);
}

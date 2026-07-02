#include "StdAfx.h"
#include "sound/ssMono2151.h"

ssMono2151::ssMono2151(int _idTimerA, int _idTimerB)
	: ssYM2151(_idTimerA, _idTimerB)
{
}

ssMono2151::~ssMono2151()
{
}

void ssMono2151::Write(int _adr, BYTE _data)
{
	if ((_adr & 1) == 0) {
		m_reg = _data;
	} else if ((m_reg & 0xf8) == 0x20) {
		_data |= 0xc0;
	}
	ssYM2151::Write(_adr, _data);
}

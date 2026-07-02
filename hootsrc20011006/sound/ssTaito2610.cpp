#include "StdAfx.h"
#include "ssSoundStream.h"
#include "ssTaito2610.h"

WORD ssTaito2610::GetBufferFlag(int _b) const
{
	return ssSoundStream::F_STEREO_SEPARATE_LONG;
}

void ssTaito2610::SetVolume(int _b, int _p, short _vol)
{
	if (_b < 2) {
		m_Volume[_b][_p] = _vol;
	} else {
		ssYM2610::SetVolume(_b, _p, _vol);
	}
}

short ssTaito2610::GetVolume(int _b, int _p) const
{
	_b += _p / 2;
	_p %= 2;
	if (_b < 2) {
		return m_Volume[_b][_p];
	}
	return ssYM2610::GetVolume(_b, _p);
}

#include "StdAfx.h"
#include "ssSoundStream.h"
#include "sound/ssStereo2151.h"

// USE_M88_ENGINE が定義されている場合はcisc版OPNAエンジン使用。
// そうでない場合は、Tatsuyuki版OPNAエンジン使用。
#define USE_M88_ENGINE

ssStereo2151::ssStereo2151(int _idTimerA, int _idTimerB)
	: ssYM2151(_idTimerA, _idTimerB)
{
}

ssStereo2151::~ssStereo2151()
{
}

WORD ssStereo2151::GetBufferFlag(int _b) const
{
#ifdef USE_M88_ENGINE
	return ssSoundStream::F_STEREO_SEPARATE_LONG;
#else // USE_M88_ENGINE
	if (_b < 2) {
		return ssSoundStream::F_SEPARATE;
	}
	return ssYM2151::GetBufferFlag(_b);
#endif // USE_M88_ENGINE
}

void ssStereo2151::SetVolume(int _b, int _p, short _vol)
{
	if (_b < 2) {
		m_Volume[_b][_p] = _vol;
	} else {
		ssYM2151::SetVolume(_b, _p, _vol);
	}
}

short ssStereo2151::GetVolume(int _b, int _p) const
{
	_b += _p / 2;
	_p %= 2;
	if (_b < 2) {
		return m_Volume[_b][_p];
	}
	return ssYM2151::GetVolume(_b, _p);
}

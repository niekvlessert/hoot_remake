#include "StdAfx.h"
#include "ssSoundChip.h"

void ssSoundChip::SetVolume(int _b, int _p, short _vol)
{
	m_Volume = _vol;
}

short ssSoundChip::GetVolume(int _b, int _p) const
{
	return m_Volume;
}

int ssSoundChip::GetTrackCount(void) const
{
	return 0;
}

ssTrackInfo *ssSoundChip::GetInfo(int _track)
{
	return NULL;
}

int ssSoundChip::GetRegs(BYTE *_buffer, int _count, int _offset)
{
	return 0;
}

void ssSoundChip::SetMask(DWORD _mask)
{
	m_mask = _mask;
}

DWORD ssSoundChip::GetMask(void)
{
	return m_mask;
}
